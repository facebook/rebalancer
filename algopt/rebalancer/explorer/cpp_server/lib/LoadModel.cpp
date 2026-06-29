// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/lib/LoadModel.h"

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/common/CoroUtils.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/Constants.h"
#include "algopt/rebalancer/interface/standalone/BackwardCompatabilityUtils.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSolver_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/materializer/Materializer.h"
#include "algopt/rebalancer/solver/solvers/SolverFactory.h"
#include "algopt/rebalancer/treeprof/EventRecorder.h"
#include "algopt/rebalancer/treeprof/ExecutorWrapper.h"
#include "rebalancer/explorer/cpp_server/lib/Utils.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <fmt/core.h>
#include <folly/FileUtil.h>
#include <folly/futures/SharedPromise.h>
#include <folly/Synchronized.h>
#include <folly/system/HardwareConcurrency.h>

#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace facebook::rebalancer::explorer {

using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::interface;

static EquivalenceSetsData buildEquivalenceSetData(
    const interface::EquivalenceSetInfo& equivSetInfo,
    const Universe& universe) {
  const auto& equivSets = *equivSetInfo.equivalenceSets();
  EquivalenceSetsData data;
  data.partitionName = std::string(kEquivSetPartition);
  for (const auto& equivSet : equivSets) {
    const auto& groupName = *equivSet.name();
    data.groupNames.emplace_back(groupName);
    for (const auto& objectName : *equivSet.objectNames()) {
      const auto objectId = universe.getObjectId(objectName);
      data.objectIdToGroupName.emplace(objectId, groupName);
    }
  }

  return data;
}

static Map<entities::ContainerId, std::vector<entities::ObjectId>>
parseFinalAssignment(
    const interface::AssignmentSolution& solution,
    const Universe& universe) {
  /* Parse the solution and return final assignment */
  Map<entities::ContainerId, std::vector<ObjectId>> finalAssignment;

  // Collect container and object names for quick lookup by extraneous entity
  // sanity check below.
  Set<std::string> containerNames, objectNames;
  for (auto containerId : universe.getContainers().getContainerIds()) {
    containerNames.insert(universe.getEntityName(containerId));
  }
  for (auto objectId : universe.getObjects().getObjectIds()) {
    objectNames.insert(universe.getEntityName(objectId));
  }

  // create entry for all containers
  for (auto containerId : universe.getContainers().getContainerIds()) {
    finalAssignment.emplace(containerId, std::vector<ObjectId>());
  }

  auto& objectToContainer = *solution.assignment();
  Set<entities::ObjectId> seenObjectIds;
  for (auto& [objectName, containerName] : objectToContainer) {
    if (!containerNames.contains(containerName)) {
      throw std::runtime_error(
          fmt::format("extraneous container in solution: {}", containerName));
    }
    if (!objectNames.contains(objectName)) {
      throw std::runtime_error(
          fmt::format("extraneous object in solution: {}", objectName));
    }
    auto objectId = universe.getObjectId(objectName);
    auto containerId = universe.getContainerId(containerName);
    finalAssignment[containerId].push_back(objectId);

    seenObjectIds.emplace(objectId);
  }

  // Look for missing objects in solution.
  for (auto objectId : universe.getObjects().getObjectIds()) {
    if (!seenObjectIds.contains(objectId)) {
      throw std::runtime_error(
          fmt::format(
              "missing object in solution: {}",
              universe.getEntityName(objectId)));
    }
  }

  return finalAssignment;
}

static std::shared_ptr<Universe> buildUniverse(
    interface::AssignmentProblem& problem) {
  /* Build the universe and return the object for further processing. */
  if (problem.universe()) {
    auto& universeThrift = *problem.universe();
    BackwardCompatabilityUtils::possiblyModify(universeThrift);
    return std::make_shared<Universe>(*problem.universe());
  }
  throw std::runtime_error("Universe missing in problem");
}

std::vector<std::shared_ptr<const Column>> LoadModel::buildPartitionCols(
    const Universe& universe,
    const EquivalenceSetsData& eqSetsData) {
  std::vector<std::shared_ptr<const Column>> columns;
  for (auto partitionId : universe.getPartitionIds()) {
    auto& partition = universe.getPartition(partitionId);
    auto& partitionName = universe.getEntityName(partitionId);
    Map<EntityId, DataCell> objectToCell;
    for (auto groupId : partition.getGroupIds()) {
      auto& groupName = universe.getEntityName(groupId);
      for (auto objectId : partition.getObjectIds(groupId)) {
        const DataCell name(groupName);
        objectToCell[toEntityId(objectId)] = name;
      }
    }
    DataCell defaultPartition("");
    auto columnData = std::make_shared<Column>(
        std::move(objectToCell),
        std::move(defaultPartition),
        partitionName,
        ColumnType::PARTITION);
    columns.push_back(std::move(columnData));
  }

  // Add equivalence set data as a partition column
  Map<EntityId, DataCell> objectToCell;
  for (const auto& [objectId, groupName] : eqSetsData.objectIdToGroupName) {
    objectToCell[toEntityId(objectId)] = DataCell(groupName);
  }
  DataCell defaultPartition("");
  auto columnData = std::make_shared<Column>(
      std::move(objectToCell),
      std::move(defaultPartition),
      eqSetsData.partitionName,
      ColumnType::PARTITION);
  columns.push_back(std::move(columnData));

  return columns;
}

