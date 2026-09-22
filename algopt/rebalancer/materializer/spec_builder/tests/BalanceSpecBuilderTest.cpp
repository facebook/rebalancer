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
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/materializer/spec_builder/BalanceSpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"

#include <folly/container/irange.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

#include <cmath>

namespace facebook::rebalancer::materializer::tests {

class BalanceSpecBuilderTest : public SpecBuilderTestBase<> {
 protected:
  static interface::Limit& upperBounds(interface::BalanceSpec& spec) {
    auto& limits = spec.upperBounds().ensure();
    limits.type() = interface::LimitType::ABSOLUTE;
    return limits;
  }

  folly::coro::Task<void> setUpDefaultUniverse() {
    setUpUniverse({
        {"host0", {"task0", "task2"}},
        {"host1", {"task1"}},
        {"host2", {"task3"}},
        {"host3", {}},
    });

    const entities::Map<entities::ObjectId, double> objValues = {
        {task(0), 10}, {task(1), 10}, {task(2), 10}, {task(3), 10}};
    co_await addObjectDimension("cpu", objValues, 1);

    co_await addScopeDimension(
        "cpu",
        host(),
        {{"host0", 100}, {"host1", 100}, {"host2", 100}, {"host3", 100}},
        0);

    co_return;
  }

  folly::coro::Task<void> setUpExampleUniverse() {
    // Set universe based on example given in this wiki
    // https://www.internalfb.com/intern/wiki/ReBalancer/API/Tutorial:_Intro_to_Rebalancer/
    // Specifically, the example will have 2 scope items, "rack0" and "rack1"
    // and 4 containers, "host0", "host1", "host2", "host3"
    // where "rack0" = {"host0", "host1"} and "rack1" = {"host2", "host3"}

    setUpUniverse({
        {"host0", {}},
        {"host1", {"task0", "task1", "task2", "task3", "task4", "task5"}},
        {"host2", {"task6", "task7", "task8"}},
        {"host3", {"task9", "task10", "task11"}},
    });

    entities::Map<entities::ObjectId, double> objValues;
    for (const auto i : folly::irange(12)) {
      objValues[task(i)] = 10;
    }

    co_await addObjectDimension("cpu", objValues, 1);

    co_await addScope(
        "rack", {{"rack0", {"host0", "host1"}}, {"rack1", {"host2", "host3"}}});

    co_await addScopeDimension(
        "cpu", rack(), {{"rack0", 300}, {"rack1", 200}}, 1);

    co_return;
  }

  entities::ScopeId rack() const {
    return scopeId("rack");
  }

  entities::ScopeId host() const {
    return scopeId("host");
  }

  entities::ObjectId task(int index) const {
    return objectId(fmt::format("task{}", index));
  }

  static void addObjectToContainersForUniverse(
      entities::Map<std::string, std::vector<std::string>>& assignment,
      const std::string& container,
      int startNumber,
      int numTasks) {
    for (const auto i : folly::irange(numTasks)) {
      assignment[container].push_back(fmt::format("task{}", startNumber + i));
    }
  }

  void addObjectToContainersForUpdates(
      entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>&
          updates,
      std::string container,
      int startNumber,
      int numTasks) {
    updates[containerId(container)] = {};
    for (const auto i : folly::irange(numTasks)) {
      updates[containerId(container)].push_back(
          objectId(fmt::format("task{}", startNumber + i)));
    }
  }
};

CO_TEST_F(BalanceSpecBuilderTest, GoalBasic) {
  co_await setUpDefaultUniverse();

  interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = interface::BalanceSpecFormula::IDEAL;

  const auto universe = buildUniverse();
  const BalanceSpecBuilder specBuilder(universe, balanceSpec, true);
  EXPECT_EQ(
      "Balance cpu on  (definition AFTER, bound type RELATIVE, upper bound 1, formula IDEAL, metric RELATIVE_UTIL)",
      specBuilder.description());

  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // tasks are not balanced hence value will be +ve
  EXPECT_NEAR(
      1,
      evaluate(
          facebook::rebalancer::any_positive(
              std::vector<ExprPtr>{goal}, *universe),
          deltaFromInitial({})),
      1e-8);

  // allUtils = {0.1, 0.1, 0.1, 0.1}; sumCapacity = 400; avgCapacity = 100
  // sumUtil = 40; avgUtil = 0.1; result = 0.01 + 0.01 + 0.01 + 0.01 = 0.04
  EXPECT_NEAR(
      0.04, evaluate(goal, deltaFromInitial({{"task2", "host3"}})), 1e-8);
}

CO_TEST_F(BalanceSpecBuilderTest, Constraint) {
  co_await setUpDefaultUniverse();

  interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = interface::BalanceSpecFormula::IDEAL;

  BalanceSpecBuilder specBuilder(buildUniverse(), balanceSpec, true);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      folly::coro::blockingWait(specBuilder.constraints(expressionBuilder())),
      "BalanceSpec not supported as a constraint");
}

