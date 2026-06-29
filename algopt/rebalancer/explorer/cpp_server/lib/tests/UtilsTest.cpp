// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "rebalancer/explorer/cpp_server/lib/Utils.h"

#include <gtest/gtest.h>

namespace entities = facebook::rebalancer::entities;

namespace facebook::rebalancer::explorer::tests {

class UtilsTest : public ::testing::Test {
 protected:
  static Table createTestTable() {
    const std::vector<EntityId> rowIds = {
        EntityId(1), EntityId(2), EntityId(3)};

    Table table(rowIds);

    entities::Map<EntityId, DataCell> nameValues;
    nameValues.emplace(EntityId(1), DataCell("Task0"));
    nameValues.emplace(EntityId(2), DataCell("Task1"));
    nameValues.emplace(EntityId(3), DataCell("Task2"));
    auto nameColumn = std::make_shared<Column>(
        nameValues, DataCell(""), "Name", ColumnType::STRING, true);

    entities::Map<EntityId, DataCell> loadValues;
    loadValues.emplace(EntityId(1), DataCell(25.0));
    loadValues.emplace(EntityId(2), DataCell(30.0));
    loadValues.emplace(EntityId(3), DataCell(35.0));
    auto loadColumn = std::make_shared<Column>(
        loadValues, DataCell(0.0), "Load", ColumnType::DOUBLE, false);

    entities::Map<EntityId, DataCell> hostValues;
    hostValues.emplace(EntityId(1), DataCell("Host0"));
    hostValues.emplace(EntityId(2), DataCell("Host1"));
    hostValues.emplace(EntityId(3), DataCell("Host2"));
    auto hostColumn = std::make_shared<Column>(
        hostValues, DataCell(""), "Host", ColumnType::STRING, false);

    table.insertColumn(nameColumn);
    table.insertColumn(loadColumn);
    table.insertColumn(hostColumn);

    return table;
  }
};

TEST_F(UtilsTest, FilterOutVec_EmptySet) {
  const std::set<int> toDeleteIds;
  const std::vector<int> ids = {1, 2, 3, 4, 5};
  const std::vector<int>& expected = ids;
  auto filteredIds = Utils::filterOut(toDeleteIds, ids);
  EXPECT_EQ(filteredIds, expected);
}
TEST_F(UtilsTest, FilterOutVec_NonEmptySet) {
  const std::set<int> toDeleteIds = {2, 4};
  const std::vector<int> ids = {1, 2, 3, 4, 5};
  const std::vector<int> expected = {1, 3, 5};
  auto filteredIds = Utils::filterOut(toDeleteIds, ids);
  EXPECT_EQ(filteredIds, expected);
}
TEST_F(UtilsTest, FilterOutVec_AllElementsInSet) {
  const std::set<int> toDeleteIds = {1, 2, 3, 4, 5};
  const std::vector<int> ids = {1, 2, 3, 4, 5};
  const std::vector<int> expected;
  auto filteredIds = Utils::filterOut(toDeleteIds, ids);
  EXPECT_EQ(filteredIds, expected);
}
TEST_F(UtilsTest, FilterOutMap_EmptySet) {
  const std::set<std::string> toDeleteIds;
  const std::map<std::string, int> map = {{"a", 1}, {"b", 2}, {"c", 3}};
  const std::map<std::string, int>& expected = map;
  auto filteredMap = Utils::filterOut(toDeleteIds, map);
  EXPECT_EQ(filteredMap, expected);
}
TEST_F(UtilsTest, FilterOutMap_NonEmptySet) {
  const std::set<std::string> toDeleteIds = {"b"};
  const std::map<std::string, int> map = {{"a", 1}, {"b", 2}, {"c", 3}};
  const std::map<std::string, int> expected = {{"a", 1}, {"c", 3}};
  auto filteredMap = Utils::filterOut(toDeleteIds, map);
  EXPECT_EQ(filteredMap, expected);
}
TEST_F(UtilsTest, FilterOutMap_AllKeysInSet) {
  const std::set<std::string> toDeleteIds = {"a", "b", "c"};
  const std::map<std::string, int> map = {{"a", 1}, {"b", 2}, {"c", 3}};
  const std::map<std::string, int> expected;
  auto filteredMap = Utils::filterOut(toDeleteIds, map);
  EXPECT_EQ(filteredMap, expected);
}

TEST_F(UtilsTest, ExistsRowRowExists) {
  auto table = createTestTable();
  const std::vector<DataCell> rowValues = {
      DataCell("Task0"), DataCell(25.0), DataCell("Host0")};
  EXPECT_TRUE(Utils::existsRow(table, rowValues));
}

TEST_F(UtilsTest, ExistsRowRowDoesNotExist) {
  auto table = createTestTable();
  const std::vector<DataCell> rowValues = {
      DataCell("Task3"), DataCell(25.0), DataCell("Host0")};
  EXPECT_FALSE(Utils::existsRow(table, rowValues));
}

TEST_F(UtilsTest, ExistsRowPartialMatch) {
  auto table = createTestTable();
  const std::vector<DataCell> rowValues = {
      DataCell("Task0"), DataCell(30.0), DataCell("Host0")};
  EXPECT_FALSE(Utils::existsRow(table, rowValues));
}

TEST_F(UtilsTest, ExistsRowWrongNumberOfValues) {
  auto table = createTestTable();
  std::vector<DataCell> rowValues = {DataCell("Task0"), DataCell(25.0)};
  REBALANCER_EXPECT_RUNTIME_ERROR(
      Utils::existsRow(table, rowValues),
      "Number of values must match number of columns in table");
}

TEST_F(UtilsTest, TableBuilderBasic) {
  TableBuilder builder;
  builder
      .addColumnDefinition(
          {.name = "Object",
           .type = ColumnType::STRING,
           .isPrimaryKey = true,
           .defaultValue = DataCell("unknown_task")})
      .addColumnDefinition(
          {.name = "Load",
           .type = ColumnType::DOUBLE,
           .isPrimaryKey = false,
           .defaultValue = DataCell(0.0)})
      .addColumnDefinition(
          {.name = "Container",
           .type = ColumnType::STRING,
           .isPrimaryKey = false,
           .defaultValue = DataCell("unknown_host")});

  builder.addRow("task1", 30.0, "host1")
      .addRow("task2", 25.0, "host2")
      .addRow("task3", 35.0, "host3");

  const Table table = builder.build();

  EXPECT_EQ(table.getRowIds().size(), 3);
  EXPECT_EQ(table.getColumnData().size(), 3);

  auto columnNames = table.getColumnNames();
  EXPECT_EQ(columnNames.size(), 3);
  EXPECT_EQ(columnNames[0], "Object");
  EXPECT_EQ(columnNames[1], "Load");
  EXPECT_EQ(columnNames[2], "Container");

  EXPECT_EQ(table.getColumnData()[0]->getColumnType(), ColumnType::STRING);
  EXPECT_TRUE(table.getColumnData()[0]->isPrimaryKey());
  EXPECT_EQ(table.getColumnData()[1]->getColumnType(), ColumnType::DOUBLE);
  EXPECT_FALSE(table.getColumnData()[1]->isPrimaryKey());
  EXPECT_EQ(table.getColumnData()[2]->getColumnType(), ColumnType::STRING);
  EXPECT_FALSE(table.getColumnData()[2]->isPrimaryKey());

  const auto& rowIds = table.getRowIds();
  auto objectColumn = table.getColumnData()[0];
  auto loadColumn = table.getColumnData()[1];
  auto containerColumn = table.getColumnData()[2];

  EXPECT_EQ(*objectColumn->getValue(rowIds[0]).strValue, "task1");
  EXPECT_EQ(*loadColumn->getValue(rowIds[0]).doubleValue, 30.0);
  EXPECT_EQ(*containerColumn->getValue(rowIds[0]).strValue, "host1");

  EXPECT_EQ(*objectColumn->getValue(rowIds[1]).strValue, "task2");
  EXPECT_EQ(*loadColumn->getValue(rowIds[1]).doubleValue, 25.0);
  EXPECT_EQ(*containerColumn->getValue(rowIds[1]).strValue, "host2");

  EXPECT_EQ(*objectColumn->getValue(rowIds[2]).strValue, "task3");
  EXPECT_EQ(*loadColumn->getValue(rowIds[2]).doubleValue, 35.0);
  EXPECT_EQ(*containerColumn->getValue(rowIds[2]).strValue, "host3");

  // Verify default values for a non-existent row
  EXPECT_EQ(*objectColumn->getValue(EntityId(999)).strValue, "unknown_task");
  EXPECT_EQ(*loadColumn->getValue(EntityId(999)).doubleValue, 0.0);
  EXPECT_EQ(*containerColumn->getValue(EntityId(999)).strValue, "unknown_host");
}

TEST_F(UtilsTest, TableBuilderEmptyTable) {
  TableBuilder builder;
  builder
      .addColumnDefinition(
          {.name = "Object", .type = ColumnType::STRING, .isPrimaryKey = true})
      .addColumnDefinition({.name = "Load", .type = ColumnType::DOUBLE});

  // verify that table has no rows
  const Table table = builder.build();
  EXPECT_EQ(table.getRowIds().size(), 0);
  EXPECT_EQ(table.getColumnData().size(), 2);
}

TEST_F(UtilsTest, TableBuilderIncorrectRowSize) {
  TableBuilder builder;
  builder.addColumnDefinition({.name = "Object", .type = ColumnType::STRING})
      .addColumnDefinition({.name = "Load", .type = ColumnType::DOUBLE})
      .addColumnDefinition({.name = "Container", .type = ColumnType::STRING});

  // Test with too few arguments using addRow directly
  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.addRow("task1", 10.5),
      "Number of values (2) provided does not match number of columns (3) in the table");

