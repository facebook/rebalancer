// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.
#include "rebalancer/explorer/cpp_server/lib/Utils.h"

#include <string>
#include <vector>

namespace facebook::rebalancer::explorer {

using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::interface;

Column::Column(
    Map<EntityId, DataCell> nonDefaultValues,
    DataCell defaultValue,
    std::string columnName,
    ColumnType columnType,
    bool primaryKey,
    std::string description,
    bool excludeFromAggregation)
    : nonDefaultValues_(std::move(nonDefaultValues)),
      defaultValue_(std::move(defaultValue)),
      columnName_(std::move(columnName)),
      columnType_(std::move(columnType)),
      primaryKey_(primaryKey),
      description_(std::move(description)),
      excludeFromAggregation_(excludeFromAggregation) {}

const DataCell& Column::getValue(const EntityId entityId) const {
  return folly::get_ref_default(nonDefaultValues_, entityId, defaultValue_);
}

explorer::ColumnType Column::getColumnType() const {
  return columnType_;
}

const std::string& Column::getColumnName() const {
  return columnName_;
}

const std::string& Column::getDescription() const {
  return description_;
}

bool Column::typeLikeIdOrInt() const {
  return columnType_ == ColumnType::INTEGER ||
      columnType_ == ColumnType::IDENTIFIER;
}

bool Column::typeLikeDouble() const {
  return columnType_ == ColumnType::DOUBLE ||
      columnType_ == ColumnType::UTILIZATION ||
      columnType_ == ColumnType::DIMENSION;
}

bool Column::typeLikeString() const {
  return columnType_ == ColumnType::STRING ||
      columnType_ == ColumnType::ENTITY_NAME ||
      columnType_ == ColumnType::PARTITION ||
      columnType_ == ColumnType::ASSIGNMENT || columnType_ == ColumnType::SCOPE;
}

Table::Table(std::vector<EntityId> rowIds) : rowIds_(std::move(rowIds)) {}

void Table::insertColumn(std::shared_ptr<const Column> columnData) {
  if (columnData->getColumnType() == ColumnType::IDENTIFIER) {
    if (idColExists_) {
      throw std::runtime_error("Expected only one column of type IDENTIFIER");
    } else {
      idColExists_ = true;
    }
  }
  if (columnData->isPrimaryKey()) {
    primaryKeyColumns_.push_back(columnData.get());
  }
  columns_.push_back(std::move(columnData));
}

void Table::insertColumnsInSortedOrder(
    std::vector<std::shared_ptr<const Column>> columnsData) {
  /* Sort the columns based on column name and insert. */
  std::sort(
      columnsData.begin(),
      columnsData.end(),
      [](std::shared_ptr<const Column> a, std::shared_ptr<const Column> b) {
        return a->getColumnName() < b->getColumnName();
      });
  for (auto& columnData : columnsData) {
    insertColumn(std::move(columnData));
  }
}

std::vector<std::string> Table::getColumnNames() const {
  std::vector<std::string> columnNames;
  std::transform(
      columns_.begin(),
      columns_.end(),
      std::back_inserter(columnNames),
      [](std::shared_ptr<const Column> colData) {
        return colData->getColumnName();
      });
  return columnNames;
}

const std::vector<std::shared_ptr<const Column>>& Table::getColumnData() const {
  return columns_;
}

const std::vector<EntityId>& Table::getRowIds() const {
  return rowIds_;
}

void Table::updateRowIds(std::vector<EntityId> newRowIds) {
  rowIds_ = std::move(newRowIds);
}

std::shared_ptr<const Column> Utils::fetchColumn(
    const std::vector<std::shared_ptr<const Column>>& columns,
    const std::string& columnName) {
  auto columnIterator = std::find_if(
      columns.begin(),
      columns.end(),
      [&columnName](std::shared_ptr<const Column> column) {
        return column->getColumnName() == columnName;
      });

  if (columnIterator == columns.end()) {
    throw std::runtime_error(fmt::format("Column {} not found", columnName));
  }
  return *columnIterator;
}

bool Utils::existsRow(
    const Table& table,
    const std::vector<DataCell>& columnValues) {
  const auto& columns = table.getColumnData();
  if (columns.size() != columnValues.size()) {
    throw std::runtime_error(
        "Number of values must match number of columns in table");
  }
  if (columnValues.empty()) {
    throw std::runtime_error(
        "Expected at least one column value to be provided");
  }

  const auto& rowIds = table.getRowIds();
  for (const auto& rowId : rowIds) {
    bool allMatch = true;
    for (size_t i = 0; i < columnValues.size(); ++i) {
      auto& column = columns.at(i);
      if (column->getValue(rowId) != columnValues[i]) {
        allMatch = false;
        break;
      }
    }
    // found a matching row
    if (allMatch) {
      return true;
    }
  }

  return false;
}
TableBuilder& TableBuilder::addColumnDefinition(
    const ColumnDefinition& columnDef) {
  columnDefinitions_.push_back(columnDef);
  columnMaps_.emplace_back();
  return *this;
}

TableBuilder& TableBuilder::addRowWithCells(
    const std::vector<DataCell>& columnValues) {
  if (columnValues.size() != columnMaps_.size()) {
    throw std::runtime_error(
        fmt::format(
            "Number of values ({}) provided does not match number of columns ({}) in the table",
            columnValues.size(),
            columnMaps_.size()));
  }

  auto rowId = EntityId(nextRowId_++);
  rowIds_.push_back(rowId);

  for (size_t i = 0; i < columnValues.size(); ++i) {
    columnMaps_.at(i).emplace(rowId, columnValues[i]);
  }

  return *this;
}

Table TableBuilder::build() {
  if (built_) {
    throw std::runtime_error("Cannot build the same table more than once");
  }

  Table table(std::move(rowIds_));

  for (size_t i = 0; i < columnMaps_.size(); i++) {
    const auto& colDef = columnDefinitions_.at(i);
    auto column = std::make_shared<const Column>(
        std::move(columnMaps_[i]),
        colDef.defaultValue,
        colDef.name,
        colDef.type,
        colDef.isPrimaryKey,
        colDef.description,
        colDef.excludeFromAggregation);

    table.insertColumn(std::move(column));
  }

  built_ = true;
  return table;
}

} // namespace facebook::rebalancer::explorer