CO_TEST_F(BalanceSpecBuilderTest, SpecInfo) {
  co_await setUpDefaultUniverse();

  interface::BalanceSpec balanceSpec;
  balanceSpec.name() = "test";
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = interface::BalanceSpecFormula::IDEAL;
  balanceSpec.definition() = interface::BalanceSpecDefinition::AFTER;
  balanceSpec.boundType() = interface::BalanceSpecBoundType::RELATIVE_UTIL;

  const BalanceSpecBuilder specBuilder(buildUniverse(), balanceSpec, true);

  auto expectedSpecInfo = SpecParameters{
      .name = "test",
      .scope = "host",
      .dimension = "cpu",
      .definition = apache::thrift::util::enumNameSafe(
          interface::BalanceSpecDefinition::AFTER),
      .boundType = apache::thrift::util::enumNameSafe(
          interface::BalanceSpecBoundType::RELATIVE_UTIL),
      .formula = apache::thrift::util::enumNameSafe(
          interface::BalanceSpecFormula::IDEAL),
      .filterAllowListSize = 0,
      .filterBlockListSize = 0};
  EXPECT_EQ(expectedSpecInfo, specBuilder.getSpecInfo());
}

CO_TEST_F(BalanceSpecBuilderTest, getAverageRelativeUtilWithFixAverage) {
  co_await setUpExampleUniverse();

  interface::BalanceSpec balanceSpec;
  balanceSpec.name() = "test";
  balanceSpec.scope() = "rack";
  balanceSpec.dimension() = "cpu";
  balanceSpec.fixAverageToInitial() = true;

  const auto universe = buildUniverse();
  const BalanceSpecBuilder specBuilder(
      universe, balanceSpec, /*continuousExpressions=*/true);

  const entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
      updates = {
          {universe->getContainerId("host1"),
           {task(0), task(1), task(2), task(3), task(4), task(5)}},
          {universe->getContainerId("host2"), {task(6), task(7), task(8)}},
          {universe->getContainerId("host3"), {task(9), task(10), task(11)}},
      };

  auto goal = co_await specBuilder.goalCoro(expressionBuilder());
  // relative utilization of rack 0 is 60 / 300 = 0.2 (60 is the sum of
  // utilization and 300 is the sum of capacity)

  // relative utilization of rack 1 is 60 / 200 = 0.3 (60 is the sum of
  // utilization and 200 is the sum of capacity)

  // average relative utilization is (0.2 + 0.3) / 2 = 0.25

  // sumOverThreshold = (0 + 0.05) / 2 = 0.025
  EXPECT_NEAR(0.025, evaluate(goal, updates), 1e-8);
}

CO_TEST_F(
    BalanceSpecBuilderTest,
    getAverageRelativeUtilWithFixAverageUsingLegacyAverage) {
  co_await setUpExampleUniverse();

  interface::BalanceSpec balanceSpec;
  balanceSpec.name() = "test";
  balanceSpec.scope() = "rack";
  balanceSpec.dimension() = "cpu";
  balanceSpec.fixAverageToInitial() = true;
  balanceSpec.useLegacyAverage() = true;

  const auto universe = buildUniverse();
  const BalanceSpecBuilder specBuilder(universe, balanceSpec, true);

  const entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
      updates = {
          {universe->getContainerId("host1"),
           {task(0), task(1), task(2), task(3), task(4), task(5)}},
          {universe->getContainerId("host2"), {task(6), task(7), task(8)}},
          {universe->getContainerId("host3"), {task(9), task(10), task(11)}},
      };

  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // using the legacy average, the average relative utilization was 0.24 because
  // sumUtil = 120 as it is the sum of all the tasks and
  // sumCapacity = 500 as it is the sum of all the hosts
  // averageRelativeUtil = 0.24 = (120 / 500) -> goal ~= 0.3
  EXPECT_NEAR(0.030, evaluate(goal, updates), 1e-8);
}

