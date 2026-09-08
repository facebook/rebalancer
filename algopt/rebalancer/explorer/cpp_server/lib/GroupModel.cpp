// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/lib/GroupModel.h"

#include "rebalancer/explorer/cpp_server/lib/Utils.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <folly/Conv.h>

#include <functional>
#include <string>
#include <vector>

namespace facebook {
namespace rebalancer {
namespace explorer {

namespace {
const static std::string kRowCount = "Row_Count";

using ColumnRef = std::reference_wrapper<const Column>;
} // namespace

using namespace facebook::rebalancer::entities;

static RowId getGroupRowId(
    GroupValueCellStruct group,
    Map<GroupValueCellStruct, RowId>& groupToRowId,
    std::vector<RowId>& newRowIds) {
  auto groupRowPtr = folly::get_ptr(groupToRowId, group);
  if (groupRowPtr) {
    return *groupRowPtr;
  } else {
    const RowId rowId(static_cast<EntityIdType>(newRowIds.size()));
    groupToRowId.emplace(std::move(group), rowId);
    newRowIds.push_back(rowId);
    return rowId;
  }
}

static std::pair<std::vector<RowId>, Map<RowId, RowId>> createNewRowIds(
    const std::vector<ColumnRef>& groupByTableColumns,
    const std::vector<RowId>& filteredRowIds) {
  // Assign one dense row ID per distinct group and map each input row to it.
  Map<GroupValueCellStruct, RowId> groupToRowId;
  std::vector<RowId> newRowIds;
  Map<RowId, RowId> rowIdToGroupRowId;

  for (const auto filteredRowId : filteredRowIds) {
    std::vector<std::string> groupValue;
    groupValue.reserve(groupByTableColumns.size());
    for (const auto column : groupByTableColumns) {
      groupValue.emplace_back(column.get().getStrView(filteredRowId));
    }
    GroupValueCellStruct value{.groupCellValue = std::move(groupValue)};
    const auto groupRowId =
        getGroupRowId(std::move(value), groupToRowId, newRowIds);
    rowIdToGroupRowId.emplace(filteredRowId, groupRowId);
  }

  return std::pair(std::move(newRowIds), std::move(rowIdToGroupRowId));
}

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

Table GroupModel::applyGroup(const Group& group, Table table) {
  /* Group by filtered rows based on requested columns */
  auto groupByColumns = *group.columns();
  const auto& columns = table.getColumnData();
  const auto& filteredRowIds = table.getRowIds();
  auto groupByTableColumns = extractGroupByColumns(groupByColumns, columns);
  auto [newRowIds, rowIdToGroupRowId] =
      createNewRowIds(groupByTableColumns, filteredRowIds);
  const auto groupCount = folly::to<EntityIdType>(newRowIds.size());

  TableBuilder<RowId> builder(newRowIds);
  for (const auto column : groupByTableColumns) {
    const auto& groupByColumn = column.get();
    Map<RowId, std::string> groupRowIdToValue;
    groupRowIdToValue.reserve(groupCount);
    for (const auto rowId : filteredRowIds) {
      const auto groupRowId = rowIdToGroupRowId.at(rowId);
      // Every row in a group has the same value for a group-by column.
      groupRowIdToValue.try_emplace(
          groupRowId, groupByColumn.getStrView(rowId));
    }
    builder.add(
        {
            .name = groupByColumn.getColumnName(),
            .type = groupByColumn.getColumnType(),
            .isPrimaryKey = true,
        },
        [&groupRowIdToValue](const RowId rowId) -> std::string {
          return std::move(groupRowIdToValue.at(rowId));
        });
  }

  for (const auto& column : columns) {
    // only columns that can be aggregated (like double/int)
    if (!column->isNumeric()) {
      continue;
    }
    if (column->isExcludedFromAggregation()) {
      continue;
    }
    const auto isColTypeId =
        (column->getColumnType() == ColumnType::IDENTIFIER);
    Map<RowId, double> groupRowIdToTotal;
    groupRowIdToTotal.reserve(groupCount);
    for (const auto rowId : filteredRowIds) {
      const auto groupRowId = rowIdToGroupRowId.at(rowId);
      const double value = isColTypeId ? 1.0 : column->getDouble(rowId);
      const auto [totalIt, inserted] =
          groupRowIdToTotal.try_emplace(groupRowId, value);
      if (!inserted) {
        totalIt->second += value;
      }
    }
    builder.add(
        {.name = isColTypeId ? kRowCount : column->getColumnName(),
         .type = isColTypeId ? ColumnType::INTEGER : column->getColumnType()},
        [&groupRowIdToTotal](const RowId rowId) {
          return groupRowIdToTotal.at(rowId);
        });
  }
  return builder.build();
}

} // namespace explorer
} // namespace rebalancer
} // namespace facebook
