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
#include <string>

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

TEST(LoadModelTest, Basic) {
  auto bundle = TestUtils::buildBundle();
  auto explorerModel = LoadModel::buildData(std::move(bundle));
  const auto& universe = *explorerModel.universe;
  const auto& tableData = std::move(explorerModel.tableData);

  // Verify column names and types for the host (object) table
  // Note: src.dynamicLoad and dst.dynamicLoad are built in ModelServer, not
  // LoadModel
  const std::map<std::string, ColumnType> expectedHostColumnNameAndType = {
      {"host", ColumnType::ENTITY_NAME},
      {"is_movable", ColumnType::INTEGER},
      {"host_count", ColumnType::DIMENSION},
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

  const auto& objectColumnData = tableData.at("host").getColumnData();
  for (auto& column : objectColumnData) {
    // Only the host column is a primary key.
    EXPECT_EQ(column->isPrimaryKey(), column->getColumnName() == "host");
    if (column->getColumnName() == "src.rack") {
      // assert host0 is initially on rack0
      EXPECT_EQ(
          "rack0",
          *column->getValue(toEntityId(universe.getObjectId("host0")))
               .strValue);

      // ensure host 3 is on rack 1
      EXPECT_EQ(
          "rack1",
          *column->getValue(toEntityId(universe.getObjectId("host3")))
               .strValue);
    } else if (column->getColumnName() == "dst.rack") {
      // assert host0 is moved to rack2
      EXPECT_EQ(
          "rack2",
          *column->getValue(toEntityId(universe.getObjectId("host0")))
               .strValue);
    } else if (column->getColumnName() == "ram") {
      // assert normal object dimension
      EXPECT_EQ(
          128000,
          *column->getValue(toEntityId(universe.getObjectId("host2")))
               .doubleValue);
    } else if (column->getColumnName() == "network") {
      // asset network dimension for host3 are added
      EXPECT_EQ(
          8.0,
          *column->getValue(toEntityId(universe.getObjectId("host3")))
               .doubleValue);
    } else if (column->getColumnName() == "scheme") {
      // assert partition data for object
      EXPECT_EQ(
          "twshared",
          *column->getValue(toEntityId(universe.getObjectId("host3")))
               .strValue);
      EXPECT_EQ(
          "cache",
          *column->getValue(toEntityId(universe.getObjectId("host1")))
               .strValue);

      // assert default partition is empty
      EXPECT_EQ(
          "",
          *column->getValue(toEntityId(universe.getObjectId("host2")))
               .strValue);
    } else if (column->getColumnName() == kEquivSetPartition.data()) {
      const auto allObjectIds = universe.getObjects().getObjectIds();
      std::set<std::string> equivSetNames;
      for (auto objectId : allObjectIds) {
        equivSetNames.insert(*column->getValue(toEntityId(objectId)).strValue);
      }

      // assert all objects are in the same equivalence set, since there is no
      // goal or constraint in the problem
      EXPECT_EQ(1, equivSetNames.size());
      EXPECT_TRUE(equivSetNames.begin()->starts_with(
          interface::kEquivSetNamePrefix.data()));
    }
  }

  // assert container data
  const auto& containerColumnData = tableData.at("rack").getColumnData();
  for (const auto& column : containerColumnData) {
    // Only the rack column is a primary key.
    EXPECT_EQ(column->isPrimaryKey(), column->getColumnName() == "rack");
    if (column->getColumnName() == "msb") {
      EXPECT_EQ(
          "msb1",
          *column->getValue(toEntityId(universe.getContainerId("rack2")))
               .strValue);
    } else if (column->getColumnName() == "row") {
      EXPECT_EQ(
          "row1",
          *column->getValue(toEntityId(universe.getContainerId("rack2")))
               .strValue);
    } else if (column->getColumnName() == "network.initUtil") {
      EXPECT_EQ(
          0.09,
          *column->getValue(toEntityId(universe.getContainerId("rack0")))
               .doubleValue);
    } else if (column->getColumnName() == "host_count.initUtil") {
      EXPECT_EQ(
          3,
          *column->getValue(toEntityId(universe.getContainerId("rack0")))
               .doubleValue);
    }
  }

  // assert row data
  const auto& rowColumnData = tableData.at("row").getColumnData();
  auto rowScopeId = universe.getScopeId("row");
  for (const auto& column : rowColumnData) {
    // Only the row column is a primary key.
    EXPECT_EQ(column->isPrimaryKey(), column->getColumnName() == "row");
    if (column->getColumnName() == "network.initUtil") {
      EXPECT_EQ(
          9.0,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(rowScopeId, "row0")))
               .doubleValue);
      EXPECT_EQ(
          4.5,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(rowScopeId, "row1")))
               .doubleValue);
    }
  }

  // assert scope data
  const auto& msbScopeColumnData = tableData.at("msb").getColumnData();
  auto msbScopeId = universe.getScopeId("msb");
  for (const auto& column : msbScopeColumnData) {
    // Only the msb column is a primary key.
    EXPECT_EQ(column->isPrimaryKey(), column->getColumnName() == "msb");
    if (column->getColumnName() == "host_count.initUtil") {
      EXPECT_EQ(
          4,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(msbScopeId, "msb0")))
               .doubleValue);
      EXPECT_EQ(
          1,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(msbScopeId, "msb1")))
               .doubleValue);
    } else if (column->getColumnName() == "host_count.finalUtil") {
      EXPECT_EQ(
          3,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(msbScopeId, "msb0")))
               .doubleValue);
      EXPECT_EQ(
          2,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(msbScopeId, "msb1")))
               .doubleValue);
    } else if (column->getColumnName() == "network") {
      EXPECT_EQ(
          1000.0,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(msbScopeId, "msb0")))
               .doubleValue);
      EXPECT_EQ(
          0,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(msbScopeId, "msb1")))
               .doubleValue);
    } else if (column->getColumnName() == "network.initUtil") {
      EXPECT_EQ(
          0.0115,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(msbScopeId, "msb0")))
               .doubleValue);
      EXPECT_TRUE(
          std::isnan(*column
                          ->getValue(toEntityId(
                              universe.getScopeItemId(msbScopeId, "msb1")))
                          .doubleValue));
    } else if (column->getColumnName() == "dynamicLoad.initUtil") {
      EXPECT_EQ(
          13.0,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(msbScopeId, "msb0")))
               .doubleValue);
      EXPECT_EQ(
          1.0,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(msbScopeId, "msb1")))
               .doubleValue);
    } else if (column->getColumnName() == "dynamicLoad.finalUtil") {
      EXPECT_EQ(
          3.0,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(msbScopeId, "msb0")))
               .doubleValue);
      EXPECT_EQ(
          2.0,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(msbScopeId, "msb1")))
               .doubleValue);
    }
  }
}

