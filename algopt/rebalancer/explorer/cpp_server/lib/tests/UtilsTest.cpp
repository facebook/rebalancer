// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "rebalancer/explorer/cpp_server/lib/Utils.h"

#include <gtest/gtest.h>

#include <functional>
#include <limits>

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

TEST_F(UtilsTest, ExistsRowWithColumnTableBuilder) {
  struct Row {
    double load;
    bool active;
    BorrowedString name;
    std::string label;
  };
  const std::string task0 = "Task0";
  const std::string empty;
  const std::vector<Row> rows = {
      {.load = 25.0, .active = false, .name = task0, .label = "owned"},
      {.load = 0.0, .active = true, .name = empty, .label = "unknown"}};

  ColumnTableBuilder<Row> builder(rows);
  builder
      .add(
          {.name = "Load", .type = ColumnType::DOUBLE},
          [](const Row& row) { return row.load; })
      .add(
          {.name = "Active", .type = ColumnType::INTEGER},
          [](const Row& row) { return row.active; })
      .add(
          {.name = "Name", .type = ColumnType::STRING},
          [](const Row& row) { return row.name; })
      .add({.name = "Label", .type = ColumnType::STRING}, [](const Row& row) {
        return row.label;
      });

  const auto table = builder.build();
  const std::vector<DataCell> expectedRow = {
      DataCell(25.0), DataCell(0.0), DataCell("Task0"), DataCell("owned")};
  EXPECT_TRUE(Utils::existsRow(table, expectedRow));
  const std::vector<DataCell> expectedDefaultRow = {
      DataCell(0.0), DataCell(1.0), DataCell(""), DataCell("unknown")};
  EXPECT_TRUE(Utils::existsRow(table, expectedDefaultRow));
  EXPECT_FALSE(
      Utils::existsRow(
          table,
          {DataCell(30.0),
           DataCell(0.0),
           DataCell("Task0"),
           DataCell("owned")}));
  EXPECT_FALSE(
      Utils::existsRow(
          table,
          {DataCell(25.0),
           DataCell("false"),
           DataCell("Task0"),
           DataCell("owned")}));
  EXPECT_FALSE(
      Utils::existsRow(
          table,
          {DataCell(25.0), DataCell(0.0), DataCell(0.0), DataCell("owned")}));
  EXPECT_FALSE(
      Utils::existsRow(
          table,
          {DataCell(25.0),
           DataCell(0.0),
           DataCell("Task0"),
           DataCell("unknown")}));
  EXPECT_FALSE(
      Utils::existsRow(
          table,
          {DataCell("25.0"),
           DataCell(0.0),
           DataCell("Task0"),
           DataCell("owned")}));
  EXPECT_FALSE(
      Utils::existsRow(
          table,
          {DataCell(25.0), DataCell(0.0), DataCell("Task0"), DataCell(1.0)}));
}

