// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/lib/GroupModel.h"

#include "rebalancer/explorer/cpp_server/lib/Utils.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <folly/Conv.h>

#include <string>
#include <vector>

namespace facebook {
namespace rebalancer {
namespace explorer {

namespace {
const static std::string kRowCount = "Row_Count";
}

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
    const std::vector<std::shared_ptr<const Column>>& groupByTableColumns,
    const std::vector<RowId>& filteredRowIds) {
  // Assign one dense row ID per distinct group and map each input row to it.
  Map<GroupValueCellStruct, RowId> groupToRowId;
  std::vector<RowId> newRowIds;
  Map<RowId, RowId> rowIdToGroupRowId;

  for (const auto filteredRowId : filteredRowIds) {
    std::vector<std::string> groupValue;
    groupValue.reserve(groupByTableColumns.size());
    for (const auto& col : groupByTableColumns) {
      groupValue.emplace_back(col->getStrView(filteredRowId));
    }
    GroupValueCellStruct value{.groupCellValue = std::move(groupValue)};
    const auto groupRowId =
        getGroupRowId(std::move(value), groupToRowId, newRowIds);
    rowIdToGroupRowId.emplace(filteredRowId, groupRowId);
  }

  return std::pair(std::move(newRowIds), std::move(rowIdToGroupRowId));
}

static std::vector<std::shared_ptr<const Column>> extractGroupByColumns(
    const std::vector<std::string>& groupByColumns,
    const std::vector<std::shared_ptr<const Column>>& columns) {
  /* Return table columns that needs to be grouped. */
  std::vector<std::shared_ptr<const Column>> groupByTableColumns;
  std::transform(
      groupByColumns.begin(),
      groupByColumns.end(),
      std::back_inserter(groupByTableColumns),
      [&columns](const auto& columnName) {
        auto column = Utils::fetchColumn(columns, columnName);
        column->requireString("Group by");
        return column;
      });
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
  for (const auto& column : groupByTableColumns) {
    Map<RowId, std::string> groupRowIdToValue;
    groupRowIdToValue.reserve(groupCount);
    for (const auto rowId : filteredRowIds) {
      const auto groupRowId = rowIdToGroupRowId.at(rowId);
      // Every row in a group has the same value for a group-by column.
      groupRowIdToValue.try_emplace(groupRowId, column->getStrView(rowId));
    }
    builder.add(
        {
            .name = column->getColumnName(),
            .type = column->getColumnType(),
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
