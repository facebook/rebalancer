// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/Constants.h"
#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/interface/tests/utils.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/AssignmentProblem_types.h"
#include "algopt/rebalancer/interface/ThriftStrategyBuilder.h"
#include "algopt/rebalancer/interface/UniverseProblemBuilder.h"
#include "rebalancer/explorer/cpp_server/lib/LoadModel.h"
#include "rebalancer/explorer/cpp_server/tests/TestUtils.h"

#include <fmt/core.h>
#include <folly/json/DynamicConverter.h>
#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace facebook::rebalancer;
using namespace facebook::rebalancer::interface;

namespace facebook::rebalancer::explorer::tests {

template <class T>
std::set<T> makeSet(std::initializer_list<T> items) {
  return std::set<T>(items);
}

template <class T>
std::set<T> makeSet(const std::vector<T>& items) {
  return std::set<T>(items.begin(), items.end());
}

void colNameAndTypeAreAsExpected(
    const std::map<std::string, ColumnType>& expected,
    const std::string& tableName,
    const PackerMap<std::string, Table>& tableNameToTable) {
  const explorer::Table& table = tableNameToTable.at(tableName);
  const auto& columns = table.getColumnData();
  std::map<std::string, ColumnType> columnNameAndType;
  std::transform(
      columns.begin(),
      columns.end(),
      std::inserter(columnNameAndType, columnNameAndType.end()),
      [](const auto& column) {
        return std::make_pair(column->getColumnName(), column->getColumnType());
      });
  for (const auto& [colName, actualType] : columnNameAndType) {
    const auto expectedType = folly::get_ptr(expected, colName);
    ASSERT_TRUE(expectedType != nullptr)
        << " column name: " << colName << " not found in table " << tableName;
    EXPECT_EQ(*expectedType, actualType)
        << "tableName: " << tableName << "; column name: " << colName
        << "; expected type: "
        << apache::thrift::util::enumNameSafe(*expectedType)
        << "; actual type: " << apache::thrift::util::enumNameSafe(actualType);
  }
}

void addObjectTable(ExplorerModel& model) {
  auto executor = getTestExecutor();
  auto table = folly::coro::blockingWait(
      LoadModel::buildObjectTable(
          *model.universe,
          model.finalAssignment,
          model.equivalenceSetsData,
          executor.get()));
  model.tableData.emplace(
      model.universe->getObjectTypeName(), std::move(table));
}

EntityId getRowId(const Table& table, const std::string& primaryKey) {
  const auto* primaryKeyColumn = table.getOnlyPrimaryKeyColumn();
  for (const auto rowId : table.getRowIds()) {
    if (primaryKeyColumn->getStrView(rowId) == primaryKey) {
      return rowId;
    }
  }
  throw std::runtime_error(
      fmt::format("Primary key '{}' not found", primaryKey));
}

TEST(LoadModelTest, Basic) {
  auto bundle = TestUtils::buildBundle();
  auto explorerModel = LoadModel::buildData(std::move(bundle));
  addObjectTable(explorerModel);
  const auto& universe = *explorerModel.universe;
  const auto& tableData = std::move(explorerModel.tableData);

  // Verify column names and types for the host (object) table
  const std::map<std::string, ColumnType> expectedHostColumnNameAndType = {
      {"host", ColumnType::ENTITY_NAME},
      {"is_movable", ColumnType::INTEGER},
      {"host_count", ColumnType::DIMENSION},
      {"src.dynamicLoad", ColumnType::DIMENSION},
      {"dst.dynamicLoad", ColumnType::DIMENSION},
      {"network_0", ColumnType::DIMENSION},
      {"network_1", ColumnType::DIMENSION},
      {"network_2", ColumnType::DIMENSION},
      {"ram", ColumnType::DIMENSION},
      {"scheme", ColumnType::PARTITION},
      {"src.rack", ColumnType::ASSIGNMENT},
      {"dst.rack", ColumnType::ASSIGNMENT},
      {"src.row", ColumnType::ASSIGNMENT},
      {"dst.row", ColumnType::ASSIGNMENT},
      {"src.row_incomplete", ColumnType::ASSIGNMENT},
      {"dst.row_incomplete", ColumnType::ASSIGNMENT},
      {"src.msb", ColumnType::ASSIGNMENT},
      {"dst.msb", ColumnType::ASSIGNMENT},
      {kEquivSetPartition.data(),
       ColumnType::PARTITION} // equivalence set partition column is always
                              // added
  };
  colNameAndTypeAreAsExpected(expectedHostColumnNameAndType, "host", tableData);

  // Verify column names and types for the rack (container) table
  const std::map<std::string, ColumnType> expectedRackColumnNameAndType = {
      {"rack", ColumnType::ENTITY_NAME},
      {"row", ColumnType::SCOPE},
      {"row_incomplete", ColumnType::SCOPE},
      {"msb", ColumnType::SCOPE},
      {"network", ColumnType::DIMENSION},
      {"host_count.initUtil", ColumnType::UTILIZATION},
      {"network.initUtil", ColumnType::UTILIZATION},
      {"ram.initUtil", ColumnType::UTILIZATION},
      {"host_count.finalUtil", ColumnType::UTILIZATION},
      {"network.finalUtil", ColumnType::UTILIZATION},
      {"ram.finalUtil", ColumnType::UTILIZATION}};
  colNameAndTypeAreAsExpected(expectedRackColumnNameAndType, "rack", tableData);

  // Verify column names and types for the row table
  const std::map<std::string, ColumnType> expectedRowColumnNameAndType = {
      {"row", ColumnType::ENTITY_NAME},
      {"network", ColumnType::DOUBLE},
      {"host_count.initUtil", ColumnType::UTILIZATION},
      {"network.initUtil", ColumnType::UTILIZATION},
      {"ram.initUtil", ColumnType::UTILIZATION},
      {"host_count.finalUtil", ColumnType::UTILIZATION},
      {"network.finalUtil", ColumnType::UTILIZATION},
      {"ram.finalUtil", ColumnType::UTILIZATION}};
  colNameAndTypeAreAsExpected(expectedRowColumnNameAndType, "row", tableData);

  // Verify column names and types for the msb (scope) table
  // Note: dynamicLoad table is built in ModelServer, but dynamicLoad.initUtil
  // and dynamicLoad.finalUtil are built in LoadModel for scope tables
  const std::map<std::string, ColumnType> expectedMsbColumnNameAndType = {
      {"msb", ColumnType::ENTITY_NAME},
      {"network", ColumnType::DIMENSION},
      {"host_count.initUtil", ColumnType::UTILIZATION},
      {"network.initUtil", ColumnType::UTILIZATION},
      {"ram.initUtil", ColumnType::UTILIZATION},
      {"host_count.finalUtil", ColumnType::UTILIZATION},
      {"network.finalUtil", ColumnType::UTILIZATION},
      {"ram.finalUtil", ColumnType::UTILIZATION},
      {"dynamicLoad.initUtil", ColumnType::UTILIZATION},
      {"dynamicLoad.finalUtil", ColumnType::UTILIZATION}};
  colNameAndTypeAreAsExpected(expectedMsbColumnNameAndType, "msb", tableData);
  const std::vector<std::string> expectedMsbColumns = {
      "msb",
      "network",
      "dynamicLoad.finalUtil",
      "dynamicLoad.initUtil",
      "host_count.finalUtil",
      "host_count.initUtil",
      "network.finalUtil",
      "network.initUtil",
      "ram.finalUtil",
      "ram.initUtil"};
  EXPECT_EQ(expectedMsbColumns, tableData.at("msb").getColumnNames());

  const auto& objectColumnData = tableData.at("host").getColumnData();
  for (auto& column : objectColumnData) {
    // Only the host column is a primary key.
    EXPECT_EQ(column->isPrimaryKey(), column->getColumnName() == "host");
    if (column->getColumnName() == "src.rack") {
      // assert host0 is initially on rack0
      EXPECT_EQ(
          "rack0",
          column->getStrView(toEntityId(universe.getObjectId("host0"))));

      // ensure host 3 is on rack 1
      EXPECT_EQ(
          "rack1",
          column->getStrView(toEntityId(universe.getObjectId("host3"))));
    } else if (column->getColumnName() == "dst.rack") {
      // assert host0 is moved to rack2
      EXPECT_EQ(
          "rack2",
          column->getStrView(toEntityId(universe.getObjectId("host0"))));
    } else if (column->getColumnName() == "ram") {
      // assert normal object dimension
      EXPECT_DOUBLE_EQ(
          128000, column->getDouble(toEntityId(universe.getObjectId("host2"))));
    } else if (column->getColumnName() == "network_0") {
      EXPECT_DOUBLE_EQ(
          3.0, column->getDouble(toEntityId(universe.getObjectId("host3"))));
    } else if (column->getColumnName() == "network_1") {
      EXPECT_DOUBLE_EQ(
          4.5, column->getDouble(toEntityId(universe.getObjectId("host3"))));
    } else if (column->getColumnName() == "network_2") {
      EXPECT_DOUBLE_EQ(
          1.0, column->getDouble(toEntityId(universe.getObjectId("host3"))));
    } else if (column->getColumnName() == "scheme") {
      // assert partition data for object
      EXPECT_EQ(
          "twshared",
          column->getStrView(toEntityId(universe.getObjectId("host3"))));
      EXPECT_EQ(
          "cache",
          column->getStrView(toEntityId(universe.getObjectId("host1"))));

      // assert default partition is empty
      EXPECT_EQ(
          "", column->getStrView(toEntityId(universe.getObjectId("host2"))));
    } else if (column->getColumnName() == kEquivSetPartition.data()) {
      const auto allObjectIds = universe.getObjects().getObjectIds();
      std::set<std::string> equivSetNames;
      for (auto objectId : allObjectIds) {
        equivSetNames.emplace(column->getStrView(toEntityId(objectId)));
      }

      // assert all objects are in the same equivalence set, since there is no
      // goal or constraint in the problem
      EXPECT_EQ(1, equivSetNames.size());
      EXPECT_TRUE(equivSetNames.begin()->starts_with(
          interface::kEquivSetNamePrefix.data()));
    }
  }

  // assert container data
  const auto& containerTable = tableData.at("rack");
  const auto& containerColumnData = containerTable.getColumnData();
  const auto rack0RowId = getRowId(containerTable, "rack0");
  const auto rack2RowId = getRowId(containerTable, "rack2");
  EXPECT_EQ(3, containerTable.getRowIds().size());
  for (const auto& column : containerColumnData) {
    // Only the rack column is a primary key.
    EXPECT_EQ(column->isPrimaryKey(), column->getColumnName() == "rack");
    if (column->getColumnName() == "msb") {
      EXPECT_EQ("msb1", column->getStrView(rack2RowId));
    } else if (column->getColumnName() == "row") {
      EXPECT_EQ("row1", column->getStrView(rack2RowId));
    } else if (column->getColumnName() == "row_incomplete") {
      EXPECT_EQ("", column->getStrView(rack2RowId));
    } else if (column->getColumnName() == "network.initUtil") {
      EXPECT_DOUBLE_EQ(0.09, column->getDouble(rack0RowId));
    } else if (column->getColumnName() == "host_count.initUtil") {
      EXPECT_DOUBLE_EQ(3.0, column->getDouble(rack0RowId));
    }
  }

  // assert row data
  const auto& rowTable = tableData.at("row");
  const auto& rowColumnData = rowTable.getColumnData();
  const auto row0RowId = getRowId(rowTable, "row0");
  const auto row1RowId = getRowId(rowTable, "row1");
  for (const auto& column : rowColumnData) {
    // Only the row column is a primary key.
    EXPECT_EQ(column->isPrimaryKey(), column->getColumnName() == "row");
    if (column->getColumnName() == "network.initUtil") {
      EXPECT_DOUBLE_EQ(9.0, column->getDouble(row0RowId));
      EXPECT_DOUBLE_EQ(4.5, column->getDouble(row1RowId));
    }
  }

  // assert scope data
  const auto& msbTable = tableData.at("msb");
  const auto& msbScopeColumnData = msbTable.getColumnData();
  const auto msb0RowId = getRowId(msbTable, "msb0");
  const auto msb1RowId = getRowId(msbTable, "msb1");
  EXPECT_EQ(2, msbTable.getRowIds().size());
  for (const auto& column : msbScopeColumnData) {
    // Only the msb column is a primary key.
    EXPECT_EQ(column->isPrimaryKey(), column->getColumnName() == "msb");
    if (column->getColumnName() == "host_count.initUtil") {
      EXPECT_DOUBLE_EQ(4.0, column->getDouble(msb0RowId));
      EXPECT_DOUBLE_EQ(1.0, column->getDouble(msb1RowId));
    } else if (column->getColumnName() == "host_count.finalUtil") {
      EXPECT_DOUBLE_EQ(3.0, column->getDouble(msb0RowId));
      EXPECT_DOUBLE_EQ(2.0, column->getDouble(msb1RowId));
    } else if (column->getColumnName() == "network") {
      EXPECT_DOUBLE_EQ(1000.0, column->getDouble(msb0RowId));
      EXPECT_DOUBLE_EQ(0.0, column->getDouble(msb1RowId));
    } else if (column->getColumnName() == "network.initUtil") {
      EXPECT_DOUBLE_EQ(0.0115, column->getDouble(msb0RowId));
      EXPECT_TRUE(std::isnan(column->getDouble(msb1RowId)));
    } else if (column->getColumnName() == "dynamicLoad.initUtil") {
      EXPECT_DOUBLE_EQ(13.0, column->getDouble(msb0RowId));
      EXPECT_DOUBLE_EQ(1.0, column->getDouble(msb1RowId));
    } else if (column->getColumnName() == "dynamicLoad.finalUtil") {
      EXPECT_DOUBLE_EQ(3.0, column->getDouble(msb0RowId));
      EXPECT_DOUBLE_EQ(2.0, column->getDouble(msb1RowId));
    }
  }
}

TEST(LoadModelTest, OverlappingPartitionJoinsGroups) {
  auto bundle = TestUtils::buildBundle({.includeOverlappedPartition = true});
  auto explorerModel = LoadModel::buildData(std::move(bundle));
  addObjectTable(explorerModel);

  const auto& universe = *explorerModel.universe;
  const auto& table = explorerModel.tableData.at("host");
  const auto partitionColumn =
      Utils::fetchColumn(table.getColumnData(), "overlapped");
  EXPECT_EQ(
      "group1, group2",
      partitionColumn->getStrView(toEntityId(universe.getObjectId("host0"))));
  EXPECT_EQ(
      "group1",
      partitionColumn->getStrView(toEntityId(universe.getObjectId("host3"))));
  EXPECT_EQ(
      "",
      partitionColumn->getStrView(toEntityId(universe.getObjectId("host4"))));
}

TEST(LoadModelTest, DynamicDimensionTableUsesGroupRowsForCompactStorage) {
  UniverseProblemBuilder builder(
      std::make_unique<AsyncConfig>(getTestExecutor(/*numThreads=*/true)));
  builder.setObjectName("host");
  builder.setContainerName("rack");
  builder.setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"rack0", {"host0"}},
          {"rack1", {"host1"}},
          {"rack2", {"host2"}},
      });
  builder.addScope(
      "zone",
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"zone0", {"rack0", "rack1"}}, {"zone1", {"rack2"}}});
  builder.addPartition(
      "service",
      std::map<std::string, std::vector<std::string>>{
          {"web", {"host0", "host1"}}, {"db", {"host2"}}});

  const auto universe = builder.build();
  const auto scopeId = universe->getScopeId("zone");
  const auto zone0Id = universe->getScopeItemId(scopeId, "zone0");
  const auto partitionId = universe->getPartitionId("service");
  auto partition = std::shared_ptr<const entities::Partition>(
      universe, &universe->getPartition(partitionId));

  entities::GroupIdToDoubleMap groupValues;
  groupValues.emplace(universe->getGroupId(partitionId, "web"), 7.0);
  entities::Map<
      entities::ScopeItemId,
      std::shared_ptr<const entities::GroupIdToDoubleMap>>
      scopeItemValues;
  scopeItemValues.emplace(
      zone0Id,
      std::make_shared<const entities::GroupIdToDoubleMap>(
          std::move(groupValues)));

  const entities::ObjectDimension compactObjectDimension(
      scopeId,
      std::move(partition),
      scopeItemValues,
      /*defaultValue=*/1.0,
      universe->getNumObjects(),
      partitionId);
  const auto& compactDimension = compactObjectDimension.only();
  EXPECT_EQ(nullptr, compactDimension.values(zone0Id).asMapOrNull());

  const auto table = LoadModel::buildDynamicDimensionTable(
      *universe, compactDimension, "load");
  const auto objectNames = Utils::fetchColumn(table.getColumnData(), "service");
  const auto scopeItemNames = Utils::fetchColumn(table.getColumnData(), "zone");
  const auto dimensionValues =
      Utils::fetchColumn(table.getColumnData(), "load");

  EXPECT_EQ(3, table.getColumnData().size());
  ASSERT_EQ(2, table.getRowIds().size());
  const EntityId defaultRow(0);
  const EntityId groupRow(1);
  EXPECT_EQ("default", objectNames->getStrView(defaultRow));
  EXPECT_EQ("default", scopeItemNames->getStrView(defaultRow));
  EXPECT_DOUBLE_EQ(1.0, dimensionValues->getDouble(defaultRow));
  EXPECT_EQ("web", objectNames->getStrView(groupRow));
  EXPECT_EQ("zone0", scopeItemNames->getStrView(groupRow));
  EXPECT_DOUBLE_EQ(7.0, dimensionValues->getDouble(groupRow));
}

