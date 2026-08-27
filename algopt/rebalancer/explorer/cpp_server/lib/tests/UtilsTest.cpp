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
    struct Row {
      std::string name;
      double load;
      std::string host;
    };
    const std::vector<Row> rows = {
        {.name = "Task0", .load = 25.0, .host = "Host0"},
        {.name = "Task1", .load = 30.0, .host = "Host1"},
        {.name = "Task2", .load = 35.0, .host = "Host2"}};
    TableBuilder builder(rows);
    return builder
        .add(
            {.name = "Name", .type = ColumnType::STRING, .isPrimaryKey = true},
            [](const Row& row) { return row.name; })
        .add(
            {.name = "Load", .type = ColumnType::DOUBLE},
            [](const Row& row) { return row.load; })
        .add(
            {.name = "Host", .type = ColumnType::STRING},
            [](const Row& row) { return row.host; })
        .build();
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
  const auto table = createTestTable();
  const std::vector<CellValue> rowValues = {
      std::string("Task0"), 25.0, std::string("Host0")};
  EXPECT_TRUE(Utils::existsRow(table, rowValues));
}

TEST_F(UtilsTest, ExistsRowRowDoesNotExist) {
  const auto table = createTestTable();
  const std::vector<CellValue> rowValues = {
      std::string("Task3"), 25.0, std::string("Host0")};
  EXPECT_FALSE(Utils::existsRow(table, rowValues));
}

TEST_F(UtilsTest, ExistsRowPartialMatch) {
  const auto table = createTestTable();
  const std::vector<CellValue> rowValues = {
      std::string("Task0"), 30.0, std::string("Host0")};
  EXPECT_FALSE(Utils::existsRow(table, rowValues));
}

TEST_F(UtilsTest, ExistsRowWrongNumberOfValues) {
  const auto table = createTestTable();
  const std::vector<CellValue> rowValues = {std::string("Task0"), 25.0};
  REBALANCER_EXPECT_RUNTIME_ERROR(
      Utils::existsRow(table, rowValues),
      "Number of values must match number of columns in table");
}

TEST_F(UtilsTest, ExistsRowWithTableBuilder) {
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

  TableBuilder<Row> builder(rows);
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
  const std::vector<CellValue> expectedRow = {
      25.0, 0.0, std::string("Task0"), std::string("owned")};
  EXPECT_TRUE(Utils::existsRow(table, expectedRow));
  const std::vector<CellValue> expectedDefaultRow = {
      0.0, 1.0, std::string(""), std::string("unknown")};
  EXPECT_TRUE(Utils::existsRow(table, expectedDefaultRow));
  EXPECT_FALSE(
      Utils::existsRow(
          table, {30.0, 0.0, std::string("Task0"), std::string("owned")}));
  EXPECT_FALSE(
      Utils::existsRow(
          table,
          {25.0,
           std::string("false"),
           std::string("Task0"),
           std::string("owned")}));
  EXPECT_FALSE(Utils::existsRow(table, {25.0, 0.0, 0.0, std::string("owned")}));
  EXPECT_FALSE(
      Utils::existsRow(
          table, {25.0, 0.0, std::string("Task0"), std::string("unknown")}));
  EXPECT_FALSE(
      Utils::existsRow(
          table,
          {std::string("25.0"),
           0.0,
           std::string("Task0"),
           std::string("owned")}));
  EXPECT_FALSE(Utils::existsRow(table, {25.0, 0.0, std::string("Task0"), 1.0}));
}

