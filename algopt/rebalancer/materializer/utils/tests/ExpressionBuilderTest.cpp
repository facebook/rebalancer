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

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/ObjectDimension.h"
#include "algopt/rebalancer/entities/ObjectStaticDimension.h"
#include "algopt/rebalancer/entities/tests/Utils.h"
#include "algopt/rebalancer/materializer/utils/ExpressionBuilder.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"

#include <folly/coro/BlockingWait.h>
#include <gtest/gtest.h>

#include <memory>

namespace facebook::rebalancer::materializer::tests {

using SortedDestinationLatencyMap = facebook::algopt::ValueSortedMap<
    entities::ScopeItemId,
    double,
    entities::CompareScopeItemLatencyPair>;

class ExpressionBuilderTest : public SpecBuilderTestBase<> {
 protected:
  folly::coro::Task<void> setUpCoro() {
    setUpUniverse(
        {{"host0", {"task0", "task1"}},
         {"host1", {"task2"}},
         {"host2", {}},
         {"host3", {"task3", "task4", "task5"}}});

    // Add object dimensions
    co_await addObjectDimension(
        "cpu",
        {{task(0), 0},
         {task(1), 0.1},
         {task(2), 0.2},
         {task(3), 0.4},
         {task(4), 0.8},
         {task(5), 1.6}},
        0);

    co_await addObjectDimension(
        "network",
        {{{task(0), 0},
          {task(1), 0},
          {task(2), 1},
          {task(3), 0},
          {task(4), 1},
          {task(5), 2}},
         {{task(0), 0},
          {task(1), 1},
          {task(2), 1},
          {task(3), 4},
          {task(4), 0},
          {task(5), 0}}},
        {0, 0});

    // Add scopes
    co_await addScope(
        "rack", {{"rack0", {"host0", "host1"}}, {"rack1", {"host2", "host3"}}});

    co_await addScope(
        "pod", {{"pod0", {"host0", "host1"}}, {"pod1", {"host2"}}});

    co_await addScope(
        "parity",
        {{"parity0", {"host0", "host2"}}, {"parity1", {"host1", "host3"}}});

    // Add scope dimensions
    co_await addScopeDimension(
        "cpu",
        host(),
        {{"host0", 0}, {"host1", 1}, {"host2", 2}, {"host3", 4}},
        0);

    co_await addScopeDimension(
        "cpu", scopeId("rack"), {{"rack0", 1}, {"rack1", 0.1}}, 1);

    // Add partitions
    co_await addPartition(
        "job",
        {{"job0", {"task0", "task2", "task4"}},
         {"job1", {"task1", "task3", "task5"}}});

    // Add dynamic object dimensions
    co_await addDynamicObjectDimension(
        "power",
        scopeId("parity"),
        {{"parity0",
          makeSharedPtrEntityToValueMap({{task(0), 10}, {task(1), 20}})},
         {"parity1",
          makeSharedPtrEntityToValueMap({{task(0), 40}, {task(1), 80}})}},
        1);

    co_await addDynamicObjectDimension(
        "disk",
        scopeId("pod"),
        {{"pod0", makeSharedPtrEntityToValueMap({{task(0), 100}})},
         {"pod1", makeSharedPtrEntityToValueMap({{task(0), 200}})}},
        10);

    co_return;
  }

  void SetUp() override {
    folly::coro::blockingWait(setUpCoro());
  }

  folly::coro::Task<void> addObjectPartitionRoutingDimension() {
    const auto routingConfigId =
        universeBuilder_.makeRoutingConfigId("routingConfig");
    const auto groupToRoutingRings =
        entities::Map<entities::GroupId, std::vector<entities::RoutingRing>>();
    auto defaultOriginToDestinationScopeItemSetsPtr =
        std::make_shared<entities::Map<
            entities::ScopeItemId,
            std::vector<std::vector<entities::ScopeItemId>>>>();
    folly::coro::blockingWait(universeBuilder_.addRoutingConfig(
        routingConfigId,
        entities::RoutingConfigData{
            .routingConfig = std::make_shared<entities::RoutingConfig>(
                groupToRoutingRings,
                std::make_shared<entities::Map<
                    entities::ScopeItemId,
                    SortedDestinationLatencyMap>>(),
                host(),
                job(),
                defaultOriginToDestinationScopeItemSetsPtr)}));

    const auto routingLoadId =
        universeBuilder_.makeObjectDimensionId("routingLoad");
    co_await universeBuilder_.addObjectDimension(
        routingLoadId,
        entities::ObjectDimensionData{
            .dimension = std::make_unique<const entities::ObjectDimension>(
                /*groupIdToValue=*/entities::Map<entities::GroupId, double>{},
                /*defaultValue=*/10.0,
                /*partitionId=*/job(),
                routingConfigId,
                /*groupIdToStaticValue=*/
                entities::Map<entities::GroupId, double>{},
                /*defaultStaticValue=*/2.0)});
  }

  entities::ScopeId host() const {
    return scopeId("host");
  }

  entities::ScopeItemId host(int index) const {
    return scopeItemId("host", fmt::format("{}{}", "host", index));
  }

  entities::ObjectId task(int index) const {
    return objectId(fmt::format("task{}", index));
  }

  entities::DimensionId cpu() const {
    return dimensionId("cpu");
  }

  entities::DimensionId network() const {
    return dimensionId("network");
  }

  entities::DimensionId power() const {
    return dimensionId("power");
  }

  entities::DimensionId disk() const {
    return dimensionId("disk");
  }

  entities::DimensionId routingLoad() const {
    return dimensionId("routingLoad");
  }

  entities::ScopeId rack() const {
    return scopeId("rack");
  }

  entities::ScopeItemId rack(int index) const {
    return scopeItemId("rack", fmt::format("rack{}", index));
  }

  entities::PartitionId job() const {
    return partitionId("job");
  }

  entities::GroupId job(int index) const {
    return groupId("job", fmt::format("job{}", index));
  }

  entities::ScopeId pod() const {
    return scopeId("pod");
  }

  entities::ScopeItemId pod(int index) const {
    return scopeItemId("pod", fmt::format("pod{}", index));
  }

  entities::ScopeId parity() const {
    return scopeId("parity");
  }

  entities::ScopeItemId parity(int index) const {
    return scopeItemId("parity", fmt::format("parity{}", index));
  }