TEST(LoadModelTest, MultiComponentDynamicDimensionsHaveUniqueNames) {
  auto bundle = TestUtils::buildBundle();
  auto& dimensions = *bundle.problem()->universe()->objects()->dimensions();
  entities::thrift::ObjectDimension* dynamicDimension = nullptr;
  for (auto& entry : dimensions) {
    auto& dimension = entry.second;
    if (*dimension.isDynamic()) {
      dynamicDimension = &dimension;
      break;
    }
  }
  if (!dynamicDimension) {
    FAIL() << "Expected the test universe to contain a dynamic dimension";
  }
  auto& scalarDimensions = *dynamicDimension->scalarDimensions();
  ASSERT_EQ(1, scalarDimensions.size());
  scalarDimensions.push_back(scalarDimensions.front());

  auto explorerModel = LoadModel::buildData(std::move(bundle));
  addObjectTable(explorerModel);
  const auto& universe = *explorerModel.universe;
  auto dynamicDimensionName = explorerModel.dynamicDimensionNames.begin();
  std::set<std::string> detailColumnNames;
  for (const auto dimensionId : universe.getObjects().getDimensionIds()) {
    const auto& dimension = universe.getObjects().getDimension(dimensionId);
    for (int scalarIndex = 0; scalarIndex < dimension.size(); ++scalarIndex) {
      if (!dimension.at(scalarIndex).isDynamic()) {
        continue;
      }
      ASSERT_NE(
          dynamicDimensionName, explorerModel.dynamicDimensionNames.end());
      const auto table = LoadModel::buildDynamicDimensionTable(
          universe, dimension.at(scalarIndex), *dynamicDimensionName++);
      for (const auto& column : table.getColumnData()) {
        if (column->getColumnType() == ColumnType::DIMENSION) {
          detailColumnNames.insert(column->getColumnName());
        }
      }
    }
  }
  EXPECT_EQ(dynamicDimensionName, explorerModel.dynamicDimensionNames.end());

  const auto expectedDetailColumnNames =
      makeSet<std::string>({"dynamicLoad_0", "dynamicLoad_1"});
  EXPECT_EQ(expectedDetailColumnNames, detailColumnNames);

  std::set<std::string> objectColumnNames;
  for (const auto& columnName :
       explorerModel.tableData.at(universe.getObjectTypeName())
           .getColumnNames()) {
    if (columnName.starts_with("src.dynamicLoad") ||
        columnName.starts_with("dst.dynamicLoad")) {
      objectColumnNames.insert(columnName);
    }
  }
  const auto expectedObjectColumnNames = makeSet<std::string>(
      {"src.dynamicLoad_0",
       "dst.dynamicLoad_0",
       "src.dynamicLoad_1",
       "dst.dynamicLoad_1"});
  EXPECT_EQ(expectedObjectColumnNames, objectColumnNames);
}