TEST_F(UtilsTest, TableBuilderBuildsColumnsFromRowKeys) {
  const std::vector<entities::ScopeItemId> scopeItemIds = {
      entities::ScopeItemId(8),
      entities::ScopeItemId(3),
      entities::ScopeItemId(5)};
  const std::string item8 = "item8";
  const std::string item3 = "item3";
  const std::string empty;

  TableBuilder<entities::ScopeItemId> builder(scopeItemIds);
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
  const std::vector<RowId> expectedRowIds = {RowId(0), RowId(1), RowId(2)};
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

TEST_F(UtilsTest, TableBuilderBuildsEmptyTable) {
  const std::vector<entities::ScopeItemId> noRows;
  const std::string empty;
  TableBuilder<entities::ScopeItemId> builder(noRows);
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

TEST_F(UtilsTest, TableBuilderRejectsNullColumn) {
  const std::vector<int> noRows;
  TableBuilder<int> builder(noRows);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.add(std::shared_ptr<const Column>{}), "Cannot add a null column");
}

TEST_F(UtilsTest, ColumnTypeClassification) {
  const std::vector<int> noRows;
  const TableBuilder<int> builder(noRows);
  for (const auto type :
       {ColumnType::DOUBLE,
        ColumnType::UTILIZATION,
        ColumnType::DIMENSION,
        ColumnType::INTEGER,
        ColumnType::IDENTIFIER}) {
    const auto column = builder.make(
        {.name = "numeric", .type = type}, [](int) { return 0.0; });
    EXPECT_TRUE(column->isNumeric());
    EXPECT_FALSE(column->isString());
  }
  for (const auto type :
       {ColumnType::STRING,
        ColumnType::ENTITY_NAME,
        ColumnType::PARTITION,
        ColumnType::ASSIGNMENT,
        ColumnType::SCOPE}) {
    const auto column = builder.make(
        {.name = "string", .type = type}, [](int) { return std::string(); });
    EXPECT_TRUE(column->isString());
    EXPECT_FALSE(column->isNumeric());
  }

  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.make(
          {.name = "unknown",
           .type = static_cast<ColumnType>(std::numeric_limits<int>::max())},
          [](int) { return std::string(); }),
      "Unknown column type: 2147483647");
}

TEST_F(UtilsTest, ColumnTypedAccessors) {
  const std::vector<int> rows = {0};
  const TableBuilder<int> builder(rows);
  const auto numeric = builder.make(
      {.name = "numeric", .type = ColumnType::DOUBLE}, [](int) { return 1.5; });
  const auto stringColumn = builder.make(
      {.name = "string", .type = ColumnType::STRING},
      [](int) { return std::string("value"); });
  const RowId rowId(0);

  EXPECT_DOUBLE_EQ(1.5, numeric->getDouble(rowId));
  EXPECT_EQ("1.500000", numeric->toString(rowId));
  EXPECT_EQ("value", stringColumn->getStrView(rowId));
  EXPECT_EQ("value", stringColumn->toString(rowId));
  REBALANCER_EXPECT_RUNTIME_ERROR(
      numeric->getStrView(rowId),
      "Reading a string value requires a string column, but column 'numeric' is numeric");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      stringColumn->getDouble(rowId),
      "Reading a double value requires a numeric column, but column 'string' is a string");
}

TEST_F(UtilsTest, TableBuilderRejectsMismatchedColumnType) {
  const std::vector<int> rows = {1};
  TableBuilder<int> builder(rows);

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

TEST_F(UtilsTest, ColumnRejectsOutOfRangeRowId) {
  const std::vector<int> rows = {0, 1};
  const TableBuilder<int> builder(rows);
  const auto column = builder.make(
      {.name = "Value", .type = ColumnType::DOUBLE},
      [](const int value) { return static_cast<double>(value); });

  REBALANCER_EXPECT_RUNTIME_ERROR(
      column->getDouble(RowId(2)),
      "Row ID 2 is out of range for column 'Value' with 2 rows");
}

TEST_F(UtilsTest, TableRejectsMismatchedColumnRowCount) {
  Table table(2);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      table.insertColumn(
          {.name = "Value", .type = ColumnType::DOUBLE},
          std::vector<double>{1.0}),
      "Column 'Value' row count 1 does not match table row count 2");
}

TEST_F(UtilsTest, TableRejectsMultipleIdentifierColumns) {
  Table table(2);
  table.insertColumn(
      {.name = "ID1", .type = ColumnType::IDENTIFIER},
      std::vector<double>{100.0, 200.0});
  REBALANCER_EXPECT_RUNTIME_ERROR(
      table.insertColumn(
          {.name = "ID2", .type = ColumnType::IDENTIFIER},
          std::vector<double>{300.0, 400.0}),
      "Expected only one column of type IDENTIFIER");
}

} // namespace facebook::rebalancer::explorer::tests