TEST_F(UtilsTest, ColumnTableBuilderBuildsColumnsFromRowKeys) {
  const std::vector<entities::ScopeItemId> scopeItemIds = {
      entities::ScopeItemId(8),
      entities::ScopeItemId(3),
      entities::ScopeItemId(5)};
  const std::string item8 = "item8";
  const std::string item3 = "item3";
  const std::string empty;

  ColumnTableBuilder<entities::ScopeItemId> builder(scopeItemIds);
  builder
      .add(
          {.name = "Scope Item",
           .type = ColumnType::ENTITY_NAME,
           .isPrimaryKey = true},
          [&](const entities::ScopeItemId scopeItemId) {
            if (scopeItemId == entities::ScopeItemId(8)) {
              return std::cref(item8);
            }
            return std::cref(
                scopeItemId == entities::ScopeItemId(3) ? item3 : empty);
          })
      .add(
          {.name = "Load", .type = ColumnType::DOUBLE},
          [](const entities::ScopeItemId scopeItemId) {
            return static_cast<double>(scopeItemId.asIndex());
          });

  std::vector<std::shared_ptr<const Column>> sortedColumns;
  sortedColumns.push_back(builder.make(
      {.name = "Owned", .type = ColumnType::STRING},
      [](const entities::ScopeItemId scopeItemId) {
        return scopeItemId == entities::ScopeItemId(8) ? std::string("owned8")
                                                       : std::string("unknown");
      }));
  sortedColumns.push_back(builder.make(
      {.name = "Enabled", .type = ColumnType::INTEGER},
      [](const entities::ScopeItemId scopeItemId) {
        return scopeItemId != entities::ScopeItemId(8);
      }));
  builder.addSorted(std::move(sortedColumns));

  const Table table = builder.build();
  const std::vector<EntityId> expectedRowIds = {
      EntityId(0), EntityId(1), EntityId(2)};
  const std::vector<std::string> expectedColumnNames = {
      "Scope Item", "Load", "Enabled", "Owned"};
  const auto& columns = table.getColumnData();
  const auto stringValues = [&](const std::size_t columnIndex) {
    std::vector<std::string> values;
    for (const auto rowId : table.getRowIds()) {
      values.emplace_back(columns.at(columnIndex)->getStrView(rowId));
    }
    return values;
  };
  const auto doubleValues = [&](const std::size_t columnIndex) {
    std::vector<double> values;
    for (const auto rowId : table.getRowIds()) {
      values.push_back(columns.at(columnIndex)->getDouble(rowId));
    }
    return values;
  };

  EXPECT_EQ(expectedRowIds, table.getRowIds());
  EXPECT_EQ(expectedColumnNames, table.getColumnNames());
  EXPECT_EQ("Scope Item", table.getOnlyPrimaryKeyColumn()->getColumnName());
  EXPECT_EQ((std::vector<std::string>{item8, item3, ""}), stringValues(0));
  EXPECT_EQ((std::vector<double>{8.0, 3.0, 5.0}), doubleValues(1));
  EXPECT_EQ((std::vector<double>{0.0, 1.0, 1.0}), doubleValues(2));
  EXPECT_EQ(
      (std::vector<std::string>{"owned8", "unknown", "unknown"}),
      stringValues(3));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.add(
          {.name = "Late", .type = ColumnType::DOUBLE},
          [](const entities::ScopeItemId) { return 0.0; }),
      "Cannot add columns after table is built");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.build(), "Cannot build the same table more than once");
}

TEST_F(UtilsTest, ColumnTableBuilderBuildsEmptyTable) {
  const std::vector<entities::ScopeItemId> noRows;
  const std::string empty;
  ColumnTableBuilder<entities::ScopeItemId> builder(noRows);
  builder
      .add(
          {.name = "Name", .type = ColumnType::STRING},
          [&empty](const entities::ScopeItemId) { return std::cref(empty); })
      .add(
          {.name = "Load", .type = ColumnType::DOUBLE},
          [](const entities::ScopeItemId) { return 0.0; });

  const auto table = builder.build();
  EXPECT_TRUE(table.getRowIds().empty());
  EXPECT_EQ(2, table.getColumnData().size());
}

TEST_F(UtilsTest, ColumnTableBuilderRejectsNullColumn) {
  const std::vector<int> noRows;
  ColumnTableBuilder<int> builder(noRows);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.add(std::shared_ptr<const Column>{}), "Cannot add a null column");
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

  EXPECT_EQ(objectColumn->getStrView(rowIds[0]), "task1");
  EXPECT_DOUBLE_EQ(loadColumn->getDouble(rowIds[0]), 30.0);
  EXPECT_EQ(containerColumn->getStrView(rowIds[0]), "host1");

  EXPECT_EQ(objectColumn->getStrView(rowIds[1]), "task2");
  EXPECT_DOUBLE_EQ(loadColumn->getDouble(rowIds[1]), 25.0);
  EXPECT_EQ(containerColumn->getStrView(rowIds[1]), "host2");

  EXPECT_EQ(objectColumn->getStrView(rowIds[2]), "task3");
  EXPECT_DOUBLE_EQ(loadColumn->getDouble(rowIds[2]), 35.0);
  EXPECT_EQ(containerColumn->getStrView(rowIds[2]), "host3");

  // Verify default values for a non-existent row
  EXPECT_EQ(objectColumn->getStrView(EntityId(999)), "unknown_task");
  EXPECT_DOUBLE_EQ(loadColumn->getDouble(EntityId(999)), 0.0);
  EXPECT_EQ(containerColumn->getStrView(EntityId(999)), "unknown_host");
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

