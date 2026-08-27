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
    : Column(
          LegacyStorage{
              .nonDefaultValues = std::move(nonDefaultValues),
              .defaultValue = std::move(defaultValue)},
          ColumnMetadata{
              .name = std::move(columnName),
              .type = columnType,
              .isPrimaryKey = primaryKey,
              .description = std::move(description),
              .excludeFromAggregation = excludeFromAggregation}) {}

Column::Column(Storage storage, ColumnMetadata metadata)
    : storage_(std::move(storage)),
      columnName_(std::move(metadata.name)),
      columnType_(metadata.type),
      primaryKey_(metadata.isPrimaryKey),
      description_(std::move(metadata.description)),
      excludeFromAggregation_(metadata.excludeFromAggregation) {
  const auto storageMatchesType = std::visit(
      [&](const auto& storage) {
        using StorageType = std::decay_t<decltype(storage)>;
        if constexpr (std::same_as<StorageType, LegacyStorage>) {
          return true;
        } else if constexpr (std::same_as<StorageType, BoolStorage>) {
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

const DataCell& Column::legacyCellAt(
    const LegacyStorage& storage,
    const EntityId entityId) {
  return folly::get_ref_default(
      storage.nonDefaultValues, entityId, storage.defaultValue);
}

bool Column::hasValuesMatchingType(
    const std::vector<EntityId>& entityIds) const {
  const auto expectsDouble = isNumeric();
  return std::visit(
      [&](const auto& storage) {
        using StorageType = std::decay_t<decltype(storage)>;
        for (const auto entityId : entityIds) {
          if constexpr (std::same_as<StorageType, LegacyStorage>) {
            if (!valueMatchesType(
                    legacyCellAt(storage, entityId), expectsDouble)) {
              return false;
            }
          } else if (entityId.asIndex() >= storage.totalSize()) {
            return false;
          }
        }
        return true;
      },
      storage_);
}

bool Column::valueMatchesType(const DataCell& value, const bool expectsDouble) {
  return expectsDouble ? value.doubleValue && !value.strValue
                       : value.strValue && !value.doubleValue;
}

double Column::getDouble(const EntityId entityId) const {
  requireNumeric("Reading a double value");
  if (const auto* data = std::get_if<DoubleStorage>(&storage_)) {
    return data->getValue(entityId);
  }
  if (const auto* data = std::get_if<BoolStorage>(&storage_)) {
    return data->getValue(entityId);
  }

  const auto& data = std::get<LegacyStorage>(storage_);
  return *legacyCellAt(data, entityId).doubleValue;
}

std::string_view Column::getStrView(const EntityId entityId) const {
  requireString("Reading a string value");
  if (const auto* data = std::get_if<BorrowedStringStorage>(&storage_)) {
    return data->getValue(entityId);
  }
  if (const auto* data = std::get_if<OwnedStringStorage>(&storage_)) {
    return data->getValue(entityId);
  }

  const auto& data = std::get<LegacyStorage>(storage_);
  return *legacyCellAt(data, entityId).strValue;
}

std::string Column::toString(const EntityId entityId) const {
  return isString() ? std::string(getStrView(entityId))
                    : std::to_string(getDouble(entityId));
}

bool Column::matches(const EntityId entityId, const DataCell& expected) const {
  if (!valueMatchesType(expected, isNumeric())) {
    return false;
  }
  return isNumeric() ? getDouble(entityId) == *expected.doubleValue
                     : getStrView(entityId) == *expected.strValue;
}

Table::Table(const std::size_t rowCount) {
  rowIds_.reserve(rowCount);
  for (const auto index : folly::irange(rowCount)) {
    rowIds_.push_back(toEntityId(index));
  }
}

Table::Table(std::vector<EntityId> rowIds) : rowIds_(std::move(rowIds)) {}

void Table::insertColumn(std::shared_ptr<const Column> columnData) {
  if (!columnData->hasValuesMatchingType(rowIds_)) {
    throw std::runtime_error(
        fmt::format(
            "Column '{}' must have exactly one value matching its type for every table row",
            columnData->getColumnName()));
  }
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
      const auto& column = columns.at(i);
      if (!column->matches(rowId, columnValues[i])) {
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