  // Test with too many arguments using addRow directly
  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.addRow("task1", 10.5, "host1", "extra"),
      "Number of values (4) provided does not match number of columns (3) in the table");
}

TEST_F(UtilsTest, TableBuilderRebuild) {
  TableBuilder builder;
  builder.addColumnDefinition({.name = "Object", .type = ColumnType::STRING})
      .addColumnDefinition({.name = "Load", .type = ColumnType::DOUBLE})
      .addColumnDefinition({.name = "Container", .type = ColumnType::STRING});

  builder.addRow("task1", 10.5, "host1");

  // First build should succeed
  const Table table = builder.build();

  builder.addRow("task3", 10.5, "host1");
  // Second build should fail
  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.build(), "Cannot build the same table more than once");
}

TEST_F(UtilsTest, ColumnTypeHelperMethods) {
  const entities::Map<EntityId, DataCell> emptyMap;
  const DataCell defaultCell("");

  // Create columns of different types
  auto doubleCol = std::make_shared<Column>(
      emptyMap, defaultCell, "double", ColumnType::DOUBLE);
  auto utilizationCol = std::make_shared<Column>(
      emptyMap, defaultCell, "util", ColumnType::UTILIZATION);
  auto dimensionCol = std::make_shared<Column>(
      emptyMap, defaultCell, "dim", ColumnType::DIMENSION);

  auto stringCol = std::make_shared<Column>(
      emptyMap, defaultCell, "string", ColumnType::STRING);
  auto entityNameCol = std::make_shared<Column>(
      emptyMap, defaultCell, "entity", ColumnType::ENTITY_NAME);
  auto partitionCol = std::make_shared<Column>(
      emptyMap, defaultCell, "partition", ColumnType::PARTITION);
  auto assignmentCol = std::make_shared<Column>(
      emptyMap, defaultCell, "assignment", ColumnType::ASSIGNMENT);
  auto scopeCol = std::make_shared<Column>(
      emptyMap, defaultCell, "scope", ColumnType::SCOPE);

  // Test typeLikeDouble()
  EXPECT_TRUE(doubleCol->typeLikeDouble());
  EXPECT_TRUE(utilizationCol->typeLikeDouble());
  EXPECT_TRUE(dimensionCol->typeLikeDouble());

  EXPECT_FALSE(stringCol->typeLikeDouble());
  EXPECT_FALSE(entityNameCol->typeLikeDouble());
  EXPECT_FALSE(partitionCol->typeLikeDouble());
  EXPECT_FALSE(assignmentCol->typeLikeDouble());
  EXPECT_FALSE(scopeCol->typeLikeDouble());

  // Test typeLikeString()
  EXPECT_TRUE(stringCol->typeLikeString());
  EXPECT_TRUE(entityNameCol->typeLikeString());
  EXPECT_TRUE(partitionCol->typeLikeString());
  EXPECT_TRUE(assignmentCol->typeLikeString());
  EXPECT_TRUE(scopeCol->typeLikeString());

  EXPECT_FALSE(doubleCol->typeLikeString());
  EXPECT_FALSE(utilizationCol->typeLikeString());
  EXPECT_FALSE(dimensionCol->typeLikeString());
}