CO_TEST_F(
    BalanceSpecBuilderTest,
    getAverageRelativeUtilWithFixAverageWithoutUsingLegacyAverage) {
  co_await setUpExampleUniverse();

  interface::BalanceSpec balanceSpec;
  balanceSpec.name() = "test";
  balanceSpec.scope() = "rack";
  balanceSpec.dimension() = "cpu";
  balanceSpec.fixAverageToInitial() = true;
  balanceSpec.useLegacyAverage() = false;

  const auto universe = buildUniverse();
  const BalanceSpecBuilder specBuilder(universe, balanceSpec, true);

  const entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
      updates = {
          {universe->getContainerId("host1"),
           {task(0), task(1), task(2), task(3), task(4), task(5)}},
          {universe->getContainerId("host2"), {task(6), task(7), task(8)}},
          {universe->getContainerId("host3"), {task(9), task(10), task(11)}},
      };

  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // relative utilization of rack 0 is 60 / 300 = 0.2 (60 is the sum of
  // utilization and 300 is the sum of capacity)

  // relative utilization of rack 1 is 60 / 200 = 0.3 (60 is the sum of
  // utilization and 200 is the sum of capacity)

  // average relative utilization is (0.2 + 0.3) / 2 = 0.25

  // sumOverThreshold = (0 + 0.05) / 2 = 0.025
  EXPECT_NEAR(0.025, evaluate(goal, updates), 1e-8);
}

class TrickyBalanceScenarioTest : public BalanceSpecBuilderTest {
 protected:
  folly::coro::Task<void> setUpTrickyUniverse() {
    // There are 5 hosts and 84 tasks. Initially, tasks are all in host 4.
    entities::Map<std::string, std::vector<std::string>> assignment = {
        {"host0", {}},
        {"host1", {}},
        {"host2", {}},
        {"host3", {}},
        {"host4", {}},
    };

    addObjectToContainersForUniverse(assignment, "host4", 0, 84);

    setUpUniverse(assignment);

    entities::Map<entities::ObjectId, double> objValues;
    for (const auto i : folly::irange(84)) {
      objValues[task(i)] = 1;
    }

    co_await addObjectDimension("cpu", objValues, 1);

    co_await addScopeDimension(
        "cpu",
        host(),
        {{"host0", 100},
         {"host1", 20},
         {"host2", 10},
         {"host3", 10},
         {"host4", 1000}},
        1);

    co_return;
  }

  std::unique_ptr<ExpressionBuilder> expressionBuilder(
      std::shared_ptr<const entities::Universe> universe) {
    entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
        updates = {};
    addObjectToContainersForUpdates(updates, "host0", 0, 44);
    addObjectToContainersForUpdates(updates, "host1", 44, 20);
    addObjectToContainersForUpdates(updates, "host2", 64, 10);
    addObjectToContainersForUpdates(updates, "host3", 74, 10);
    addObjectToContainersForUpdates(updates, "host4", 0, 0);

    return std::make_unique<ExpressionBuilder>(std::move(universe), updates);
  }
};

CO_TEST_F(TrickyBalanceScenarioTest, trickyScenarioWithIdealWithoutFixAverage) {
  co_await setUpTrickyUniverse();

  interface::BalanceSpec balanceSpec;
  balanceSpec.name() = "test";
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.fixAverageToInitial() = false;
  balanceSpec.definition() = interface::BalanceSpecDefinition::DURING;

  const auto universe = buildUniverse();
  const BalanceSpecBuilder specBuilder(universe, balanceSpec, true);

  entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
      updates = {};
  addObjectToContainersForUpdates(updates, "host0", 0, 44);
  addObjectToContainersForUpdates(updates, "host1", 44, 20);
  addObjectToContainersForUpdates(updates, "host2", 64, 10);
  addObjectToContainersForUpdates(updates, "host3", 74, 10);
  addObjectToContainersForUpdates(updates, "host4", 0, 0);

  const auto exprBuilder = expressionBuilder(universe);
  const auto goal = co_await specBuilder.goalCoro(*exprBuilder);

  // Initial relative utilization:
  // average relative utilization for host 0 is 0 / 100 = 0
  // average relative utilization for host 1 is 0 / 20 = 0
  // average relative utilization for host 2 is 0 / 10 = 0
  // average relative utilization for host 3 is 0 / 10 = 0
  // average relative utilization for host 4 is 84 / 1000 = 0.084

  // average utilization after update
  // average relative utilization for host 0 is 44 / 100 = 0.44
  // average relative utilization for host 1 is 20 / 20 = 1
  // average relative utilization for host 2 is 10 / 10 = 1
  // average relative utilization for host 3 is 10 / 10 = 1
  // average relative utilization for host 4 is 0 / 1000 = 0

  // sumRelativeUtil = (0.084) + (0.44 + 1 + 1 + 1) = 3.524
  //                   Initial   Update
  // average relative utilization for all host is 3.524 / 5 = 0.7048

  // sum over threshold = (0 + 0.2952 + 0.2952 + 0.2952 + 0) / 5 =
  // 0.8856 / 5 = 0.17712

  EXPECT_NEAR(0.17712, evaluate(goal, updates), 1e-8);
}