TEST(ModelTest, MoveGroupTogether) {
  // Build the problem.
  UniverseProblemBuilder builder(
      std::make_unique<AsyncConfig>(getTestExecutor(/*numThreads=*/true)));
  builder.setObjectName("host");
  builder.setContainerName("rack");
  builder.setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"rack0", {"host0", "host1", "host3"}},
          {"rack1", {"host4"}},
          {"rack2", {}},
      });
  builder.addPartition(
      "units",
      std::map<std::string, std::vector<std::string>>({
          {"unit1", {"host0", "host1"}},
          {"unit2", {"host3"}},
          {"unit3", {"host4"}},
      }));

  {
    auto spec = MoveGroupSpec();
    spec.partitionName() = "units";
    builder.addConstraint(
        spec, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
  }

  // final assignment
  const folly::F14FastMap<std::string, std::string> finalAssignment = {
      {"host0", "rack1"},
      {"host1", "rack1"},
      {"host3", "rack0"},
      {"host4", "rack2"},
  };

  // load the problem
  AssignmentProblem problem;
  problem.constraintPolicy() = ConstraintPolicy::DEFAULT;

  ThriftStrategyBuilder strategyBuilder;

  LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec()));
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(TripleLoopMoveTypeSpec()));
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(KLSearchMoveTypeSpec()));
  strategyBuilder.addSolver(solverSpec);

  problem.strategy() = strategyBuilder.build();
  problem.universe() = builder.build()->toThrift();

  AssignmentSolution solution;
  solution.assignment() = finalAssignment;

  Bundle bundle;
  bundle.problem() = std::move(problem);
  bundle.solution() = std::move(solution);

  auto explorerModel = LoadModel::buildData(std::move(bundle));
  addObjectTable(explorerModel);
  const auto& universe = *explorerModel.universe;
  const auto& tableData = explorerModel.tableData;

  const auto& objectColumnData = tableData.at("host").getColumnData();
  for (auto& column : objectColumnData) {
    if (column->getColumnName() == "src.rack") {
      // assert host0 and host1 were initially at rack 0
      EXPECT_EQ(
          "rack0",
          column->getStrView(toEntityId(universe.getObjectId("host0"))));
      EXPECT_EQ(
          "rack0",
          column->getStrView(toEntityId(universe.getObjectId("host1"))));
      // host 3 remained at rack 0
      EXPECT_EQ(
          "rack0",
          column->getStrView(toEntityId(universe.getObjectId("host3"))));
      // host 4 moved from rack1
      EXPECT_EQ(
          "rack1",
          column->getStrView(toEntityId(universe.getObjectId("host4"))));
    } else if (column->getColumnName() == "dst.rack") {
      // assert host0 and host1 moved to rack1
      EXPECT_EQ(
          "rack1",
          column->getStrView(toEntityId(universe.getObjectId("host0"))));
      EXPECT_EQ(
          "rack1",
          column->getStrView(toEntityId(universe.getObjectId("host1"))));
      // host 3 remained at rack0
      EXPECT_EQ(
          "rack0",
          column->getStrView(toEntityId(universe.getObjectId("host3"))));
      // host 4 moved from rack2
      EXPECT_EQ(
          "rack2",
          column->getStrView(toEntityId(universe.getObjectId("host4"))));
    } else if (column->getColumnName() == kEquivSetPartition.data()) {
      // expect two equivalence sets, one for host0 and host1, and one for
      // host3 and host4
      EXPECT_EQ(
          column->getStrView(toEntityId(universe.getObjectId("host0"))),
          column->getStrView(toEntityId(universe.getObjectId("host1"))));
      EXPECT_EQ(
          column->getStrView(toEntityId(universe.getObjectId("host3"))),
          column->getStrView(toEntityId(universe.getObjectId("host4"))));
    }
  }
}

