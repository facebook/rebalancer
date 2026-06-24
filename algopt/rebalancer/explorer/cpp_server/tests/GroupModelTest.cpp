// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/server/ModelServer.h"
#include "rebalancer/explorer/cpp_server/tests/TestUtils.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <folly/coro/BlockingWait.h>
#include <gtest/gtest.h>

#include <iterator>
#include <optional>
#include <string>

using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::explorer;

template <class T>
std::set<T> makeSet(std::initializer_list<T> items) {
  return std::set<T>(items);
}

template <class T>
std::set<T> makeSet(const std::vector<T>& items) {
  return std::set<T>(items.begin(), items.end());
}

class GroupModelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    model = TestUtils::buildUniverse();
  }

  // Helper method for synchronous access to async getData
  static Result getData(ModelServer& server, const Query& query) {
    return folly::coro::blockingWait(server.getData(query));
  }

  std::unique_ptr<ModelServer> model;
};

TEST_F(GroupModelTest, GroupModelBasic) {
  Group group;
  group.columns() = {"src.rack"};
  auto query = TestUtils::prepareQuery(std::string("host"), group);
  auto result = getData(*model, query);

  // result should have only group_by column and other numeric columns.
  // Only the group by columns should be primary keys.
  // `expectedObjectColumns` is a map of expected columns and their primary key
  // status.
  const std::map<std::string, bool> expectedObjectColumns = {
      {"src.rack", true},
      {"network_0", false},
      {"network_1", false},
      {"network_2", false},
      {"ram", false},
      {"host_count", false},
      {"dst.dynamicLoad", false},
      {"src.dynamicLoad", false},
      {"is_movable", false}};

  // `actualColumns` is a map of actual columns and their primary key
  // status.
  std::map<std::string, bool> actualColumns;
  std::transform(
      result.columns()->begin(),
      result.columns()->end(),
      std::inserter(actualColumns, actualColumns.end()),
      [](const auto& description) {
        return std::make_pair(*description.name(), *description.primaryKey());
      });
  EXPECT_EQ(expectedObjectColumns, actualColumns);

  std::map<std::string, std::map<std::string, double>> expectedValues,
      actualValues;
  expectedValues["rack0"] = {
      {"dst.dynamicLoad", 3},
      {"host_count", 3},
      {"network_0", 5},
      {"network_1", 7},
      {"network_2", 9},
      {"ram", 224000},
      {"src.dynamicLoad", 12},
      {"is_movable", 3}};
  expectedValues["rack1"] = {
      {"dst.dynamicLoad", 1},
      {"host_count", 1},
      {"network_0", 3},
      {"network_1", 4.5},
      {"network_2", 1},
      {"ram", 128000},
      {"src.dynamicLoad", 1},
      {"is_movable", 1}};
  expectedValues["rack2"] = {
      {"dst.dynamicLoad", 1},
      {"host_count", 1},
      {"network_0", 0},
      {"network_1", 0},
      {"network_2", 0},
      {"ram", 128000},
      {"src.dynamicLoad", 1},
      {"is_movable", 1}};

  for (auto& row : *result.rows()) {
    const auto& rackNumber = *row.cells()->front().stringValue();
    // `row` should contain a cell for each of the expected columns and an
    // additional cell for the group by column.
    EXPECT_EQ(row.cells()->size(), expectedValues[rackNumber].size() + 1);
    for (size_t i = 1; i < row.cells()->size(); i++) {
      actualValues[rackNumber][*result.columns()->at(i).name()] =
          (*row.cells()->at(i).doubleValue());
    }
  }
  EXPECT_EQ(expectedValues, actualValues);
  EXPECT_EQ(3, *result.totalCount());
}

TEST_F(GroupModelTest, GroupModelWithFilter) {
  Group group;
  group.columns() = {"src.rack"};
  FilterRuleRegex regex;
  regex.column() = "host";
  regex.regex() = ".*3$";
  FilterRule rule;
  rule.regex() = regex;
  std::vector<FilterRule> rules = {rule};
  auto query = TestUtils::prepareQuery(std::string("host"), group, rules);
  auto result = getData(*model, query);
  EXPECT_EQ("rack1", *result.rows()->front().cells()->front().stringValue());
  EXPECT_EQ(1, *result.totalCount());
}