CO_TEST_F(TrickyBalanceScenarioTest, trickyScenarioWithIdealWithFixAverage) {
  co_await setUpTrickyUniverse();

  interface::BalanceSpec balanceSpec;
  balanceSpec.name() = "test";
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.fixAverageToInitial() = true;

  const auto universe = buildUniverse();
  const auto exprBuilder = expressionBuilder(universe);
  const BalanceSpecBuilder specBuilder(universe, balanceSpec, true);

  entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
      updates = {};
  addObjectToContainersForUpdates(updates, "host0", 0, 44);
  addObjectToContainersForUpdates(updates, "host1", 44, 20);
  addObjectToContainersForUpdates(updates, "host2", 64, 10);
  addObjectToContainersForUpdates(updates, "host3", 74, 10);
  addObjectToContainersForUpdates(updates, "host4", 0, 0);

  // average relative utilization for host 0 is 44 / 100 = 0.44
  // average relative utilization for host 1 is 20 / 20 = 1
  // average relative utilization for host 2 is 10 / 10 = 1
  // average relative utilization for host 3 is 10 / 10 = 1
  // average relative utilization for host 4 is 0 / 1000 = 0
  // average relative utilization for all host is (0.44 + 1 + 1 + 1 + 0) = 0.688

  // sum over threshold = (0 + 0.312 + 0.312 + 0.312 + 0) / 5 =
  // 0.936 / 5 = 0.1872

  auto goal = co_await specBuilder.goalCoro(*exprBuilder);
  EXPECT_NEAR(0.1872, evaluate(goal, updates), 1e-8);
}

class BalanceWithZeroCapacityContainersTest : public BalanceSpecBuilderTest {
 protected:
  folly::coro::Task<void> setUpUniverseWithZeroCapacityContainers() {
    setUpUniverse({
        {"host0", {"task0", "task2"}},
        {"host1", {"task1"}},
        {"host2", {"task3"}},
        {"host3", {}},
    });

    const entities::Map<entities::ObjectId, double> objValues = {
        {task(0), 10}, {task(1), 10}, {task(2), 10}, {task(3), 10}};
    co_await addObjectDimension("cpu", objValues, 1);

    co_await addScopeDimension(
        "cpu",
        host(),
        {{"host0", 100}, {"host1", 100}, {"host2", 100}, {"host3", 0}},
        0);

    co_return;
  }

  static std::unique_ptr<ExpressionBuilder> expressionBuilder(
      std::shared_ptr<const entities::Universe> universe) {
    return std::make_unique<ExpressionBuilder>(
        universe,
        entities::
            Map<entities::ContainerId, std::vector<entities::ObjectId>>{});
  }
};

CO_TEST_F(
    BalanceWithZeroCapacityContainersTest,
    ZeroCapacityContainerAverageUtilizationTest) {
  co_await setUpUniverseWithZeroCapacityContainers();

  interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.fixAverageToInitial() = true;
  balanceSpec.includeInInitialAverage() = {"host3"};

  const auto universe = buildUniverse();
  const BalanceSpecBuilder specBuilder(universe, balanceSpec, true);

  // This will fail if we do not skip the zero capacity container
  const auto exprBuilder = expressionBuilder(universe);
  auto goal = co_await specBuilder.goalCoro(*exprBuilder);

  EXPECT_NEAR(0.13333333333333333, evaluate(goal, deltaFromInitial({})), 1e-8);
}

CO_TEST_F(BalanceSpecBuilderTest, ZeroInitialUtilizationTest) {
  // Create a universe with containers but no objects initially
  setUpUniverse({
      {"host0", {}},
      {"host1", {}},
      {"host2", {}},
      {"host3", {}},
  });

  // Set up dimension with no objects (empty objValues map)
  co_await addObjectDimension(
      "cpu", entities::Map<entities::ObjectId, double>{}, 1);

  co_await addScopeDimension(
      "cpu",
      host(),
      {{"host0", 100}, {"host1", 100}, {"host2", 100}, {"host3", 100}},
      0);

  // Create BalanceSpec with LEGACY formula to trigger the untested code path
  interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = interface::BalanceSpecFormula::LEGACY;
  upperBounds(balanceSpec).globalLimit() = 1.5;

  const BalanceSpecBuilder specBuilder(buildUniverse(), balanceSpec, true);
  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // When initialUtil is zero, the method should return const_expr(-upperBound,
  // universe_) So we expect the goal to evaluate to -1.5
  EXPECT_NEAR(-1.5, evaluate(goal, deltaFromInitial({})), 1e-8);
}

