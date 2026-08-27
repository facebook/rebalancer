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

static EntityId getGroupId(
    GroupValueCellStruct group,
    Map<GroupValueCellStruct, EntityId>& groupToNewRowIds,
    std::vector<EntityId>& newRowIds) {
  auto groupRowPtr = folly::get_ptr(groupToNewRowIds, group);
  if (groupRowPtr) {
    return *groupRowPtr;
  } else {
    EntityId entityID(newRowIds.size());
    groupToNewRowIds.emplace(std::move(group), entityID);
    newRowIds.push_back(entityID);
    return entityID;
  }
}

static std::pair<std::vector<EntityId>, Map<EntityId, EntityId>>
createNewRowIds(
    const std::vector<std::shared_ptr<const Column>>& groupByTableColumns,
    const std::vector<EntityId>& filteredRows) {
  /* This method create new row id for each row in new group_by table.
    It also maps original row to new row which will help in filling the
    group_by table.
   */
  Map<GroupValueCellStruct, EntityId> groupToNewRowIds;
  std::vector<EntityId> newRowIds;
  Map<EntityId, EntityId> origRowToGroupRow;

  for (auto filteredRow : filteredRows) {
    std::vector<std::string> groupValue;
    groupValue.reserve(groupByTableColumns.size());
    for (const auto& col : groupByTableColumns) {
      groupValue.emplace_back(col->getStrView(filteredRow));
    }
    GroupValueCellStruct value{.groupCellValue = std::move(groupValue)};
    auto groupId = getGroupId(std::move(value), groupToNewRowIds, newRowIds);
    origRowToGroupRow.emplace(filteredRow, groupId);
  }

  return std::pair(std::move(newRowIds), std::move(origRowToGroupRow));
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
  const auto& filteredRows = table.getRowIds();
  auto groupByTableColumns = extractGroupByColumns(groupByColumns, columns);
  auto [newRowIds, origRowToGroupRow] =
      createNewRowIds(groupByTableColumns, filteredRows);
  const auto groupCount = folly::to<EntityIdType>(newRowIds.size());

  TableBuilder<EntityId> builder(newRowIds);
  for (const auto& column : groupByTableColumns) {
    Map<EntityId, std::string> groupRowIdToValue;
    groupRowIdToValue.reserve(groupCount);
    for (const auto origRowId : filteredRows) {
      const auto groupRowId = origRowToGroupRow.at(origRowId);
      // Every row in a group has the same value for a group-by column.
      groupRowIdToValue.try_emplace(groupRowId, column->getStrView(origRowId));
    }
    builder.add(
        {
            .name = column->getColumnName(),
            .type = column->getColumnType(),
            .isPrimaryKey = true,
        },
        [&groupRowIdToValue](const EntityId rowId) -> std::string {
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
    Map<EntityId, double> groupRowIdToTotal;
    groupRowIdToTotal.reserve(groupCount);
    for (const auto origRowId : filteredRows) {
      const auto groupRowId = origRowToGroupRow.at(origRowId);
      const double value = isColTypeId ? 1.0 : column->getDouble(origRowId);
      const auto [totalIt, inserted] =
          groupRowIdToTotal.try_emplace(groupRowId, value);
      if (!inserted) {
        totalIt->second += value;
      }
    }
    builder.add(
        {.name = isColTypeId ? kRowCount : column->getColumnName(),
         .type = isColTypeId ? ColumnType::INTEGER : column->getColumnType()},
        [&groupRowIdToTotal](const EntityId rowId) {
          return groupRowIdToTotal.at(rowId);
        });
  }
  return builder.build();
}

} // namespace explorer
} // namespace rebalancer
} // namespace facebook
