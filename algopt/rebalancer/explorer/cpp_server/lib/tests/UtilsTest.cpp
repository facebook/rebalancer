// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "rebalancer/explorer/cpp_server/lib/Utils.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

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

TEST_F(UtilsTest, ExistsRowWithTypedStorage) {
  const std::string task0 = "Task0";
  Table table({EntityId(0), EntityId(1)});
  Column::DoubleStorage loads(
      /*totalSize=*/2, /*defaultValue=*/0.0, /*expectedNonDefaultSize=*/1);
  loads.emplace(EntityId(0), 25.0);
  table.insertColumn(
      std::make_shared<Column>(
          std::move(loads),
          ColumnMetadata{.name = "Load", .type = ColumnType::DOUBLE}));

  Column::BoolStorage active(/*totalSize=*/2, /*defaultValue=*/true);
  active.emplace(EntityId(0), false);
  table.insertColumn(
      std::make_shared<Column>(
          std::move(active),
          ColumnMetadata{.name = "Active", .type = ColumnType::INTEGER}));

  entities::Map<EntityId, const std::string*> names;
  names.emplace(EntityId(0), &task0);
  table.insertColumn(
      std::make_shared<Column>(
          Column::BorrowedStringStorage(std::move(names), /*totalSize=*/2),
          ColumnMetadata{.name = "Name", .type = ColumnType::STRING}));

  Column::OwnedStringStorage labels(
      /*totalSize=*/2,
      /*defaultValue=*/"unknown",
      /*expectedNonDefaultSize=*/1);
  labels.emplace(EntityId(0), "owned");
  table.insertColumn(
      std::make_shared<Column>(
          std::move(labels),
          ColumnMetadata{.name = "Label", .type = ColumnType::STRING}));

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

TEST_F(UtilsTest, ColumnDoubleStorage) {
  Column::DoubleStorage numericValues(
      /*totalSize=*/100, /*defaultValue=*/0.0, /*expectedNonDefaultSize=*/1);
  numericValues.emplace(EntityId(0), 1.5);
  numericValues.emplace(EntityId(0), 2.0);
  const Column numeric(
      std::move(numericValues),
      {.name = "numeric", .type = ColumnType::DOUBLE});
  EXPECT_DOUBLE_EQ(1.5, numeric.getDouble(EntityId(0)));
  EXPECT_DOUBLE_EQ(0.0, numeric.getDouble(EntityId(99)));
  EXPECT_EQ("1.500000", numeric.toString(EntityId(0)));
  REBALANCER_EXPECT_RUNTIME_ERROR(
      numeric.getStrView(EntityId(0)),
      "Reading a string value requires a string column, but column 'numeric' is numeric");
}

TEST_F(UtilsTest, ColumnBoolStorage) {
  Column::BoolStorage boolValues(/*totalSize=*/2, /*defaultValue=*/true);
  boolValues.emplace(EntityId(0), false);
  boolValues.emplace(EntityId(0), true);
  const Column boolean(
      std::move(boolValues), {.name = "boolean", .type = ColumnType::INTEGER});
  EXPECT_DOUBLE_EQ(0.0, boolean.getDouble(EntityId(0)));
  EXPECT_DOUBLE_EQ(1.0, boolean.getDouble(EntityId(1)));
  EXPECT_EQ("0.000000", boolean.toString(EntityId(0)));

  Column::BoolStorage falseDefaultValues(
      /*totalSize=*/2, /*defaultValue=*/false);
  falseDefaultValues.emplace(EntityId(0), true);
  falseDefaultValues.emplace(EntityId(0), false);
  const Column falseDefault(
      std::move(falseDefaultValues),
      {.name = "false_default", .type = ColumnType::INTEGER});
  EXPECT_DOUBLE_EQ(1.0, falseDefault.getDouble(EntityId(0)));
  EXPECT_DOUBLE_EQ(0.0, falseDefault.getDouble(EntityId(1)));
}

TEST_F(UtilsTest, ColumnBorrowedStringStorage) {
  const std::string empty;
  const std::string first = "first";
  const std::string second = "second";
  Column::BorrowedStringStorage referencedValues(
      /*totalSize=*/100, /*expectedNonDefaultSize=*/1);
  referencedValues.emplace(EntityId(0), empty);
  referencedValues.emplace(EntityId(0), first);
  referencedValues.emplace(EntityId(0), second);
  const Column referenced(
      std::move(referencedValues),
      {.name = "referenced", .type = ColumnType::STRING});
  EXPECT_EQ(first, referenced.getStrView(EntityId(0)));
  EXPECT_EQ("", referenced.getStrView(EntityId(99)));
  EXPECT_EQ("first", referenced.toString(EntityId(0)));
  EXPECT_EQ("", referenced.toString(EntityId(99)));
}

TEST_F(UtilsTest, ColumnOwnedStringStorage) {
  Column::OwnedStringStorage ownedValues(
      /*totalSize=*/2,
      /*defaultValue=*/"unknown",
      /*expectedNonDefaultSize=*/1);
  ownedValues.emplace(EntityId(0), "owned");
  ownedValues.emplace(EntityId(0), "replacement");
  ownedValues.emplace(EntityId(0), "unknown");
  const Column owned(
      std::move(ownedValues),
      ColumnMetadata{.name = "owned", .type = ColumnType::STRING});
  EXPECT_EQ("owned", owned.getStrView(EntityId(0)));
  EXPECT_EQ("unknown", owned.getStrView(EntityId(1)));
  EXPECT_EQ("owned", owned.toString(EntityId(0)));
}

TEST_F(UtilsTest, ColumnOwnedStringStorageConsumesMap) {
  entities::Map<EntityId, std::string> rowIdToValue;
  rowIdToValue.emplace(EntityId(0), "owned");
  const Column owned(
      Column::OwnedStringStorage(
          std::move(rowIdToValue), /*defaultValue=*/"unknown", /*totalSize=*/2),
      ColumnMetadata{.name = "owned", .type = ColumnType::STRING});

  EXPECT_EQ("owned", owned.getStrView(EntityId(0)));
  EXPECT_EQ("unknown", owned.getStrView(EntityId(1)));
}

TEST_F(UtilsTest, ColumnRejectsMismatchedTypedStorage) {
  const auto constructMismatchedColumn = [] {
    return Column(
        Column::DoubleStorage(
            /*totalSize=*/0,
            /*defaultValue=*/0.0,
            /*expectedNonDefaultSize=*/0),
        ColumnMetadata{.name = "double_as_string", .type = ColumnType::STRING});
  };
  REBALANCER_EXPECT_RUNTIME_ERROR(
      constructMismatchedColumn(),
      "Column 'double_as_string' storage does not match its type");

  const auto constructMismatchedStringColumn = [] {
    return Column(
        Column::OwnedStringStorage(
            /*totalSize=*/0,
            /*defaultValue=*/"",
            /*expectedNonDefaultSize=*/0),
        ColumnMetadata{.name = "owned_as_double", .type = ColumnType::DOUBLE});
  };
  REBALANCER_EXPECT_RUNTIME_ERROR(
      constructMismatchedStringColumn(),
      "Column 'owned_as_double' storage does not match its type");

  const auto constructMismatchedBorrowedColumn = [] {
    return Column(
        Column::BorrowedStringStorage(
            /*totalSize=*/0, /*expectedNonDefaultSize=*/0),
        ColumnMetadata{
            .name = "borrowed_as_double", .type = ColumnType::DOUBLE});
  };
  REBALANCER_EXPECT_RUNTIME_ERROR(
      constructMismatchedBorrowedColumn(),
      "Column 'borrowed_as_double' storage does not match its type");

  const auto constructMismatchedBoolColumn = [] {
    return Column(
        Column::BoolStorage(/*totalSize=*/0, /*defaultValue=*/false),
        ColumnMetadata{.name = "bool_as_double", .type = ColumnType::DOUBLE});
  };
  REBALANCER_EXPECT_RUNTIME_ERROR(
      constructMismatchedBoolColumn(),
      "Column 'bool_as_double' storage does not match its type");
}

TEST_F(UtilsTest, TableRejectsTypedStorageWithoutEveryRow) {
  Table table({EntityId(0), EntityId(1)});
  auto column = std::make_shared<Column>(
      Column::DoubleStorage(
          /*totalSize=*/1,
          /*defaultValue=*/0.0,
          /*expectedNonDefaultSize=*/0),
      ColumnMetadata{.name = "numeric", .type = ColumnType::DOUBLE});

  REBALANCER_EXPECT_RUNTIME_ERROR(
      table.insertColumn(std::move(column)),
      "Column 'numeric' must have exactly one value matching its type for every table row");
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