CO_TEST_F(BalanceSpecBuilderTest, IdealFormulaWithRelativeBoundIgnored) {
  co_await setUpExampleUniverse();

  interface::BalanceSpec balanceSpec;
  balanceSpec.name() = "test";
  balanceSpec.scope() = "rack";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = interface::BalanceSpecFormula::IDEAL;
  balanceSpec.ignoreUpperBoundForIdealWithAbsOrRelBoundTypes() = true;

  const auto universe = buildUniverse();
  const BalanceSpecBuilder specBuilder(
      universe, balanceSpec, /*continuousExpressions=*/true);

  const entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
      updates = {
          {universe->getContainerId("host1"),
           {task(0), task(1), task(2), task(3), task(4), task(5)}},
          {universe->getContainerId("host2"), {task(6), task(7), task(8)}},
          {universe->getContainerId("host3"), {task(9), task(10), task(11)}},
      };

  auto goal = co_await specBuilder.goalCoro(expressionBuilder());
  // IDEAL formula computes penalty for each scopeItem as:
  //   penalty = (absUtil / capacity)^2 * (capacity / avgCapacity)
  //           = absUtil^2 / (capacity * avgCapacity)
  //
  // For this test with IGNORE bound type (no threshold applied):
  // - rack0: absUtil = 60, capacity = 300
  // - rack1: absUtil = 60, capacity = 200
  // - sumCapacity = 500, avgCapacity = 250
  //
  // rack0 penalty = (60/300)^2 * (300/250) = 0.04 * 1.2 = 0.048
  // rack1 penalty = (60/200)^2 * (200/250) = 0.09 * 0.8 = 0.072
  // total = 0.048 + 0.072 = 0.12
  EXPECT_NEAR(0.12, evaluate(goal, updates), 1e-8);
}

CO_TEST_F(BalanceSpecBuilderTest, RelativeUtilVarianceFormula) {
  // Example universe: rack capacities 300 vs 200.
  co_await setUpExampleUniverse();

  const auto universe = buildUniverse();

  const entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
      updates = {
          {universe->getContainerId("host1"),
           {task(0), task(1), task(2), task(3), task(4), task(5)}},
          {universe->getContainerId("host2"), {task(6), task(7), task(8)}},
          {universe->getContainerId("host3"), {task(9), task(10), task(11)}},
      };

  // IDEAL: penalty = sum(relUtil^2 * cap/avgCap) = 0.048 + 0.072 = 0.12
  {
    interface::BalanceSpec idealSpec;
    idealSpec.scope() = "rack";
    idealSpec.dimension() = "cpu";
    idealSpec.formula() = interface::BalanceSpecFormula::IDEAL;
    idealSpec.ignoreUpperBoundForIdealWithAbsOrRelBoundTypes() = true;
    const BalanceSpecBuilder idealBuilder(universe, idealSpec, true);
    auto idealGoal = co_await idealBuilder.goalCoro(expressionBuilder());
    EXPECT_NEAR(0.12, evaluate(idealGoal, updates), 1e-8);
  }

  // RELATIVE_UTIL_VARIANCE: sum(relUtil^2) - sum(relUtil)^2/n = 0.13 - 0.125 =
  // 0.005
  // Set upperBound to 0 so clamping is a no-op (relUtils are 0.2, 0.3).
  {
    interface::BalanceSpec pureSpec;
    pureSpec.scope() = "rack";
    pureSpec.dimension() = "cpu";
    pureSpec.formula() = interface::BalanceSpecFormula::RELATIVE_UTIL_VARIANCE;
    pureSpec.boundType() = interface::BalanceSpecBoundType::RELATIVE_UTIL;
    upperBounds(pureSpec).globalLimit() = 0.0;
    const BalanceSpecBuilder pureBuilder(universe, pureSpec, true);
    auto pureGoal = co_await pureBuilder.goalCoro(expressionBuilder());
    EXPECT_NEAR(0.005, evaluate(pureGoal, updates), 1e-8);
  }

  // Different penalties (0.12 vs 0.005) confirm formulas are not equivalent.
}

CO_TEST_F(
    BalanceSpecBuilderTest,
    RelativeUtilVarianceFormulaWithRelativeUtilBound) {
  co_await setUpDefaultUniverse();

  interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = interface::BalanceSpecFormula::RELATIVE_UTIL_VARIANCE;
  balanceSpec.boundType() = interface::BalanceSpecBoundType::RELATIVE_UTIL;
  upperBounds(balanceSpec).globalLimit() = 0.15;

  const auto universe = buildUniverse();
  const BalanceSpecBuilder specBuilder(universe, balanceSpec, true);

  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // Clamped relUtils (bound=0.15): 0.2, 0.15, 0.15, 0.15
  // penalty = 0.1075 - 0.65^2/4 = 0.001875
  EXPECT_NEAR(0.001875, evaluate(goal, deltaFromInitial({})), 1e-8);
}

