// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include "algopt/rebalancer/algopt_common/CompressedIdMap.h"
#include "algopt/rebalancer/algopt_common/DynamicBitSet.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <fmt/core.h>
#include <folly/lang/SafeAssert.h>

#include <concepts>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <variant>
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

  bool operator==(const DataCell& other) const {
    return strValue == other.strValue && doubleValue == other.doubleValue;
  }
};

struct ColumnMetadata {
  std::string name;
  ColumnType type;
  bool isPrimaryKey = false;
  std::string description = {};
  bool excludeFromAggregation = false;
};

class Column {
  /* Stores column details. */
  static inline const std::string kEmptyString;

 public:
  // Once a non-default value is stored for a row, later emplace calls do not
  // replace it.
  using DoubleStorage = algopt::CompressedIdMap<EntityId, double>;

  class BoolStorage {
   public:
    BoolStorage(std::size_t totalSize, bool defaultValue)
        : defaultValue_(defaultValue), nonDefaultValues_(totalSize) {}

    void emplace(EntityId entityId, bool value) {
      const auto index = indexFor(entityId);
      if (value != defaultValue_) {
        nonDefaultValues_.set(index);
      }
    }

    bool getValue(EntityId entityId) const {
      const auto hasNonDefaultValue =
          nonDefaultValues_.isSet(indexFor(entityId));
      return hasNonDefaultValue ? !defaultValue_ : defaultValue_;
    }

    std::size_t totalSize() const {
      return nonDefaultValues_.size();
    }

   private:
    std::size_t indexFor(EntityId entityId) const {
      const auto index = entityId.asIndex();
      FOLLY_SAFE_CHECK(
          index < nonDefaultValues_.size(),
          "BoolStorage: entity ID out of range: ",
          index);
      return index;
    }

    bool defaultValue_;
    algopt::DynamicBitSet nonDefaultValues_;
  };

  // Inserted strings must remain at the same address and outlive the storage.
  class BorrowedStringStorage {
   public:
    BorrowedStringStorage(
        std::size_t totalSize,
        std::size_t expectedNonDefaultSize)
        : values_(totalSize, nullptr, expectedNonDefaultSize) {}

    BorrowedStringStorage(
        entities::Map<EntityId, const std::string*>&& keyToValue,
        std::size_t totalSize)
        : values_(std::move(keyToValue), nullptr, totalSize) {}

    void emplace(EntityId entityId, const std::string& value) {
      if (!value.empty()) {
        values_.emplace(entityId, &value);
      }
    }
    void emplace(EntityId, std::string&&) = delete;

    std::string_view getValue(EntityId entityId) const {
      const auto* value = values_.getValue(entityId);
      return value ? std::string_view(*value) : std::string_view{};
    }

    std::size_t totalSize() const {
      return values_.totalSize();
    }

   private:
    algopt::CompressedIdMap<EntityId, const std::string*> values_;
  };

  class OwnedStringStorage {
   public:
    OwnedStringStorage(
        std::size_t totalSize,
        std::string defaultValue,
        std::size_t expectedNonDefaultSize)
        : values_(totalSize, std::move(defaultValue), expectedNonDefaultSize) {}

    OwnedStringStorage(
        entities::Map<EntityId, std::string>&& keyToValue,
        std::string defaultValue,
        std::size_t totalSize)
        : values_(std::move(keyToValue), std::move(defaultValue), totalSize) {}

    void emplace(EntityId entityId, std::string value) {
      values_.emplace(entityId, std::move(value));
    }

    const std::string& getValue(EntityId entityId) const {
      return values_.getValue(entityId);
    }

    std::size_t totalSize() const {
      return values_.totalSize();
    }

   private:
    algopt::CompressedIdMap<EntityId, std::string> values_;
  };

  // TODO: Delete this constructor with LegacyStorage after all tables use
  // typed storage.
  Column(
      entities::Map<EntityId, DataCell> nonDefaultValues,
      DataCell defaultValue,
      std::string columnName,
      ColumnType columnType,
      bool primaryKey = false,
      std::string description = kEmptyString,
      bool excludeFromAggregation = false);

  template <typename T>
    requires(
        std::same_as<T, DoubleStorage> || std::same_as<T, BoolStorage> ||
        std::same_as<T, BorrowedStringStorage> ||
        std::same_as<T, OwnedStringStorage>)
  Column(T storage, ColumnMetadata metadata)
      : Column(Storage(std::move(storage)), std::move(metadata)) {}

  double getDouble(EntityId entityId) const;
  std::string_view getStrView(EntityId entityId) const;
  std::string toString(EntityId entityId) const;

  ColumnType getColumnType() const;
  const std::string& getColumnName() const;
  bool isPrimaryKey() const {
    return primaryKey_;
  }
  bool isExcludedFromAggregation() const {
    return excludeFromAggregation_;
  }
  const std::string& getDescription() const;

  bool isNumeric() const;
  bool isString() const;
  void requireNumeric(std::string_view operation) const;
  void requireString(std::string_view operation) const;

 private:
  friend class Table;
  friend class Utils;

  bool matches(EntityId entityId, const DataCell& expected) const;
  bool hasValuesMatchingType(const std::vector<EntityId>& entityIds) const;
  static bool valueMatchesType(const DataCell& value, bool expectsDouble);

  struct LegacyStorage {
    entities::Map<EntityId, DataCell> nonDefaultValues;
    DataCell defaultValue;
  };

  static const DataCell& legacyCellAt(
      const LegacyStorage& storage,
      EntityId entityId);

  using Storage = std::variant<
      LegacyStorage,
      DoubleStorage,
      BoolStorage,
      BorrowedStringStorage,
      OwnedStringStorage>;

  Column(Storage storage, ColumnMetadata metadata);

  const Storage storage_;
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
