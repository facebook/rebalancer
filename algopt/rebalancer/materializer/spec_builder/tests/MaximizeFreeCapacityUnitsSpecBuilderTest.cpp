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
#include "algopt/rebalancer/materializer/spec_builder/MaximizeFreeCapacityUnitsSpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"

#include <folly/container/irange.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::materializer::tests {
namespace {

enum class SolverType { LOCAL_SEARCH, OPTIMAL_SOLVER };

struct SolverValues {
  double localSearch;
  double optimalSolver;
};

interface::MaximizeFreeCapacityUnitsSpec makeSpec(double unitSize) {
  interface::MaximizeFreeCapacityUnitsSpec spec;
  spec.name() = "free-capacity-units";
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.unitSize()->type() = interface::LimitType::ABSOLUTE;
  spec.unitSize()->globalLimit() = unitSize;
  spec.filter()->itemsBlacklist() = {"host2"};
  return spec;
}

} // namespace

class MaximizeFreeCapacityUnitsSpecBuilderTest
    : public SpecBuilderTestBase<SolverType> {
 protected:
  static bool continuousExpressions() {
    switch (GetParam()) {
      case SolverType::LOCAL_SEARCH:
        return true;
      case SolverType::OPTIMAL_SOLVER:
        return false;
    }
    throw std::runtime_error("Unknown solver type");
  }

  static double expectedValue(const SolverValues& values) {
    switch (GetParam()) {
      case SolverType::LOCAL_SEARCH:
        return values.localSearch;
      case SolverType::OPTIMAL_SOLVER:
        return values.optimalSolver;
    }
    throw std::runtime_error("Unknown solver type");
  }

  static void
  verifyExpectedValue(const SolverValues& expected, double actual, int line) {
    const testing::ScopedTrace trace(__FILE__, line, /*message=*/"");
    EXPECT_DOUBLE_EQ(expectedValue(expected), actual);
  }

  folly::coro::Task<void> setUpCoro() {
    entities::Map<std::string, std::vector<std::string>> assignment = {
        {"host0", {}}, {"host1", {}}, {"host2", {}}};
    for (const auto i : folly::irange(18)) {
      assignment["host0"].push_back(fmt::format("task{}", i));
    }
    for (const auto i : folly::irange(18, 48)) {
      assignment["host1"].push_back(fmt::format("task{}", i));
    }
    for (const auto i : folly::irange(48, 56)) {
      assignment["host2"].push_back(fmt::format("task{}", i));
    }
    setUpUniverse(assignment);

    co_await addObjectDimension(
        "cpu",
        /*objectToValue=*/entities::Map<entities::ObjectId, double>{},
        /*defaultValue=*/1);
    co_await addScopeDimension(
        "cpu",
        scopeId("host"),
        /*scopeItemNameToValue=*/{},
        /*defaultValue=*/32);
  }

  void SetUp() override {
    folly::coro::blockingWait(setUpCoro());
  }
};

INSTANTIATE_TEST_SUITE_P(
    OptimalSolver,
    MaximizeFreeCapacityUnitsSpecBuilderTest,
    testing::Values(SolverType::OPTIMAL_SOLVER));

INSTANTIATE_TEST_SUITE_P(
    LocalSearch,
    MaximizeFreeCapacityUnitsSpecBuilderTest,
    testing::Values(SolverType::LOCAL_SEARCH));