TEST(LoadModelTest, IsMovableTest) {
  AvoidMovingSpec spec;
  spec.name() = "avoid_moving";
  spec.objects() = {"host1"};

  std::vector<MoveInProgress> movesInProgress;
  MoveInProgress moveInProgress;
  moveInProgress.objName() = "host3";
  moveInProgress.toContainer() = "rack2";
  movesInProgress.push_back(std::move(moveInProgress));
  MovesInProgressSpec inProgressSpec;
  inProgressSpec.name() = "in_progress";
  inProgressSpec.moves() = std::move(movesInProgress);

  auto bundle = TestUtils::buildBundle(
      {.spec = std::move(spec), .inProgressSpec = std::move(inProgressSpec)});
  auto explorerModel = LoadModel::buildData(std::move(bundle));
  addObjectTable(explorerModel);
  const auto& universe = *explorerModel.universe;
  const auto& tableData = explorerModel.tableData;

  const auto getObjectId = [&universe](const std::string& name) {
    return universe.getObjectId(name);
  };

  const auto& objectsTable = tableData.at("host");
  const auto& objectColumnData = objectsTable.getColumnData();
  const auto& isMovableColumn = objectColumnData.at(1);
  EXPECT_EQ("is_movable", isMovableColumn->getColumnName());
  EXPECT_EQ(ColumnType::INTEGER, isMovableColumn->getColumnType());
  EXPECT_DOUBLE_EQ(
      1, isMovableColumn->getDouble(toEntityId(getObjectId("host0"))));
  EXPECT_DOUBLE_EQ(
      0, isMovableColumn->getDouble(toEntityId(getObjectId("host1"))));
  EXPECT_DOUBLE_EQ(
      0, isMovableColumn->getDouble(toEntityId(getObjectId("host3"))));

  const auto& movesInProgressColumn = objectColumnData.at(2);
  EXPECT_EQ("has_move_in_progress", movesInProgressColumn->getColumnName());
  EXPECT_EQ(ColumnType::INTEGER, movesInProgressColumn->getColumnType());
  EXPECT_FALSE(movesInProgressColumn->isPrimaryKey());
  EXPECT_DOUBLE_EQ(
      0, movesInProgressColumn->getDouble(toEntityId(getObjectId("host0"))));
  EXPECT_DOUBLE_EQ(
      0, movesInProgressColumn->getDouble(toEntityId(getObjectId("host1"))));
  EXPECT_DOUBLE_EQ(
      1, movesInProgressColumn->getDouble(toEntityId(getObjectId("host3"))));
  EXPECT_EQ(
      "1 when object is part of a MovesInProgressSpec, 0 otherwise",
      movesInProgressColumn->getDescription());
}