// For every object in assignment, compute the value of the object for the given
// `scalarDimension`.
static Map<EntityId, DataCell> getObjectDimensionValues(
    const Map<ContainerId, std::vector<ObjectId>>& assignment,
    const ObjectScalarDimension& scalarDimension,
    const Universe& universe) {
  Map<EntityId, DataCell> objectIdToDimensionValue;
  const Scope* scope = nullptr;
  if (scalarDimension.isDynamic()) {
    scope = &universe.getScope(scalarDimension.getScopeId());
  }

  for (const auto& [container, objects] : assignment) {
    const std::optional<ScopeItemId> scopeItem =
        scope ? scope->getScopeItemId(container) : std::nullopt;
    for (const ObjectId& objectId : objects) {
      objectIdToDimensionValue[toEntityId(objectId)].doubleValue =
          scalarDimension.getValue(objectId, scopeItem);
    }
  }
  return objectIdToDimensionValue;
}

//  Constructs dynamic dimension related columns.
DynamicDimensionColumns::DynamicDimensionColumns(
    const Universe& universe,
    const ObjectScalarDimension& dimension,
    const std::string& dimensionName,
    const Map<ContainerId, std::vector<ObjectId>>& finalAssignment) {
  if (!dimension.isDynamic()) {
    throw std::runtime_error(
        fmt::format(
            "Expected to be called only for a dynamic dimension, called for {}.",
            dimensionName));
  }
  // Construct two columns for objects table containing each object's value
  // for this dimension based on initial and final assignment.
  DataCell defaultCell;
  const Map<ContainerId, std::vector<ObjectId>>& initialAssignment =
      universe.getContainers().getInitialAssignment();
  srcColumn = std::make_shared<Column>(
      getObjectDimensionValues(initialAssignment, dimension, universe),
      defaultCell,
      fmt::format("src.{}", dimensionName),
      ColumnType::DIMENSION);

  dstColumn = std::make_shared<Column>(
      getObjectDimensionValues(finalAssignment, dimension, universe),
      defaultCell,
      fmt::format("dst.{}", dimensionName),
      ColumnType::DIMENSION);

  // Construct three columns for the dimension specific entity page containing
  // object or group name, scope item name, and the corresponding value from the
  // problem config.
  entities::Map<EntityId, DataCell> objectNames;
  entities::Map<EntityId, DataCell> scopeItemNames;
  entities::Map<EntityId, DataCell> dimensionValues;

  const ScopeId& scopeId = dimension.getScopeId();
  const Scope& scope = universe.getScope(scopeId);

  const double defaultValue = dimension.getDefaultValue();
  const auto addRow = [&](const std::string& itemName,
                          const std::string& scopeItemName,
                          const double value) {
    const EntityId entityId = toEntityId(rowIds.size());
    rowIds.push_back(entityId);
    dimensionValues[entityId].doubleValue = value;
    objectNames[entityId].strValue = itemName;
    scopeItemNames[entityId].strValue = scopeItemName;
  };

  // Add a default row for the dimension table.
  // TODO: add a new UI component to show default value.
  // Also show an error message if dimension is too big to display, for
  // example when more than 10M values.
  addRow("default", "default", defaultValue);
  std::string itemColumnName = universe.getObjectTypeName();
  for (const ScopeItemId scopeItem : scope.getScopeItemIds()) {
    const std::string& scopeItemName = universe.getEntityName(scopeItem);
    dimension.values(scopeItem).visit(
        [&](const ObjectIdToDoubleMap& objectValues) {
          itemColumnName = universe.getObjectTypeName();
          objectValues.forEachNonDefault(
              [&](const ObjectId objectId, const double value) {
                addRow(universe.getEntityName(objectId), scopeItemName, value);
              });
        },
        [&](const PartitionId partitionId,
            const GroupIdToDoubleMap& groupValues) {
          itemColumnName = universe.getEntityName(partitionId);
          for (const auto& [groupId, value] : groupValues) {
            addRow(universe.getEntityName(groupId), scopeItemName, value);
          }
        });
  }

  const std::string& scopeName = universe.getEntityName(dimension.getScopeId());

  objectNamesColumn = std::make_shared<Column>(
      std::move(objectNames),
      defaultCell,
      std::move(itemColumnName),
      ColumnType::ENTITY_NAME,
      /*primaryKey=*/true);

  scopeItemNamesColumn = std::make_shared<Column>(
      std::move(scopeItemNames),
      defaultCell,
      scopeName,
      ColumnType::SCOPE,
      /*primaryKey=*/true);

  defaultCell.doubleValue = defaultValue;
  dimensionValuesColumn = std::make_shared<Column>(
      std::move(dimensionValues),
      std::move(defaultCell),
      dimensionName,
      ColumnType::DIMENSION);
}

