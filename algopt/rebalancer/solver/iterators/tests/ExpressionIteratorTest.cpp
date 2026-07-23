// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/iterators/ExpressionIterator.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"

#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class ExpressionIteratorTest : public ExpressionTestsBase {};

CO_TEST_F(ExpressionIteratorTest, GetSortedChildren) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1", {"object1"}}});

  co_await addPartition("partition1", {{"group1", {"object1"}}});
  co_await addScope("scope", {{"scopeItem", {"container1"}}});

  const auto universe = buildUniverse();
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  const auto partition1Id = partitionId("partition1");
  const auto objectCountDimId = dimensionId("object_count");
  const auto scopeId = ExpressionTestsBase::scopeId("scope");
  const auto scopeItemId =
      ExpressionTestsBase::scopeItemId(scopeId, "scopeItem");

  auto op = object_partition(
      std::make_shared<const PartitionInfo>(*universe, partition1Id),
      objectCountDimId,
      {});

  // val = 1, bound = 0 => potential = 1
  auto a = object_partition_lookup(
      op,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(1)}),
      scopeId,
      scopeItemId,
      assignment);
  // val = 2, bound = 2 => potential = 0
  auto b = const_expr(2, *universe);
  auto c = rebalancer::max({a, b}, *universe);

  Context context;
  c->fullApply(
      TopToBottomEvaluator(context), Assignment({{container(1), {object(1)}}}));
  c->init_unconstrained_bounds(context);

  std::vector<Expression*> actual_children = {},
                           expected_children = {a.get(), b.get()};
  std::vector<double> actual_values = {}, expected_values = {1, 2};
  std::vector<double> actual_bounds = {}, expected_bounds = {0, 2};
  for (auto& [child, _] : c->get_sorted_children(true /* descending */)) {
    actual_children.push_back(child);
    actual_values.push_back(child->value);
    actual_bounds.push_back(child->lowerAndUpperBounds(context).lower_bound);
  }
  EXPECT_EQ(actual_children, expected_children);
  EXPECT_EQ(actual_values, expected_values);
  EXPECT_EQ(actual_bounds, expected_bounds);
}

CO_TEST_F(ExpressionIteratorTest, PreOrderExpressionTraversal) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1", {"object1", "object2"}},
          {"container2", {}},
          {"container3", {}}});

  const entities::Map<std::string, std::vector<std::string>> groupToObjects = {
      {"group1", {"object1"}}, {"group2", {"object2"}}};
  co_await addPartition("partition1", groupToObjects);

  const entities::Map<std::string, std::vector<std::string>>
      scopeItemToContainers = {{"scopeItem", {"container1"}}};
  co_await addScope("scope", scopeItemToContainers);

  const auto universe = buildUniverse();
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  const auto partition1Id = partitionId("partition1");
  const auto objectCountDimId = dimensionId("object_count");
  const auto scopeId = ExpressionTestsBase::scopeId("scope");
  const auto scopeItemId =
      ExpressionTestsBase::scopeItemId(scopeId, "scopeItem");

  const auto op = object_partition(
      std::make_shared<const PartitionInfo>(*universe, partition1Id),
      objectCountDimId,
      {});

  auto a = object_partition_lookup(
      op,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(1), container(2)}),
      scopeId,
      scopeItemId,
      assignment); // 1
  auto b = object_partition_lookup(
      op,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(1), container(3)}),
      scopeId,
      scopeItemId,
      assignment);
  auto c = rebalancer::max({a, b}, *universe); // 2
  auto d = square(b); // 4
  auto e = c + d; // 6
  Context context;
  e->fullApply(
      TopToBottomEvaluator(context),
      Assignment({{container(1), {object(1)}}, {container(3), {object(2)}}}));
  e->init_unconstrained_bounds(context);

  auto extract = [](PreOrderExpressionTraversal iter) {
    std::vector<Expression*> res;
    for (auto [expr, _] : iter) {
      res.push_back(expr);
    }
    return res;
  };

  const std::vector<Expression*> ascending_expected = {
      e.get(), c.get(), a.get(), b.get(), d.get()};
  EXPECT_EQ(
      ascending_expected,
      extract(PreOrderAscendingExpressionTraversal(e.get())));

  const std::vector<Expression*> descending_expected = {
      e.get(), d.get(), b.get(), c.get(), a.get()};
  EXPECT_EQ(
      descending_expected,
      extract(PreOrderDescendingExpressionTraversal(e.get())));
}
} // namespace facebook::rebalancer::packer::tests