CO_TEST_P(
    MaximizeFreeCapacityUnitsSpecBuilderTest,
    ContinuousPenaltyGuidesOnlyLocalSearchTowardNextAvailableUnit) {
  const auto universe = buildUniverse();
  const MaximizeFreeCapacityUnitsSpecBuilder builder(
      universe, makeSpec(/*unitSize=*/16), continuousExpressions());
  const auto goal = co_await builder.goalCoro(expressionBuilder());

  // Moving task0 from host0 or task18 from host1 leaves the discrete objective
  // unchanged. Because host0 needs 2 more capacity units to make another unit
  // available while host1 needs 14, the penalty makes Local Search prefer
  // moving task0.
  const auto moveFromCloser =
      evaluate(goal, deltaFromInitial({{"task0", "host2"}}));
  const auto moveFromFarther =
      evaluate(goal, deltaFromInitial({{"task18", "host2"}}));
  switch (GetParam()) {
    case SolverType::LOCAL_SEARCH:
      EXPECT_LT(moveFromCloser, moveFromFarther);
      break;
    case SolverType::OPTIMAL_SOLVER:
      EXPECT_DOUBLE_EQ(moveFromCloser, moveFromFarther);
      break;
  }

  // Moving both tasks makes another unit available on host0. The penalty is
  // below one objective unit, so it cannot outweigh this discrete improvement.
  const auto makeAnotherUnitAvailable = evaluate(
      goal, deltaFromInitial({{"task0", "host2"}, {"task1", "host2"}}));
  EXPECT_LT(makeAnotherUnitAvailable, moveFromCloser);

  // When adding load, Local Search preserves host0's greater progress toward
  // making another unit available by consuming capacity on host1 instead.
  const auto addToCloser =
      evaluate(goal, deltaFromInitial({{"task48", "host0"}}));
  const auto addToFarther =
      evaluate(goal, deltaFromInitial({{"task48", "host1"}}));
  switch (GetParam()) {
    case SolverType::LOCAL_SEARCH:
      EXPECT_LT(addToFarther, addToCloser);
      break;
    case SolverType::OPTIMAL_SOLVER:
      EXPECT_DOUBLE_EQ(addToFarther, addToCloser);
      break;
  }
}

CO_TEST_P(
    MaximizeFreeCapacityUnitsSpecBuilderTest,
    HeterogeneousCapacitiesWithOneAvailableUnitHaveEqualObjectives) {
  entities::Map<entities::ObjectId, double> objectValues;
  for (const auto i : folly::irange(8)) {
    objectValues[objectId(fmt::format("task{}", i))] = 1;
  }
  objectValues[objectId("task18")] = 1;
  for (const auto i : folly::irange(48, 56)) {
    objectValues[objectId(fmt::format("task{}", i))] = 1;
  }
  co_await addObjectDimension(
      "heterogeneous", objectValues, /*defaultValue=*/0);
  co_await addScopeDimension(
      "heterogeneous",
      scopeId("host"),
      /*scopeItemNameToValue=*/{{"host0", 8}, {"host1", 9}},
      /*defaultValue=*/100);

  auto spec = makeSpec(/*unitSize=*/8);
  spec.dimension() = "heterogeneous";
  const MaximizeFreeCapacityUnitsSpecBuilder builder(
      buildUniverse(), spec, continuousExpressions());
  const auto goal = co_await builder.goalCoro(expressionBuilder());

  // Initially, host0 is full at 8/8 and host1 is at 1/9, leaving one
  // available unit on host1. Scenario two moves host0's 8 units out and 8
  // units into host1, leaving host0 empty and host1 full. Both scenarios have
  // one available unit.
  entities::Map<std::string, std::string> scenarioTwo;
  for (const auto i : folly::irange(8)) {
    scenarioTwo[fmt::format("task{}", i)] = "host2";
  }
  for (const auto i : folly::irange(48, 56)) {
    scenarioTwo[fmt::format("task{}", i)] = "host1";
  }

  EXPECT_DOUBLE_EQ(
      evaluate(goal, deltaFromInitial({})),
      evaluate(goal, deltaFromInitial(scenarioTwo)));
}