void LoadModel::buildStaticObjectDimensionCols(
    const entities::Universe& universe,
    entities::Map<std::string, Table>& tableData) {
  std::vector<std::shared_ptr<const Column>> staticDimensionColumns;

  auto& initialAssignment = universe.getContainers().getInitialAssignment();
  for (auto dimId : universe.getObjects().getDimensionIds()) {
    auto& dimName = universe.getEntityName(dimId);
    auto& objectDimension = universe.getObjects().getDimension(dimId);

    for (int i = 0; i < objectDimension.size(); i++) {
      auto& scalarDimension = objectDimension.at(i);
      // Skip ObjectPartitionRoutingDimension to prevent an exception below.
      if (scalarDimension.isRoutingConfigBased()) {
        continue;
      }
      auto colName = (objectDimension.size() > 1)
          ? fmt::format("{}_{}", dimName, i)
          : dimName;

      if (!scalarDimension.isDynamic()) {
        DataCell defaultDimension(0.0);
        // construct a static dimension column for every object in
        // `initialAssignment`.
        auto columnData = std::make_shared<Column>(
            getObjectDimensionValues(
                initialAssignment, scalarDimension, universe),
            std::move(defaultDimension),
            colName,
            ColumnType::DIMENSION);
        staticDimensionColumns.push_back(std::move(columnData));
      }
    }
  }
  // Add all static dimension columns in order sorted by dimension name.
  tableData.at(universe.getObjectTypeName())
      .insertColumnsInSortedOrder(std::move(staticDimensionColumns));
}

static std::pair<Map<ObjectId, ContainerId>, Map<ObjectId, ContainerId>>
buildObjectToContainer(
    const Map<ContainerId, std::vector<ObjectId>>& finalAssignment,
    const Universe& universe) {
  Map<ObjectId, ContainerId> initialObjectToContainer;
  Map<ObjectId, ContainerId> finalObjectToContainer;

  for (auto containerId : universe.getContainers().getContainerIds()) {
    for (auto objectId :
         universe.getContainers().getInitialObjectIds(containerId)) {
      initialObjectToContainer.emplace(objectId, containerId);
    }

    if (finalAssignment.contains(containerId)) {
      for (auto objectId : finalAssignment.at(containerId)) {
        finalObjectToContainer.emplace(objectId, containerId);
      }
    }
  }
  return std::pair(
      std::move(initialObjectToContainer), std::move(finalObjectToContainer));
}

static Map<ContainerId, ScopeItemId> getContainerToScopeItemId(
    const Universe& universe,
    ScopeId scopeId) {
  Map<ContainerId, ScopeItemId> containerToScopeItemId;
  for (auto scopeItemId : universe.getScope(scopeId).getScopeItemIds()) {
    for (auto containerId :
         universe.getScope(scopeId).getContainerIds(scopeItemId)) {
      auto [_, insertSuccess] =
          containerToScopeItemId.emplace(containerId, scopeItemId);

      if (!insertSuccess) {
        throw std::runtime_error(
            fmt::format(
                "Container '{}' belongs to more than one scope item in scope '{}'. This is not supported currently",
                universe.getEntityName(containerId),
                universe.getEntityName(scopeId)));
      }
    }
  }

  return containerToScopeItemId;
}

static void buildScopeDimensionCols(
    const Universe& universe,
    Map<std::string, Table>& tableData) {
  /* Create dimension rows for all scope. */
  Map<std::string, std::vector<std::shared_ptr<const Column>>> scopeToColumnVec;
  for (auto scopeId : universe.getScopeIds()) {
    auto& scope = universe.getScope(scopeId);
    auto& scopeName = universe.getEntityName(scopeId);
    for (auto dimId : scope.getDimensionIds()) {
      auto& scopeDimension = scope.getDimension(dimId);
      auto& dimName = universe.getEntityName(dimId);

      Map<EntityId, DataCell> scopeItemToCell;
      for (auto& [scopeItemId, dimValue] :
           scopeDimension.getNonDefaultValues()) {
        auto rowId = toEntityId(scopeItemId);
        if (scopeName == universe.getContainerTypeName()) {
          // use container id if preparing column for container table
          const auto& scopeItemName = universe.getEntityName(scopeItemId);
          rowId = toEntityId(universe.getContainerId(scopeItemName));
        }
        scopeItemToCell[rowId].doubleValue = dimValue;
      }

      DataCell defaultDimension(scopeDimension.getDefaultValue());
      auto columnData = std::make_shared<Column>(
          std::move(scopeItemToCell),
          std::move(defaultDimension),
          dimName,
          ColumnType::DIMENSION);
      scopeToColumnVec[scopeName].push_back(columnData);
    }
  }

  // insert columns
  for (auto& [scopeName, columnVec] : scopeToColumnVec) {
    tableData.at(scopeName).insertColumnsInSortedOrder(std::move(columnVec));
  }
}