CO_TEST_F(
    BalanceSpecBuilderTest,
    RelativeUtilVarianceFormulaWithRelativeBound) {
  co_await setUpDefaultUniverse();

  interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = interface::BalanceSpecFormula::RELATIVE_UTIL_VARIANCE;
  balanceSpec.boundType() = interface::BalanceSpecBoundType::RELATIVE;
  upperBounds(balanceSpec).globalLimit() = 1.5;

  const auto universe = buildUniverse();
  const BalanceSpecBuilder specBuilder(universe, balanceSpec, true);

  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // relUtils: host0=0.2, host1=0.1, host2=0.1, host3=0.0; avgUtil = 0.1
  // RELATIVE bound=1.5 → threshold = 1.5 * 0.1 = 0.15
  // Clamped relUtils: max(0, r-0.15)+0.15 → 0.2, 0.15, 0.15, 0.15
  // Same as RELATIVE_UTIL=0.15 → penalty = 0.001875
  EXPECT_NEAR(0.001875, evaluate(goal, deltaFromInitial({})), 1e-8);
}

CO_TEST_F(
    BalanceSpecBuilderTest,
    RelativeUtilVarianceFormulaWithAbsoluteBound) {
  co_await setUpDefaultUniverse();

  interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = interface::BalanceSpecFormula::RELATIVE_UTIL_VARIANCE;
  balanceSpec.boundType() = interface::BalanceSpecBoundType::ABSOLUTE;
  upperBounds(balanceSpec).globalLimit() = 0.05;

  const auto universe = buildUniverse();
  const BalanceSpecBuilder specBuilder(universe, balanceSpec, true);

  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // relUtils: host0=0.2, host1=0.1, host2=0.1, host3=0.0; avgUtil = 0.1
  // ABSOLUTE bound=0.05 → threshold = 0.1 + 0.05 = 0.15
  // Clamped relUtils: max(0, r-0.15)+0.15 → 0.2, 0.15, 0.15, 0.15
  // Same as RELATIVE_UTIL=0.15 → penalty = 0.001875
  EXPECT_NEAR(0.001875, evaluate(goal, deltaFromInitial({})), 1e-8);
}

CO_TEST_F(
    BalanceSpecBuilderTest,
    RelativeUtilVarianceUsesScopeItemUpperBounds) {
  co_await setUpDefaultUniverse();

  interface::BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.formula() = interface::BalanceSpecFormula::RELATIVE_UTIL_VARIANCE;
  spec.boundType() = interface::BalanceSpecBoundType::RELATIVE_UTIL;
  auto& limits = upperBounds(spec);
  limits.globalLimit() = 0;
  limits.scopeItemLimits() = {{"host0", 0.1}};

  const BalanceSpecBuilder builder(buildUniverse(), spec, true);
  auto goal = co_await builder.goalCoro(expressionBuilder());

  // Relative utilizations are {0.2, 0.1, 0.1, 0}, so excess above each
  // scope item's limit is {0.1, 0.1, 0.1, 0}; n * variance is 0.0075.
  EXPECT_NEAR(0.0075, evaluate(goal, deltaFromInitial({})), 1e-8);
}

CO_TEST_F(BalanceSpecBuilderTest, CapacityPerItemFormula) {
  co_await setUpExampleUniverse();

  interface::BalanceSpec balanceSpec;
  balanceSpec.name() = "test";
  balanceSpec.scope() = "rack";
  balanceSpec.dimension() = "cpu";
  balanceSpec.balanceMetric() = interface::BalanceSpecMetric::CAPACITY_PER_ITEM;

  const auto universe = buildUniverse();
  const BalanceSpecBuilder specBuilder(universe, balanceSpec, true);

  const entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
      updates = {
          {universe->getContainerId("host1"),
           {task(0), task(1), task(2), task(3), task(4), task(5)}},
          {universe->getContainerId("host2"), {task(6), task(7), task(8)}},
          {universe->getContainerId("host3"), {task(9), task(10), task(11)}},
      };

  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // Evaluate directly: quotient creates a non-linear expression that
  // cannot be verified via LP solvers.
  const Assignment assignment(updates);
  Context context;
  auto value = goal->fullApply(TopToBottomEvaluator(context), assignment);

  // rack0: 6 objects (cpu=10 each), absUtil = 60 → capPerItem = 60/6 = 10
  // rack1: 6 objects (cpu=10 each), absUtil = 60 → capPerItem = 60/6 = 10
  // avgCapPerItem = 10, threshold = avgCapPerItem = 10
  // sum_over_threshold = max(0, 10 - 10) + max(0, 10 - 10) = 0
  // result = 0 / 2 = 0.0
  EXPECT_NEAR(0.0, value, 1e-8);
}

