// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "algopt/rebalancer/interface/Constants.h"
#include "rebalancer/explorer/cpp_server/server/ModelServer.h"
#include "rebalancer/explorer/cpp_server/tests/TestUtils.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <folly/coro/BlockingWait.h>
#include <gtest/gtest.h>

#include <iterator>
#include <string>

namespace facebook::rebalancer::explorer::tests {

template <class T>
std::set<T> makeSet(std::initializer_list<T> items) {
  return std::set<T>(items);
}

template <class T>
std::set<T> makeSet(const std::vector<T>& items) {
  return std::set<T>(items.begin(), items.end());
}

class FilterModelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    model = TestUtils::buildUniverse();
  }

  static Result getData(ModelServer& server, const Query& query) {
    return folly::coro::blockingWait(server.getData(query));
  }

  std::unique_ptr<ModelServer> model;
};

TEST_F(FilterModelTest, QueryDataFilterRegex) {
  FilterRuleRegex regex;
  regex.column() = "host";
  regex.regex() = "^(host1|host2)$";
  FilterRule rule;
  rule.regex() = regex;
  const std::vector<FilterRule> rules = {rule};
  auto query = TestUtils::prepareQuery(std::string("host"), rules);
  {
    OrderColumn orderColumn;
    orderColumn.name() = "host";
    orderColumn.direction() = OrderDirection::ASCENDING;
    Order order;
    order.columns() = {std::move(orderColumn)};
    query.order() = std::move(order);
  }
  auto result = getData(*model, query);

  const std::set<std::string> expectedObjectColumns = {
      "host",
      "is_movable",
      "host_count",
      "network_0",
      "network_1",
      "network_2",
      "ram",
      "scheme",
      "src.dynamicLoad",
      "dst.dynamicLoad",
      "src.rack",
      "dst.rack",
      "src.row",
      "dst.row",
      "src.row_incomplete",
      "dst.row_incomplete",
      "src.msb",
      "dst.msb",
      interface::kEquivSetPartition
          .data(), // equivalence set partition column is always added
  };

  std::set<std::string> actualColumns;
  std::transform(
      result.columns()->begin(),
      result.columns()->end(),
      std::inserter(actualColumns, actualColumns.begin()),
      [](const auto& description) { return *description.name(); });
  EXPECT_EQ(expectedObjectColumns, actualColumns);
  EXPECT_EQ("host1", *result.rows()->front().cells()->front().stringValue());
  EXPECT_EQ(2, *result.totalCount());
}

TEST_F(FilterModelTest, QueryDataFilterRegexPartial) {
  FilterRuleRegex regex;
  regex.column() = "host";
  regex.regex() = "host";
  FilterRule rule;
  rule.regex() = regex;
  const std::vector<FilterRule> rules = {rule};
  auto query = TestUtils::prepareQuery(std::string("host"), rules);
  auto result = getData(*model, query);
  EXPECT_EQ(5, *result.totalCount());
}

TEST_F(FilterModelTest, QueryDataFilterAny) {
  FilterRuleStringAny anyRule;
  anyRule.column() = "host";
  anyRule.values() = {"host2", "host3", "abc"};
  FilterRule rule;
  rule.stringAny() = anyRule;
  const std::vector<FilterRule> rules = {rule};
  auto query = TestUtils::prepareQuery(std::string("host"), rules);
  auto result = getData(*model, query);

  const std::set<std::string> expectedRowStringValue = {"host2", "host3"};
  std::set<std::string> actualRowStringValue;
  std::transform(
      result.rows()->begin(),
      result.rows()->end(),
      std::inserter(actualRowStringValue, actualRowStringValue.begin()),
      [](const auto& row) { return *row.cells()->front().stringValue(); });

  EXPECT_EQ(expectedRowStringValue, actualRowStringValue);
}

TEST_F(FilterModelTest, QueryDataFilterNumericLT) {
  FilterRuleNumeric numericRule;
  numericRule.column() = "ram";
  numericRule.doubleValue() = 64000;
  numericRule.comparator() = Comparator::LT;
  {
    FilterRule rule;
    rule.numeric() = numericRule;
    const std::vector<FilterRule> rules = {rule};
    const auto query = TestUtils::prepareQuery(std::string("host"), rules);
    const auto result = getData(*model, query);
    EXPECT_EQ("host1", *result.rows()->front().cells()->front().stringValue());
    EXPECT_EQ(1, *result.totalCount());
  }

  { // Test precision comparison
    numericRule.doubleValue() = 64000 + algopt::kEpsilon;
    FilterRule rule;
    rule.numeric() = numericRule;
    const std::vector<FilterRule> rules = {rule};
    const auto query = TestUtils::prepareQuery(std::string("host"), rules);
    const auto result = getData(*model, query);
    EXPECT_EQ(1, *result.totalCount());
  }
}