std::vector<std::shared_ptr<const Column>> LoadModel::buildAssignmentCols(
    const Universe& universe,
    const Map<ContainerId, std::vector<ObjectId>>& finalAssignment) {
  std::vector<std::shared_ptr<const Column>> columns;
  const DataCell defaultAssignment("");
  for (auto scopeId : universe.getScopeIds()) {
    auto& scope = universe.getScope(scopeId);
    auto& scopeName = universe.getEntityName(scopeId);

    Map<EntityId, DataCell> objectToInitialCell;
    Map<EntityId, DataCell> objectToFinalCell;

    for (auto scopeItemId : scope.getScopeItemIds()) {
      auto& scopeItemName = universe.getEntityName(scopeItemId);
      for (auto containerId : scope.getContainerIds(scopeItemId)) {
        // initial assignment
        for (auto objectId :
             universe.getContainers().getInitialObjectIds(containerId)) {
          DataCell scopeValue(scopeItemName);
          objectToInitialCell[toEntityId(objectId)] = std::move(scopeValue);
        }

        // final assignment
        if (finalAssignment.contains(containerId)) {
          for (auto objectId : finalAssignment.at(containerId)) {
            DataCell scopeValue(scopeItemName);
            objectToFinalCell[toEntityId(objectId)] = std::move(scopeValue);
          }
        }
      }
    }

    auto srcColumnData = std::make_shared<Column>(
        std::move(objectToInitialCell),
        defaultAssignment,
        fmt::format("src.{}", scopeName),
        ColumnType::ASSIGNMENT);
    columns.push_back(std::move(srcColumnData));

    auto dstColumnData = std::make_shared<Column>(
        std::move(objectToFinalCell),
        defaultAssignment,
        fmt::format("dst.{}", scopeName),
        ColumnType::ASSIGNMENT);
    columns.push_back(std::move(dstColumnData));
  }
  return columns;
}

static double getRelativeUtilization(double absValue, double scopDimValue) {
  if (scopDimValue == 0) {
    return absValue * std::numeric_limits<double>::infinity();
  } else {
    return absValue / scopDimValue;
  }
}

static std::pair<Map<ScopeItemId, double>, Map<ScopeItemId, double>>
getInitialAndFinalAbsoluteUtils(
    DimensionId dimensionId,
    [[maybe_unused]] ScopeId scopeId,
    const Universe& universe,
    const Map<ContainerId, ScopeItemId>& containerToScopeItemId,
    const Map<ObjectId, ContainerId>& objectToInitialContainer,
    const Map<ObjectId, ContainerId>& objectToFinalContainer) {
  Map<ScopeItemId, double> scopeItemToInitialUtil;
  Map<ScopeItemId, double> scopeItemToFinalUtil;

  /* Calculate initial and final utilization for each scalar dimension and
   * take the max */
  auto& objectDimension = universe.getObjects().getDimension(dimensionId);
  for (int index = 0; index < objectDimension.size(); ++index) {
    auto& scalarDimension = objectDimension.at(index);
    if (scalarDimension.isRoutingConfigBased()) {
      throw std::runtime_error(
          "unexpected call to getInitialAndFinalAbsoluteUtils(...) with an ObjectPartitionRoutingDimension");
    }

    Map<ScopeItemId, double> scopeItemToInitialScalarUtil;
    Map<ScopeItemId, double> scopeItemToFinalScalarUtil;
    for (auto objectId : universe.getObjects().getObjectIds()) {
      auto srcContainerId = objectToInitialContainer.at(objectId);
      auto srcScopeItemIdPtr =
          folly::get_ptr(containerToScopeItemId, srcContainerId);
      if (srcScopeItemIdPtr) {
        scopeItemToInitialScalarUtil[*srcScopeItemIdPtr] +=
            scalarDimension.getValue(objectId, *srcScopeItemIdPtr);
      }

      // it is possible that the object may not be in the final assignment if
      // solution file is missing
      auto dstContainerIdPtr = folly::get_ptr(objectToFinalContainer, objectId);
      if (!dstContainerIdPtr) {
        continue;
      }

      auto dstScopeItemIdPtr =
          folly::get_ptr(containerToScopeItemId, *dstContainerIdPtr);
      if (dstScopeItemIdPtr) {
        scopeItemToFinalScalarUtil[*dstScopeItemIdPtr] +=
            scalarDimension.getValue(objectId, *dstScopeItemIdPtr);
      }
    }

    auto update = [](auto& utilMap, const auto& scalarUtilMap) {
      for (const auto& [scopeItemId, scalarUtil] : scalarUtilMap) {
        auto ptr = folly::get_ptr(utilMap, scopeItemId);
        if (!ptr) {
          utilMap.emplace(scopeItemId, scalarUtil);
        } else {
          *ptr = std::max(scalarUtil, *ptr);
        }
      }
    };

    update(scopeItemToInitialUtil, scopeItemToInitialScalarUtil);
    update(scopeItemToFinalUtil, scopeItemToFinalScalarUtil);
  }
  return std::make_pair(scopeItemToInitialUtil, scopeItemToFinalUtil);
}