TEST_F(GroupModelTest, GroupModelMultipleColumns) {
  Group group;
  group.columns() = {"src.rack", "scheme"};
  FilterRuleRegex regex;
  regex.column() = "host";
  regex.regex() = "ost";
  FilterRule rule;
  rule.regex() = regex;
  std::vector<FilterRule> rules = {rule};
  auto query = TestUtils::prepareQuery(std::string("host"), group, rules);
  auto result = getData(*model, query);

  // result should have only group_by columns and other numeric columns
  // Only the group by columns should be primary keys.
  // `expectedObjectColumns` is a map of expected columns and their primary key
  // status.
  const std::map<std::string, bool> expectedObjectColumns = {
      {"src.rack", true},
      {"scheme", true},
      {"network_0", false},
      {"network_1", false},
      {"network_2", false},
      {"ram", false},
      {"host_count", false},
      {"dst.dynamicLoad", false},
      {"src.dynamicLoad", false},
      {"is_movable", false}};

  // `actualColumns` is a map of actual columns and their primary key
  // status.
  std::map<std::string, bool> actualColumns;
  std::transform(
      result.columns()->begin(),
      result.columns()->end(),
      std::inserter(actualColumns, actualColumns.end()),
      [](const auto& description) {
        return std::make_pair(*description.name(), *description.primaryKey());
      });
  EXPECT_EQ(expectedObjectColumns, actualColumns);

  // Keys are rack, scheme, and numeric column names in that order.
  std::map<std::string, std::map<std::string, std::map<std::string, double>>>
      expectedValues, actualValues;
  expectedValues["rack0"]["twshared"] = {
      {"network_0", 1.5},
      {"network_1", 2.5},
      {"network_2", 9},
      {"ram", 64000},
      {"host_count", 1},
      {"dst.dynamicLoad", 1},
      {"src.dynamicLoad", 10},
      {"is_movable", 1}};

  expectedValues["rack0"]["cache"] = {
      {"network_0", 0},
      {"network_1", 0},
      {"network_2", 0},
      {"ram", 32000},
      {"host_count", 1},
      {"dst.dynamicLoad", 1},
      {"src.dynamicLoad", 1},
      {"is_movable", 1}};

  expectedValues["rack0"][""] = {
      {"network_0", 3.5},
      {"network_1", 4.5},
      {"network_2", 0},
      {"ram", 128000},
      {"host_count", 1},
      {"dst.dynamicLoad", 1},
      {"src.dynamicLoad", 1},
      {"is_movable", 1}};

  expectedValues["rack1"]["twshared"] = {
      {"network_0", 3},
      {"network_1", 4.5},
      {"network_2", 1},
      {"ram", 128000},
      {"host_count", 1},
      {"dst.dynamicLoad", 1},
      {"src.dynamicLoad", 1},
      {"is_movable", 1}};

  expectedValues["rack2"][""] = {
      {"network_0", 0},
      {"network_1", 0},
      {"network_2", 0},
      {"ram", 128000},
      {"host_count", 1},
      {"dst.dynamicLoad", 1},
      {"src.dynamicLoad", 1},
      {"is_movable", 1}};

  for (auto& row : *result.rows()) {
    const auto& rack = *row.cells()->at(0).stringValue();
    const auto& scheme = *row.cells()->at(1).stringValue();
    // `row` should contain a cell for each of the expected columns
    // and additional two cells for the group by columns.
    EXPECT_EQ(row.cells()->size(), expectedValues[rack][scheme].size() + 2);
    for (size_t i = 2; i < row.cells()->size(); i++) {
      actualValues[rack][scheme][*result.columns()->at(i).name()] =
          *row.cells()->at(i).doubleValue();
    }
  }
  EXPECT_EQ(expectedValues, actualValues);
  EXPECT_EQ(5, *result.totalCount());
}