TEST_F(UtilsTest, ColumnTypeClassification) {
  const entities::Map<EntityId, DataCell> emptyMap;
  for (const auto type :
       {ColumnType::DOUBLE,
        ColumnType::UTILIZATION,
        ColumnType::DIMENSION,
        ColumnType::INTEGER,
        ColumnType::IDENTIFIER}) {
    const auto column = Column(emptyMap, DataCell(0.0), "Value", type);
    EXPECT_TRUE(column.isNumeric());
    EXPECT_FALSE(column.isString());
  }
  for (const auto type :
       {ColumnType::STRING,
        ColumnType::ENTITY_NAME,
        ColumnType::PARTITION,
        ColumnType::ASSIGNMENT,
        ColumnType::SCOPE}) {
    const auto column = Column(emptyMap, DataCell(""), "Value", type);
    EXPECT_FALSE(column.isNumeric());
    EXPECT_TRUE(column.isString());
  }

  const Column unknown(
      emptyMap,
      DataCell(""),
      "unknown",
      static_cast<ColumnType>(std::numeric_limits<int>::max()));
  REBALANCER_EXPECT_RUNTIME_ERROR(
      unknown.isNumeric(), "Unknown column type: 2147483647");
}

TEST_F(UtilsTest, InsertColumnRejectsMissingValue) {
  Table table({EntityId(1), EntityId(2)});
  const auto values =
      entities::Map<EntityId, DataCell>{{EntityId(1), DataCell(1.0)}};
  const auto column =
      std::make_shared<Column>(values, DataCell(), "Value", ColumnType::DOUBLE);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      table.insertColumn(column),
      "Column 'Value' must have exactly one value matching its type for every table row");
}

TEST_F(UtilsTest, InsertColumnRejectsMultipleValues) {
  Table table({EntityId(1)});
  auto value = DataCell("one");
  value.doubleValue = 1.0;
  const auto values =
      entities::Map<EntityId, DataCell>{{EntityId(1), std::move(value)}};
  const auto column =
      std::make_shared<Column>(values, DataCell(), "Value", ColumnType::STRING);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      table.insertColumn(column),
      "Column 'Value' must have exactly one value matching its type for every table row");
}

TEST_F(UtilsTest, InsertColumnRejectsMismatchedValueType) {
  Table table({EntityId(1)});
  const auto values =
      entities::Map<EntityId, DataCell>{{EntityId(1), DataCell("one")}};
  const auto column =
      std::make_shared<Column>(values, DataCell(), "Value", ColumnType::DOUBLE);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      table.insertColumn(column),
      "Column 'Value' must have exactly one value matching its type for every table row");
}

TEST_F(UtilsTest, ColumnTypedAccessors) {
  const entities::Map<EntityId, DataCell> emptyMap;
  const EntityId rowId(0);
  const Column numeric(emptyMap, DataCell(1.5), "numeric", ColumnType::DOUBLE);
  const Column stringColumn(
      emptyMap, DataCell("value"), "string", ColumnType::STRING);

  EXPECT_DOUBLE_EQ(1.5, numeric.getDouble(rowId));
  EXPECT_EQ("1.500000", numeric.toString(rowId));
  EXPECT_EQ("value", stringColumn.getStrView(rowId));
  EXPECT_EQ("value", stringColumn.toString(rowId));
  REBALANCER_EXPECT_RUNTIME_ERROR(
      numeric.getStrView(rowId),
      "Reading a string value requires a string column, but column 'numeric' is numeric");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      stringColumn.getDouble(rowId),
      "Reading a double value requires a numeric column, but column 'string' is a string");
}

TEST_F(UtilsTest, ColumnTableBuilderRejectsMismatchedColumnType) {
  const std::vector<int> rows = {1};
  ColumnTableBuilder<int> builder(rows);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.make(
          {.name = "numeric_as_string", .type = ColumnType::STRING},
          [](const int value) { return static_cast<double>(value); }),
      "Column 'numeric_as_string' storage does not match its type");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.make(
          {.name = "string_as_numeric", .type = ColumnType::DOUBLE},
          [](const int) { return std::string("value"); }),
      "Column 'string_as_numeric' storage does not match its type");
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