  folly::coro::Task<ExprPtr> getStayedForStableOptimizationTest() {
    auto& builder = expressionBuilder();
    auto util = co_await builder.getAbsoluteUtil(
        UtilMetric::NEW, cpu(), host(), host(0));

    // Extract the "StableStayed" component based on the sign and the knowledge
    // that "new = after - stayed".
    EXPECT_EQ("LinearSum", util->getType());

    std::vector<ExprPtr> children(
        util->children().begin(), util->children().end());
    EXPECT_EQ(2, children.size());

    if (util->getChildCoefficient(children.at(0).get()) == -1) {
      swap(children.at(0), children.at(1));
    }

    EXPECT_EQ(1, util->getChildCoefficient(children.at(0).get()));
    EXPECT_EQ(-1, util->getChildCoefficient(children.at(1).get()));
    auto after = children.at(0);
    auto stayed = children.at(1);

    EXPECT_EQ("ObjectLookup", after->getType());
    co_return stayed;
  }
};

CO_TEST_F(ExpressionBuilderTest, AfterAbsolute) {
  auto& builder = expressionBuilder();
  auto host0 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), host(), host(0));
  auto host1 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), host(), host(1));
  auto host2 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), host(), host(2));
  auto host3 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), host(), host(3));

  auto host123 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), host(), {host(1), host(2), host(3)});

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0.1, evaluate(host0, initial), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, initial), 1e-8);
  EXPECT_NEAR(2.8, evaluate(host3, initial), 1e-8);
  EXPECT_NEAR(3.0, evaluate(host123, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(0.1, evaluate(host0, other1), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, other1), 1e-8);
  EXPECT_NEAR(0.4, evaluate(host2, other1), 1e-8);
  EXPECT_NEAR(2.4, evaluate(host3, other1), 1e-8);
  EXPECT_NEAR(3.0, evaluate(host123, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(0.1, evaluate(host0, other2), 1e-8);
  EXPECT_NEAR(1.6, evaluate(host1, other2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other2), 1e-8);
  EXPECT_NEAR(1.4, evaluate(host3, other2), 1e-8);
  EXPECT_NEAR(3.0, evaluate(host123, other2), 1e-8);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  EXPECT_NEAR(0.1, evaluate(host0, other3), 1e-8);
  EXPECT_NEAR(1.6, evaluate(host1, other3), 1e-8);
  EXPECT_NEAR(0.4, evaluate(host2, other3), 1e-8);
  EXPECT_NEAR(1.0, evaluate(host3, other3), 1e-8);
  EXPECT_NEAR(3.0, evaluate(host123, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  EXPECT_NEAR(0.9, evaluate(host0, other4), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other4), 1e-8);
  EXPECT_NEAR(2.0, evaluate(host3, other4), 1e-8);
  EXPECT_NEAR(2.2, evaluate(host123, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  EXPECT_NEAR(0.8, evaluate(host0, other5), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, other5), 1e-8);
  EXPECT_NEAR(0.1, evaluate(host2, other5), 1e-8);
  EXPECT_NEAR(2.0, evaluate(host3, other5), 1e-8);
  EXPECT_NEAR(2.3, evaluate(host123, other5), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, AbsoluteBounded) {
  entities::Map<entities::GroupId, double> groupLimits = {
      {job(0), 0.5}, {job(1), 0.7}};

  auto getBoundedUtil =
      [&](const std::map<entities::GroupId, double>& jobToUtil,
          bool isUpperBound) {
        double total = 0;
        for (auto [job, util] : jobToUtil) {
          if (auto val = folly::get_ptr(groupLimits, job)) {
            total += isUpperBound ? std::min(util, *val) : std::max(util, *val);
          } else {
            total += util;
          }
        }
        return total;
      };

  auto& builder = expressionBuilder();
  for (auto isUpperbound : {true, false}) {
    for (auto defaultUnbounded : {true, false}) {
      std::optional<double> defaultBound = std::nullopt;
      if (!defaultUnbounded) {
        defaultBound = 0;
      }
      auto host0 = co_await builder.getBoundedAbsoluteUtil(
          UtilMetric::AFTER,
          cpu(),
          host(),
          host(0),
          job(),
          groupLimits,
          isUpperbound,
          defaultBound);
      auto host1 = co_await builder.getBoundedAbsoluteUtil(
          UtilMetric::AFTER,
          cpu(),
          host(),
          host(1),
          job(),
          groupLimits,
          isUpperbound,
          defaultBound);
      auto host2 = co_await builder.getBoundedAbsoluteUtil(
          UtilMetric::AFTER,
          cpu(),
          host(),
          host(2),
          job(),
          groupLimits,
          isUpperbound,
          defaultBound);
      auto host3 = co_await builder.getBoundedAbsoluteUtil(
          UtilMetric::AFTER,
          cpu(),
          host(),
          host(3),
          job(),
          groupLimits,
          isUpperbound,
          defaultBound);

      // initially task0 (0) task1 (0.1) are on host0,
      // task2 (0.2) is on host1
      // host2 is empty
      // task3 (0.4) task4 (0.8) task5 (1.6) are on host3
      // job0 = task0, task2, task4
      // job1 = task1, task3, task5
      auto initial = deltaFromInitial({});
      EXPECT_NEAR(
          getBoundedUtil({{job(1), 0.1}}, isUpperbound),
          evaluate(host0, initial),
          1e-8);
      EXPECT_NEAR(
          getBoundedUtil({{job(0), 0.2}}, isUpperbound),
          evaluate(host1, initial),
          1e-8);
      EXPECT_NEAR(0.0, evaluate(host2, initial), 1e-8);
      EXPECT_NEAR(
          getBoundedUtil({{job(0), 0.8}, {job(1), 2.0}}, isUpperbound),
          evaluate(host3, initial),
          1e-8);

      auto other1 = deltaFromInitial({{"task5", "host2"}});
      // only host2 and host3 should change
      EXPECT_NEAR(
          getBoundedUtil({{job(1), 0.1}}, isUpperbound),
          evaluate(host0, other1),
          1e-8);
      EXPECT_NEAR(
          getBoundedUtil({{job(0), 0.2}}, isUpperbound),
          evaluate(host1, other1),
          1e-8);
      EXPECT_NEAR(
          getBoundedUtil({{job(1), 1.6}}, isUpperbound),
          evaluate(host2, other1),
          1e-8);
      EXPECT_NEAR(
          getBoundedUtil({{job(0), 0.8}, {job(1), 0.4}}, isUpperbound),
          evaluate(host3, other1),
          1e-8);

      auto other2 = deltaFromInitial({{"task3", "host0"}, {"task4", "host1"}});
      EXPECT_NEAR(
          getBoundedUtil({{job(1), 0.5}}, isUpperbound),
          evaluate(host0, other2),
          1e-8);
      EXPECT_NEAR(
          getBoundedUtil({{job(0), 1.0}}, isUpperbound),
          evaluate(host1, other2),
          1e-8);
      EXPECT_NEAR(0.0, evaluate(host2, other2), 1e-8);
      EXPECT_NEAR(
          getBoundedUtil({{job(1), 1.6}}, isUpperbound),
          evaluate(host3, other2),
          1e-8);

      auto other3 = deltaFromInitial({{"task2", "host3"}, {"task4", "host1"}});
      // swap between host1 and host3
      // host0 and host2 remain unchanged
      EXPECT_NEAR(
          getBoundedUtil({{job(1), 0.1}}, isUpperbound),
          evaluate(host0, other3),
          1e-8);
      EXPECT_NEAR(
          getBoundedUtil({{job(0), 0.8}}, isUpperbound),
          evaluate(host1, other3),
          1e-8);
      EXPECT_NEAR(0.0, evaluate(host2, other2), 1e-8);
      EXPECT_NEAR(
          getBoundedUtil({{job(0), 0.2}, {job(1), 2.0}}, isUpperbound),
          evaluate(host3, other3),
          1e-8);
    }
  }
}

CO_TEST_F(ExpressionBuilderTest, DuringAbsolute) {
  auto& builder = expressionBuilder();
  auto host0 = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, cpu(), host(), host(0));
  auto host1 = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, cpu(), host(), host(1));
  auto host2 = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, cpu(), host(), host(2));
  auto host3 = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, cpu(), host(), host(3));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0.1, evaluate(host0, initial), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, initial), 1e-8);
  EXPECT_NEAR(2.8, evaluate(host3, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(0.1, evaluate(host0, other1), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, other1), 1e-8);
  EXPECT_NEAR(0.4, evaluate(host2, other1), 1e-8);
  EXPECT_NEAR(2.8, evaluate(host3, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(0.1, evaluate(host0, other2), 1e-8);
  EXPECT_NEAR(1.8, evaluate(host1, other2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other2), 1e-8);
  EXPECT_NEAR(3.0, evaluate(host3, other2), 1e-8);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  EXPECT_NEAR(0.1, evaluate(host0, other3), 1e-8);
  EXPECT_NEAR(1.8, evaluate(host1, other3), 1e-8);
  EXPECT_NEAR(0.4, evaluate(host2, other3), 1e-8);
  EXPECT_NEAR(3.0, evaluate(host3, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  EXPECT_NEAR(0.9, evaluate(host0, other4), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other4), 1e-8);
  EXPECT_NEAR(2.8, evaluate(host3, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  EXPECT_NEAR(0.9, evaluate(host0, other5), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, other5), 1e-8);
  EXPECT_NEAR(0.1, evaluate(host2, other5), 1e-8);
  EXPECT_NEAR(2.8, evaluate(host3, other5), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, NewAbsolute) {
  auto& builder = expressionBuilder();
  auto host0 =
      co_await builder.getAbsoluteUtil(UtilMetric::NEW, cpu(), host(), host(0));
  auto host1 =
      co_await builder.getAbsoluteUtil(UtilMetric::NEW, cpu(), host(), host(1));
  auto host2 =
      co_await builder.getAbsoluteUtil(UtilMetric::NEW, cpu(), host(), host(2));
  auto host3 =
      co_await builder.getAbsoluteUtil(UtilMetric::NEW, cpu(), host(), host(3));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0.0, evaluate(host0, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host1, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host3, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(0.0, evaluate(host0, other1), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host1, other1), 1e-8);
  EXPECT_NEAR(0.4, evaluate(host2, other1), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host3, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(0.0, evaluate(host0, other2), 1e-8);
  EXPECT_NEAR(1.6, evaluate(host1, other2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other2), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host3, other2), 1e-8);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  EXPECT_NEAR(0.0, evaluate(host0, other3), 1e-8);
  EXPECT_NEAR(1.6, evaluate(host1, other3), 1e-8);
  EXPECT_NEAR(0.4, evaluate(host2, other3), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host3, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  EXPECT_NEAR(0.8, evaluate(host0, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host1, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host3, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  EXPECT_NEAR(0.8, evaluate(host0, other5), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host1, other5), 1e-8);
  EXPECT_NEAR(0.1, evaluate(host2, other5), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host3, other5), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, OldAbsolute) {
  auto& builder = expressionBuilder();
  auto host0 =
      co_await builder.getAbsoluteUtil(UtilMetric::OLD, cpu(), host(), host(0));
  auto host1 =
      co_await builder.getAbsoluteUtil(UtilMetric::OLD, cpu(), host(), host(1));
  auto host2 =
      co_await builder.getAbsoluteUtil(UtilMetric::OLD, cpu(), host(), host(2));
  auto host3 =
      co_await builder.getAbsoluteUtil(UtilMetric::OLD, cpu(), host(), host(3));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0.0, evaluate(host0, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host1, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host3, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(0.0, evaluate(host0, other1), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host1, other1), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other1), 1e-8);
  EXPECT_NEAR(0.4, evaluate(host3, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(0.0, evaluate(host0, other2), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, other2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other2), 1e-8);
  EXPECT_NEAR(1.6, evaluate(host3, other2), 1e-8);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  EXPECT_NEAR(0.0, evaluate(host0, other3), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, other3), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other3), 1e-8);
  EXPECT_NEAR(2.0, evaluate(host3, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  EXPECT_NEAR(0.0, evaluate(host0, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host1, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other4), 1e-8);
  EXPECT_NEAR(0.8, evaluate(host3, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  EXPECT_NEAR(0.1, evaluate(host0, other5), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host1, other5), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other5), 1e-8);
  EXPECT_NEAR(0.8, evaluate(host3, other5), 1e-8);
}

TEST_F(ExpressionBuilderTest, RelativeZeroCapacity) {
  REBALANCER_EXPECT_RUNTIME_ERROR(
      folly::coro::blockingWait(
          expressionBuilder().getRelativeUtil(
              UtilMetric::AFTER, cpu(), host(), host(0))),
      "capacity of host host0 on dimension cpu is zero");
}

CO_TEST_F(ExpressionBuilderTest, Relative) {
  // Note: The relative version leverages the absolute one. There's no
  // practical need to test multiple util metrics as we are testing the
  // common logic for relative here. We've chosen to test it using the util
  // metric "after".
  auto& builder = expressionBuilder();
  auto host1 = co_await builder.getRelativeUtil(
      UtilMetric::AFTER, cpu(), host(), host(1));
  auto host2 = co_await builder.getRelativeUtil(
      UtilMetric::AFTER, cpu(), host(), host(2));
  auto host3 = co_await builder.getRelativeUtil(
      UtilMetric::AFTER, cpu(), host(), host(3));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0.2, evaluate(host1, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, initial), 1e-8);
  EXPECT_NEAR(0.7, evaluate(host3, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(0.2, evaluate(host1, other1), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host2, other1), 1e-8);
  EXPECT_NEAR(0.6, evaluate(host3, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(1.6, evaluate(host1, other2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other2), 1e-8);
  EXPECT_NEAR(0.35, evaluate(host3, other2), 1e-8);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  EXPECT_NEAR(1.6, evaluate(host1, other3), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host2, other3), 1e-8);
  EXPECT_NEAR(0.25, evaluate(host3, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  EXPECT_NEAR(0.2, evaluate(host1, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other4), 1e-8);
  EXPECT_NEAR(0.5, evaluate(host3, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  EXPECT_NEAR(0.2, evaluate(host1, other5), 1e-8);
  EXPECT_NEAR(0.05, evaluate(host2, other5), 1e-8);
  EXPECT_NEAR(0.5, evaluate(host3, other5), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, AfterVectorDimension) {
  auto& builder = expressionBuilder();
  auto host0 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, network(), host(), host(0));
  auto host1 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, network(), host(), host(1));
  auto host2 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, network(), host(), host(2));
  auto host3 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, network(), host(), host(3));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(1, evaluate(host0, initial), 1e-8);
  EXPECT_NEAR(1, evaluate(host1, initial), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, initial), 1e-8);
  EXPECT_NEAR(4, evaluate(host3, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(1, evaluate(host0, other1), 1e-8);
  EXPECT_NEAR(1, evaluate(host1, other1), 1e-8);
  EXPECT_NEAR(4, evaluate(host2, other1), 1e-8);
  EXPECT_NEAR(3, evaluate(host3, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(1, evaluate(host0, other2), 1e-8);
  EXPECT_NEAR(2, evaluate(host1, other2), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, other2), 1e-8);
  EXPECT_NEAR(5, evaluate(host3, other2), 1e-8);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  EXPECT_NEAR(1, evaluate(host0, other3), 1e-8);
  EXPECT_NEAR(2, evaluate(host1, other3), 1e-8);
  EXPECT_NEAR(4, evaluate(host2, other3), 1e-8);
  EXPECT_NEAR(2, evaluate(host3, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  EXPECT_NEAR(1, evaluate(host0, other4), 1e-8);
  EXPECT_NEAR(1, evaluate(host1, other4), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, other4), 1e-8);
  EXPECT_NEAR(4, evaluate(host3, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  EXPECT_NEAR(1, evaluate(host0, other5), 1e-8);
  EXPECT_NEAR(1, evaluate(host1, other5), 1e-8);
  EXPECT_NEAR(1, evaluate(host2, other5), 1e-8);
  EXPECT_NEAR(4, evaluate(host3, other5), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, DuringVectorDimension) {
  auto& builder = expressionBuilder();
  auto host0 = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, network(), host(), host(0));
  auto host1 = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, network(), host(), host(1));
  auto host2 = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, network(), host(), host(2));
  auto host3 = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, network(), host(), host(3));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(1, evaluate(host0, initial), 1e-8);
  EXPECT_NEAR(1, evaluate(host1, initial), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, initial), 1e-8);
  EXPECT_NEAR(4, evaluate(host3, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(1, evaluate(host0, other1), 1e-8);
  EXPECT_NEAR(1, evaluate(host1, other1), 1e-8);
  EXPECT_NEAR(4, evaluate(host2, other1), 1e-8);
  EXPECT_NEAR(4, evaluate(host3, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(1, evaluate(host0, other2), 1e-8);
  EXPECT_NEAR(3, evaluate(host1, other2), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, other2), 1e-8);
  EXPECT_NEAR(5, evaluate(host3, other2), 1e-8);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  EXPECT_NEAR(1, evaluate(host0, other3), 1e-8);
  EXPECT_NEAR(3, evaluate(host1, other3), 1e-8);
  EXPECT_NEAR(4, evaluate(host2, other3), 1e-8);
  EXPECT_NEAR(5, evaluate(host3, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  EXPECT_NEAR(1, evaluate(host0, other4), 1e-8);
  EXPECT_NEAR(1, evaluate(host1, other4), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, other4), 1e-8);
  EXPECT_NEAR(4, evaluate(host3, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  EXPECT_NEAR(1, evaluate(host0, other5), 1e-8);
  EXPECT_NEAR(1, evaluate(host1, other5), 1e-8);
  EXPECT_NEAR(1, evaluate(host2, other5), 1e-8);
  EXPECT_NEAR(4, evaluate(host3, other5), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, NewVectorDimension) {
  auto& builder = expressionBuilder();
  auto host0 = co_await builder.getAbsoluteUtil(
      UtilMetric::NEW, network(), host(), host(0));
  auto host1 = co_await builder.getAbsoluteUtil(
      UtilMetric::NEW, network(), host(), host(1));
  auto host2 = co_await builder.getAbsoluteUtil(
      UtilMetric::NEW, network(), host(), host(2));
  auto host3 = co_await builder.getAbsoluteUtil(
      UtilMetric::NEW, network(), host(), host(3));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0, evaluate(host0, initial), 1e-8);
  EXPECT_NEAR(0, evaluate(host1, initial), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, initial), 1e-8);
  EXPECT_NEAR(0, evaluate(host3, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(0, evaluate(host0, other1), 1e-8);
  EXPECT_NEAR(0, evaluate(host1, other1), 1e-8);
  EXPECT_NEAR(4, evaluate(host2, other1), 1e-8);
  EXPECT_NEAR(0, evaluate(host3, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(0, evaluate(host0, other2), 1e-8);
  EXPECT_NEAR(2, evaluate(host1, other2), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, other2), 1e-8);
  EXPECT_NEAR(1, evaluate(host3, other2), 1e-8);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  EXPECT_NEAR(0, evaluate(host0, other3), 1e-8);
  EXPECT_NEAR(2, evaluate(host1, other3), 1e-8);
  EXPECT_NEAR(4, evaluate(host2, other3), 1e-8);
  EXPECT_NEAR(1, evaluate(host3, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  EXPECT_NEAR(1, evaluate(host0, other4), 1e-8);
  EXPECT_NEAR(0, evaluate(host1, other4), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, other4), 1e-8);
  EXPECT_NEAR(0, evaluate(host3, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  EXPECT_NEAR(1, evaluate(host0, other5), 1e-8);
  EXPECT_NEAR(0, evaluate(host1, other5), 1e-8);
  EXPECT_NEAR(1, evaluate(host2, other5), 1e-8);
  EXPECT_NEAR(0, evaluate(host3, other5), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, OldVectorDimension) {
  auto& builder = expressionBuilder();
  auto host0 = co_await builder.getAbsoluteUtil(
      UtilMetric::OLD, network(), host(), host(0));
  auto host1 = co_await builder.getAbsoluteUtil(
      UtilMetric::OLD, network(), host(), host(1));
  auto host2 = co_await builder.getAbsoluteUtil(
      UtilMetric::OLD, network(), host(), host(2));
  auto host3 = co_await builder.getAbsoluteUtil(
      UtilMetric::OLD, network(), host(), host(3));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0, evaluate(host0, initial), 1e-8);
  EXPECT_NEAR(0, evaluate(host1, initial), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, initial), 1e-8);
  EXPECT_NEAR(0, evaluate(host3, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(0, evaluate(host0, other1), 1e-8);
  EXPECT_NEAR(0, evaluate(host1, other1), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, other1), 1e-8);
  EXPECT_NEAR(4, evaluate(host3, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(0, evaluate(host0, other2), 1e-8);
  EXPECT_NEAR(1, evaluate(host1, other2), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, other2), 1e-8);
  EXPECT_NEAR(2, evaluate(host3, other2), 1e-8);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  EXPECT_NEAR(0, evaluate(host0, other3), 1e-8);
  EXPECT_NEAR(1, evaluate(host1, other3), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, other3), 1e-8);
  EXPECT_NEAR(4, evaluate(host3, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  EXPECT_NEAR(0, evaluate(host0, other4), 1e-8);
  EXPECT_NEAR(0, evaluate(host1, other4), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, other4), 1e-8);
  EXPECT_NEAR(1, evaluate(host3, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  EXPECT_NEAR(1, evaluate(host0, other5), 1e-8);
  EXPECT_NEAR(0, evaluate(host1, other5), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, other5), 1e-8);
  EXPECT_NEAR(1, evaluate(host3, other5), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, AfterAbsoluteWithGroup) {
  auto& builder = expressionBuilder();
  auto rack0job0 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), rack(), rack(0), job(), job(0));
  auto rack0job1 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), rack(), rack(0), job(), job(1));
  auto rack1job0 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), rack(), rack(1), job(), job(0));
  auto rack1job1 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), rack(), rack(1), job(), job(1));
  // Vector overload
  auto allRacksJob1 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), rack(), {rack(0), rack(1)}, job(), job(1));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0.2, evaluate(rack0job0, initial), 1e-8);
  EXPECT_NEAR(0.1, evaluate(rack0job1, initial), 1e-8);
  EXPECT_NEAR(0.8, evaluate(rack1job0, initial), 1e-8);
  EXPECT_NEAR(2.0, evaluate(rack1job1, initial), 1e-8);
  EXPECT_NEAR(2.1, evaluate(allRacksJob1, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(0.2, evaluate(rack0job0, other1), 1e-8);
  EXPECT_NEAR(0.1, evaluate(rack0job1, other1), 1e-8);
  EXPECT_NEAR(0.8, evaluate(rack1job0, other1), 1e-8);
  EXPECT_NEAR(2.0, evaluate(rack1job1, other1), 1e-8);
  EXPECT_NEAR(2.1, evaluate(allRacksJob1, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(0.0, evaluate(rack0job0, other2), 1e-8);
  EXPECT_NEAR(1.7, evaluate(rack0job1, other2), 1e-8);
  EXPECT_NEAR(1.0, evaluate(rack1job0, other2), 1e-8);
  EXPECT_NEAR(0.4, evaluate(rack1job1, other2), 1e-8);
  EXPECT_NEAR(2.1, evaluate(allRacksJob1, other2), 1e-8);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  EXPECT_NEAR(0.0, evaluate(rack0job0, other3), 1e-8);
  EXPECT_NEAR(1.7, evaluate(rack0job1, other3), 1e-8);
  EXPECT_NEAR(1.0, evaluate(rack1job0, other3), 1e-8);
  EXPECT_NEAR(0.4, evaluate(rack1job1, other3), 1e-8);
  EXPECT_NEAR(2.1, evaluate(allRacksJob1, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  EXPECT_NEAR(1.0, evaluate(rack0job0, other4), 1e-8);
  EXPECT_NEAR(0.1, evaluate(rack0job1, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job0, other4), 1e-8);
  EXPECT_NEAR(2.0, evaluate(rack1job1, other4), 1e-8);
  EXPECT_NEAR(2.1, evaluate(allRacksJob1, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  EXPECT_NEAR(1.0, evaluate(rack0job0, other5), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack0job1, other5), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job0, other5), 1e-8);
  EXPECT_NEAR(2.1, evaluate(rack1job1, other5), 1e-8);
  EXPECT_NEAR(2.1, evaluate(allRacksJob1, other5), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, DuringAbsoluteWithGroup) {
  auto& builder = expressionBuilder();
  auto rack0job0 = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, cpu(), rack(), rack(0), job(), job(0));
  auto rack0job1 = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, cpu(), rack(), rack(0), job(), job(1));
  auto rack1job0 = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, cpu(), rack(), rack(1), job(), job(0));
  auto rack1job1 = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, cpu(), rack(), rack(1), job(), job(1));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0.2, evaluate(rack0job0, initial), 1e-8);
  EXPECT_NEAR(0.1, evaluate(rack0job1, initial), 1e-8);
  EXPECT_NEAR(0.8, evaluate(rack1job0, initial), 1e-8);
  EXPECT_NEAR(2.0, evaluate(rack1job1, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(0.2, evaluate(rack0job0, other1), 1e-8);
  EXPECT_NEAR(0.1, evaluate(rack0job1, other1), 1e-8);
  EXPECT_NEAR(0.8, evaluate(rack1job0, other1), 1e-8);
  EXPECT_NEAR(2.0, evaluate(rack1job1, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(0.2, evaluate(rack0job0, other2), 1e-8);
  EXPECT_NEAR(1.7, evaluate(rack0job1, other2), 1e-8);
  EXPECT_NEAR(1.0, evaluate(rack1job0, other2), 1e-8);
  EXPECT_NEAR(2.0, evaluate(rack1job1, other2), 1e-8);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  EXPECT_NEAR(0.2, evaluate(rack0job0, other3), 1e-8);
  EXPECT_NEAR(1.7, evaluate(rack0job1, other3), 1e-8);
  EXPECT_NEAR(1.0, evaluate(rack1job0, other3), 1e-8);
  EXPECT_NEAR(2.0, evaluate(rack1job1, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  EXPECT_NEAR(1.0, evaluate(rack0job0, other4), 1e-8);
  EXPECT_NEAR(0.1, evaluate(rack0job1, other4), 1e-8);
  EXPECT_NEAR(0.8, evaluate(rack1job0, other4), 1e-8);
  EXPECT_NEAR(2.0, evaluate(rack1job1, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  EXPECT_NEAR(1.0, evaluate(rack0job0, other5), 1e-8);
  EXPECT_NEAR(0.1, evaluate(rack0job1, other5), 1e-8);
  EXPECT_NEAR(0.8, evaluate(rack1job0, other5), 1e-8);
  EXPECT_NEAR(2.1, evaluate(rack1job1, other5), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, NewAbsoluteWithGroup) {
  auto& builder = expressionBuilder();
  auto rack0job0 = co_await builder.getAbsoluteUtil(
      UtilMetric::NEW, cpu(), rack(), rack(0), job(), job(0));
  auto rack0job1 = co_await builder.getAbsoluteUtil(
      UtilMetric::NEW, cpu(), rack(), rack(0), job(), job(1));
  auto rack1job0 = co_await builder.getAbsoluteUtil(
      UtilMetric::NEW, cpu(), rack(), rack(1), job(), job(0));
  auto rack1job1 = co_await builder.getAbsoluteUtil(
      UtilMetric::NEW, cpu(), rack(), rack(1), job(), job(1));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0.0, evaluate(rack0job0, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack0job1, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job0, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job1, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(0.0, evaluate(rack0job0, other1), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack0job1, other1), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job0, other1), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job1, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(0.0, evaluate(rack0job0, other2), 1e-8);
  EXPECT_NEAR(1.6, evaluate(rack0job1, other2), 1e-8);
  EXPECT_NEAR(0.2, evaluate(rack1job0, other2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job1, other2), 1e-8);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  EXPECT_NEAR(0.0, evaluate(rack0job0, other3), 1e-8);
  EXPECT_NEAR(1.6, evaluate(rack0job1, other3), 1e-8);
  EXPECT_NEAR(0.2, evaluate(rack1job0, other3), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job1, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  EXPECT_NEAR(0.8, evaluate(rack0job0, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack0job1, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job0, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job1, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  EXPECT_NEAR(0.8, evaluate(rack0job0, other5), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack0job1, other5), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job0, other5), 1e-8);
  EXPECT_NEAR(0.1, evaluate(rack1job1, other5), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, OldAbsoluteWithGroup) {
  auto& builder = expressionBuilder();
  auto rack0job0 = co_await builder.getAbsoluteUtil(
      UtilMetric::OLD, cpu(), rack(), rack(0), job(), job(0));
  auto rack0job1 = co_await builder.getAbsoluteUtil(
      UtilMetric::OLD, cpu(), rack(), rack(0), job(), job(1));
  auto rack1job0 = co_await builder.getAbsoluteUtil(
      UtilMetric::OLD, cpu(), rack(), rack(1), job(), job(0));
  auto rack1job1 = co_await builder.getAbsoluteUtil(
      UtilMetric::OLD, cpu(), rack(), rack(1), job(), job(1));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0.0, evaluate(rack0job0, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack0job1, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job0, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job1, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(0.0, evaluate(rack0job0, other1), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack0job1, other1), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job0, other1), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job1, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(0.2, evaluate(rack0job0, other2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack0job1, other2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job0, other2), 1e-8);
  EXPECT_NEAR(1.6, evaluate(rack1job1, other2), 1e-8);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  EXPECT_NEAR(0.2, evaluate(rack0job0, other3), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack0job1, other3), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job0, other3), 1e-8);
  EXPECT_NEAR(1.6, evaluate(rack1job1, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  EXPECT_NEAR(0.0, evaluate(rack0job0, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack0job1, other4), 1e-8);
  EXPECT_NEAR(0.8, evaluate(rack1job0, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job1, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  EXPECT_NEAR(0.0, evaluate(rack0job0, other5), 1e-8);
  EXPECT_NEAR(0.1, evaluate(rack0job1, other5), 1e-8);
  EXPECT_NEAR(0.8, evaluate(rack1job0, other5), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job1, other5), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, NestedImage) {
  co_await addScope(
      "type",
      {{"T1_BGM", {"host0", "host1"}},
       {"T1_SKL", {"host2"}},
       {"T1_CPL", {"host3"}}});

  co_await addScope("decomHosts", {{"T1_SKL", {"host0"}}});

  const auto serverType = scopeId("type");
  const auto t1BGM = scopeItemId("type", "T1_BGM");
  const auto t1SKL = scopeItemId("type", "T1_SKL");
  const auto t1CPL = scopeItemId("type", "T1_CPL");
  const auto decomHostsScope = scopeId("decomHosts");

  auto& builder = expressionBuilder();

  // scope 'host' is the lowest level, it is always nested in any outer scope
  const std::vector<entities::ScopeItemId> rack0Hosts = {host(0), host(1)};
  const std::vector<entities::ScopeItemId> rack1Hosts = {host(2), host(3)};
  EXPECT_EQ(rack0Hosts, builder.getNestedImage(rack(), host(), rack(0)));
  EXPECT_EQ(rack1Hosts, builder.getNestedImage(rack(), host(), rack(1)));
  const std::vector<entities::ScopeItemId> bgmHosts = {host(0), host(1)};
  const std::vector<entities::ScopeItemId> sklHosts = {host(2)};
  const std::vector<entities::ScopeItemId> cplHosts = {host(3)};
  EXPECT_EQ(bgmHosts, builder.getNestedImage(serverType, host(), t1BGM));
  EXPECT_EQ(sklHosts, builder.getNestedImage(serverType, host(), t1SKL));
  EXPECT_EQ(cplHosts, builder.getNestedImage(serverType, host(), t1CPL));

  // serverType is also nested under the rack scope
  //  |---rack0---|---rack1---|
  //  |----BGM----|-SKL-|-CPL-|
  // t1BGM is fully nested in rack0
  EXPECT_EQ(
      std::vector<entities::ScopeItemId>({t1BGM}),
      builder.getNestedImage(rack(), serverType, rack(0)));
  // t1SKL and t1CPL together are fully nested in rack1
  EXPECT_EQ(
      std::vector<entities::ScopeItemId>({t1SKL, t1CPL}),
      builder.getNestedImage(rack(), serverType, rack(1)));

  // rack0 is fully nested in t1BGM
  EXPECT_EQ(
      std::vector<entities::ScopeItemId>({rack(0)}),
      builder.getNestedImage(serverType, rack(), t1BGM));

  // rack1 is not nested in t1SKL
  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.getNestedImage(serverType, rack(), t1SKL),
      "Expect scopeItem rack1 to be fully contained in T1_SKL");
  // rack1 is not nested in t1CPL
  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.getNestedImage(serverType, rack(), t1CPL),
      "Expect scopeItem rack1 to be fully contained in T1_CPL");

  // decomHosts is an incomplete scope
  //  |---rack0---|---rack1---|
  //  |host0|--?--|---?????---|
  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.getNestedImage(rack(), decomHostsScope, rack(0)),
      "rack0 must be fully covered by scopeitems of scope decomHosts");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.getNestedImage(rack(), decomHostsScope, rack(1)),
      "expect that rack1's image in decomHosts scope is non-empty");
}

CO_TEST_F(ExpressionBuilderTest, AggregatedAbsoluteWithGroup) {
  auto& builder = expressionBuilder();
  auto rack0job0 = co_await builder.getAbsoluteUtil(
      UtilMetric::NEW, cpu(), rack(), rack(0), job(), job(0));
  auto rack0job1 = co_await builder.getAbsoluteUtil(
      UtilMetric::NEW, cpu(), rack(), rack(0), job(), job(1));
  auto rack1job0 = co_await builder.getAbsoluteUtil(
      UtilMetric::NEW, cpu(), rack(), rack(1), job(), job(0));
  auto rack1job1 = co_await builder.getAbsoluteUtil(
      UtilMetric::NEW, cpu(), rack(), rack(1), job(), job(1));
  // compute util aggregated at host level
  auto rack0job0Agg = co_await builder.getAggregatedAbsoluteUtil(
      UtilMetric::NEW, cpu(), rack(), host(), rack(0), job(), job(0));
  auto rack0job1Agg = co_await builder.getAggregatedAbsoluteUtil(
      UtilMetric::NEW, cpu(), rack(), host(), rack(0), job(), job(1));
  auto rack1job0Agg = co_await builder.getAggregatedAbsoluteUtil(
      UtilMetric::NEW, cpu(), rack(), host(), rack(1), job(), job(0));
  auto rack1job1Agg = co_await builder.getAggregatedAbsoluteUtil(
      UtilMetric::NEW, cpu(), rack(), host(), rack(1), job(), job(1));

  // scenario 1: All moves are between different racks
  // => aggregated and non-aggregated util are the same
  // task0: host0 --> host2 (rack1), task3: host3 --> host1 (rack0)
  auto scenario1 = deltaFromInitial({{"task0", "host2"}, {"task3", "host1"}});
  EXPECT_NEAR(
      evaluate(rack0job0Agg, scenario1), evaluate(rack0job0, scenario1), 1e-8);
  EXPECT_NEAR(
      evaluate(rack0job1Agg, scenario1), evaluate(rack0job1, scenario1), 1e-8);
  EXPECT_NEAR(
      evaluate(rack1job0Agg, scenario1), evaluate(rack1job0, scenario1), 1e-8);
  EXPECT_NEAR(
      evaluate(rack1job1Agg, scenario1), evaluate(rack1job1, scenario1), 1e-8);

  // scenario 2:  All moves are within same rack
  // task0: host0 --> host1 (rack0), task3: host3 --> host2 (rack1)
  auto scenario2 = deltaFromInitial({{"task0", "host1"}, {"task3", "host2"}});
  EXPECT_NEAR(0.0, evaluate(rack0job0, scenario2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack0job1, scenario2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job0, scenario2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job1, scenario2), 1e-8);
  // cpu values for task0 is 0 and task3 is 0.4
  EXPECT_NEAR(0.0, evaluate(rack0job0Agg, scenario2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack0job1Agg, scenario2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job0Agg, scenario2), 1e-8);
  EXPECT_NEAR(0.4, evaluate(rack1job1Agg, scenario2), 1e-8);

  // scenario 3:  All moves are within same rack
  // task2: host1 --> host0 (rack0), task4: host3 --> host2 (rack1)
  auto scenario3 = deltaFromInitial({{"task2", "host0"}, {"task4", "host2"}});
  EXPECT_NEAR(0.0, evaluate(rack0job0, scenario3), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack0job1, scenario3), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job0, scenario3), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job1, scenario3), 1e-8);
  // cpu values for task2 (job0) is 0.2 and task4 (job0) is 0.8
  EXPECT_NEAR(0.2, evaluate(rack0job0Agg, scenario3), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack0job1Agg, scenario3), 1e-8);
  EXPECT_NEAR(0.8, evaluate(rack1job0Agg, scenario3), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job1Agg, scenario3), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, RelativeWithGroup) {
  // Note: The relative version leverages the absolute one. There's no
  // practical need to test multiple util metrics as we are testing the
  // common logic for relative here. We've chosen to test it using the util
  // metric "after".
  auto& builder = expressionBuilder();
  auto rack0job0 = co_await builder.getRelativeUtil(
      UtilMetric::AFTER, cpu(), rack(), rack(0), job(), job(0));
  auto rack0job1 = co_await builder.getRelativeUtil(
      UtilMetric::AFTER, cpu(), rack(), rack(0), job(), job(1));
  auto rack1job0 = co_await builder.getRelativeUtil(
      UtilMetric::AFTER, cpu(), rack(), rack(1), job(), job(0));
  auto rack1job1 = co_await builder.getRelativeUtil(
      UtilMetric::AFTER, cpu(), rack(), rack(1), job(), job(1));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0.2, evaluate(rack0job0, initial), 1e-8);
  EXPECT_NEAR(0.1, evaluate(rack0job1, initial), 1e-8);
  EXPECT_NEAR(8.0, evaluate(rack1job0, initial), 1e-8);
  EXPECT_NEAR(20.0, evaluate(rack1job1, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(0.2, evaluate(rack0job0, other1), 1e-8);
  EXPECT_NEAR(0.1, evaluate(rack0job1, other1), 1e-8);
  EXPECT_NEAR(8.0, evaluate(rack1job0, other1), 1e-8);
  EXPECT_NEAR(20.0, evaluate(rack1job1, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(0.0, evaluate(rack0job0, other2), 1e-8);
  EXPECT_NEAR(1.7, evaluate(rack0job1, other2), 1e-8);
  EXPECT_NEAR(10.0, evaluate(rack1job0, other2), 1e-8);
  EXPECT_NEAR(4.0, evaluate(rack1job1, other2), 1e-8);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  EXPECT_NEAR(0.0, evaluate(rack0job0, other3), 1e-8);
  EXPECT_NEAR(1.7, evaluate(rack0job1, other3), 1e-8);
  EXPECT_NEAR(10.0, evaluate(rack1job0, other3), 1e-8);
  EXPECT_NEAR(4.0, evaluate(rack1job1, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  EXPECT_NEAR(1.0, evaluate(rack0job0, other4), 1e-8);
  EXPECT_NEAR(0.1, evaluate(rack0job1, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job0, other4), 1e-8);
  EXPECT_NEAR(20.0, evaluate(rack1job1, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  EXPECT_NEAR(1.0, evaluate(rack0job0, other5), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack0job1, other5), 1e-8);
  EXPECT_NEAR(0.0, evaluate(rack1job0, other5), 1e-8);
  EXPECT_NEAR(21.0, evaluate(rack1job1, other5), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, CacheAbsoluteUtil) {
  auto& builder = expressionBuilder();
  auto expr0 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), host(), host(0));
  auto expr1 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), host(), host(0));

  EXPECT_EQ(expr0, expr1);
}

CO_TEST_F(ExpressionBuilderTest, CacheRelativeUtil) {
  auto& builder = expressionBuilder();
  auto expr0 = co_await builder.getRelativeUtil(
      UtilMetric::AFTER, cpu(), host(), host(1));
  auto expr1 = co_await builder.getRelativeUtil(
      UtilMetric::AFTER, cpu(), host(), host(1));

  EXPECT_EQ(expr0, expr1);
}

CO_TEST_F(ExpressionBuilderTest, CacheAbsoluteUtilAfter) {
  auto& builder = expressionBuilder();
  auto after = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), host(), host(0));
  auto during = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, cpu(), host(), host(0));

  EXPECT_NE(after, during);

  // after is an ObjectLookup
  EXPECT_EQ("ObjectLookup", after->getType());

  // during is BoundsOverride over LinearSum
  EXPECT_EQ("BoundsOverride", during->getType());
  // first child of during (BoundsOverride) is a LinearSum
  EXPECT_EQ(1, during->children().size());
  EXPECT_EQ("LinearSum", during->getOnlyChild()->getType());
  // During = after + initial - stayed, should have at least 2 children
  EXPECT_GE(during->getOnlyChild()->children().size(), 2);
  // During = after + initial - stayed, so after appears as one of the children
  EXPECT_TRUE(during->getOnlyChild()->children().contains(after));
}

CO_TEST_F(ExpressionBuilderTest, CacheObjectVector) {
  auto& builder = expressionBuilder();
  auto host0 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), host(), host(0));
  auto host1 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), host(), host(1));

  EXPECT_NE(host0, host1);

  EXPECT_EQ("ObjectLookup", host0->getType());
  EXPECT_EQ("ObjectLookup", host1->getType());

  auto vector0 = *host0->children().begin();
  auto vector1 = *host1->children().begin();

  EXPECT_EQ("ObjectVector", vector0->getType());
  EXPECT_EQ("ObjectVector", vector1->getType());

  EXPECT_EQ(vector0, vector1);
}

CO_TEST_F(ExpressionBuilderTest, CacheObjectVectorWithGroup) {
  auto& builder = expressionBuilder();
  auto rack0 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), rack(), rack(0), job(), job(0));
  auto rack1 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), rack(), rack(1), job(), job(0));

  EXPECT_NE(rack0, rack1);

  EXPECT_EQ("ObjectLookup", rack0->getType());
  EXPECT_EQ("ObjectLookup", rack1->getType());

  auto vector0 = *rack0->children().begin();
  auto vector1 = *rack1->children().begin();

  EXPECT_EQ("ObjectVector", vector0->getType());
  EXPECT_EQ("ObjectVector", vector1->getType());

  EXPECT_EQ(vector0, vector1);
}

CO_TEST_F(ExpressionBuilderTest, StableOptimizationDisabled) {
  universeBuilder_.setStableOptimization(false);
  auto stayed = co_await getStayedForStableOptimizationTest();
  EXPECT_EQ("ObjectLookup", stayed->getType());
}

CO_TEST_F(ExpressionBuilderTest, StableOptimizationEnabled) {
  universeBuilder_.setStableOptimization(true);
  auto stayed = co_await getStayedForStableOptimizationTest();
  EXPECT_EQ("StableStayed", stayed->getType());
}

CO_TEST_F(ExpressionBuilderTest, CustomAbsolute) {
  const entities::ObjectStaticDimension objectDimension(
      entities::tests::makeObjectIdToDoubleMap(
          entities::Map<entities::ObjectId, double>{
              {task(0), 10.0},
              {task(1), 20.0},
              {task(2), 40.0},
              {task(3), 80.0},
              {task(4), 160.0}},
          /*defaultValue=*/320.0,
          getNumObjects()));

  auto& builder = expressionBuilder();
  auto rack0 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, objectDimension, rack(), rack(0));
  auto rack1 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, objectDimension, rack(), rack(1));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(70.0, evaluate(rack0, initial), 1e-8);
  EXPECT_NEAR(560.0, evaluate(rack1, initial), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, CustomAbsoluteUnsupportedMetric) {
  const entities::ObjectStaticDimension objectDimension(
      entities::tests::makeObjectIdToDoubleMap(
          entities::Map<entities::ObjectId, double>{
              {task(0), 10.0},
              {task(1), 20.0},
              {task(2), 40.0},
              {task(3), 80.0},
              {task(4), 160.0}},
          /*defaultValue=*/320.0,
          getNumObjects()));
  auto expr = co_await expressionBuilder().getAbsoluteUtil(
      UtilMetric::OLD, objectDimension, rack(), rack(0));
  EXPECT_NEAR(
      10.0, evaluate(expr, deltaFromInitial({{"task0", "host3"}})), 1e-8);
}

TEST_F(ExpressionBuilderTest, CustomAbsoluteOutOfScope) {
  const entities::ObjectStaticDimension objectDimension(
      entities::tests::makeObjectIdToDoubleMap(
          entities::Map<entities::ObjectId, double>{
              {task(0), 1.0},
              {task(1), 2.0},
              {task(2), 4.0},
              {task(3), 8.0},
              {task(4), 16.0}},
          /*defaultValue=*/32.0,
          getNumObjects()));

  auto outOfPod = expressionBuilder().getAbsoluteUtilOutOfScope(
      UtilMetric::AFTER, objectDimension, pod());

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(56.0, evaluate(outOfPod, initial), 1e-8);
}

TEST_F(ExpressionBuilderTest, CustomAbsoluteOutOfScopeUnsupportedMetric) {
  entities::ObjectStaticDimension objectDimension(
      entities::ObjectIdToDoubleMap(
          /*totalSize=*/0,
          /*defaultValue=*/0.0,
          /*expectedNonDefaultSize=*/0));
  REBALANCER_EXPECT_RUNTIME_ERROR(
      expressionBuilder().getAbsoluteUtilOutOfScope(
          UtilMetric::DURING, objectDimension, rack()),
      "util metric not supported");
}

CO_TEST_F(ExpressionBuilderTest, AbsoluteOutOfScopeAfter) {
  auto outOfPodScope = co_await expressionBuilder().getAbsoluteUtilOutOfScope(
      UtilMetric::AFTER, cpu(), scopeId("pod"));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(2.8, evaluate(outOfPodScope, initial), 1e-8);
  // task2 moved to host3 hence the value will increse
  EXPECT_NEAR(
      3, evaluate(outOfPodScope, deltaFromInitial({{"task2", "host3"}})), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, AbsoluteOutOfScopeDuring) {
  auto outOfPodScope = co_await expressionBuilder().getAbsoluteUtilOutOfScope(
      UtilMetric::DURING, cpu(), scopeId("pod"));

  // during = after + initial - stayed = 2.2 + 2.8 - 2.0 = 3
  EXPECT_NEAR(
      3,
      evaluate(
          outOfPodScope,
          deltaFromInitial({{"task2", "host3"}, {"task4", "host0"}})),
      1e-8);
}

CO_TEST_F(ExpressionBuilderTest, AbsoluteOutOfScopeNew) {
  auto outOfPodScope = co_await expressionBuilder().getAbsoluteUtilOutOfScope(
      UtilMetric::NEW, cpu(), scopeId("pod"));

  // task2 moved outOfScope hence value will be 0.2 (task 2 dim value)
  EXPECT_NEAR(
      0.2,
      evaluate(outOfPodScope, deltaFromInitial({{"task2", "host3"}})),
      1e-8);
}

CO_TEST_F(ExpressionBuilderTest, AbsoluteOutOfScopeOld) {
  auto outOfPodScope = co_await expressionBuilder().getAbsoluteUtilOutOfScope(
      UtilMetric::OLD, cpu(), scopeId("pod"));

  // task3 moved outOfScope to inScope, hence value will be 0.4 (task 3 dim
  // value)
  EXPECT_NEAR(
      0.4,
      evaluate(outOfPodScope, deltaFromInitial({{"task3", "host0"}})),
      1e-8);
}

CO_TEST_F(ExpressionBuilderTest, InitialValue) {
  auto& builder = expressionBuilder();
  auto host0 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, cpu(), host(), host(0));
  EXPECT_NEAR(0.1, host0->getInitialValue(), 1e-8);
}

TEST_F(ExpressionBuilderTest, ObjectPartitionLookup) {
  const entities::Map<entities::GroupId, double> limits = {
      {job(1), 0.01}, {job(0), 0.02}};

  auto& builder = expressionBuilder();
  auto objectPartition =
      builder.getObjectPartition(limits, cpu(), job(), false);

  auto partitionGroupCount = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition,
      {},
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      false);

  // Scope item has more than 1 group
  // = max(0, 0 - 0.01) + max(0, 0.1 - 0.01) + max(0, 0.2 - 0.02) = 0.27
  EXPECT_NEAR(0.27, evaluate(partitionGroupCount, deltaFromInitial({})), 1e-8);

  // 0.2 - 0.02 = 0.18
  EXPECT_NEAR(
      0.18,
      evaluate(partitionGroupCount, deltaFromInitial({{"task1", "host2"}})),
      1e-8);

  // Test with allowedGroups
  // Violation penalty for top-k groups is waived
  packer::tests::LpAssertOptions lpAssertOptions = {
      .exceptionOnlyForLpExprMax =
          "ObjectPartitionLookup: groupsAllowed_ > 0 when minimizing=false is not supported in LP"};
  constexpr int k = 1;
  auto partitionGroupCountAllowed = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition,
      {},
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      k,
      false);
  // Penalty for
  // = max(0, 0 - 0.01) + max(0, 0.1 - 0.01) + max(0, 0.2 - 0.02)
  //   - max(0, 0.2 - 0.02) = 0.09
  EXPECT_NEAR(
      0.09,
      evaluate(
          partitionGroupCountAllowed, deltaFromInitial({}), lpAssertOptions),
      1e-8);
}

TEST_F(ExpressionBuilderTest, ObjectPartitionLookupMinBound) {
  const entities::Map<entities::GroupId, double> limits = {
      {job(1), 0.15}, {job(0), 0.3}};

  auto& builder = expressionBuilder();
  auto objectPartition =
      builder.getObjectPartition(limits, cpu(), job(), false);

  auto partitionGroupCount = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition,
      {},
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      true);

  // Scope item has more than 1 group
  // = max(0, 0.15 - 0.1) + max(0, 0.3 - 0.2) = 0.15
  EXPECT_NEAR(0.15, evaluate(partitionGroupCount, deltaFromInitial({})), 1e-8);

  // = max(0, 0.15 - 0.0) + max(0, 0.3 - 0.2) = 0.25
  EXPECT_NEAR(
      0.25,
      evaluate(partitionGroupCount, deltaFromInitial({{"task1", "host2"}})),
      1e-8);

  // Test with allowedGroups
  // Violation penalty for top-k groups is waived
  packer::tests::LpAssertOptions lpAssertOptions = {
      .exceptionOnlyForLpExprMax =
          "ObjectPartitionLookup: groupsAllowed_ > 0 when minimizing=false is not supported in LP"};
  constexpr int k = 1;
  auto partitionGroupCountAllowed = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition,
      {},
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      k,
      true);
  EXPECT_NEAR(
      0.05,
      evaluate(
          partitionGroupCountAllowed, deltaFromInitial({}), lpAssertOptions),
      1e-8);
}

TEST_F(ExpressionBuilderTest, ObjectPartitionMoveLimit) {
  // no even tasks (job0) can move, at most 2 odd tasks (job1) can move
  const entities::Map<entities::GroupId, double> limits = {
      {job(1), 2}, {job(0), 0}};

  auto& builder = expressionBuilder();
  auto moveLimitExpr = builder.getObjectPartitionMoveLimit(
      limits,
      job(),
      dimensionId("task_count"),
      {} /*sourceContainersIdsNotAffectingLimit*/,
      {} /*destinationContainersIdsNotAffectingLimit*/);
  // Initially the expr must evaluate to zero
  EXPECT_NEAR(0, evaluate(moveLimitExpr, deltaFromInitial({})), 1e-8);
  // both moves are violating
  EXPECT_NEAR(
      2,
      evaluate(
          moveLimitExpr,
          deltaFromInitial({{"task0", "host2"}, {"task2", "host2"}})),
      1e-8);
  // one move is violating
  EXPECT_NEAR(
      1,
      evaluate(
          moveLimitExpr,
          deltaFromInitial({{"task0", "host2"}, {"task1", "host2"}})),
      1e-8);
  // neither move is violating
  EXPECT_NEAR(
      0,
      evaluate(
          moveLimitExpr,
          deltaFromInitial({{"task1", "host2"}, {"task3", "host2"}})),
      1e-8);
}

CO_TEST_F(ExpressionBuilderTest, AfterAbsoluteDynamic) {
  auto& builder = expressionBuilder();
  auto pod0 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, power(), pod(), pod(0));
  auto pod1 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, power(), pod(), pod(1));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(31, evaluate(pod0, initial), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task0", "host1"}});
  EXPECT_NEAR(61, evaluate(pod0, other1), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task1", "host1"}});
  EXPECT_NEAR(91, evaluate(pod0, other2), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, other2), 1e-8);

  auto other3 = deltaFromInitial({{"task0", "host2"}});
  EXPECT_NEAR(21, evaluate(pod0, other3), 1e-8);
  EXPECT_NEAR(10, evaluate(pod1, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task1", "host2"}});
  EXPECT_NEAR(11, evaluate(pod0, other4), 1e-8);
  EXPECT_NEAR(20, evaluate(pod1, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task0", "host1"}, {"task1", "host3"}});
  EXPECT_NEAR(41, evaluate(pod0, other5), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, other5), 1e-8);

  auto other6 = deltaFromInitial({{"task2", "host2"}});
  EXPECT_NEAR(30, evaluate(pod0, other6), 1e-8);
  EXPECT_NEAR(1, evaluate(pod1, other6), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, DuringAbsoluteDynamic) {
  auto& builder = expressionBuilder();
  auto pod0 = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, power(), pod(), pod(0));
  auto pod1 = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, power(), pod(), pod(1));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(31, evaluate(pod0, initial), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task0", "host1"}});
  EXPECT_NEAR(61, evaluate(pod0, other1), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task1", "host1"}});
  EXPECT_NEAR(91, evaluate(pod0, other2), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, other2), 1e-8);

  auto other3 = deltaFromInitial({{"task0", "host2"}});
  EXPECT_NEAR(31, evaluate(pod0, other3), 1e-8);
  EXPECT_NEAR(10, evaluate(pod1, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task1", "host2"}});
  EXPECT_NEAR(31, evaluate(pod0, other4), 1e-8);
  EXPECT_NEAR(20, evaluate(pod1, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task0", "host1"}, {"task1", "host3"}});
  EXPECT_NEAR(61, evaluate(pod0, other5), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, other5), 1e-8);

  auto other6 = deltaFromInitial({{"task2", "host2"}});
  EXPECT_NEAR(31, evaluate(pod0, other6), 1e-8);
  EXPECT_NEAR(1, evaluate(pod1, other6), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, NewAbsoluteDynamic) {
  auto& builder = expressionBuilder();
  auto pod0 =
      co_await builder.getAbsoluteUtil(UtilMetric::NEW, power(), pod(), pod(0));
  auto pod1 =
      co_await builder.getAbsoluteUtil(UtilMetric::NEW, power(), pod(), pod(1));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0, evaluate(pod0, initial), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task0", "host1"}});
  EXPECT_NEAR(30, evaluate(pod0, other1), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task1", "host1"}});
  EXPECT_NEAR(60, evaluate(pod0, other2), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, other2), 1e-8);

  auto other3 = deltaFromInitial({{"task0", "host2"}});
  EXPECT_NEAR(0, evaluate(pod0, other3), 1e-8);
  EXPECT_NEAR(10, evaluate(pod1, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task1", "host2"}});
  EXPECT_NEAR(0, evaluate(pod0, other4), 1e-8);
  EXPECT_NEAR(20, evaluate(pod1, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task0", "host1"}, {"task1", "host3"}});
  EXPECT_NEAR(30, evaluate(pod0, other5), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, other5), 1e-8);

  auto other6 = deltaFromInitial({{"task2", "host2"}});
  EXPECT_NEAR(0, evaluate(pod0, other6), 1e-8);
  EXPECT_NEAR(1, evaluate(pod1, other6), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, OldAbsoluteDynamic) {
  auto& builder = expressionBuilder();
  auto pod0 =
      co_await builder.getAbsoluteUtil(UtilMetric::OLD, power(), pod(), pod(0));
  auto pod1 =
      co_await builder.getAbsoluteUtil(UtilMetric::OLD, power(), pod(), pod(1));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0, evaluate(pod0, initial), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task0", "host1"}});
  EXPECT_NEAR(0, evaluate(pod0, other1), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task1", "host1"}});
  EXPECT_NEAR(0, evaluate(pod0, other2), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, other2), 1e-8);

  auto other3 = deltaFromInitial({{"task0", "host2"}});
  EXPECT_NEAR(10, evaluate(pod0, other3), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task1", "host2"}});
  EXPECT_NEAR(20, evaluate(pod0, other4), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task0", "host1"}, {"task1", "host3"}});
  EXPECT_NEAR(20, evaluate(pod0, other5), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, other5), 1e-8);

  auto other6 = deltaFromInitial({{"task2", "host2"}});
  EXPECT_NEAR(1, evaluate(pod0, other6), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1, other6), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, AfterAbsoluteDynamicWithGroup) {
  auto& builder = expressionBuilder();
  auto pod0job0 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, power(), pod(), pod(0), job(), job(0));
  auto pod0job1 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, power(), pod(), pod(0), job(), job(1));
  auto pod1job0 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, power(), pod(), pod(1), job(), job(0));
  auto pod1job1 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, power(), pod(), pod(1), job(), job(1));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(11, evaluate(pod0job0, initial), 1e-8);
  EXPECT_NEAR(20, evaluate(pod0job1, initial), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1job0, initial), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1job1, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task0", "host1"}});
  EXPECT_NEAR(41, evaluate(pod0job0, other1), 1e-8);
  EXPECT_NEAR(20, evaluate(pod0job1, other1), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1job0, other1), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1job1, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task1", "host1"}});
  EXPECT_NEAR(11, evaluate(pod0job0, other2), 1e-8);
  EXPECT_NEAR(80, evaluate(pod0job1, other2), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1job0, other2), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1job1, other2), 1e-8);

  auto other3 = deltaFromInitial({{"task0", "host2"}});
  EXPECT_NEAR(1, evaluate(pod0job0, other3), 1e-8);
  EXPECT_NEAR(20, evaluate(pod0job1, other3), 1e-8);
  EXPECT_NEAR(10, evaluate(pod1job0, other3), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1job1, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task1", "host2"}});
  EXPECT_NEAR(11, evaluate(pod0job0, other4), 1e-8);
  EXPECT_NEAR(0, evaluate(pod0job1, other4), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1job0, other4), 1e-8);
  EXPECT_NEAR(20, evaluate(pod1job1, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task0", "host1"}, {"task1", "host3"}});
  EXPECT_NEAR(41, evaluate(pod0job0, other5), 1e-8);
  EXPECT_NEAR(0, evaluate(pod0job1, other5), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1job0, other5), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1job1, other5), 1e-8);

  auto other6 = deltaFromInitial({{"task2", "host2"}});
  EXPECT_NEAR(10, evaluate(pod0job0, other6), 1e-8);
  EXPECT_NEAR(20, evaluate(pod0job1, other6), 1e-8);
  EXPECT_NEAR(1, evaluate(pod1job0, other6), 1e-8);
  EXPECT_NEAR(0, evaluate(pod1job1, other6), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, AfterAbsoluteDynamicOutOfScope) {
  auto& builder = expressionBuilder();
  auto noPod = co_await builder.getAbsoluteUtilOutOfScope(
      UtilMetric::AFTER, power(), pod());

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(3, evaluate(noPod, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task0", "host1"}});
  EXPECT_NEAR(3, evaluate(noPod, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task3", "host0"}});
  EXPECT_NEAR(2, evaluate(noPod, other2), 1e-8);

  auto other3 = deltaFromInitial({{"task0", "host3"}});
  EXPECT_NEAR(43, evaluate(noPod, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task1", "host3"}, {"task4", "host2"}});
  EXPECT_NEAR(82, evaluate(noPod, other4), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, DynamicDimensionOutOfScopeDefault) {
  auto& builder = expressionBuilder();
  auto host0 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, disk(), host(), host(0));
  auto host1 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, disk(), host(), host(1));
  auto host2 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, disk(), host(), host(2));
  auto host3 = co_await builder.getAbsoluteUtil(
      UtilMetric::AFTER, disk(), host(), host(3));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(110, evaluate(host0, initial), 1e-8);
  EXPECT_NEAR(10, evaluate(host1, initial), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, initial), 1e-8);
  EXPECT_NEAR(30, evaluate(host3, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task0", "host1"}});
  EXPECT_NEAR(10, evaluate(host0, other1), 1e-8);
  EXPECT_NEAR(110, evaluate(host1, other1), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, other1), 1e-8);
  EXPECT_NEAR(30, evaluate(host3, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task0", "host2"}});
  EXPECT_NEAR(10, evaluate(host0, other2), 1e-8);
  EXPECT_NEAR(10, evaluate(host1, other2), 1e-8);
  EXPECT_NEAR(200, evaluate(host2, other2), 1e-8);
  EXPECT_NEAR(30, evaluate(host3, other2), 1e-8);

  auto other3 = deltaFromInitial({{"task0", "host3"}});
  EXPECT_NEAR(10, evaluate(host0, other3), 1e-8);
  EXPECT_NEAR(10, evaluate(host1, other3), 1e-8);
  EXPECT_NEAR(0, evaluate(host2, other3), 1e-8);
  EXPECT_NEAR(40, evaluate(host3, other3), 1e-8);
}

TEST_F(ExpressionBuilderTest, IsAssigned) {
  auto task0 = objectId("task0");
  auto task3 = objectId("task3");
  auto hostScopeId = scopeId("host");
  auto scopeItem0 = host(0);
  auto scopeItem1 = host(1);

  auto& builder = expressionBuilder();
  auto assigned00 = builder.isAssigned(hostScopeId, scopeItem0, task0);
  auto assigned03 = builder.isAssigned(hostScopeId, scopeItem0, task3);
  auto assigned10 = builder.isAssigned(hostScopeId, scopeItem1, task0);
  auto assigned13 = builder.isAssigned(hostScopeId, scopeItem1, task3);

  {
    auto assignment = deltaFromInitial({});

    EXPECT_EQ(1.0, evaluate(assigned00, assignment));
    EXPECT_EQ(0.0, evaluate(assigned03, assignment));
    EXPECT_EQ(0.0, evaluate(assigned10, assignment));
    EXPECT_EQ(0.0, evaluate(assigned13, assignment));
  }

  {
    auto assignment =
        deltaFromInitial({{"task0", "host1"}, {"task3", "host1"}});

    EXPECT_EQ(0.0, evaluate(assigned00, assignment));
    EXPECT_EQ(0.0, evaluate(assigned03, assignment));
    EXPECT_EQ(1.0, evaluate(assigned10, assignment));
    EXPECT_EQ(1.0, evaluate(assigned13, assignment));
  }
}

CO_TEST_F(ExpressionBuilderTest, MovedAbsolute) {
  auto& builder = expressionBuilder();
  auto host0 = co_await builder.getAbsoluteUtil(
      UtilMetric::MOVED, cpu(), host(), host(0));
  auto host1 = co_await builder.getAbsoluteUtil(
      UtilMetric::MOVED, cpu(), host(), host(1));
  auto host2 = co_await builder.getAbsoluteUtil(
      UtilMetric::MOVED, cpu(), host(), host(2));
  auto host3 = co_await builder.getAbsoluteUtil(
      UtilMetric::MOVED, cpu(), host(), host(3));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0.0, evaluate(host0, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host1, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host3, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(0.0, evaluate(host0, other1), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host1, other1), 1e-8);
  EXPECT_NEAR(0.4, evaluate(host2, other1), 1e-8);
  EXPECT_NEAR(0.4, evaluate(host3, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(0.0, evaluate(host0, other2), 1e-8);
  EXPECT_NEAR(1.8, evaluate(host1, other2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other2), 1e-8);
  EXPECT_NEAR(1.8, evaluate(host3, other2), 1e-8);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  EXPECT_NEAR(0.0, evaluate(host0, other3), 1e-8);
  EXPECT_NEAR(1.8, evaluate(host1, other3), 1e-8);
  EXPECT_NEAR(0.4, evaluate(host2, other3), 1e-8);
  EXPECT_NEAR(2.2, evaluate(host3, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  EXPECT_NEAR(0.8, evaluate(host0, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host1, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other4), 1e-8);
  EXPECT_NEAR(0.8, evaluate(host3, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  EXPECT_NEAR(0.9, evaluate(host0, other5), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host1, other5), 1e-8);
  EXPECT_NEAR(0.1, evaluate(host2, other5), 1e-8);
  EXPECT_NEAR(0.8, evaluate(host3, other5), 1e-8);
}

TEST_F(ExpressionBuilderTest, ErrorWithVecAndDynamicOverScopeItems) {
  auto errorMsg =
      "getAbsoluteUtilOverScopeItems() is not supported with a) dynamic object dimensions, b) when object dimension size is greater than 1, c) an ObjectPartitionRoutingDimension";

  auto& builder = expressionBuilder();
  REBALANCER_EXPECT_RUNTIME_ERROR(
      folly::coro::blockingWait(builder.getAbsoluteUtil(
          UtilMetric::AFTER, network(), host(), {host(1), host(3)})),
      errorMsg);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      folly::coro::blockingWait(builder.getAbsoluteUtil(
          UtilMetric::AFTER, power(), pod(), {pod(0), pod(1)})),
      errorMsg);
}

TEST_F(ExpressionBuilderTest, ErrorWithNonAfterMetricOverScopeItems) {
  auto metrics = {
      UtilMetric::DURING,
      UtilMetric::STAYED,
      UtilMetric::INITIAL,
      UtilMetric::NEW,
      UtilMetric::OLD,
      UtilMetric::MOVED,
  };

  auto& builder = expressionBuilder();
  for (auto metric : metrics) {
    REBALANCER_EXPECT_RUNTIME_ERROR(
        folly::coro::blockingWait(builder.getAbsoluteUtil(
            metric, network(), host(), {host(1), host(3)})),
        "getAbsoluteUtil() with a set of scopeItems is only supported with UtilMetric::AFTER");
  }
}

CO_TEST_F(ExpressionBuilderTest, ErrorWithRoutingDimensionAndObjectPartition) {
  co_await addObjectPartitionRoutingDimension();

  REBALANCER_EXPECT_RUNTIME_ERROR(
      expressionBuilder().getObjectPartition({}, routingLoad(), job(), false),
      "non-scalar dimensions or ObjectPartitionRoutingDimensions are not currently supported with objectPartition");
}

CO_TEST_F(ExpressionBuilderTest, ErrorWithRoutingDimensionAndAbsoluteUtil) {
  co_await addObjectPartitionRoutingDimension();

  // error when using all metrics except AFTER or DURING
  auto metrics = {
      UtilMetric::STAYED,
      UtilMetric::INITIAL,
      UtilMetric::NEW,
      UtilMetric::OLD,
      UtilMetric::MOVED,
  };
  for (auto metric : metrics) {
    REBALANCER_EXPECT_RUNTIME_ERROR(
        folly::coro::blockingWait(
            expressionBuilder().getAbsoluteUtil(
                metric, routingLoad(), host(), host(1))),
        "Using an ObjectPartitionRoutingDimension is currently only supported when using AFTER or DURING definition");
  }

  // error when using AFTER, but scope used in routingConfig is not same as
  // the scope for absoluteUtil
  REBALANCER_EXPECT_RUNTIME_ERROR(
      folly::coro::blockingWait(
          expressionBuilder().getAbsoluteUtil(
              UtilMetric::AFTER, routingLoad(), rack(), host(1))),
      "Absolute util is requested on scope 'rack', but routingConfig is defined on scope 'host'");

  REBALANCER_EXPECT_RUNTIME_ERROR(
      folly::coro::blockingWait(
          expressionBuilder().getAbsoluteUtilOutOfScope(
              UtilMetric::DURING, routingLoad(), rack())),
      "Using an ObjectPartitionRoutingDimension is currently NOT supported when either the outOfScope is set in the descriptor OR when scopeItemId is not set");
}

CO_TEST_F(ExpressionBuilderTest, DuringBoundsOveride) {
  // With boundsOverride, the lower bound is fixed to the initial lower bound
  auto expectedLBUB = [](double lb1,
                         double ub1,
                         double lb2,
                         double ub2,
                         double lb3,
                         double ub3,
                         double lb4,
                         double ub4) {
    EXPECT_NEAR(0.1, lb1, 1e-8);
    EXPECT_NEAR(3.2, ub1, 1e-8);
    EXPECT_NEAR(0.2, lb2, 1e-8);
    EXPECT_NEAR(3.3, ub2, 1e-8);
    EXPECT_NEAR(0, lb3, 1e-8);
    EXPECT_NEAR(3.1, ub3, 1e-8);
    EXPECT_NEAR(2.8, lb4, 1e-8);
    EXPECT_NEAR(5.9, ub4, 1e-8);
  };

  Context context;
  const auto universe = buildUniverse();
  auto initial = deltaFromInitial({});
  auto builder = std::make_unique<ExpressionBuilder>(universe, initial);
  auto host0 = co_await builder->getAbsoluteUtil(
      UtilMetric::DURING, cpu(), host(), host(0));
  auto host1 = co_await builder->getAbsoluteUtil(
      UtilMetric::DURING, cpu(), host(), host(1));
  auto host2 = co_await builder->getAbsoluteUtil(
      UtilMetric::DURING, cpu(), host(), host(2));
  auto host3 = co_await builder->getAbsoluteUtil(
      UtilMetric::DURING, cpu(), host(), host(3));

  // Initial assignment

  auto [lb1, ub1] = host0->lowerAndUpperBounds(context);
  auto [lb2, ub2] = host1->lowerAndUpperBounds(context);
  auto [lb3, ub3] = host2->lowerAndUpperBounds(context);
  auto [lb4, ub4] = host3->lowerAndUpperBounds(context);

  expectedLBUB(lb1, ub1, lb2, ub2, lb3, ub3, lb4, ub4);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  builder = std::make_unique<ExpressionBuilder>(universe, other1);

  auto [o1lb1, o1ub1] = host0->lowerAndUpperBounds(context);
  auto [o1lb2, o1ub2] = host1->lowerAndUpperBounds(context);
  auto [o1lb3, o1ub3] = host2->lowerAndUpperBounds(context);
  auto [o1lb4, o1ub4] = host3->lowerAndUpperBounds(context);

  expectedLBUB(o1lb1, o1ub1, o1lb2, o1ub2, o1lb3, o1ub3, o1lb4, o1ub4);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  builder = std::make_unique<ExpressionBuilder>(universe, other2);

  auto [o2lb1, o2ub1] = host0->lowerAndUpperBounds(context);
  auto [o2lb2, o2ub2] = host1->lowerAndUpperBounds(context);
  auto [o2lb3, o2ub3] = host2->lowerAndUpperBounds(context);
  auto [o2lb4, o2ub4] = host3->lowerAndUpperBounds(context);

  expectedLBUB(o2lb1, o2ub1, o2lb2, o2ub2, o2lb3, o2ub3, o2lb4, o2ub4);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  builder = std::make_unique<ExpressionBuilder>(universe, other3);

  auto [o3lb1, o3ub1] = host0->lowerAndUpperBounds(context);
  auto [o3lb2, o3ub2] = host1->lowerAndUpperBounds(context);
  auto [o3lb3, o3ub3] = host2->lowerAndUpperBounds(context);
  auto [o3lb4, o3ub4] = host3->lowerAndUpperBounds(context);

  expectedLBUB(o3lb1, o3ub1, o3lb2, o3ub2, o3lb3, o3ub3, o3lb4, o3ub4);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  builder = std::make_unique<ExpressionBuilder>(universe, other4);

  auto [o4lb1, o4ub1] = host0->lowerAndUpperBounds(context);
  auto [o4lb2, o4ub2] = host1->lowerAndUpperBounds(context);
  auto [o4lb3, o4ub3] = host2->lowerAndUpperBounds(context);
  auto [o4lb4, o4ub4] = host3->lowerAndUpperBounds(context);

  expectedLBUB(o4lb1, o4ub1, o4lb2, o4ub2, o4lb3, o4ub3, o4lb4, o4ub4);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  builder = std::make_unique<ExpressionBuilder>(universe, other5);

  auto [o5lb1, o5ub1] = host0->lowerAndUpperBounds(context);
  auto [o5lb2, o5ub2] = host1->lowerAndUpperBounds(context);
  auto [o5lb3, o5ub3] = host2->lowerAndUpperBounds(context);
  auto [o5lb4, o5ub4] = host3->lowerAndUpperBounds(context);

  expectedLBUB(o5lb1, o5ub1, o5lb2, o5ub2, o5lb3, o5ub3, o5lb4, o5ub4);
}

CO_TEST_F(ExpressionBuilderTest, DuringBoundsOverideWithNegativeValue) {
  // With boundsOverride and negative dimensions,
  // we do not override the lowerbound as the lowerbound can improve
  co_await addObjectDimension(
      "testDimension",
      {{task(0), 0},
       {task(1), 0.1},
       {task(2), -10.2},
       {task(3), 0.4},
       {task(4), 0.8},
       {task(5), 1.6}},
      0);

  const auto testDimensionId = dimensionId("testDimension");

  // With boundsOverride and negative dimensions,
  // we do not override the lowerbound as the lowerbound can improve
  const Context context;
  auto& builder = expressionBuilder();
  auto host0 = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, testDimensionId, host(), host(0));

  EXPECT_EQ("LinearSum", host0->getType());
  // Negative dimension values are extracted out in one ObjectLookup
  // and the rest are extracted out in another ObjectLookup
  // the utilization is Lookup2 - Lookup1
  EXPECT_EQ(2, host0->children().size());

  // With boundsOverride and no negative dimensions, boundsOverride should
  // appear as the top level expression and the only child should be a
  // core during expression which is a linearsum (after + initial - stayed)
  auto host1 = co_await builder.getAbsoluteUtil(
      UtilMetric::DURING, cpu(), host(), host(0));

  EXPECT_EQ("BoundsOverride", host1->getType());
  EXPECT_EQ(1, host1->children().size());
  EXPECT_EQ("LinearSum", host1->getOnlyChild()->getType());
}

class ExpressionBuilderNestedImagePartitionTest : public ExpressionBuilderTest {
 protected:
  folly::coro::Task<void> setUpCoroDerived() {
    co_await ExpressionBuilderTest::setUpCoro();

    co_await addPartition(
        "tenant",
        {{"tenant1", {"task0"}}, // task0 is in both tenant1 and tenant5
         {"tenant2", {"task2"}},
         {"tenant3", {"task4"}},
         {"tenant4", {"task1", "task3", "task5"}}});

    co_await addPartition(
        "tenantWithDuplicate",
        {{"tenantWithDuplicate1", {"task0"}},
         {"tenantWithDuplicate2", {"task2"}},
         {"tenantWithDuplicate3", {"task4"}},
         {"tenantWithDuplicate4", {"task1", "task3", "task5"}},
         {"tenantWithDuplicate5", {"task0"}}}); // task0 in both 1 and 5

    co_await addPartition(
        "tenantSet",
        {{"tenantSet1", {"task0", "task2"}},
         {"tenantSet2", {"task1", "task3", "task5"}}});

    co_return;
  }

  void SetUp() override {
    folly::coro::blockingWait(setUpCoroDerived());
  }

  entities::PartitionId tenant() const {
    return partitionId("tenant");
  }

  entities::PartitionId tenantWithDuplicate() const {
    return partitionId("tenantWithDuplicate");
  }

  entities::PartitionId tenantSet() const {
    return partitionId("tenantSet");
  }

  entities::GroupId tenant(int index) const {
    return groupId("tenant", fmt::format("tenant{}", index));
  }

  entities::GroupId tenantSet(int index) const {
    return groupId("tenantSet", fmt::format("tenantSet{}", index));
  }
};

TEST_F(ExpressionBuilderNestedImagePartitionTest, SamePartitions) {
  auto& builder = expressionBuilder();
  EXPECT_EQ(
      std::set<entities::GroupId>{tenant(1)},
      toSet<entities::GroupId>(
          *builder.getNestedImage(tenant(), tenant(), tenant(1))));

  EXPECT_EQ(
      std::set<entities::GroupId>{tenant(2)},
      toSet<entities::GroupId>(
          *builder.getNestedImage(tenant(), tenant(), tenant(2))));
}

TEST_F(
    ExpressionBuilderNestedImagePartitionTest,
    ProperNestingAndErrorIfInnerPartitionNoDisjoint) {
  auto& builder = expressionBuilder();
  const std::set<entities::GroupId> j0TenantExpected = {
      tenant(1), tenant(2), tenant(3)};
  EXPECT_EQ(
      j0TenantExpected,
      toSet<entities::GroupId>(
          *builder.getNestedImage(job(), tenant(), job(0))));

  EXPECT_EQ(
      std::set<entities::GroupId>{tenant(4)},
      toSet<entities::GroupId>(
          *builder.getNestedImage(job(), tenant(), job(1))));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.getNestedImage(job(), tenantWithDuplicate(), job(0)),
      "Expected all objects in outer group 'job0' to be part of at most one of the groups of inner partition 'tenantWithDuplicate', but found object 'task0' that is part of multiple groups");
}

TEST_F(ExpressionBuilderNestedImagePartitionTest, SymmetricNestingCheck) {
  // tenantSet1 = {task0, task2} should map to job(0) = {task0, task2, task4}
  // However, task4 is not in tenantSet1, so this should be an error
  auto& builder = expressionBuilder();
  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.getNestedImage(tenantSet(), job(), tenantSet(1)),
      "Expect inner group 'job0' to be a subset of outer group 'tenantSet1', but found object 'task4' that is not in 'tenantSet1'");

  // tenantSet2 = {task1, task3, task5} maps to job(1) = {task1, task3, task5}
  EXPECT_EQ(
      std::set<entities::GroupId>{job(1)},
      toSet<entities::GroupId>(
          *builder.getNestedImage(tenantSet(), job(), tenantSet(2))));
}

TEST_F(
    ExpressionBuilderNestedImagePartitionTest,
    ErrorWhenOuterGroupNotFullyCovered) {
  REBALANCER_EXPECT_RUNTIME_ERROR(
      expressionBuilder().getNestedImage(job(), tenantSet(), job(0)),
      "Expected all objects in outer group 'job0' to be part of at least one of the groups of inner partition 'tenantSet', but did not find any group for object 'task4'");
}

TEST_F(
    ExpressionBuilderNestedImagePartitionTest,
    ErrorWhenInnerGroupNotFullyContainedInOuter) {
  REBALANCER_EXPECT_RUNTIME_ERROR(
      expressionBuilder().getNestedImage(tenant(), tenantSet(), tenant(2)),
      "Expect inner group 'tenantSet1' to be a subset of outer group 'tenant2', but found object 'task0' that is not in 'tenant2'");
}

TEST_F(
    ExpressionBuilderTest,
    ObjectPartitionLookupDynamicDimensionScopeMismatch) {
  // Create an object partition using a dynamic dimension (power uses parity
  // scope)
  auto& builder = expressionBuilder();
  const entities::Map<entities::GroupId, double> limits = {
      {job(0), 0.5}, {job(1), 0.7}};

  auto scopeParams = materializer::ExpressionBuilder::ScopeParams{
      .scopeId = rack(), .scopeItemId = rack(0)};
  auto objectPartition = builder.getObjectPartition(
      limits, power(), job(), false, std::move(scopeParams));

  // Verify that getObjectPartitionLookup now supports a different scope (rack
  // instead of parity) for dynamic dimensions by using a scope image
  auto partitionGroupCount = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition,
      {},
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      false);

  // Verify that the expression was created successfully
  EXPECT_NE(partitionGroupCount, nullptr);

  // Check the initial value
  // rack(0) contains host(0) and host(1)
  // host(0) has task(0) [parity0, power=10] and task(1) [parity0, power=20]
  // host(1) has task(2) [parity1, power=1 (default)]
  // job(0) = {task(0), task(2), task(4)}, on rack(0): task(0)=10, task(2)=1,
  // total=11 job(1) = {task(1), task(3), task(5)}, on rack(0): task(1)=20,
  // total=20 Penalty = max(0, 11 - 0.5) + max(0, 20 - 0.7) = 10.5 + 19.3 = 29.8
  auto initial = deltaFromInitial({});
  EXPECT_NEAR(29.8, evaluate(partitionGroupCount, initial), 1e-8);

  // Test with task0 moved to host1 (still in rack(0) but different parity)
  // task(0) now in parity1, power=40
  // job(0) on rack(0): task(0)=40, task(2)=1, total=41
  // job(1) on rack(0): task(1)=20, total=20
  // Penalty = max(0, 41 - 0.5) + max(0, 20 - 0.7) = 40.5 + 19.3 = 59.8
  initial = deltaFromInitial({{"task0", "host1"}});
  EXPECT_NEAR(59.8, evaluate(partitionGroupCount, initial), 1e-8);

  // Test with task1 moved to host1
  // task(1) now in parity1, power=80
  // job(0) on rack(0): task(0)=10, task(2)=1, total=11
  // job(1) on rack(0): task(1)=80, total=80
  // Penalty = max(0, 11 - 0.5) + max(0, 80 - 0.7) = 10.5 + 79.3 = 89.8
  initial = deltaFromInitial({{"task1", "host1"}});
  EXPECT_NEAR(89.8, evaluate(partitionGroupCount, initial), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, InitialAbsolute) {
  auto& builder = expressionBuilder();
  auto host0 = co_await builder.getAbsoluteUtil(
      UtilMetric::INITIAL, cpu(), host(), host(0));
  auto host1 = co_await builder.getAbsoluteUtil(
      UtilMetric::INITIAL, cpu(), host(), host(1));
  auto host2 = co_await builder.getAbsoluteUtil(
      UtilMetric::INITIAL, cpu(), host(), host(2));
  auto host3 = co_await builder.getAbsoluteUtil(
      UtilMetric::INITIAL, cpu(), host(), host(3));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0.1, evaluate(host0, initial), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, initial), 1e-8);
  EXPECT_NEAR(2.8, evaluate(host3, initial), 1e-8);

  // INITIAL should not change with moves
  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(0.1, evaluate(host0, other1), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, other1), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other1), 1e-8);
  EXPECT_NEAR(2.8, evaluate(host3, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(0.1, evaluate(host0, other2), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, other2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other2), 1e-8);
  EXPECT_NEAR(2.8, evaluate(host3, other2), 1e-8);
}

CO_TEST_F(ExpressionBuilderTest, StayedAbsolute) {
  auto& builder = expressionBuilder();
  auto host0 = co_await builder.getAbsoluteUtil(
      UtilMetric::STAYED, cpu(), host(), host(0));
  auto host1 = co_await builder.getAbsoluteUtil(
      UtilMetric::STAYED, cpu(), host(), host(1));
  auto host2 = co_await builder.getAbsoluteUtil(
      UtilMetric::STAYED, cpu(), host(), host(2));
  auto host3 = co_await builder.getAbsoluteUtil(
      UtilMetric::STAYED, cpu(), host(), host(3));

  auto initial = deltaFromInitial({});
  EXPECT_NEAR(0.1, evaluate(host0, initial), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, initial), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, initial), 1e-8);
  EXPECT_NEAR(2.8, evaluate(host3, initial), 1e-8);

  auto other1 = deltaFromInitial({{"task3", "host2"}});
  EXPECT_NEAR(0.1, evaluate(host0, other1), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, other1), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other1), 1e-8);
  EXPECT_NEAR(2.4, evaluate(host3, other1), 1e-8);

  auto other2 = deltaFromInitial({{"task2", "host3"}, {"task5", "host1"}});
  EXPECT_NEAR(0.1, evaluate(host0, other2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host1, other2), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other2), 1e-8);
  EXPECT_NEAR(1.2, evaluate(host3, other2), 1e-8);

  auto other3 = deltaFromInitial(
      {{"task2", "host3"}, {"task3", "host2"}, {"task5", "host1"}});
  EXPECT_NEAR(0.1, evaluate(host0, other3), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host1, other3), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other3), 1e-8);
  EXPECT_NEAR(0.8, evaluate(host3, other3), 1e-8);

  auto other4 = deltaFromInitial({{"task4", "host0"}});
  EXPECT_NEAR(0.1, evaluate(host0, other4), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, other4), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other4), 1e-8);
  EXPECT_NEAR(2.0, evaluate(host3, other4), 1e-8);

  auto other5 = deltaFromInitial({{"task1", "host2"}, {"task4", "host0"}});
  EXPECT_NEAR(0.0, evaluate(host0, other5), 1e-8);
  EXPECT_NEAR(0.2, evaluate(host1, other5), 1e-8);
  EXPECT_NEAR(0.0, evaluate(host2, other5), 1e-8);
  EXPECT_NEAR(2.0, evaluate(host3, other5), 1e-8);
}

TEST_F(ExpressionBuilderTest, ObjectPartitionLookupWithMetrics) {
  // Create a Metrics::Builder to enable the metrics_ code path
  auto metricsBuilder = std::make_shared<Metrics::Builder>();

  // Create a new ExpressionBuilder with metrics enabled
  const auto universe = buildUniverse();
  auto builderWithMetrics = std::make_unique<ExpressionBuilder>(
      universe, deltaFromInitial({}), nullptr, metricsBuilder);

  // Create an object partition
  const entities::Map<entities::GroupId, double> limits = {
      {job(1), 0.01}, {job(0), 0.02}};

  auto objectPartition =
      builderWithMetrics->getObjectPartition(limits, cpu(), job(), false);

  // Call getObjectPartitionLookup which should trigger the metrics_ code path
  // This will execute lines 1170-1171 where the dynamic_pointer_cast happens
  auto partitionGroupCount = builderWithMetrics->getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition,
      {},
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      false);

  // Verify that the expression was created successfully
  EXPECT_NE(partitionGroupCount, nullptr);

  // Verify it evaluates correctly
  EXPECT_NEAR(0.27, evaluate(partitionGroupCount, deltaFromInitial({})), 1e-8);
}

TEST_F(ExpressionBuilderTest, ObjectPartitionCacheHit) {
  // Test that cache returns the same expression pointer for identical inputs
  const entities::Map<entities::GroupId, double> emptyGroupLimits = {};

  // Create two object partitions with identical parameters
  auto& builder = expressionBuilder();
  auto expr1 = builder.getObjectPartition(
      emptyGroupLimits, cpu(), job(), /*normalizeByGroupSize=*/false);
  auto expr2 = builder.getObjectPartition(
      emptyGroupLimits, cpu(), job(), /*normalizeByGroupSize=*/false);

  // Should be the exact same pointer (cache hit)
  EXPECT_EQ(expr1.get(), expr2.get());

  // Test with scopeParams
  ExpressionBuilder::ScopeParams scopeParams{
      .scopeId = rack(), .scopeItemId = rack(0)};
  auto expr3 = builder.getObjectPartition(
      emptyGroupLimits, power(), job(), false, scopeParams);
  auto expr4 = builder.getObjectPartition(
      emptyGroupLimits, power(), job(), false, scopeParams);

  EXPECT_EQ(expr3.get(), expr4.get());

  // Different scopeParams should produce different expressions
  ExpressionBuilder::ScopeParams differentScopeParams{
      .scopeId = rack(), .scopeItemId = rack(1)};
  auto expr5 = builder.getObjectPartition(
      emptyGroupLimits, power(), job(), false, differentScopeParams);

  EXPECT_NE(expr3.get(), expr5.get());
}

TEST_F(ExpressionBuilderTest, ObjectPartitionNonCacheable) {
  // Test that non-cacheable path works (when groupLimits is not empty)
  const entities::Map<entities::GroupId, double> groupLimits = {
      {job(0), 1.0}, {job(1), 2.0}};

  // Create two object partitions with non-empty groupLimits
  auto& builder = expressionBuilder();
  auto expr1 = builder.getObjectPartition(
      groupLimits, cpu(), job(), /*normalizeByGroupSize=*/false);
  auto expr2 = builder.getObjectPartition(
      groupLimits, cpu(), job(), /*normalizeByGroupSize=*/false);

  // Should be different pointers (not cached)
  EXPECT_NE(expr1.get(), expr2.get());
}

TEST_F(ExpressionBuilderTest, ObjectPartitionFilteredGroupIdsOrderCacheHit) {
  // Test that filtered group IDs order doesn't matter for cache hits
  const entities::Map<entities::GroupId, double> emptyGroupLimits = {};

  // Create PackerSets with same elements but potentially different insertion
  // order
  auto filtered1 = std::make_shared<PackerSet<entities::GroupId>>();
  filtered1->insert(job(0));
  filtered1->insert(job(1));

  auto filtered2 = std::make_shared<PackerSet<entities::GroupId>>();
  filtered2->insert(job(1)); // Insert in reverse order
  filtered2->insert(job(0));

  auto& builder = expressionBuilder();
  auto expr1 = builder.getObjectPartition(
      emptyGroupLimits, cpu(), job(), false, std::nullopt, filtered1);
  auto expr2 = builder.getObjectPartition(
      emptyGroupLimits, cpu(), job(), false, std::nullopt, filtered2);

  // Should be the same pointer (cache hit) because the sorted vector
  // representation is the same
  EXPECT_EQ(expr1.get(), expr2.get());

  // Different filtered groups should produce different expressions
  auto filtered3 = std::make_shared<PackerSet<entities::GroupId>>();
  filtered3->insert(job(0));

  auto expr3 = builder.getObjectPartition(
      emptyGroupLimits, cpu(), job(), false, std::nullopt, filtered3);

  EXPECT_NE(expr1.get(), expr3.get());

  // Test with no filter vs with filter
  auto expr4 = builder.getObjectPartition(
      emptyGroupLimits, cpu(), job(), false, std::nullopt, nullptr);

  EXPECT_NE(expr1.get(), expr4.get());
}

TEST_F(ExpressionBuilderTest, ObjectPartitionLookupCacheHit) {
  // Test that cache returns the same expression pointer for identical inputs
  const entities::Map<entities::GroupId, double> emptyOverrides = {};
  const entities::Map<entities::GroupId, double> limits = {
      {job(1), 0.01}, {job(0), 0.02}};

  auto& builder = expressionBuilder();
  auto objectPartition =
      builder.getObjectPartition(limits, cpu(), job(), false);

  // Create two object partition lookups with identical parameters
  auto expr1 = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition,
      emptyOverrides,
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      false);
  auto expr2 = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition,
      emptyOverrides,
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      false);

  // Should be the exact same pointer (cache hit)
  EXPECT_EQ(expr1.get(), expr2.get());

  // Test with DURING metric
  auto expr3 = builder.getObjectPartitionLookup(
      UtilMetric::DURING,
      rack(),
      rack(0),
      objectPartition,
      emptyOverrides,
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      false);
  auto expr4 = builder.getObjectPartitionLookup(
      UtilMetric::DURING,
      rack(),
      rack(0),
      objectPartition,
      emptyOverrides,
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      false);

  // Should be the same pointer (cache hit)
  EXPECT_EQ(expr3.get(), expr4.get());

  // AFTER and DURING should produce different expressions
  EXPECT_NE(expr1.get(), expr3.get());
}

TEST_F(ExpressionBuilderTest, ObjectPartitionLookupCacheMiss) {
  // Test that cache misses occur when parameters differ
  const entities::Map<entities::GroupId, double> emptyOverrides = {};
  const entities::Map<entities::GroupId, double> limits = {
      {job(1), 0.01}, {job(0), 0.02}};

  auto& builder = expressionBuilder();
  auto objectPartition =
      builder.getObjectPartition(limits, cpu(), job(), false);

  auto expr1 = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition,
      emptyOverrides,
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      false);

  // Different scopeItemId
  auto expr2 = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(1),
      objectPartition,
      emptyOverrides,
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      false);
  EXPECT_NE(expr1.get(), expr2.get());

  // Different penalty transform
  auto expr3 = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition,
      emptyOverrides,
      ObjectPartitionLookupPenaltyTransform::SQUARE,
      0,
      false);
  EXPECT_NE(expr1.get(), expr3.get());

  // Different groupsAllowed parameter
  auto expr4 = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition,
      emptyOverrides,
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      1,
      false);
  EXPECT_NE(expr1.get(), expr4.get());

  // Different minBound parameter
  auto expr5 = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition,
      emptyOverrides,
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      true);
  EXPECT_NE(expr1.get(), expr5.get());

  // Different objectPartition
  auto objectPartition2 =
      builder.getObjectPartition(limits, cpu(), job(), true);
  auto expr6 = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition2,
      emptyOverrides,
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      false);
  EXPECT_NE(expr1.get(), expr6.get());
}