static std::pair<Map<EntityId, DataCell>, Map<EntityId, DataCell>>
getScopeItemToInitialAndFinalRelativeUtils(
    DimensionId dimensionId,
    const Universe& universe,
    ScopeId scopeId,
    const Map<ContainerId, ScopeItemId>& containerToScopeItemId,
    const Map<ObjectId, ContainerId>& objectToInitialContainer,
    const Map<ObjectId, ContainerId>& objectToFinalContainer) {
  auto& scope = universe.getScope(scopeId);
  auto& scopeName = universe.getEntityName(scopeId);
  auto [initScopeItemUtils, finalScopeItemUtils] =
      getInitialAndFinalAbsoluteUtils(
          dimensionId,
          scopeId,
          universe,
          containerToScopeItemId,
          objectToInitialContainer,
          objectToFinalContainer);

  Map<EntityId, DataCell> scopeItemToInitialRelativeUtilCell;
  Map<EntityId, DataCell> scopeItemToFinalRelativeUtilCell;
  for (auto scopeItemId : scope.getScopeItemIds()) {
    auto rowId = toEntityId(scopeItemId);
    // if scope is container, then rowId will be container id (since
    // everything in the "containers table" is indexed based on ContainerId)
    if (scopeName == universe.getContainerTypeName()) {
      rowId = toEntityId(
          universe.getContainerId(universe.getEntityName(scopeItemId)));
    }

    const double scopeDimValue =
        scope.getDimension(dimensionId).getValue(scopeItemId);
    auto initScopeItemUil =
        folly::get_default(initScopeItemUtils, scopeItemId, 0);
    auto finalScopeItemUil =
        folly::get_default(finalScopeItemUtils, scopeItemId, 0);

    scopeItemToInitialRelativeUtilCell[rowId].doubleValue =
        getRelativeUtilization(initScopeItemUil, scopeDimValue);
    scopeItemToFinalRelativeUtilCell[rowId].doubleValue =
        getRelativeUtilization(finalScopeItemUil, scopeDimValue);
  }

  return std::make_pair(
      scopeItemToInitialRelativeUtilCell, scopeItemToFinalRelativeUtilCell);
}

static void buildUtilizationCols(
    const Universe& universe,
    Map<std::string, Table>& tableData,
    const Map<ObjectId, ContainerId>& objectToInitialContainer,
    const Map<ObjectId, ContainerId>& objectToFinalContainer,
    std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor) {
  /* Create utilization rows for all scopes*/
  Map<std::string, std::vector<std::shared_ptr<const Column>>>
      scopeToUtilColumns;
  for (auto scopeId : universe.getScopeIds()) {
    XLOGF(
        INFO,
        "building utilization columns for scope '{}' w.r.t. {} dimensions",
        universe.getEntityName(scopeId),
        universe.getObjects().getDimensionIds().size());

    auto containerToScopeItemId = getContainerToScopeItemId(universe, scopeId);
    const auto dimensionIds = universe.getObjects().getDimensionIds();
    folly::coro::blockingWait(
        CoroUtils::runEachFuncAndUpdate(
            dimensionIds.begin(),
            dimensionIds.end(),
            [&](auto it) -> std::optional<std::pair<
                             Map<EntityId, DataCell>,
                             Map<EntityId, DataCell>>> {
              auto dimensionId = *it;
              auto& objectDimension =
                  universe.getObjects().getDimension(dimensionId);
              const bool shouldSkipDim =
                  objectDimension.at(0).isRoutingConfigBased() ||
                  (objectDimension.at(0).isDynamic() &&
                   objectDimension.at(0).getScopeId() != scopeId);

              // skip dimension if it is routing config based or if it is a
              // dynamic dimension defined on a different scope
              if (shouldSkipDim) {
                return std::nullopt;
              }

              return getScopeItemToInitialAndFinalRelativeUtils(
                  dimensionId,
                  universe,
                  scopeId,
                  containerToScopeItemId,
                  objectToInitialContainer,
                  objectToFinalContainer);
            },
            [&scopeToUtilColumns, &universe, scopeId](
                const auto& initialAndFinalUtilMaps, auto it) {
              if (!initialAndFinalUtilMaps) {
                return;
              }

              auto defaultUtil = DataCell(0.0);
              auto& scopeName = universe.getEntityName(scopeId);
              auto& dimensionName = universe.getEntityName(*it);
              scopeToUtilColumns[scopeName].push_back(
                  std::make_shared<Column>(
                      initialAndFinalUtilMaps->first,
                      defaultUtil,
                      fmt::format("{}.initUtil", dimensionName),
                      ColumnType::UTILIZATION));

              scopeToUtilColumns[scopeName].push_back(
                  std::make_shared<Column>(
                      initialAndFinalUtilMaps->second,
                      defaultUtil,
                      fmt::format("{}.finalUtil", dimensionName),
                      ColumnType::UTILIZATION));
            },
            executor));
  }

  // insert columns
  for (auto& [scopeName, columnVec] : scopeToUtilColumns) {
    tableData.at(scopeName).insertColumnsInSortedOrder(std::move(columnVec));
  }
}

