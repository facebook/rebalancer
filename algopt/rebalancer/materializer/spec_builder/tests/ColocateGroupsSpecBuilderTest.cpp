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

#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/materializer/spec_builder/ColocateGroupsSpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"

#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::materializer::tests {

class ColocateGroupsSpecBuilderTest : public SpecBuilderTestBase<> {
 protected:
  folly::coro::Task<void> setUpBasicUniverse() {
    setUpUniverse(
        {{"host0", {"task0", "task1"}},
         {"host1", {"task2"}},
         {"host2", {"task3", "task4"}}});

    co_await addPartition(
        "job",
        {{"job1", {"task0", "task1"}},
         {"job2", {"task2"}},
         {"job3", {"task3", "task4"}}});

    co_return;
  }

  entities::ScopeId host() const {
    return scopeId("host");
  }
};

INSTANTIATE_TEST_CASE_P(
    WithContinuousExpressions,
    ColocateGroupsSpecBuilderTest,
    ::testing::Values(true));

INSTANTIATE_TEST_CASE_P(
    WithoutContinuousExpressions,
    ColocateGroupsSpecBuilderTest,
    ::testing::Values(false));

CO_TEST_P(ColocateGroupsSpecBuilderTest, Constraint) {
  co_await setUpBasicUniverse();

  interface::ColocateGroupsSpec spec;
  spec.name() = "test";
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.limits()->globalLimit() = 1;

  const ColocateGroupsSpecBuilder specBuilder(
      buildUniverse(), spec, GetParam());

  EXPECT_EQ(
      "colocate groups in partition 'job' across scope items in scope 'host' w.r.t. dimension 'task_count' and when using bound = MAX",
      specBuilder.description());

  auto constraint = co_await specBuilder.constraints(expressionBuilder());
  // all objects of the partition are in same host hence all expr will evaluate
  // to 0
  EXPECT_NEAR(0, evaluate(constraint.at(0), deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(0, evaluate(constraint.at(1), deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(0, evaluate(constraint.at(2), deltaFromInitial({})), 1e-8);

  // with this move, job1 will have tasks in 2 hosts hence value will be 1
  EXPECT_NEAR(
      1,
      evaluate(constraint.at(0), deltaFromInitial({{"task1", "host1"}})),
      1e-8);

  // this should be fine as both tasks for job1 moved to host1
  EXPECT_NEAR(
      0,
      evaluate(
          constraint.at(0),
          deltaFromInitial({{"task1", "host1"}, {"task0", "host1"}})),
      1e-8);
  EXPECT_NEAR(
      0,
      evaluate(
          constraint.at(2),
          deltaFromInitial({{"task1", "host1"}, {"task0", "host1"}})),
      1e-8);
}

CO_TEST_P(ColocateGroupsSpecBuilderTest, Goal) {
  co_await setUpBasicUniverse();
  const auto universe = buildUniverse();
  auto& exprBuilder = expressionBuilder();

  interface::ColocateGroupsSpec spec;
  spec.name() = "test";
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.limits()->globalLimit() = 1;

  ColocateGroupsSpecBuilder specBuilder(universe, spec, GetParam());
  auto goal = co_await specBuilder.goalCoro(exprBuilder);

  auto assertOptions = packer::tests::LpAssertOptions{
      .lpTolerances =
          algopt::lp::Tolerances{.constraint = 1e-7, .integer = 1e-6}};
  EXPECT_NEAR(0, evaluate(goal, deltaFromInitial({}), assertOptions), 1e-8);

  // value should be 2 as we are job1 and job3 have tasks in more than 1 host
  // Both job1 and job3 has 2 tasks, normalized util per task = 0.5
  // linear penalty = 0.5 + 0.5 = 1
  // quadratic penalty = 0.5^2 + 0.5^2 = 0.5
  // additionalPenalty = 1 - 0.1 * 0.5 = 0.95
  auto continuousPenalty = GetParam();
  const double additionalPenaltyJob1 = continuousPenalty ? 0.95 : 0;
  const double additionalPenaltyJob3 = additionalPenaltyJob1;
  EXPECT_NEAR(
      2 + additionalPenaltyJob1 + additionalPenaltyJob3,
      evaluate(
          goal,
          deltaFromInitial({{"task1", "host1"}, {"task4", "host1"}}),
          assertOptions),
      1e-8);

  spec.limits()->globalLimit() = 2;
  goal =
      co_await ColocateGroupsSpecBuilder(universe, spec).goalCoro(exprBuilder);
  // value should be 0 as we allow objects in partition to span across 2 scope
  // items
  EXPECT_NEAR(
      0,
      evaluate(
          goal, deltaFromInitial({{"task1", "host1"}, {"task4", "host1"}})),
      1e-8);
}

CO_TEST_P(ColocateGroupsSpecBuilderTest, DifferentWeight) {
  co_await setUpBasicUniverse();
  const auto universe = buildUniverse();
  auto& exprBuilder = expressionBuilder();

  interface::ColocateGroupsSpec spec;
  spec.name() = "test";
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.limits()->globalLimit() = 1;
  spec.scopeItemWeights() = {{"host0", 2}};

  const ColocateGroupsSpecBuilder specBuilder(universe, spec, GetParam());
  auto constraint = co_await specBuilder.constraints(exprBuilder);
  const auto& job1Constraint = constraint.at(0);
  // weight for host0 is 2 hence value will be (1 + 2 - 1 /*limit*/) = 2
  // Both job1 and job3 has 2 tasks, normalized util per task = 0.5
  // if job1's tasks are in host0 and host1
  // linear penalty = 0.5 + 2 * 0.5 = 1.5
  // quadratic penalty = 0.5^2 + 2 * 0.5^2 = 0.75
  // additionalPenalty = 1.5 - 0.1 * 0.75 = 1.425
  auto continuousPenalty = GetParam();
  const double additionalPenaltyJob1 = continuousPenalty ? 1.425 : 0;
  EXPECT_NEAR(
      2,
      evaluate(
          job1Constraint.constraintExpr,
          deltaFromInitial({{"task1", "host1"}})),
      1e-8);

  auto assertOptions = packer::tests::LpAssertOptions{
      .lpTolerances =
          algopt::lp::Tolerances{.constraint = 1e-7, .integer = 1e-6}};
  if (continuousPenalty) {
    EXPECT_NEAR(
        additionalPenaltyJob1,
        evaluate(
            job1Constraint.additionalPenaltyExpr,
            deltaFromInitial({{"task1", "host1"}}),
            assertOptions),
        1e-8);
  } else {
    EXPECT_EQ(nullptr, job1Constraint.additionalPenaltyExpr);
  }

  // weight for host2 is not specified hence default 1
  // if job3's tasks are in host2 and host1
  // linear penalty = 0.5 + 0.5 = 1
  // quadratic penalty = 0.5^2 + 0.5^2 = 0.5
  // additionalPenalty = 1 - 0.1 * 0.5 = 0.95
  const auto& job3Constraint = constraint.at(2);
  const double additionalPenaltyJob3 = continuousPenalty ? 0.95 : 0;
  EXPECT_NEAR(
      1,
      evaluate(
          job3Constraint.constraintExpr,
          deltaFromInitial({{"task4", "host1"}})),
      1e-8);
  if (continuousPenalty) {
    EXPECT_NEAR(
        additionalPenaltyJob3,
        evaluate(
            job3Constraint.additionalPenaltyExpr,
            deltaFromInitial({{"task4", "host1"}}),
            assertOptions),
        1e-8);
  } else {
    EXPECT_EQ(nullptr, job3Constraint.additionalPenaltyExpr);
  }

  auto goal = co_await specBuilder.goalCoro(exprBuilder);
  EXPECT_NEAR(
      3 + additionalPenaltyJob1 + additionalPenaltyJob3,
      evaluate(
          goal,
          deltaFromInitial({{"task1", "host1"}, {"task4", "host1"}}),
          assertOptions),
      1e-8);
}

CO_TEST_P(ColocateGroupsSpecBuilderTest, Filter) {
  co_await setUpBasicUniverse();

  interface::ColocateGroupsSpec spec;
  spec.name() = "test";
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.limits()->globalLimit() = 1;

  interface::Filter filter;
  filter.itemsBlacklist() = {"host1", "host2"};
  spec.filter() = filter;

  const ColocateGroupsSpecBuilder specBuilder(
      buildUniverse(), spec, GetParam());
  auto goal = co_await specBuilder.goalCoro(expressionBuilder());
  EXPECT_NEAR(0, evaluate(goal, deltaFromInitial({})), 1e-8);

  // value should be 0 as job1 and job2 have tasks in more than 1 host
  // but host 1 is ignored
  EXPECT_NEAR(
      0,
      evaluate(
          goal, deltaFromInitial({{"task1", "host1"}, {"task4", "host1"}})),
      1e-8);
}

CO_TEST_P(ColocateGroupsSpecBuilderTest, SpecInfo) {
  co_await setUpBasicUniverse();
  // Add network dimension for the second block of this test
  co_await addObjectDimension(
      "network", entities::Map<std::string, double>{}, 0);
  const auto universe = buildUniverse();

  interface::ColocateGroupsSpec spec;
  spec.name() = "test";
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.limits()->globalLimit() = 1;
  {
    const ColocateGroupsSpecBuilder specBuilder(universe, spec, GetParam());
    auto expectedSpecInfo = SpecParameters{
        .name = "test",
        .scope = "host",
        .partition = "job",
        .dimension = "task_count",
        .boundType = "MAX",
        .filterAllowListSize = 0,
        .filterBlockListSize = 0};
    EXPECT_EQ(expectedSpecInfo, specBuilder.getSpecInfo());
  }

  {
    spec.dimension() = "network";
    const ColocateGroupsSpecBuilder specBuilder(universe, spec, GetParam());
    auto expectedSpecInfo = SpecParameters{
        .name = "test",
        .scope = "host",
        .partition = "job",
        .dimension = "network",
        .boundType = "MAX",
        .filterAllowListSize = 0,
        .filterBlockListSize = 0};
    EXPECT_EQ(expectedSpecInfo, specBuilder.getSpecInfo());
  }
}

CO_TEST_P(ColocateGroupsSpecBuilderTest, Dimension) {
  co_await setUpBasicUniverse();
  // all objects except task0 have a value of 2; task0 has a value of 0
  co_await addObjectDimension("weight", {{objectId("task0"), 0.0}}, 2);

  interface::ColocateGroupsSpec spec;
  spec.name() = "test";
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "weight";
  spec.limits()->globalLimit() = 1;

  const ColocateGroupsSpecBuilder specBuilder(
      buildUniverse(), spec, GetParam());
  auto constraint = co_await specBuilder.constraints(expressionBuilder());

  // All objects of the partition are in same host hence all expr will evaluate
  // to 0
  EXPECT_NEAR(0, evaluate(constraint.at(0), deltaFromInitial({})), 1e-8);

  // Moving task0 to another host also should not change the value (since task0
  // has a value of 0 for the dimension). If this was count dimension, we would
  // expect a value to be 1.
  EXPECT_NEAR(
      0,
      evaluate(constraint.at(0), deltaFromInitial({{"task0", "host1"}})),
      1e-8);
}

CO_TEST_P(ColocateGroupsSpecBuilderTest, DynamicDimension) {
  co_await setUpBasicUniverse();
  // task0 has a value of 0 in host2
  // everywhere else all other objects have a value of 1
  co_await addDynamicObjectDimension(
      "dynamicWeight",
      host(),
      {{"host2", makeSharedPtrEntityToValueMap({{objectId("task0"), 0.0}})}},
      1);

  interface::ColocateGroupsSpec spec;
  spec.name() = "test";
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "dynamicWeight";
  spec.limits()->globalLimit() = 1;

  const ColocateGroupsSpecBuilder specBuilder(
      buildUniverse(), spec, GetParam());
  auto constraint = co_await specBuilder.constraints(expressionBuilder());

  // All objects of the partition are in same host hence all expr will evaluate
  // to 0
  EXPECT_NEAR(0, evaluate(constraint.at(0), deltaFromInitial({})), 1e-8);

  // Moving task0 to host2 also should not change the value (since task0
  // has a value of 0 for the dynamic dimension in host2).
  EXPECT_NEAR(
      0,
      evaluate(constraint.at(0), deltaFromInitial({{"task0", "host2"}})),
      1e-8);
  // However moving task0 to host1 should violate the constraint
  auto violation =
      evaluate(constraint.at(0), deltaFromInitial({{"task0", "host1"}}));
  EXPECT_EQ(1, violation);
}

CO_TEST_P(ColocateGroupsSpecBuilderTest, GroupWeight) {
  co_await setUpBasicUniverse();

  interface::ColocateGroupsSpec spec;
  spec.name() = "test";
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.limits()->globalLimit() = 1;
  spec.groupToWeight() = {{"job1", 2.0}, {"job3", 0.5}};

  const ColocateGroupsSpecBuilder specBuilder(
      buildUniverse(), spec, GetParam());
  const auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  const auto assertOptions = packer::tests::LpAssertOptions{
      .lpTolerances =
          algopt::lp::Tolerances{.constraint = 1e-7, .integer = 1e-6}};
  EXPECT_NEAR(0, evaluate(goal, deltaFromInitial({}), assertOptions), 1e-8);

  // Move tasks to create violations for job1 and job3
  // job1 has weight 2.0, job3 has weight 0.5, job2 has default weight 1.0
  // Both job1 and job3 have 2 tasks, normalized util per task = 0.5
  // For job1: excess above limit = 2 - 1 = 1, weighted excess = 1 * 2 = 2
  // For job3:  excess above limit = 2 - 1 = 1, weighted excess = 1 * 0.5 = 0.5
  // Total excess = 2.0 + 0.5 = 2.5
  const double goalValueWithoutPenalty = 2.5;
  const auto continuousPenalty = GetParam();
  if (continuousPenalty) {
    // For job1:
    // quadratic penalty = 0.5^2 + 0.5^2 = 0.5, additionalPenalty without
    // weights = 1.0 - 0.1 *  0.5 = 0.95
    // For job3:
    // quadratic penalty = 0.5^2 + 0.5^2 = 0.5, additionalPenalty without
    // weights = 1.0 - 0.1 * 0.5 = 0.95
    const double weightedAdditionalPenaltyJob1 = 0.95 * 2;
    const double weightedAdditionalPenaltyJob3 = 0.95 * 0.5;
    EXPECT_NEAR(
        goalValueWithoutPenalty + weightedAdditionalPenaltyJob1 +
            weightedAdditionalPenaltyJob3,
        evaluate(
            goal,
            deltaFromInitial({{"task1", "host1"}, {"task4", "host1"}}),
            assertOptions),
        1e-8);
  } else {
    EXPECT_NEAR(
        goalValueWithoutPenalty,
        evaluate(
            goal,
            deltaFromInitial({{"task1", "host1"}, {"task4", "host1"}}),
            assertOptions),
        1e-8);
  }
}
} // namespace facebook::rebalancer::materializer::tests