TEST_F(UtilsTest, InsertMultipleIdentifierColumnsViaInsertColumn) {
  const std::vector<EntityId> rowIds = {EntityId(1), EntityId(2)};
  Table table(rowIds);

  entities::Map<EntityId, DataCell> idValues1;
  idValues1.emplace(EntityId(1), DataCell(100.0));
  idValues1.emplace(EntityId(2), DataCell(200.0));
  auto idColumn1 = std::make_shared<Column>(
      idValues1, DataCell(0.0), "ID1", ColumnType::IDENTIFIER, true);

  entities::Map<EntityId, DataCell> idValues2;
  idValues2.emplace(EntityId(1), DataCell(300.0));
  idValues2.emplace(EntityId(2), DataCell(400.0));
  auto idColumn2 = std::make_shared<Column>(
      idValues2, DataCell(0.0), "ID2", ColumnType::IDENTIFIER, true);

  // First IDENTIFIER column should succeed
  table.insertColumn(idColumn1);
  EXPECT_EQ(table.getColumnData().size(), 1);

  // Second IDENTIFIER column should fail
  REBALANCER_EXPECT_RUNTIME_ERROR(
      table.insertColumn(idColumn2),
      "Expected only one column of type IDENTIFIER");
}

TEST_F(
    UtilsTest,
    InsertMultipleIdentifierColumnsViaInsertColumnsInSortedOrder) {
  const std::vector<EntityId> rowIds = {EntityId(1), EntityId(2)};
  Table table(rowIds);

  entities::Map<EntityId, DataCell> idValues1;
  idValues1.emplace(EntityId(1), DataCell(100.0));
  idValues1.emplace(EntityId(2), DataCell(200.0));
  auto idColumn1 = std::make_shared<Column>(
      idValues1, DataCell(0.0), "ID1", ColumnType::IDENTIFIER, true);

  entities::Map<EntityId, DataCell> idValues2;
  idValues2.emplace(EntityId(1), DataCell(300.0));
  idValues2.emplace(EntityId(2), DataCell(400.0));
  auto idColumn2 = std::make_shared<Column>(
      idValues2, DataCell(0.0), "ID2", ColumnType::IDENTIFIER, true);

  entities::Map<EntityId, DataCell> stringValues;
  stringValues.emplace(EntityId(1), DataCell("Value1"));
  stringValues.emplace(EntityId(2), DataCell("Value2"));
  auto stringColumn = std::make_shared<Column>(
      stringValues, DataCell(""), "Name", ColumnType::STRING, false);

  std::vector<std::shared_ptr<const Column>> columns = {
      idColumn1, idColumn2, stringColumn};

  // Should fail when trying to insert multiple IDENTIFIER columns
  REBALANCER_EXPECT_RUNTIME_ERROR(
      table.insertColumnsInSortedOrder(columns),
      "Expected only one column of type IDENTIFIER");
}

} // namespace facebook::rebalancer::explorer::tests
