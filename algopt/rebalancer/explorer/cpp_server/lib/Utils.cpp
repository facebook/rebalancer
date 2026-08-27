// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.
#include "rebalancer/explorer/cpp_server/lib/Utils.h"

#include <cmath>
#include <string>
#include <vector>

namespace facebook::rebalancer::explorer {

using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::interface;

Column::Column(Storage storage, ColumnMetadata metadata)
    : storage_(std::move(storage)),
      columnName_(std::move(metadata.name)),
      columnType_(metadata.type),
      primaryKey_(metadata.isPrimaryKey),
      description_(std::move(metadata.description)),
      excludeFromAggregation_(metadata.excludeFromAggregation) {
  const auto storageMatchesType = std::visit(
      [this](const auto& storage) {
        using StorageType = std::decay_t<decltype(storage)>;
        if constexpr (std::same_as<StorageType, BoolStorage>) {
          return columnType_ == ColumnType::INTEGER;
        } else if constexpr (std::same_as<StorageType, DoubleStorage>) {
          return isNumeric();
        } else if constexpr (
            std::same_as<StorageType, BorrowedStringStorage> ||
            std::same_as<StorageType, OwnedStringStorage>) {
          return isString();
        } else {
          static_assert(
              std::same_as<StorageType, void>, "Unhandled Column storage type");
        }
      },
      storage_);
  if (!storageMatchesType) {
    throw std::runtime_error(
        fmt::format(
            "Column '{}' storage does not match its type", columnName_));
  }
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

bool Column::isNumeric() const {
  switch (columnType_) {
    case ColumnType::DOUBLE:
    case ColumnType::UTILIZATION:
    case ColumnType::DIMENSION:
    case ColumnType::INTEGER:
    case ColumnType::IDENTIFIER:
      return true;
    case ColumnType::STRING:
    case ColumnType::ENTITY_NAME:
    case ColumnType::PARTITION:
    case ColumnType::ASSIGNMENT:
    case ColumnType::SCOPE:
      return false;
  }
  throw std::runtime_error(
      fmt::format("Unknown column type: {}", static_cast<int>(columnType_)));
}

bool Column::isString() const {
  return !isNumeric();
}

void Column::requireNumeric(const std::string_view operation) const {
  if (!isNumeric()) {
    throw std::runtime_error(
        fmt::format(
            "{} requires a numeric column, but column '{}' is a string",
            operation,
            columnName_));
  }
}

void Column::requireString(const std::string_view operation) const {
  if (!isString()) {
    throw std::runtime_error(
        fmt::format(
            "{} requires a string column, but column '{}' is numeric",
            operation,
            columnName_));
  }
}

std::size_t Column::getRowCount() const {
  return std::visit(
      [](const auto& storage) { return storage.totalSize(); }, storage_);
}

double Column::getDouble(const EntityId entityId) const {
  requireNumeric("Reading a double value");
  return std::visit(
      [this, entityId](const auto& storage) -> double {
        using StorageType = std::decay_t<decltype(storage)>;
        if constexpr (
            std::same_as<StorageType, DoubleStorage> ||
            std::same_as<StorageType, BoolStorage>) {
          checkRowId(entityId, storage.totalSize());
          return storage.getValue(entityId);
        } else {
          throw std::runtime_error(
              fmt::format(
                  "Column '{}' storage does not match its type", columnName_));
        }
      },
      storage_);
}

std::string_view Column::getStrView(const EntityId entityId) const {
  requireString("Reading a string value");
  return std::visit(
      [this, entityId](const auto& storage) -> std::string_view {
        using StorageType = std::decay_t<decltype(storage)>;
        if constexpr (
            std::same_as<StorageType, BorrowedStringStorage> ||
            std::same_as<StorageType, OwnedStringStorage>) {
          checkRowId(entityId, storage.totalSize());
          return storage.getValue(entityId);
        } else {
          throw std::runtime_error(
              fmt::format(
                  "Column '{}' storage does not match its type", columnName_));
        }
      },
      storage_);
}

void Column::checkRowId(const EntityId entityId, const std::size_t rowCount)
    const {
  if (entityId.asIndex() >= rowCount) {
    throw std::runtime_error(
        fmt::format(
            "Row ID {} is out of range for column '{}' with {} rows",
            entityId.asIndex(),
            columnName_,
            rowCount));
  }
}

std::string Column::toString(const EntityId entityId) const {
  return isString() ? std::string(getStrView(entityId))
                    : std::to_string(getDouble(entityId));
}

bool Column::matches(const EntityId entityId, const CellValue& expected) const {
  if (isNumeric()) {
    const auto* expectedValue = std::get_if<double>(&expected);
    if (!expectedValue) {
      return false;
    }
    const auto actualValue = getDouble(entityId);
    return actualValue == *expectedValue ||
        (std::isnan(actualValue) && std::isnan(*expectedValue));
  }
  const auto* expectedValue = std::get_if<std::string>(&expected);
  return expectedValue && getStrView(entityId) == *expectedValue;
}

Table::Table(const std::size_t rowCount) {
  rowIds_.reserve(rowCount);
  for (const auto index : folly::irange(rowCount)) {
    rowIds_.push_back(toEntityId(index));
  }
}

void Table::insertColumn(std::shared_ptr<const Column> column) {
  const auto columnRowCount = column->getRowCount();
  if (columnRowCount != rowIds_.size()) {
    throw std::runtime_error(
        fmt::format(
            "Column '{}' row count {} does not match table row count {}",
            column->getColumnName(),
            columnRowCount,
            rowIds_.size()));
  }
  if (column->getColumnType() == ColumnType::IDENTIFIER) {
    if (idColExists_) {
      throw std::runtime_error("Expected only one column of type IDENTIFIER");
    }
    idColExists_ = true;
  }
  if (column->isPrimaryKey()) {
    primaryKeyColumns_.push_back(column.get());
  }
  columns_.push_back(std::move(column));
}

void Table::insertColumn(ColumnMetadata metadata, std::vector<double> values) {
  insertColumn(
      std::make_shared<Column>(
          Column::DoubleStorage{.values = std::move(values)},
          std::move(metadata)));
}

void Table::insertColumn(
    ColumnMetadata metadata,
    std::vector<BorrowedString> values) {
  insertColumn(
      std::make_shared<Column>(
          Column::BorrowedStringStorage{.values = std::move(values)},
          std::move(metadata)));
}

void Table::insertColumnsInSortedOrder(
    std::vector<std::shared_ptr<const Column>> columns) {
  std::sort(
      columns.begin(), columns.end(), [](const auto& lhs, const auto& rhs) {
        return lhs->getColumnName() < rhs->getColumnName();
      });
  for (auto& column : columns) {
    insertColumn(std::move(column));
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
    const std::vector<CellValue>& rowValues) {
  const auto& columns = table.getColumnData();
  if (columns.size() != rowValues.size()) {
    throw std::runtime_error(
        "Number of values must match number of columns in table");
  }
  if (rowValues.empty()) {
    throw std::runtime_error(
        "Expected at least one column value to be provided");
  }

  const auto& rowIds = table.getRowIds();
  for (const auto& rowId : rowIds) {
    bool allMatch = true;
    for (const auto i : folly::irange(rowValues.size())) {
      const auto& column = columns[i];
      if (!column->matches(rowId, rowValues[i])) {
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
} // namespace facebook::rebalancer::explorer
