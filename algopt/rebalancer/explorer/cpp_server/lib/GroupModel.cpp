// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/lib/GroupModel.h"

#include "rebalancer/explorer/cpp_server/lib/Utils.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace facebook {
namespace rebalancer {
namespace explorer {

namespace {
const static std::string kRowCount = "Row_Count";

using ColumnRef = std::reference_wrapper<const Column>;

struct GroupRowData {
  RowId representativeRowId;
  std::vector<double> aggregateValues;
};
} // namespace

using namespace facebook::rebalancer::entities;

static std::vector<ColumnRef> extractGroupByColumns(
    const std::vector<std::string>& groupByColumns,
    const std::vector<std::shared_ptr<const Column>>& columns) {
  std::vector<ColumnRef> groupByTableColumns;
  groupByTableColumns.reserve(groupByColumns.size());
  for (const auto& columnName : groupByColumns) {
    const auto& column = Utils::fetchColumn(columns, columnName);
    column.requireString("Group by");
    groupByTableColumns.push_back(std::cref(column));
  }
  return groupByTableColumns;
}

static std::vector<ColumnRef> extractAggregateColumns(
    const std::vector<std::shared_ptr<const Column>>& columns) {
  std::vector<ColumnRef> aggregateColumns;
  aggregateColumns.reserve(columns.size());
  for (const auto& column : columns) {
    if (column->isNumeric() && !column->isExcludedFromAggregation()) {
      aggregateColumns.push_back(std::cref(*column));
    }
  }
  return aggregateColumns;
}

static std::vector<GroupRowData> buildGroupRows(
    const std::vector<ColumnRef>& groupByTableColumns,
    const std::vector<ColumnRef>& aggregateColumns,
    const std::vector<RowId>& rowIds) {
  Map<GroupValueCellStruct, std::size_t> groupKeyToRowIndex;
  std::vector<GroupRowData> groupRows;
  for (const auto rowId : rowIds) {
    GroupValueCellStruct groupKey;
    groupKey.groupCellValue.reserve(groupByTableColumns.size());
    for (const auto column : groupByTableColumns) {
      groupKey.groupCellValue.emplace_back(column.get().getStrView(rowId));
    }

    const auto [groupIt, inserted] =
        groupKeyToRowIndex.try_emplace(std::move(groupKey), groupRows.size());
    if (inserted) {
      groupRows.push_back(
          {.representativeRowId = rowId,
           .aggregateValues =
               std::vector<double>(aggregateColumns.size(), 0.0)});
    }

    auto& aggregateValues = groupRows.at(groupIt->second).aggregateValues;
    for (const auto i : folly::irange(aggregateColumns.size())) {
      const auto& column = aggregateColumns[i].get();
      const auto value = column.getColumnType() == ColumnType::IDENTIFIER
          ? 1.0
          : column.getDouble(rowId);
      aggregateValues[i] += value;
    }
  }
  return groupRows;
}

Table GroupModel::applyGroup(const Group& group, const Table& table) {
  const auto& groupByColumns = *group.columns();
  const auto& columns = table.getColumnData();
  const auto groupByTableColumns =
      extractGroupByColumns(groupByColumns, columns);
  const auto aggregateColumns = extractAggregateColumns(columns);
  const auto groupRows =
      buildGroupRows(groupByTableColumns, aggregateColumns, table.getRowIds());

  TableBuilder<GroupRowData> builder(groupRows);
  for (const auto i : folly::irange(groupByTableColumns.size())) {
    const auto& column = groupByTableColumns.at(i).get();
    builder.add(
        {
            .name = column.getColumnName(),
            .type = column.getColumnType(),
            .isPrimaryKey = true,
        },
        [&column](const GroupRowData& row) -> std::string {
          return std::string(column.getStrView(row.representativeRowId));
        });
  }

  for (const auto i : folly::irange(aggregateColumns.size())) {
    const auto& column = aggregateColumns.at(i).get();
    const auto countsRows = column.getColumnType() == ColumnType::IDENTIFIER;
    builder.add(
        {.name = countsRows ? kRowCount : column.getColumnName(),
         .type = countsRows ? ColumnType::INTEGER : column.getColumnType()},
        [i](const GroupRowData& row) { return row.aggregateValues.at(i); });
  }
  return builder.build();
}

} // namespace explorer
} // namespace rebalancer
} // namespace facebook
