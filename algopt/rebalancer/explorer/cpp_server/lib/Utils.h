// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include "algopt/rebalancer/algopt_common/DynamicBitSet.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <fmt/core.h>
#include <folly/container/irange.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace facebook::rebalancer::explorer {

using RowId = entities::EntityId<struct ExplorerRowTag>;
using BorrowedString = std::reference_wrapper<const std::string>;
using CellValue = std::variant<std::string, double>;

struct ColumnMetadata {
  std::string name;
  ColumnType type;
  bool isPrimaryKey = false;
  std::string description = {};
  bool excludeFromAggregation = false;
};

class Column {
  /* Stores column details. */
 public:
  double getDouble(RowId rowId) const;
  std::string_view getStrView(RowId rowId) const;
  std::string toString(RowId rowId) const;

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
  template <typename>
  friend class TableBuilder;

  std::size_t getRowCount() const;
  bool matches(RowId rowId, const CellValue& expected) const;

  struct DoubleStorage {
    std::vector<double> values;

    double getValue(RowId rowId) const {
      return values.at(rowId.asIndex());
    }

    std::size_t totalSize() const {
      return values.size();
    }
  };

  struct BoolStorage {
    algopt::DynamicBitSet values;

    bool getValue(RowId rowId) const {
      return values.isSet(rowId.asIndex());
    }

    std::size_t totalSize() const {
      return values.size();
    }
  };

  struct BorrowedStringStorage {
    std::vector<BorrowedString> values;

    std::string_view getValue(RowId rowId) const {
      return values.at(rowId.asIndex()).get();
    }

    std::size_t totalSize() const {
      return values.size();
    }
  };

  struct OwnedStringStorage {
    std::vector<std::string> values;

    const std::string& getValue(RowId rowId) const {
      return values.at(rowId.asIndex());
    }

    std::size_t totalSize() const {
      return values.size();
    }
  };

  using Storage = std::variant<
      DoubleStorage,
      BoolStorage,
      BorrowedStringStorage,
      OwnedStringStorage>;

 public:
  // Must be public because std::make_shared cannot call a private constructor.
  Column(Storage storage, ColumnMetadata metadata);

 private:
  void checkRowId(RowId rowId, std::size_t rowCount) const;

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
  explicit Table(std::size_t rowCount);
  void insertColumn(std::shared_ptr<const Column> column);
  void insertColumn(ColumnMetadata metadata, std::vector<double> values);
  void insertColumn(
      ColumnMetadata metadata,
      std::vector<BorrowedString> values);
  void insertColumnsInSortedOrder(
      std::vector<std::shared_ptr<const Column>> columns);
  std::vector<std::string> getColumnNames() const;
  const std::vector<std::shared_ptr<const Column>>& getColumnData() const;
  const std::vector<RowId>& getRowIds() const;
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
  void updateRowIds(std::vector<RowId>);

 private:
  std::vector<std::shared_ptr<const Column>> columns_;
  std::vector<RowId> rowIds_;
  std::vector<const Column*> primaryKeyColumns_;
  bool idColExists_ = false;
};

template <typename RowKey>
class TableBuilder {
 public:
  // rowKeys must remain valid and unchanged until build().
  explicit TableBuilder(const std::vector<RowKey>& rowKeys)
      : rowKeys_(rowKeys), table_(rowKeys.size()) {}

  TableBuilder(std::vector<RowKey>&&) = delete;
  TableBuilder(const std::vector<RowKey>&&) = delete;

  // Strings borrowed with std::cref must outlive the built table.
  template <typename GetValue>
    requires std::invocable<GetValue&, const RowKey&>
  std::shared_ptr<const Column> make(ColumnMetadata metadata, GetValue getValue)
      const {
    checkNotBuilt();
    using Value = std::invoke_result_t<GetValue&, const RowKey&>;
    auto storage = [&]() -> Column::Storage {
      if constexpr (std::same_as<Value, bool>) {
        return buildBoolStorage(getValue);
      } else if constexpr (std::same_as<Value, double>) {
        return buildVectorStorage<double, Column::DoubleStorage>(getValue);
      } else if constexpr (std::same_as<Value, BorrowedString>) {
        return buildVectorStorage<
            BorrowedString,
            Column::BorrowedStringStorage>(getValue);
      } else if constexpr (std::same_as<Value, std::string>) {
        return buildVectorStorage<std::string, Column::OwnedStringStorage>(
            getValue);
      } else {
        static_assert(
            !std::same_as<Value, Value>,
            "Column callback must return bool, double, std::string, or BorrowedString");
      }
    }();

    return std::make_shared<Column>(std::move(storage), std::move(metadata));
  }

  template <typename GetValue>
    requires std::invocable<GetValue&, const RowKey&>
  TableBuilder& add(ColumnMetadata metadata, GetValue getValue) {
    table_.insertColumn(make(std::move(metadata), std::move(getValue)));
    return *this;
  }

  TableBuilder& add(std::shared_ptr<const Column> column) {
    checkNotBuilt();
    if (!column) {
      throw std::runtime_error("Cannot add a null column");
    }
    table_.insertColumn(std::move(column));
    return *this;
  }

  TableBuilder& addSorted(std::vector<std::shared_ptr<const Column>> columns) {
    checkNotBuilt();
    table_.insertColumnsInSortedOrder(std::move(columns));
    return *this;
  }

  Table build() {
    if (built_) {
      throw std::runtime_error("Cannot build the same table more than once");
    }
    built_ = true;
    return std::move(table_);
  }

 private:
  void checkNotBuilt() const {
    if (built_) {
      throw std::runtime_error("Cannot add columns after table is built");
    }
  }

  template <typename GetValue>
  Column::BoolStorage buildBoolStorage(GetValue& getValue) const {
    Column::BoolStorage storage{
        .values = algopt::DynamicBitSet(rowKeys_.size())};
    for (const auto index : folly::irange(rowKeys_.size())) {
      if (getValue(rowKeys_[index])) {
        storage.values.set(index);
      }
    }
    return storage;
  }

  template <typename Value, typename Storage, typename GetValue>
  Storage buildVectorStorage(GetValue& getValue) const {
    std::vector<Value> values;
    values.reserve(rowKeys_.size());
    for (const auto& rowKey : rowKeys_) {
      values.push_back(getValue(rowKey));
    }
    return Storage{.values = std::move(values)};
  }

  const std::vector<RowKey>& rowKeys_;
  Table table_;
  bool built_ = false;
};

class Utils {
 private:
  explicit Utils();

 public:
  static std::shared_ptr<const Column> fetchColumn(
      const std::vector<std::shared_ptr<const Column>>& columns,
      const std::string& columnName);

  static bool existsRow(
      const Table& table,
      const std::vector<CellValue>& rowValues);

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