CO_TEST_F(BalanceSpecBuilderTest, CapacityPerItemFormulaBalanced) {
  co_await setUpDefaultUniverse();

  interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.balanceMetric() = interface::BalanceSpecMetric::CAPACITY_PER_ITEM;

  const BalanceSpecBuilder specBuilder(buildUniverse(), balanceSpec, true);
  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // Evaluate directly: quotient creates a non-linear expression that
  // cannot be verified via LP solvers.
  auto updates = deltaFromInitial({{"task2", "host3"}});
  const Assignment assignment(updates);
  Context context;
  auto value = goal->fullApply(TopToBottomEvaluator(context), assignment);

  // After moving task2 to host3: each host has 1 task (cpu=10)
  // capPerItem = absUtil/numObjects = 10/1 = 10 for all → avgCapPerItem = 10
  // threshold = avgCapPerItem = 10
  // All values at threshold → sum_over_threshold = 0, result = 0
  EXPECT_NEAR(0.0, value, 1e-8);
}

CO_TEST_F(BalanceSpecBuilderTest, CapacityPerItemFormulaImbalanced) {
  // 10 heavy tasks (cpu=10) on host0, 10 light tasks (cpu=1) on host1.
  // This tests that the formula correctly penalizes unequal avg demand.
  entities::Map<std::string, std::vector<std::string>> assignment;
  for (const auto i : folly::irange(10)) {
    assignment["host0"].push_back(fmt::format("task{}", i));
  }
  for (const auto i : folly::irange(10, 20)) {
    assignment["host1"].push_back(fmt::format("task{}", i));
  }
  setUpUniverse(assignment);

  entities::Map<entities::ObjectId, double> objValues;
  for (const auto i : folly::irange(10)) {
    objValues[task(i)] = 10;
  }
  for (const auto i : folly::irange(10, 20)) {
    objValues[task(i)] = 1;
  }
  co_await addObjectDimension("cpu", objValues, 1);

  co_await addScopeDimension(
      "cpu", host(), {{"host0", 1000}, {"host1", 1000}}, 0);

  interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.balanceMetric() = interface::BalanceSpecMetric::CAPACITY_PER_ITEM;

  const BalanceSpecBuilder specBuilder(buildUniverse(), balanceSpec, true);
  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  const Assignment assign(deltaFromInitial({}));
  Context context;
  auto value = goal->fullApply(TopToBottomEvaluator(context), assign);

  // host0: 10 heavy (cpu=10), absUtil = 100, numObjects = 10 → capPerItem = 10
  // host1: 10 light (cpu=1), absUtil = 10, numObjects = 10 → capPerItem = 1
  // avgCapPerItem = (10 + 1) / 2 = 5.5, threshold = 5.5
  // sum_over_threshold = max(0, 10 - 5.5) + max(0, 1 - 5.5) = 4.5 + 0 = 4.5
  // LINEAR result = 4.5 / 2 = 2.25
  EXPECT_NEAR(2.25, value, 1e-8);
}

CO_TEST_F(BalanceSpecBuilderTest, CapacityPerItemSquaresFormula) {
  // Same imbalanced scenario with SQUARES formula.
  entities::Map<std::string, std::vector<std::string>> assignment;
  for (const auto i : folly::irange(10)) {
    assignment["host0"].push_back(fmt::format("task{}", i));
  }
  for (const auto i : folly::irange(10, 20)) {
    assignment["host1"].push_back(fmt::format("task{}", i));
  }
  setUpUniverse(assignment);

  entities::Map<entities::ObjectId, double> objValues;
  for (const auto i : folly::irange(10)) {
    objValues[task(i)] = 10;
  }
  for (const auto i : folly::irange(10, 20)) {
    objValues[task(i)] = 1;
  }
  co_await addObjectDimension("cpu", objValues, 1);

  co_await addScopeDimension(
      "cpu", host(), {{"host0", 1000}, {"host1", 1000}}, 0);

  interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.balanceMetric() = interface::BalanceSpecMetric::CAPACITY_PER_ITEM;
  balanceSpec.formula() = interface::BalanceSpecFormula::SQUARES;
  balanceSpec.boundType() = interface::BalanceSpecBoundType::RELATIVE;
  upperBounds(balanceSpec).globalLimit() = 1.0;

  const BalanceSpecBuilder specBuilder(buildUniverse(), balanceSpec, true);
  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  const Assignment assign(deltaFromInitial({}));
  Context context;
  auto value = goal->fullApply(TopToBottomEvaluator(context), assign);

  // capPerItems: 10, 1; avgCapPerItem = 5.5; threshold = 5.5
  // SQUARES applies power(x, 1.1) transform.
  // transformedThreshold = power(5.5, 1.1)
  // transformedUtils = power(10, 1.1), power(1, 1.1)
  // sum_over_threshold(transformedThreshold, transformedUtils) / 2
  const double threshold = std::pow(5.5, 1.1);
  const double t10 = std::pow(10, 1.1);
  const double t1 = std::pow(1, 1.1);
  const double expected =
      (std::max(0.0, t10 - threshold) + std::max(0.0, t1 - threshold)) / 2;
  EXPECT_NEAR(expected, value, 1e-6);
}

