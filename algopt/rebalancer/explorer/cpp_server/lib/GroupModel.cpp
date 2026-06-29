// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/lib/GroupModel.h"

#include "rebalancer/explorer/cpp_server/lib/Utils.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

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
      groupValue.push_back(*col->getValue(filteredRow).strValue);
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
        return Utils::fetchColumn(columns, columnName);
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

  // new group_by table
  Table groupByTable(std::move(newRowIds));
  // first process group_by columns
  for (const auto& column : groupByTableColumns) {
    Map<EntityId, DataCell> groupEntityToCell;
    for (auto origRowId : filteredRows) {
      auto newRowId = origRowToGroupRow.at(origRowId);
      // different rows belonging to same group will have same value
      // for the column, hence its okay to refer to any value
      groupEntityToCell[newRowId] = column->getValue(origRowId);
    }
    auto groupColumn = std::make_shared<Column>(
        std::move(groupEntityToCell),
        // This table should be complete, we shouldn't need any dafaults
        DataCell(),
        column->getColumnName(),
        column->getColumnType(),
        /*primaryKey=*/true);
    groupByTable.insertColumn(groupColumn);
  }

  for (const auto& column : columns) {
    // only columns that can be aggregated (like double/int)
    if (!column->typeLikeIdOrInt() && !column->typeLikeDouble()) {
      continue;
    }
    if (column->isExcludedFromAggregation()) {
      continue;
    }
    const auto isColTypeId =
        (column->getColumnType() == ColumnType::IDENTIFIER);
    Map<EntityId, DataCell> groupEntityToCell;
    for (auto origRowId : filteredRows) {
      const auto newRowId = origRowToGroupRow.at(origRowId);
      const auto cellValue = *column->getValue(origRowId).doubleValue;
      auto cellPtr = folly::get_ptr(groupEntityToCell, newRowId);

      // if colType is IDENTIFIER, then we just count the number of rows
      if (cellPtr) {
        *cellPtr->doubleValue += isColTypeId ? 1.0 : cellValue;
      } else {
        DataCell cell(isColTypeId ? 1.0 : cellValue);
        groupEntityToCell[newRowId] = std::move(cell);
      }
    }
    const auto& colName = column->getColumnName();
    auto groupColumn = std::make_shared<Column>(
        std::move(groupEntityToCell),
        // This table should be complete, we shouldn't need any dafaults
        DataCell(),
        isColTypeId ? kRowCount : colName,
        isColTypeId ? ColumnType::INTEGER : column->getColumnType());
    groupByTable.insertColumn(std::move(groupColumn));
  }
  return groupByTable;
}

} // namespace explorer
} // namespace rebalancer
} // namespace facebook