static void buildMovableCol(
    const Universe& universe,
    Map<std::string, Table>& tableData) {
  /* Parse avoidMovingConstraint and MoveInProgressConstraint */
  Map<EntityId, DataCell> objectToIsMovableCell;
  Map<EntityId, DataCell> objectToInProgressCell;
  for (auto constraintId : universe.getConstraints().getConstraintIds()) {
    const auto& constarint =
        universe.getConstraints().getConstraint(constraintId);
    const auto& spec = constarint.getSpec();
    if (spec.getType() == ConstraintSpecs::Type::movesInProgressSpec) {
      for (const auto& move : *spec.movesInProgressSpec()->moves()) {
        const auto& objectName = *move.objName();
        auto objectId = universe.getObjectId(objectName);
        objectToIsMovableCell[toEntityId(objectId)] = DataCell(0.0);
        objectToInProgressCell[toEntityId(objectId)] = DataCell(1.0);
      }
    } else if (spec.getType() == ConstraintSpecs::Type::avoidMovingSpec) {
      for (const auto& objectName : *spec.avoidMovingSpec()->objects()) {
        auto objectId = universe.getObjectId(objectName);
        objectToIsMovableCell[toEntityId(objectId)] = DataCell(0.0);
      }
    }
  }

  auto& objectTable = tableData.at(universe.getObjectTypeName());
  objectTable.insertColumn(
      std::make_shared<Column>(
          std::move(objectToIsMovableCell),
          DataCell(1.0), // by default objects are movable
          "is_movable",
          explorer::ColumnType::INTEGER));

  // only add column if there are movesInProgress
  if (!objectToInProgressCell.empty()) {
    objectTable.insertColumn(
        std::make_shared<Column>(
            std::move(objectToInProgressCell),
            DataCell(0.0), // by default objects are not part of movesInProgress
            "has_move_in_progress",
            explorer::ColumnType::INTEGER,
            /*isPrimaryKey=*/false,
            "1 when object is part of a MovesInProgressSpec, 0 otherwise"));
  }
}

static void buildObjectCol(
    const Universe& universe,
    Map<std::string, Table>& tableData) {
  /* Build object column */
  auto& objectColumn = universe.getObjectTypeName();
  Map<EntityId, DataCell> objectCell;
  for (auto objectId : universe.getObjects().getObjectIds()) {
    DataCell cell(universe.getEntityName(objectId));
    objectCell[toEntityId(objectId)] = std::move(cell);
  }
  DataCell defaultCell("");
  auto columnData = std::make_shared<Column>(
      std::move(objectCell),
      std::move(defaultCell),
      objectColumn,
      ColumnType::ENTITY_NAME,
      /*primaryKey=*/true);

  tableData.at(objectColumn).insertColumn(columnData);
}

static void buildContainerAndScopeItemCols(
    const Universe& universe,
    Map<std::string, Table>& tableData) {
  /* Build container column */
  for (auto scopeId : universe.getScopeIds()) {
    auto& scope = universe.getScope(scopeId);
    auto& scopeName = universe.getEntityName(scopeId);
    const ColumnType colType = (scopeName == universe.getContainerTypeName())
        ? ColumnType::ENTITY_NAME
        : ColumnType::SCOPE;

    Map<EntityId, DataCell> containerToCell;
    Map<EntityId, DataCell> scopeItemToCell;
    for (auto scopeItemId : scope.getScopeItemIds()) {
      auto& scopeItemName = universe.getEntityName(scopeItemId);
      DataCell cell(scopeItemName);
      for (auto containerId : scope.getContainerIds(scopeItemId)) {
        containerToCell[toEntityId(containerId)] = cell;
      }
      scopeItemToCell[toEntityId(scopeItemId)] = std::move(cell);
    }
    DataCell defaultCell("");
    auto columnData = std::make_shared<Column>(
        std::move(containerToCell),
        defaultCell,
        scopeName,
        colType,
        /*primaryKey=*/(scopeName == universe.getContainerTypeName()));
    tableData.at(universe.getContainerTypeName()).insertColumn(columnData);

    // add column for scope (one column per scope table).
    if (scopeName != universe.getContainerTypeName()) {
      const std::shared_ptr<Column> scopeData = std::make_shared<Column>(
          std::move(scopeItemToCell),
          std::move(defaultCell),
          scopeName,
          ColumnType::ENTITY_NAME,
          /*primaryKey=*/true);
      tableData.at(scopeName).insertColumn(scopeData);
    }
  }
}
// copy-pasted from problemsolverfactoy
static int get_core_count() {
  return folly::available_concurrency();
}

std::vector<std::string> LoadModel::getDynamicDimensionNames(
    const Universe& universe) {
  algopt::treeprof::EventRecorder event("Build dynamic dimension names");
  std::vector<std::string> dynamicDimensionNames;
  for (const auto dimId : universe.getObjects().getDimensionIds()) {
    const ObjectDimension& dimension =
        universe.getObjects().getDimension(dimId);
    const std::string& dimName = universe.getEntityName(dimId);
    for (int i = 0; i < dimension.size(); i++) {
      const ObjectScalarDimension& scalarDimension = dimension.at(i);
      if (scalarDimension.isDynamic()) {
        auto dimensionName =
            (dimension.size() > 1) ? fmt::format("{}_{}", dimName, i) : dimName;
        dynamicDimensionNames.emplace_back(dimensionName);
      }
    }
  }
  event.stop();
  return dynamicDimensionNames;
}