CO_TEST_F(BalanceSpecBuilderTest, CapacityPerItemMaxFormula) {
  // Same imbalanced scenario with MAX formula.
  entities::Map<std::string, std::vector<std::string>> assignment;
  for (const auto i : folly::irange(10)) {
    assignment["host0"].push_back(fmt::format("task{}", i));
  }
  for (const auto i : folly::irange(10, 20)) {
    assignment["host1"].push_back(fmt::format("task{}", i));
  }
  setUpUniverse(assignment);

  entities::Map<entities::ObjectId, double> objValues;
  for (const auto i : folly::irange(10)) {
    objValues[task(i)] = 10;
  }
  for (const auto i : folly::irange(10, 20)) {
    objValues[task(i)] = 1;
  }
  co_await addObjectDimension("cpu", objValues, 1);

  co_await addScopeDimension(
      "cpu", host(), {{"host0", 1000}, {"host1", 1000}}, 0);

  interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.balanceMetric() = interface::BalanceSpecMetric::CAPACITY_PER_ITEM;
  balanceSpec.formula() = interface::BalanceSpecFormula::MAX;

  const BalanceSpecBuilder specBuilder(buildUniverse(), balanceSpec, true);
  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  const Assignment assign(deltaFromInitial({}));
  Context context;
  auto value = goal->fullApply(TopToBottomEvaluator(context), assign);

  // capPerItems: 10, 1; avgCapPerItem = 5.5; threshold = 5.5
  // MAX = max(max(10, 1) - 5.5, 0) = max(4.5, 0) = 4.5
  EXPECT_NEAR(4.5, value, 1e-8);
}

CO_TEST_F(BalanceSpecBuilderTest, CapacityPerItemVarianceFormula) {
  // Same imbalanced scenario with RELATIVE_UTIL_VARIANCE formula.
  entities::Map<std::string, std::vector<std::string>> assignment;
  for (const auto i : folly::irange(10)) {
    assignment["host0"].push_back(fmt::format("task{}", i));
  }
  for (const auto i : folly::irange(10, 20)) {
    assignment["host1"].push_back(fmt::format("task{}", i));
  }
  setUpUniverse(assignment);

  entities::Map<entities::ObjectId, double> objValues;
  for (const auto i : folly::irange(10)) {
    objValues[task(i)] = 10;
  }
  for (const auto i : folly::irange(10, 20)) {
    objValues[task(i)] = 1;
  }
  co_await addObjectDimension("cpu", objValues, 1);

  co_await addScopeDimension(
      "cpu", host(), {{"host0", 1000}, {"host1", 1000}}, 0);

  interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.balanceMetric() = interface::BalanceSpecMetric::CAPACITY_PER_ITEM;
  balanceSpec.formula() = interface::BalanceSpecFormula::RELATIVE_UTIL_VARIANCE;
  balanceSpec.boundType() = interface::BalanceSpecBoundType::RELATIVE_UTIL;
  upperBounds(balanceSpec).globalLimit() = 0.0;

  const BalanceSpecBuilder specBuilder(buildUniverse(), balanceSpec, true);
  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  const Assignment assign(deltaFromInitial({}));
  Context context;
  auto value = goal->fullApply(TopToBottomEvaluator(context), assign);

  // capPerItems: 10, 1; sum = 11, sumSquared = 100 + 1 = 101
  // variance*n = 101 - 11^2/2 = 101 - 60.5 = 40.5
  EXPECT_NEAR(40.5, value, 1e-8);
}

} // namespace facebook::rebalancer::materializer::tests
