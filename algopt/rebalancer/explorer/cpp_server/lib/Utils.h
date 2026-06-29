// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <optional>
#include <ranges>
#include <string>
#include <vector>

namespace facebook::rebalancer::explorer {

using EntityId = entities::EntityId<struct ExplorerTag>;

template <typename T>
constexpr EntityId toEntityId(T id) {
  return EntityId(static_cast<entities::EntityIdType>(id));
}

struct DataCell {
  /* Stores value of the cell */
  std::optional<std::string> strValue = std::nullopt;
  std::optional<double> doubleValue = std::nullopt;

  DataCell() = default;
  explicit DataCell(const std::string& value) : strValue(value) {}
  explicit DataCell(std::string&& value) : strValue(std::move(value)) {}
  explicit DataCell(double value) : doubleValue(value) {}

  std::string toString() const {
    if (strValue.has_value()) {
      return strValue.value();
    } else if (doubleValue.has_value()) {
      return std::to_string(doubleValue.value());
    }
    return "";
  }

  bool operator==(const DataCell& other) const {
    return strValue == other.strValue && doubleValue == other.doubleValue;
  }
};

class Column {
  /* Stores column details. */
  static inline const std::string kEmptyString;

 public:
  Column(
      entities::Map<EntityId, DataCell> nonDefaultValues,
      DataCell defaultValue,
      std::string columnName,
      ColumnType columnType,
      bool primaryKey = false,
      std::string description = kEmptyString,
      bool excludeFromAggregation = false);

  const DataCell& getValue(EntityId entityId) const;
  ColumnType getColumnType() const;
  const std::string& getColumnName() const;
  bool isPrimaryKey() const {
    return primaryKey_;
  }
  bool isExcludedFromAggregation() const {
    return excludeFromAggregation_;
  }
  const std::string& getDescription() const;

  const entities::Map<EntityId, DataCell>& getNonDefaultValues() const;

  bool typeLikeIdOrInt() const;
  bool typeLikeDouble() const;
  bool typeLikeString() const;

 private:
  const entities::Map<EntityId, DataCell> nonDefaultValues_;
  const DataCell defaultValue_;
  const std::string columnName_;
  const ColumnType columnType_;
  // Denotes whether this column is part of the set of primary keys for the
  // table it belongs to. Multiple columns can be part of the primary key for a
  // table, and the value of these columns should uniquely identify a row in the
  // table.
  const bool primaryKey_;
  // Description of the column. Used, for example, to show a toolTip in the UI
  // when non-empty
  const std::string description_;
  // When true, this column is skipped during group-by aggregation even if it
  // has an aggregatable type (int/double). Useful for metadata columns like
  // Stage Id or Cycle Id where summing values is meaningless.
  const bool excludeFromAggregation_;
};

class Table {
  /* Stores details about Table */
 public:
  explicit Table(std::vector<EntityId> rowIds);
  void insertColumn(std::shared_ptr<const Column> columnData);
  void insertColumnsInSortedOrder(
      std::vector<std::shared_ptr<const Column>> columnsData);
  std::vector<std::string> getColumnNames() const;
  const std::vector<std::shared_ptr<const Column>>& getColumnData() const;
  const std::vector<EntityId>& getRowIds() const;
  const std::vector<const Column*>& getPrimaryKeyColumns() const {
    return primaryKeyColumns_;
  }
  const Column* getOnlyPrimaryKeyColumn() const {
    if (primaryKeyColumns_.size() != 1) {
      throw std::runtime_error(
          fmt::format(
              "Expected exactly one primary key column, found {}",
              primaryKeyColumns_.size()));
    }
    return primaryKeyColumns_.front();
  }
  void updateRowIds(std::vector<EntityId>);

 private:
  std::vector<std::shared_ptr<const Column>> columns_;
  std::vector<EntityId> rowIds_;
  std::vector<const Column*> primaryKeyColumns_;
  bool idColExists_ = false;
};

class TableBuilder {
  static inline const DataCell kEmptyDataCell = DataCell();

 public:
  struct ColumnDefinition {
    std::string name;
    ColumnType type;
    bool isPrimaryKey = false;
    DataCell defaultValue = kEmptyDataCell;
    std::string description{};
    bool excludeFromAggregation = false;
  };

  TableBuilder() = default;

  TableBuilder& addColumnDefinition(const ColumnDefinition& columnDef);

  template <typename... T>
  TableBuilder& addRow(T&&... args) {
    return addRowWithCells({DataCell(std::forward<T>(args))...});
  }

  TableBuilder& addRowWithCells(const std::vector<DataCell>& values);

  Table build();

 private:
  std::vector<ColumnDefinition> columnDefinitions_;
  std::vector<entities::Map<EntityId, DataCell>> columnMaps_;
  std::vector<EntityId> rowIds_;
  entities::EntityIdType nextRowId_ = 0;
  bool built_ = false;
};

class Utils {
 private:
  explicit Utils();

 public:
  static std::shared_ptr<const Column> fetchColumn(
      const std::vector<std::shared_ptr<const Column>>& columns,
      const std::string& columnName);

  // check if a row with the specified columnValues exists in the table
  static bool existsRow(
      const Table& table,
      const std::vector<DataCell>& columnValues);

  template <typename T>
  static inline std::vector<T> filterOut(
      const std::set<T>& toDeleteIds,
      const std::vector<T>& ids) {
    auto filteredView =
        ids | std::ranges::views::filter([&toDeleteIds](const T& id) {
          return !toDeleteIds.contains(id);
        });
    // Convert the filtered view to a vector
    return std::vector<T>(filteredView.begin(), filteredView.end());
  }
  template <class Map>
  static inline Map filterOut(
      const std::set<typename Map::key_type>& toDeleteIds,
      const Map& map)
    requires IsIterableOverPairs<
        Map,
        typename Map::key_type,
        typename Map::mapped_type>
  {
    auto filteredView =
        map | std::ranges::views::filter([&toDeleteIds](const auto& pair) {
          return !toDeleteIds.contains(pair.first);
        });
    // Convert the filtered view to a map
    return Map(filteredView.begin(), filteredView.end());
  }
};

} // namespace facebook::rebalancer::explorer