folly::coro::Task<void> buildDynamicTableAsync(
    const Universe& universe,
    DimensionId dimId,
    int index,
    std::string dimName,
    const Map<entities::ContainerId, std::vector<entities::ObjectId>>&
        finalAssignment,
    std::shared_ptr<folly::SharedPromise<Table>> promise) {
  try {
    const ObjectDimension& dimension =
        universe.getObjects().getDimension(dimId);
    const ObjectScalarDimension& scalarDimension = dimension.at(index);
    DynamicDimensionColumns columns(
        universe, scalarDimension, dimName, finalAssignment);
    Table table(std::move(columns.rowIds));
    table.insertColumn(std::move(columns.objectNamesColumn));
    table.insertColumn(std::move(columns.scopeItemNamesColumn));
    table.insertColumn(std::move(columns.dimensionValuesColumn));

    promise->setValue(std::move(table));
  } catch (...) {
    promise->setException(folly::exception_wrapper(std::current_exception()));
  }
  co_return;
}

// Initializes and populates a table for every dynamic dimension.
// Each such table contains three columns - one for object names, one for
// scope item names and one for the corresponding value.
void LoadModel::initDynamicDimensionTables(
    const Universe& universe,
    const Map<entities::ContainerId, std::vector<entities::ObjectId>>&
        finalAssignment,
    Map<std::string, std::shared_ptr<folly::SharedPromise<Table>>>&
        tablePromises,
    folly::coro::AsyncScope& asyncScope,
    folly::Executor* executor) {
  // Add a table for each dynamic dimension
  for (const auto dimId : universe.getObjects().getDimensionIds()) {
    const ObjectDimension& dimension =
        universe.getObjects().getDimension(dimId);
    const std::string& dimName = universe.getEntityName(dimId);
    for (int i = 0; i < dimension.size(); i++) {
      const ObjectScalarDimension& scalarDimension = dimension.at(i);
      if (scalarDimension.isDynamic()) {
        auto dimensionName =
            (dimension.size() > 1) ? fmt::format("{}_{}", dimName, i) : dimName;
        auto promise = std::make_shared<folly::SharedPromise<Table>>();
        tablePromises[dimensionName] = promise;
        // Launch the coroutine on the thread pool
        asyncScope.add(
            folly::coro::co_withExecutor(
                executor,
                buildDynamicTableAsync(
                    universe, dimId, i, dimName, finalAssignment, promise)));
      }
    }
  }
}

// Coroutine that builds and inserts a single dynamic dimension's srcColumn and
// dstColumn into the objects table.
static folly::coro::Task<std::vector<std::shared_ptr<const Column>>>
buildDynamicObjectDimensionColAsync(
    const Universe& universe,
    DimensionId dimId,
    int index,
    std::string dimName,
    const Map<entities::ContainerId, std::vector<entities::ObjectId>>&
        finalAssignment) {
  algopt::treeprof::EventRecorder event("Build dynamic object dimension cols");
  XLOG(INFO) << "Building object dimension cols for " << dimName;
  const algopt::Timer timer(true);

  const ObjectDimension& dimension = universe.getObjects().getDimension(dimId);
  const ObjectScalarDimension& scalarDimension = dimension.at(index);

  // Compute srcColumn and dstColumn independently
  const DataCell defaultCell;
  const Map<ContainerId, std::vector<ObjectId>>& initialAssignment =
      universe.getContainers().getInitialAssignment();

  auto srcColumn = std::make_shared<Column>(
      getObjectDimensionValues(initialAssignment, scalarDimension, universe),
      defaultCell,
      fmt::format("src.{}", dimName),
      ColumnType::DIMENSION);

  auto dstColumn = std::make_shared<Column>(
      getObjectDimensionValues(finalAssignment, scalarDimension, universe),
      defaultCell,
      fmt::format("dst.{}", dimName),
      ColumnType::DIMENSION);

  // Insert columns into the objects table
  std::vector<std::shared_ptr<const Column>> result;
  result.reserve(2);
  result.emplace_back(std::move(srcColumn));
  result.emplace_back(std::move(dstColumn));

  XLOG(INFO) << "Built object dimension cols for " << dimName << " in "
             << timer.getSeconds() << " seconds";
  event.stop();
  co_return result;
}

folly::coro::Task<void> LoadModel::initDynamicObjectDimensionColsAsync(
    const Universe& universe,
    const Map<entities::ContainerId, std::vector<entities::ObjectId>>&
        finalAssignment,
    Map<std::string, Table>& tableData,
    folly::coro::AsyncScope& asyncScope,
    std::shared_ptr<folly::CPUThreadPoolExecutor>& executor) {
  algopt::treeprof::EventRecorder event(
      "Build dynamic object dimension cols async");

  // Collect all tasks to run in parallel
  std::vector<
      folly::coro::TaskWithExecutor<std::vector<std::shared_ptr<const Column>>>>
      tasks;

  auto& objectsTable = tableData.at(universe.getObjectTypeName());

  for (const DimensionId& dimId : universe.getObjects().getDimensionIds()) {
    const ObjectDimension& dimension =
        universe.getObjects().getDimension(dimId);
    const std::string& dimName = universe.getEntityName(dimId);
    for (int i = 0; i < dimension.size(); i++) {
      const ObjectScalarDimension& scalarDimension = dimension.at(i);
      if (scalarDimension.isDynamic()) {
        // Launch the coroutine on the thread pool
        tasks.push_back(
            folly::coro::co_withExecutor(
                executor.get(),
                buildDynamicObjectDimensionColAsync(
                    universe, dimId, i, dimName, finalAssignment)));
      }
    }
  }
  // Collect all results from parallel tasks using collectAllRange
  auto results = co_await folly::coro::collectAllRange(std::move(tasks));

  // Flatten all columns into a single vector (single-threaded insertion)
  std::vector<std::shared_ptr<const Column>> allColumns;
  for (auto& columnVec : results) {
    for (auto& col : columnVec) {
      allColumns.push_back(std::move(col));
    }
  }

  // Insert all columns into the objects table
  for (auto& col : allColumns) {
    objectsTable.insertColumn(std::move(col));
  }

  event.stop();
}