TEST(LoadModelTest, BasicWithNoSolutionObject) {
  auto buildOptions = BuildFilesOptions();
  buildOptions.includeSolutionObject = false;

  auto bundle = TestUtils::buildBundle(buildOptions);
  auto explorerModel = LoadModel::buildData(std::move(bundle));
  addObjectTable(explorerModel);
  const auto& universe = *explorerModel.universe;
  auto tableData = std::move(explorerModel.tableData);

  const std::set<std::string> expectedObjectColumnsSet = {
      "host",
      "is_movable",
      "host_count",
      "network_0",
      "network_1",
      "network_2",
      "ram",
      "scheme",
      "src.dynamicLoad",
      "dst.dynamicLoad",
      "src.rack",
      "dst.rack",
      "src.row",
      "dst.row",
      "src.row_incomplete",
      "dst.row_incomplete",
      "src.msb",
      "dst.msb",
      kEquivSetPartition
          .data(), // equivalence set partition column is always added
  };

  const auto& objectColumns = tableData.at("host").getColumnNames();
  auto objectColumnSet =
      std::set<std::string>(objectColumns.begin(), objectColumns.end());
  EXPECT_EQ(expectedObjectColumnsSet, objectColumnSet);

  const std::set<std::string> expectedMsbColumnsSet = {
      "msb",
      "network",
      "host_count.initUtil",
      "network.initUtil",
      "ram.initUtil",
      "host_count.finalUtil",
      "network.finalUtil",
      "ram.finalUtil",
      // dynamic dimensions only appear in the appropriate scope
      "dynamicLoad.initUtil",
      "dynamicLoad.finalUtil"};
  const auto& scopeColumns = tableData.at("msb").getColumnNames();
  auto scopeColumnsSet =
      std::set<std::string>(scopeColumns.begin(), scopeColumns.end());
  EXPECT_EQ(expectedMsbColumnsSet, scopeColumnsSet);

  const std::set<std::string> expectedContainerColumnsSet = {
      "rack",
      "row",
      "row_incomplete",
      "msb",
      "network",
      "host_count.initUtil",
      "network.initUtil",
      "ram.initUtil",
      "host_count.finalUtil",
      "network.finalUtil",
      "ram.finalUtil"};
  const auto& containerColumns = tableData.at("rack").getColumnNames();
  auto containerColumnsSet =
      std::set<std::string>(containerColumns.begin(), containerColumns.end());
  EXPECT_EQ(expectedContainerColumnsSet, containerColumnsSet);

  const auto& objectColumnData = tableData.at("host").getColumnData();
  for (auto& column : objectColumnData) {
    // Only the host column is a primary key.
    EXPECT_EQ(column->isPrimaryKey(), column->getColumnName() == "host");
    if (column->getColumnName() == "ram") {
      // assert normal object dimension
      EXPECT_DOUBLE_EQ(
          128000, column->getDouble(toEntityId(universe.getObjectId("host2"))));
    } else if (column->getColumnName() == "dst.dynamicLoad") {
      EXPECT_DOUBLE_EQ(
          1.0, column->getDouble(toEntityId(universe.getObjectId("host0"))));
    } else if (column->getColumnName() == "network_0") {
      EXPECT_DOUBLE_EQ(
          3.0, column->getDouble(toEntityId(universe.getObjectId("host3"))));
    } else if (column->getColumnName() == "network_1") {
      EXPECT_DOUBLE_EQ(
          4.5, column->getDouble(toEntityId(universe.getObjectId("host3"))));
    } else if (column->getColumnName() == "network_2") {
      EXPECT_DOUBLE_EQ(
          1.0, column->getDouble(toEntityId(universe.getObjectId("host3"))));
    } else if (column->getColumnName() == kEquivSetPartition.data()) {
      const auto allObjectIds = universe.getObjects().getObjectIds();
      std::set<std::string> equivSetNames;
      for (auto objectId : allObjectIds) {
        equivSetNames.emplace(column->getStrView(toEntityId(objectId)));
      }

      // assert all objects are in the same equivalence set, since there is no
      // goal or constraint in the problem
      EXPECT_EQ(1, equivSetNames.size());
      EXPECT_TRUE(equivSetNames.begin()->starts_with(
          interface::kEquivSetNamePrefix.data()));
    }
  }

  // assert row data
  const auto& rowTable = tableData.at("row");
  const auto& rowColumnData = rowTable.getColumnData();
  const auto row0RowId = getRowId(rowTable, "row0");
  for (auto& column : rowColumnData) {
    if (column->getColumnName() == "network.initUtil") {
      EXPECT_DOUBLE_EQ(9.0, column->getDouble(row0RowId));
    }
  }

  // assert scope data
  const auto& msbTable = tableData.at("msb");
  const auto& msbScopeColumnData = msbTable.getColumnData();
  const auto msb0RowId = getRowId(msbTable, "msb0");
  const auto msb1RowId = getRowId(msbTable, "msb1");
  for (auto& column : msbScopeColumnData) {
    if (column->getColumnName() == "host_count.initUtil") {
      EXPECT_DOUBLE_EQ(4.0, column->getDouble(msb0RowId));
    } else if (column->getColumnName() == "host_count.finalUtil") {
      // note that this is zero because there is no solution object
      EXPECT_DOUBLE_EQ(0.0, column->getDouble(msb0RowId));
    } else if (column->getColumnName() == "network") {
      EXPECT_DOUBLE_EQ(1000.0, column->getDouble(msb0RowId));
      EXPECT_DOUBLE_EQ(0.0, column->getDouble(msb1RowId));
    } else if (column->getColumnName() == "network.initUtil") {
      EXPECT_DOUBLE_EQ(0.0115, column->getDouble(msb0RowId));
      EXPECT_TRUE(std::isnan(column->getDouble(msb1RowId)));
    }
  }
}

} // namespace facebook::rebalancer::explorer::tests