TEST_F(FilterModelTest, QueryDataFilterNumericEQ) {
  FilterRuleNumeric numericRule;
  numericRule.column() = "ram.initUtil";
  numericRule.doubleValue() = 352000;
  numericRule.comparator() = Comparator::EQ;
  {
    FilterRule rule;
    rule.numeric() = numericRule;
    const std::vector<FilterRule> rules = {rule};
    const auto query = TestUtils::prepareQuery(std::string("msb"), rules);
    const auto result = getData(*model, query);
    EXPECT_EQ(1, *result.totalCount());
  }

  // Test precision comparison
  {
    numericRule.doubleValue() = 352000.0 - algopt::kEpsilon;
    FilterRule rule;
    rule.numeric() = numericRule;
    const std::vector<FilterRule> rules2 = {rule};
    const auto query = TestUtils::prepareQuery(std::string("msb"), rules2);
    const auto result = getData(*model, query);
    EXPECT_EQ(1, *result.totalCount());
  }
}

TEST_F(FilterModelTest, QueryDataFilterNumericGT) {
  FilterRuleNumeric numericRule;
  numericRule.column() = "ram";
  numericRule.doubleValue() = 32000;
  numericRule.comparator() = Comparator::GT;
  FilterRule rule;
  rule.numeric() = numericRule;
  const std::vector<FilterRule> rules = {rule};
  auto query = TestUtils::prepareQuery(std::string("host"), rules);
  auto result = getData(*model, query);
  EXPECT_EQ(4, *result.totalCount());

  // Test precision comparison
  numericRule.doubleValue() = 32000 - algopt::kEpsilon;
  rule.numeric() = numericRule;
  const std::vector<FilterRule> rules2 = {rule};
  query = TestUtils::prepareQuery(std::string("host"), rules2);
  result = getData(*model, query);
  EXPECT_EQ(4, *result.totalCount());
}

TEST_F(FilterModelTest, QueryDataFilterNumericNE) {
  FilterRuleNumeric numericRule;
  numericRule.column() = "ram";
  numericRule.doubleValue() = 32000;
  numericRule.comparator() = Comparator::NE;
  FilterRule rule;
  rule.numeric() = numericRule;
  const std::vector<FilterRule> rules = {rule};
  auto query = TestUtils::prepareQuery(std::string("host"), rules);
  auto result = getData(*model, query);
  EXPECT_EQ(4, *result.totalCount());

  // Test precision comparison
  numericRule.doubleValue() = 32000 + algopt::kEpsilon / 10;
  rule.numeric() = numericRule;
  const std::vector<FilterRule> rules2 = {rule};
  query = TestUtils::prepareQuery(std::string("host"), rules2);
  result = getData(*model, query);
  EXPECT_EQ(4, *result.totalCount());
}

TEST_F(FilterModelTest, QueryDataFilterNe) {
  FilterRuleStringNe neRule;
  neRule.column() = "rack";
  neRule.value() = "rack0";
  FilterRule rule;
  rule.stringNe() = neRule;
  const std::vector<FilterRule> rules = {rule};
  auto query = TestUtils::prepareQuery(std::string("rack"), rules);
  auto result = getData(*model, query);

  // there are 2 rows
  EXPECT_EQ(2, *result.totalCount());

  // find the column index for 'rack'
  const auto& it = std::find_if(
      result.columns()->begin(), result.columns()->end(), [](const auto& col) {
        return col.name() == "rack";
      });
  EXPECT_NE(result.columns()->end(), it);
  const size_t colIdx = std::distance(result.columns()->begin(), it);

  // neither row has "rack0" as value in column 'rack'
  auto expectedRow0 = *result.rows()->at(0).cells()->at(colIdx).stringValue();
  EXPECT_FALSE(expectedRow0 == "rack0");

  auto expectedRow1 = *result.rows()->at(1).cells()->at(colIdx).stringValue();
  EXPECT_FALSE(expectedRow1 == "rack0");
}

TEST_F(FilterModelTest, QueryDataFilterMultiple) {
  // test applying all filters
  std::vector<FilterRule> rules;

  FilterRuleStringNe neStrRule;
  neStrRule.column() = "host";
  neStrRule.value() = "host2";
  FilterRule rule1;
  rule1.stringNe() = neStrRule;
  rules.push_back(rule1);

  FilterRuleNumeric numericRule;
  numericRule.column() = "ram";
  numericRule.doubleValue() = 128000;
  numericRule.comparator() = Comparator::EQ;
  FilterRule rule2;
  rule2.numeric() = numericRule;
  rules.push_back(rule2);

  FilterRuleStringAny anyRule;
  anyRule.column() = "src.msb";
  anyRule.values() = {"msb0", "msb1"};
  FilterRule rule3;
  rule3.stringAny() = anyRule;
  rules.push_back(rule3);

  FilterRuleRegex regex;
  regex.column() = "src.rack";
  regex.regex() = "1";
  FilterRule rule4;
  rule4.regex() = regex;
  rules.push_back(rule4);

  auto query = TestUtils::prepareQuery(std::string("host"), rules);
  auto result = getData(*model, query);
  EXPECT_EQ("host3", *result.rows()->front().cells()->front().stringValue());
  EXPECT_EQ(1, *result.totalCount());
}

} // namespace facebook::rebalancer::explorer::tests