// Initializes a table for objects, a table for containers and a table for
// every scope.
static void initTables(
    const Universe& universe,
    Map<std::string, Table>& tableData) {
  auto toEntityIds = [](const auto& ids) {
    std::vector<EntityId> rowIds;
    rowIds.reserve(ids.size());
    for (const auto& id : ids) {
      rowIds.push_back(toEntityId(id));
    }
    return rowIds;
  };

  // add object table
  tableData.emplace(
      universe.getObjectTypeName(),
      Table(toEntityIds(universe.getObjects().getObjectIds())));

  // add container table
  tableData.emplace(
      universe.getContainerTypeName(),
      Table(toEntityIds(universe.getContainers().getContainerIds())));

  // add scope tables
  for (auto scopeId : universe.getScopeIds()) {
    auto& scopeName = universe.getEntityName(scopeId);
    if (scopeName == universe.getContainerTypeName()) {
      continue;
    }
    tableData.emplace(
        scopeName,
        Table(toEntityIds(universe.getScope(scopeId).getScopeItemIds())));
  }
}

ExplorerModel LoadModel::buildData(interface::Bundle&& bundle) {
  auto executor = std::make_shared<folly::CPUThreadPoolExecutor>(
      get_core_count(),
      std::make_unique<folly::LifoSemMPMCQueue<
          folly::CPUThreadPoolExecutor::CPUTask,
          folly::QueueBehaviorIfFull::BLOCK>>(
          folly::CPUThreadPoolExecutor::kDefaultMaxQueueSize),
      std::make_shared<folly::NamedThreadFactory>("CPUThreadPool"));
  auto wrappedExecutor =
      std::make_shared<algopt::treeprof::ExecutorWrapper>(executor);

  // Build problem, universe, and parse solution
  auto& problemSpec = *bundle.problem();
  auto universe = buildUniverse(problemSpec);
  auto solution = bundle.solution()
      ? std::make_optional(std::move(*bundle.solution()))
      : std::nullopt;

  Map<entities::ContainerId, std::vector<entities::ObjectId>> finalAssignment;
  if (solution) {
    finalAssignment = parseFinalAssignment(*solution, *universe);
  }

  // Materialize and build problem for bounds, equivalenceSets, etc.
  auto solver = SolverFactory::createSolver(*problemSpec.strategy());
  auto materialized = materializer::Materializer::materialize(
      wrappedExecutor,
      universe,
      solver->needs_continuous_expressions(),
      std::make_shared<RebalancerLog>(),
      /*shouldCollectMetrics=*/true);

  const ProblemConfigs problemConfigs{
      .threadPool = executor,
      .moveStatsSpec = {},
      .runId = {},
      .useDynamicObjectOrdering = true,
      .enableParallelizedBoundsComputing = true,
      .addMetricsExprsToOrchestrator = true,
  };
  auto problem =
      std::make_unique<Problem>(universe, materialized, problemConfigs);
  auto equivalenceSetsData =
      buildEquivalenceSetData(problem->makeEquivalenceSetInfo(), *universe);

  // Initialize all required tables
  Map<std::string, Table> tableData_;
  initTables(*universe, tableData_);

  // Build object and container column
  buildObjectCol(*universe, tableData_);
  buildContainerAndScopeItemCols(*universe, tableData_);
  buildMovableCol(*universe, tableData_);

  // Build scope dimension
  buildScopeDimensionCols(*universe, tableData_);

  // Build scope utilization
  auto [objectToInitialContainer, objectToFinalContainer] =
      buildObjectToContainer(finalAssignment, *universe);
  buildUtilizationCols(
      *universe,
      tableData_,
      objectToInitialContainer,
      objectToFinalContainer,
      wrappedExecutor);

  auto dynamicDimensionNames = getDynamicDimensionNames(*universe);

  ExplorerModel explorerModel = {
      .problemSpec = std::move(problemSpec),
      .universe = std::move(universe),
      .tableData = std::move(tableData_),
      .finalAssignment = std::move(finalAssignment),
      .solution = std::move(solution),
      .materialized = std::move(materialized),
      .dynamicDimensionNames = std::move(dynamicDimensionNames),
      .problem = std::move(problem),
      .equivalenceSetsData = std::move(equivalenceSetsData),
  };
  return explorerModel;
}

} // namespace facebook::rebalancer::explorer