TEST_F(ExpressionBuilderTest, ObjectPartitionLookupNonCacheable) {
  // Test that non-cacheable path works (when overrides is not empty)
  const entities::Map<entities::GroupId, double> limits = {
      {job(1), 0.01}, {job(0), 0.02}};
  const entities::Map<entities::GroupId, double> overrides = {{job(0), 0.05}};

  auto& builder = expressionBuilder();
  auto objectPartition =
      builder.getObjectPartition(limits, cpu(), job(), false);

  // Create two object partition lookups with non-empty overrides
  auto expr1 = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition,
      overrides,
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      false);
  auto expr2 = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition,
      overrides,
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      false);

  // Should be different pointers (not cached)
  EXPECT_NE(expr1.get(), expr2.get());

  // Both should still evaluate correctly
  EXPECT_NEAR(
      evaluate(expr1, deltaFromInitial({})),
      evaluate(expr2, deltaFromInitial({})),
      1e-8);
}

TEST_F(ExpressionBuilderTest, ObjectPartitionLookupCacheWithDifferentScopes) {
  // Test that different scopes produce different cached expressions
  const entities::Map<entities::GroupId, double> emptyOverrides = {};
  const entities::Map<entities::GroupId, double> limits = {
      {job(1), 0.01}, {job(0), 0.02}};

  auto& builder = expressionBuilder();
  auto objectPartition =
      builder.getObjectPartition(limits, cpu(), job(), false);

  // Same scope, same item - should cache
  auto expr1 = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition,
      emptyOverrides,
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      false);
  auto expr2 = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      rack(),
      rack(0),
      objectPartition,
      emptyOverrides,
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      false);
  EXPECT_EQ(expr1.get(), expr2.get());

  // Different scope (host vs rack) - should not cache
  auto expr3 = builder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      host(),
      host(0),
      objectPartition,
      emptyOverrides,
      ObjectPartitionLookupPenaltyTransform::IDENTITY,
      0,
      false);
  EXPECT_NE(expr1.get(), expr3.get());
}
} // namespace facebook::rebalancer::materializer::tests