CO_TEST_P(
    MaximizeFreeCapacityUnitsSpecBuilderTest,
    GoalUsesPerScopeItemUnitSizes) {
  auto spec = makeSpec(/*unitSize=*/16);
  spec.unitSize()->scopeItemLimits() = {{"host0", 8}};
  const MaximizeFreeCapacityUnitsSpecBuilder builder(
      buildUniverse(), spec, continuousExpressions());
  const auto goal = co_await builder.goalCoro(expressionBuilder());

  // host0: floor(32/8) - floor((32-18)/8) = 3 unavailable units.
  // host1: floor(32/16) - floor((32-30)/16) = 2 unavailable units.
  const auto value = evaluate(goal, deltaFromInitial({}));
  const auto expectedPenalty =
      0.99 / 2 * (std::pow(0.25, 0.9) + std::pow(0.875, 0.9));
  verifyExpectedValue(
      {.localSearch = 5 + expectedPenalty, .optimalSolver = 5},
      value,
      __LINE__);
}

TEST_P(
    MaximizeFreeCapacityUnitsSpecBuilderTest,
    GoalRejectsNonPositiveGlobalUnitSize) {
  const MaximizeFreeCapacityUnitsSpecBuilder builder(
      buildUniverse(), makeSpec(/*unitSize=*/0), continuousExpressions());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      folly::coro::blockingWait(builder.goalCoro(expressionBuilder())),
      "MaximizeFreeCapacityUnitsSpec unit size for scope item 'host0' must be finite and positive, but got 0");
}

TEST_P(
    MaximizeFreeCapacityUnitsSpecBuilderTest,
    GoalRejectsNonPositiveScopeItemUnitSize) {
  auto spec = makeSpec(/*unitSize=*/16);
  spec.unitSize()->scopeItemLimits() = {{"host0", -1}};
  const MaximizeFreeCapacityUnitsSpecBuilder builder(
      buildUniverse(), spec, continuousExpressions());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      folly::coro::blockingWait(builder.goalCoro(expressionBuilder())),
      "MaximizeFreeCapacityUnitsSpec unit size for scope item 'host0' must be finite and positive, but got -1");
}

CO_TEST_P(
    MaximizeFreeCapacityUnitsSpecBuilderTest,
    ConstructorRejectsNonScalarDimension) {
  co_await addObjectDimension(
      "multi",
      /*values=*/
      std::vector<entities::Map<entities::ObjectId, double>>{{}, {}},
      /*defaultValues=*/
      std::vector<double>{1, 1});
  co_await addScopeDimension(
      "multi",
      scopeId("host"),
      /*scopeItemNameToValue=*/{},
      /*defaultValue=*/32);
  auto spec = makeSpec(/*unitSize=*/16);
  spec.dimension() = "multi";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      (void)MaximizeFreeCapacityUnitsSpecBuilder(
          buildUniverse(), spec, continuousExpressions()),
      "MaximizeFreeCapacityUnitsSpecBuilder is not supported with non-scalar dimensions");
}

CO_TEST_P(
    MaximizeFreeCapacityUnitsSpecBuilderTest,
    ConstructorRejectsNegativeDimensionValues) {
  co_await addObjectDimension(
      "negative",
      /*objectNameToValue=*/entities::Map<std::string, double>{{"task0", -1}},
      /*defaultValue=*/1);
  co_await addScopeDimension(
      "negative",
      scopeId("host"),
      /*scopeItemNameToValue=*/{},
      /*defaultValue=*/32);
  auto spec = makeSpec(/*unitSize=*/16);
  spec.dimension() = "negative";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      (void)MaximizeFreeCapacityUnitsSpecBuilder(
          buildUniverse(), spec, continuousExpressions()),
      "MaximizeFreeCapacityUnitsSpecBuilder is not supported with negative dimension values");
}

TEST_P(MaximizeFreeCapacityUnitsSpecBuilderTest, ConstraintIsUnsupported) {
  const MaximizeFreeCapacityUnitsSpecBuilder builder(
      buildUniverse(), makeSpec(/*unitSize=*/16), continuousExpressions());

  REBALANCER_EXPECT_RUNTIME_ERROR(
      folly::coro::blockingWait(builder.constraints(expressionBuilder())),
      "MaximizeFreeCapacityUnitsSpec is not supported as a constraint");
}

} // namespace facebook::rebalancer::materializer::tests
