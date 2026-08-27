// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/lib/FilterModel.h"

#include "algopt/rebalancer/algopt_common/Precision.h"
#include "rebalancer/explorer/cpp_server/lib/Utils.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <re2/re2.h>

#include <vector>

namespace facebook::rebalancer::explorer {

using namespace facebook::rebalancer::entities;

static bool satisfiesNumericCondition(
    const Comparator comparator,
    const double cellValue,
    const double targetValue) {
  switch (comparator) {
    case Comparator::EQ:
      return algopt::Precision::isEqual(cellValue, targetValue);
    case Comparator::NE:
      return !algopt::Precision::isEqual(cellValue, targetValue);
    case Comparator::LT:
      return algopt::Precision::isStrictlyLesser(cellValue, targetValue);
    case Comparator::GT:
      return algopt::Precision::isstrictlyGreater(cellValue, targetValue);
    case Comparator::LE:
      return algopt::Precision::isLesserOrEqual(cellValue, targetValue);
    case Comparator::GE:
      return algopt::Precision::isGreaterOrEqual(cellValue, targetValue);
  }
}

static void applyFilterRuleRegex(
    const std::vector<std::shared_ptr<const Column>>& columns,
    const FilterRuleRegex& rule,
    std::vector<RowId>& rowIds) {
  const auto& column = Utils::fetchColumn(columns, *rule.column());
  column->requireString("Regex filter");
  rowIds.erase(
      std::remove_if(
          rowIds.begin(),
          rowIds.end(),
          [&column, &rule](auto rowId) {
            return !re2::RE2::PartialMatch(
                column->getStrView(rowId), *rule.regex());
          }),
      rowIds.end());
}

static void applyFilterRuleNumeric(
    const std::vector<std::shared_ptr<const Column>>& columns,
    const FilterRuleNumeric& rule,
    std::vector<RowId>& rowIds) {
  const auto& column = Utils::fetchColumn(columns, *rule.column());
  column->requireNumeric("Numeric filter");
  rowIds.erase(
      std::remove_if(
          rowIds.begin(),
          rowIds.end(),
          [&column, &rule](auto rowId) {
            const auto targetValue = *rule.doubleValue();
            return !satisfiesNumericCondition(
                *rule.comparator(), column->getDouble(rowId), targetValue);
          }),
      rowIds.end());
}

static void applyFilterStringAny(
    const std::vector<std::shared_ptr<const Column>>& columns,
    const FilterRuleStringAny& rule,
    std::vector<RowId>& rowIds) {
  const auto& column = Utils::fetchColumn(columns, *rule.column());
  column->requireString("Any filter");
  rowIds.erase(
      std::remove_if(
          rowIds.begin(),
          rowIds.end(),
          [&column, &rule](auto rowId) {
            const auto value = column->getStrView(rowId);
            return std::find(
                       rule.values()->begin(), rule.values()->end(), value) ==
                rule.values()->end();
          }),
      rowIds.end());
}

static void applyFilterStringNe(
    const std::vector<std::shared_ptr<const Column>>& columns,
    const FilterRuleStringNe& rule,
    std::vector<RowId>& rowIds) {
  const auto& column = Utils::fetchColumn(columns, *rule.column());
  column->requireString("Not-equal filter");
  rowIds.erase(
      std::remove_if(
          rowIds.begin(),
          rowIds.end(),
          [&column, &rule](auto rowId) {
            return column->getStrView(rowId) == *rule.value();
          }),
      rowIds.end());
}

static void applyFilterRule(
    const std::vector<std::shared_ptr<const Column>>& columns,
    const FilterRule& rule,
    std::vector<RowId>& rowIds) {
  switch (rule.getType()) {
    case FilterRule::Type::regex:
      return applyFilterRuleRegex(columns, rule.get_regex(), rowIds);
    case FilterRule::Type::numeric:
      return applyFilterRuleNumeric(columns, rule.get_numeric(), rowIds);
    case FilterRule::Type::stringAny:
      return applyFilterStringAny(columns, rule.get_stringAny(), rowIds);
    case FilterRule::Type::stringNe:
      return applyFilterStringNe(columns, rule.get_stringNe(), rowIds);
    default:
      throw std::runtime_error("Unrecognized filter type");
  }
}

Table FilterModel::applyFilter(const Filter& filter, Table table) {
  /* Apply required filters */
  auto filteredRows = table.getRowIds();
  const auto& tableColumns = table.getColumnData();

  // filter rows based on criteria
  for (const auto& rule : filter.rules().value()) {
    applyFilterRule(tableColumns, rule, filteredRows);
  }
  table.updateRowIds(std::move(filteredRows));
  return table;
}

} // namespace facebook::rebalancer::explorer