TEST(LoadModelTest, DynamicDimensionColumnsUseGroupRowsForCompactStorage) {
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

  const DynamicDimensionColumns columns(
      *universe,
      compactDimension,
      "load",
      universe->getContainers().getInitialAssignment());

  EXPECT_EQ("service", columns.objectNamesColumn->getColumnName());
  ASSERT_EQ(2, columns.rowIds.size());
  const EntityId defaultRow(0);
  const EntityId groupRow(1);
  EXPECT_EQ(
      DataCell("default"), columns.objectNamesColumn->getValue(defaultRow));
  EXPECT_EQ(
      DataCell("default"), columns.scopeItemNamesColumn->getValue(defaultRow));
  EXPECT_EQ(DataCell(1.0), columns.dimensionValuesColumn->getValue(defaultRow));
  EXPECT_EQ(DataCell("web"), columns.objectNamesColumn->getValue(groupRow));
  EXPECT_EQ(
      DataCell("zone0"), columns.scopeItemNamesColumn->getValue(groupRow));
  EXPECT_EQ(DataCell(7.0), columns.dimensionValuesColumn->getValue(groupRow));
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
  const auto& universe = *explorerModel.universe;
  const auto& tableData = explorerModel.tableData;

  const auto& objectColumnData = tableData.at("host").getColumnData();
  for (auto& column : objectColumnData) {
    if (column->getColumnName() == "src.rack") {
      // assert host0 and host1 were initially at rack 0
      EXPECT_EQ(
          "rack0",
          *column->getValue(toEntityId(universe.getObjectId("host0")))
               .strValue);
      EXPECT_EQ(
          "rack0",
          *column->getValue(toEntityId(universe.getObjectId("host1")))
               .strValue);
      // host 3 remained at rack 0
      EXPECT_EQ(
          "rack0",
          *column->getValue(toEntityId(universe.getObjectId("host3")))
               .strValue);
      // host 4 moved from rack1
      EXPECT_EQ(
          "rack1",
          *column->getValue(toEntityId(universe.getObjectId("host4")))
               .strValue);
    } else if (column->getColumnName() == "dst.rack") {
      // assert host0 and host1 moved to rack1
      EXPECT_EQ(
          "rack1",
          *column->getValue(toEntityId(universe.getObjectId("host0")))
               .strValue);
      EXPECT_EQ(
          "rack1",
          *column->getValue(toEntityId(universe.getObjectId("host1")))
               .strValue);
      // host 3 remained at rack0
      EXPECT_EQ(
          "rack0",
          *column->getValue(toEntityId(universe.getObjectId("host3")))
               .strValue);
      // host 4 moved from rack2
      EXPECT_EQ(
          "rack2",
          *column->getValue(toEntityId(universe.getObjectId("host4")))
               .strValue);
    } else if (column->getColumnName() == kEquivSetPartition.data()) {
      // expect two equivalence sets, one for host0 and host1, and one for
      // host3 and host4
      EXPECT_EQ(
          *column->getValue(toEntityId(universe.getObjectId("host0"))).strValue,
          *column->getValue(toEntityId(universe.getObjectId("host1")))
               .strValue);
      EXPECT_EQ(
          *column->getValue(toEntityId(universe.getObjectId("host3"))).strValue,
          *column->getValue(toEntityId(universe.getObjectId("host4")))
               .strValue);
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
  EXPECT_EQ(
      1,
      *isMovableColumn->getValue(toEntityId(getObjectId("host0"))).doubleValue);
  EXPECT_EQ(
      0,
      *isMovableColumn->getValue(toEntityId(getObjectId("host1"))).doubleValue);
  EXPECT_EQ(
      0,
      *isMovableColumn->getValue(toEntityId(getObjectId("host3"))).doubleValue);

  const auto& movesInProgressColumn = objectColumnData.at(2);
  EXPECT_EQ("has_move_in_progress", movesInProgressColumn->getColumnName());
  EXPECT_EQ(ColumnType::INTEGER, movesInProgressColumn->getColumnType());
  EXPECT_FALSE(movesInProgressColumn->isPrimaryKey());
  EXPECT_EQ(
      0,
      *movesInProgressColumn->getValue(toEntityId(getObjectId("host0")))
           .doubleValue);
  EXPECT_EQ(
      0,
      *movesInProgressColumn->getValue(toEntityId(getObjectId("host1")))
           .doubleValue);
  EXPECT_EQ(
      1,
      *movesInProgressColumn->getValue(toEntityId(getObjectId("host3")))
           .doubleValue);
  EXPECT_EQ(
      "1 when object is part of a MovesInProgressSpec, 0 otherwise",
      movesInProgressColumn->getDescription());
}

TEST(LoadModelTest, BasicWithNoSolutionObject) {
  auto buildOptions = BuildFilesOptions();
  buildOptions.includeSolutionObject = false;

  auto bundle = TestUtils::buildBundle(buildOptions);
  auto explorerModel = LoadModel::buildData(std::move(bundle));
  const auto& universe = *explorerModel.universe;
  auto tableData = std::move(explorerModel.tableData);

  // Object dimension columns, partition columns, and assignment columns are
  // now built asynchronously in ModelServer, so buildData only produces the
  // base object columns (entity name + movable flag). Call the public static
  // methods here to verify they still produce the expected columns.
  auto& objectsTable = tableData.at("host");
  objectsTable.insertColumnsInSortedOrder(
      LoadModel::buildPartitionCols(
          universe, explorerModel.equivalenceSetsData));
  for (auto& col : LoadModel::buildAssignmentCols(
           universe, explorerModel.finalAssignment)) {
    objectsTable.insertColumn(std::move(col));
  }

  const std::set<std::string> expectedObjectColumnsSet = {
      "host",
      "is_movable",
      "scheme",
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
      EXPECT_EQ(
          128000,
          *column->getValue(toEntityId(universe.getObjectId("host2")))
               .doubleValue);
    } else if (column->getColumnName() == "network") {
      // assert network dimension for host3 are added
      EXPECT_EQ(
          8.0,
          *column->getValue(toEntityId(universe.getObjectId("host3")))
               .doubleValue);
    } else if (column->getColumnName() == kEquivSetPartition.data()) {
      const auto allObjectIds = universe.getObjects().getObjectIds();
      std::set<std::string> equivSetNames;
      for (auto objectId : allObjectIds) {
        equivSetNames.insert(*column->getValue(toEntityId(objectId)).strValue);
      }

      // assert all objects are in the same equivalence set, since there is no
      // goal or constraint in the problem
      EXPECT_EQ(1, equivSetNames.size());
      EXPECT_TRUE(equivSetNames.begin()->starts_with(
          interface::kEquivSetNamePrefix.data()));
    }
  }

  // assert row data
  const auto& rowColumnData = tableData.at("row").getColumnData();
  auto rowScopeId = universe.getScopeId("row");
  for (auto& column : rowColumnData) {
    if (column->getColumnName() == "network.initUtil") {
      EXPECT_EQ(
          9.0,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(rowScopeId, "row0")))
               .doubleValue);
    }
  }

  // assert scope data
  const auto& msbScopeColumnData = tableData.at("msb").getColumnData();
  auto msbScopeId = universe.getScopeId("msb");
  for (auto& column : msbScopeColumnData) {
    if (column->getColumnName() == "host_count.initUtil") {
      EXPECT_EQ(
          4,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(msbScopeId, "msb0")))
               .doubleValue);
    } else if (column->getColumnName() == "host_count.finalUtil") {
      // note that this is zero because there is no solution object
      EXPECT_EQ(
          0,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(msbScopeId, "msb0")))
               .doubleValue);
    } else if (column->getColumnName() == "network") {
      EXPECT_EQ(
          1000.0,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(msbScopeId, "msb0")))
               .doubleValue);
      EXPECT_EQ(
          0,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(msbScopeId, "msb1")))
               .doubleValue);
    } else if (column->getColumnName() == "network.initUtil") {
      EXPECT_EQ(
          0.0115,
          *column
               ->getValue(
                   toEntityId(universe.getScopeItemId(msbScopeId, "msb0")))
               .doubleValue);
      EXPECT_TRUE(
          std::isnan(*column
                          ->getValue(toEntityId(
                              universe.getScopeItemId(msbScopeId, "msb1")))
                          .doubleValue));
    }
  }
}

} // namespace facebook::rebalancer::explorer::tests
