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
#include "algopt/rebalancer/materializer/spec_builder/GroupCountSpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"

#include <folly/container/irange.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

#include <set>
#include <utility>

namespace facebook::rebalancer::materializer::tests {

class GroupCountSpecBuilderTest : public SpecBuilderTestBase<> {
 protected:
  folly::coro::Task<void> setUpCoro() {
    setUpUniverse({
        {"host0", {"task0", "task1", "task2", "task3", "task4"}},
        {"host1", {"task5", "task6", "task7", "task8"}},
    });

    co_await addPartition(
        "job",
        {{"job1", {"task0", "task1", "task2", "task3", "task4"}},
         {"job2", {"task5", "task6", "task7", "task8"}}});

    co_return;
  }

  void SetUp() override {
    folly::coro::blockingWait(setUpCoro());
  }

  entities::ObjectId task(int index) const {
    return objectId(fmt::format("task{}", index));
  }

  entities::ScopeId host() const {
    return scopeId("host");
  }
};

CO_TEST_F(GroupCountSpecBuilderTest, GlobalAbsoluteLimit) {
  interface::GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "host";
  groupCountSpec.partitionName() = "job";
  groupCountSpec.dimension() = "task_count";

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  limit.globalLimit() = 3;
  groupCountSpec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), groupCountSpec);

  EXPECT_EQ(
      "Group MAX limit AFTER for job on scope host on dimension task_count",
      specBuilder.description());

  auto constraints = co_await specBuilder.constraints(expressionBuilder());

  auto root = any_positive(constraints);

  // value will be +ve as job has 3+ task on single host
  EXPECT_NEAR(1, evaluate(root, deltaFromInitial({})), 1e-8);
  // value should be 0 as constraint is satisfied.
  EXPECT_NEAR(
      0,
      evaluate(
          root,
          deltaFromInitial(
              {{"task1", "host1"}, {"task2", "host1"}, {"task5", "host0"}})),
      1e-8);
}

CO_TEST_F(GroupCountSpecBuilderTest, GlobalRelativeLimit) {
  interface::GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "host";
  groupCountSpec.partitionName() = "job";
  groupCountSpec.dimension() = "task_count";

  interface::Limit limit;
  limit.type() = interface::LimitType::RELATIVE;
  limit.globalLimit() = 0.7;
  groupCountSpec.limit() = limit;

  const auto universe = buildUniverse();
  const GroupCountSpecBuilder specBuilder(universe, groupCountSpec);
  auto goal = co_await specBuilder.goalCoro(expressionBuilder());
  auto root =
      facebook::rebalancer::any_positive(std::vector<ExprPtr>{goal}, *universe);

  // initially goal will not be satisfied
  EXPECT_NEAR(1, evaluate(root, deltaFromInitial({})), 1e-8);

  // limit is 70% which means max 3 task for job 0 in single host
  // and max 2 task for job 1 in single host
  EXPECT_NEAR(
      1,
      evaluate(
          root,
          deltaFromInitial(
              {{"task1", "host1"}, {"task2", "host1"}, {"task5", "host0"}})),
      1e-8);

  EXPECT_NEAR(
      0,
      evaluate(
          root,
          deltaFromInitial(
              {{"task1", "host1"},
               {"task2", "host1"},
               {"task5", "host0"},
               {"task8", "host0"}})),
      1e-8);
}

CO_TEST_F(GroupCountSpecBuilderTest, MinBound) {
  interface::GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "host";
  groupCountSpec.partitionName() = "job";
  groupCountSpec.dimension() = "task_count";
  groupCountSpec.bound() = interface::GroupCountSpecBound::MIN;

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  limit.globalLimit() = 1;
  groupCountSpec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), groupCountSpec);
  auto constraints = co_await specBuilder.constraints(expressionBuilder());

  auto root = any_positive(constraints);

  // initially constraint is broken
  EXPECT_NEAR(1, evaluate(root, deltaFromInitial({})), 1e-8);

  // each job should have atleast 1 task per host
  EXPECT_NEAR(1, evaluate(root, deltaFromInitial({{"task1", "host1"}})), 1e-8);

  EXPECT_NEAR(
      0,
      evaluate(
          root, deltaFromInitial({{"task1", "host1"}, {"task5", "host0"}})),
      1e-8);
}

CO_TEST_F(GroupCountSpecBuilderTest, MultipleBound) {
  interface::GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "host";
  groupCountSpec.partitionName() = "job";
  groupCountSpec.dimension() = "task_count";
  groupCountSpec.bound() = interface::GroupCountSpecBound::MULTIPLE;
  groupCountSpec.filter()->itemsWhitelist() = {"host0"};

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  limit.globalLimit() = 2; // only multiples of 2 are allowed
  groupCountSpec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), groupCountSpec);
  auto constraints = co_await specBuilder.constraints(expressionBuilder());
  // one per (job, host) pair
  EXPECT_EQ(2, constraints.size());
  // constraint corresponding to job1 will always be zero because job1
  // lies entirely on host1
  auto expr =
      constraints.at(0).constraintExpr + constraints.at(1).constraintExpr;

  // initially host0 has 5 job0-tasks, one away from closest multiple of 2
  EXPECT_NEAR(1, evaluate(expr, deltaFromInitial({})), 1e-8);

  // moving any task from host0 to host1 should bring down the number of tasks
  // to 4, which is a multiple of 2
  EXPECT_NEAR(0, evaluate(expr, deltaFromInitial({{"task1", "host1"}})), 1e-8);

  // moving two tasks from host0 to host1 should bring down the number of tasks
  // to 3, which is one away from closest multiple of 2
  EXPECT_NEAR(
      1,
      evaluate(
          expr, deltaFromInitial({{"task0", "host1"}, {"task1", "host1"}})),
      1e-8);

  // moving three tasks from host0 to host1 should bring down the number of
  // tasks to 2, which is a multiple of 2
  EXPECT_NEAR(
      0,
      evaluate(
          expr,
          deltaFromInitial(
              {{"task0", "host1"}, {"task1", "host1"}, {"task2", "host1"}})),
      1e-8);

  // moving four tasks from host0 to host1 should bring down the number of
  // tasks to 1, which is one away from closest multiple of 2
  EXPECT_NEAR(
      1,
      evaluate(
          expr,
          deltaFromInitial(
              {{"task0", "host1"},
               {"task1", "host1"},
               {"task2", "host1"},
               {"task3", "host1"}})),
      1e-8);

  // moving all tasks from host0 to host1 should bring down the number of
  // tasks to 0, which is one away from closest multiple of 2
  EXPECT_NEAR(
      0,
      evaluate(
          expr,
          deltaFromInitial({
              {"task0", "host1"},
              {"task1", "host1"},
              {"task2", "host1"},
              {"task3", "host1"},
              {"task4", "host1"},
          })),
      1e-8);
}

CO_TEST_F(GroupCountSpecBuilderTest, Stayed) {
  interface::GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "host";
  groupCountSpec.partitionName() = "job";
  groupCountSpec.dimension() = "task_count";
  groupCountSpec.bound() = interface::GroupCountSpecBound::MIN;
  groupCountSpec.definition() = interface::GroupCountSpecDefinition::STAYED;
  groupCountSpec.limit()->type() = interface::LimitType::ABSOLUTE;
  groupCountSpec.limit()->globalLimit() = 0;
  groupCountSpec.limit()->scopeItemToGroupLimits()->insert(
      {"host0", {{"job1", 3}}});

  const GroupCountSpecBuilder specBuilder(buildUniverse(), groupCountSpec);
  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // Move tasks of job1 from host0 to host1, one at a time, and assert the
  // increasing penalty as the number of tasks in host0 becomes less than the
  // limit of 3.
  EXPECT_NEAR(0, evaluate(goal, deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(
      0,
      evaluate(
          goal,
          deltaFromInitial({
              {"task3", "host1"},
              {"task4", "host1"},
          })),
      1e-8);
  EXPECT_NEAR(
      1,
      evaluate(
          goal,
          deltaFromInitial({
              {"task2", "host1"},
              {"task3", "host1"},
              {"task4", "host1"},
          })),
      1e-8);
  EXPECT_NEAR(
      2,
      evaluate(
          goal,
          deltaFromInitial({
              {"task1", "host1"},
              {"task2", "host1"},
              {"task3", "host1"},
              {"task4", "host1"},
          })),
      1e-8);
  EXPECT_NEAR(
      3,
      evaluate(
          goal,
          deltaFromInitial({
              {"task0", "host1"},
              {"task1", "host1"},
              {"task2", "host1"},
              {"task3", "host1"},
              {"task4", "host1"},
          })),
      1e-8);
}

CO_TEST_F(GroupCountSpecBuilderTest, MinimumLimitZero) {
  interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "task_count";
  spec.bound() = interface::GroupCountSpecBound::MIN;
  spec.zeroAllowed() = true;
  spec.filter()->itemsWhitelist() = {"host1"};
  spec.limit()->globalLimit() = 0;
  spec.limit()->groupLimits() = {{"job1", 4}};
  spec.minimumLimit() = 0;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), spec);
  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // Valid number of objects of job1 allowed in host1: 0, 4
  EXPECT_NEAR(0, evaluate(goal, deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(1, evaluate(goal, deltaFromInitial({{"task0", "host1"}})), 1e-8);
  EXPECT_NEAR(
      1,
      evaluate(
          goal, deltaFromInitial({{"task0", "host1"}, {"task1", "host1"}})),
      1e-8);
  EXPECT_NEAR(
      1,
      evaluate(
          goal,
          deltaFromInitial(
              {{"task0", "host1"}, {"task1", "host1"}, {"task2", "host1"}})),
      1e-8);
  EXPECT_NEAR(
      0,
      evaluate(
          goal,
          deltaFromInitial(
              {{"task0", "host1"},
               {"task1", "host1"},
               {"task2", "host1"},
               {"task3", "host1"}})),
      1e-8);
}

CO_TEST_F(GroupCountSpecBuilderTest, MinimumLimitOne) {
  interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "task_count";
  spec.bound() = interface::GroupCountSpecBound::MIN;
  spec.zeroAllowed() = true;
  spec.filter()->itemsWhitelist() = {"host1"};
  spec.limit()->globalLimit() = 0;
  spec.limit()->groupLimits() = {{"job1", 4}};
  spec.minimumLimit() = 1;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), spec);
  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // Valid number of objects of job1 allowed in host1: 0, 1, 4
  EXPECT_NEAR(0, evaluate(goal, deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(0, evaluate(goal, deltaFromInitial({{"task0", "host1"}})), 1e-8);
  EXPECT_NEAR(
      1,
      evaluate(
          goal, deltaFromInitial({{"task0", "host1"}, {"task1", "host1"}})),
      1e-8);
  EXPECT_NEAR(
      1,
      evaluate(
          goal,
          deltaFromInitial(
              {{"task0", "host1"}, {"task1", "host1"}, {"task2", "host1"}})),
      1e-8);
  EXPECT_NEAR(
      0,
      evaluate(
          goal,
          deltaFromInitial(
              {{"task0", "host1"},
               {"task1", "host1"},
               {"task2", "host1"},
               {"task3", "host1"}})),
      1e-8);
}

CO_TEST_F(GroupCountSpecBuilderTest, DynamicDimensionScopeUnequalToSpecScope) {
  co_await addScope("region", {{"region1", {"host0", "host1"}}});

  // define dynamic load on scope "host"
  co_await addDynamicObjectDimension(
      "dynamicLoad",
      host(),
      {{"host0",
        makeSharedPtrEntityToValueMap<entities::ObjectId>(
            {{task(0), 1},
             {task(1), 2},
             {task(2), 1},
             {task(3), 2},
             {task(4), 4}})},
       {"host1",
        makeSharedPtrEntityToValueMap<entities::ObjectId>(
            {{task(5), 2}, {task(6), 2}, {task(7), 2}, {task(8), 4}})}},
      100);

  // Note that the spec's scope is "region" but the "dynamicLoad" is defined on
  // scope "host".
  interface::GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "region";
  groupCountSpec.partitionName() = "job";
  groupCountSpec.dimension() = "dynamicLoad";

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  limit.globalLimit() = 10;
  groupCountSpec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), groupCountSpec);
  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // Job1 tasks are in host0 and job2 tasks are in host1. Expect the initial
  // value to be zero since limit on "region1" is 10, and utilization of each
  // group is exactly 10.
  // If the fact the spec's scope and dimension's scope are different is not
  // handled correctly, then the goal will evaluate to some very high value
  // since the default dimension value is 100.
  EXPECT_NEAR(0, evaluate(goal, deltaFromInitial({})), 1e-8);
}

TEST_F(GroupCountSpecBuilderTest, SpecInfo) {
  interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "task_count";
  spec.bound() = interface::GroupCountSpecBound::MIN;
  spec.zeroAllowed() = true;
  spec.filter()->itemsWhitelist() = {"host1"};
  spec.limit()->globalLimit() = 0;
  spec.limit()->groupLimits() = {{"job1", 4}};
  spec.minimumLimit() = 1;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), spec);

  auto expectedSpecInfo = SpecParameters{
      .name = "",
      .scope = "host",
      .partition = "job",
      .dimension = "task_count",
      .definition = "AFTER",
      .boundType = "MIN",
      .limitType = "ABSOLUTE",
      .zeroAllowed = "yes",
      .squares = "no",
      .filterAllowListSize = 1,
      .filterBlockListSize = 0};
  EXPECT_EQ(expectedSpecInfo, specBuilder.getSpecInfo());
}
TEST_F(GroupCountSpecBuilderTest, PreFilterBlocksZeroLimitPairs) {
  interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "task_count";
  spec.bound() = interface::GroupCountSpecBound::MAX;

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  // job1 can go to host0 but not host1; job2 can go to host1 but not host0
  limit.scopeItemToGroupLimits() = {
      {"host0", {{"job2", 0}}},
      {"host1", {{"job1", 0}}},
  };
  limit.isDefaultLimitUnbounded() = true;
  spec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), spec);
  const auto numObjects = universe_->getObjects().getObjectIds().size();
  const auto numContainers =
      universe_->getContainers().getContainerIds().size();
  InvalidMoveFilter filter(numObjects, numContainers);

  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  EXPECT_FALSE(filter.empty());
  // job1 tasks (task0-4) are blocked from host1
  // job2 tasks (task5-8) are blocked from host0
  std::set<InvalidPair> expectedInvalidPairs;
  for (const auto i : folly::irange(0, 5)) {
    expectedInvalidPairs.emplace(task(i), containerId("host1"));
  }
  for (const auto i : folly::irange(5, 9)) {
    expectedInvalidPairs.emplace(task(i), containerId("host0"));
  }
  EXPECT_EQ(expectedInvalidPairs, collectInvalidPairs(filter));
}

TEST_F(GroupCountSpecBuilderTest, PreFilterNoOpForNonZeroLimits) {
  interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "task_count";
  spec.bound() = interface::GroupCountSpecBound::MAX;

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  limit.globalLimit() = 3;
  spec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), spec);
  const auto numObjects = universe_->getObjects().getObjectIds().size();
  const auto numContainers =
      universe_->getContainers().getContainerIds().size();
  InvalidMoveFilter filter(numObjects, numContainers);

  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  EXPECT_TRUE(filter.empty());
}

TEST_F(GroupCountSpecBuilderTest, PreFilterNoOpForMinBound) {
  interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "task_count";
  spec.bound() = interface::GroupCountSpecBound::MIN;

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  limit.scopeItemToGroupLimits() = {{"host0", {{"job1", 0}}}};
  spec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), spec);
  const auto numObjects = universe_->getObjects().getObjectIds().size();
  const auto numContainers =
      universe_->getContainers().getContainerIds().size();
  InvalidMoveFilter filter(numObjects, numContainers);

  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  EXPECT_TRUE(filter.empty());
}

TEST_F(GroupCountSpecBuilderTest, FilterBlocksZeroLimitPairsForExactBound) {
  interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "task_count";
  spec.bound() = interface::GroupCountSpecBound::EXACT;

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  limit.scopeItemToGroupLimits() = {{"host1", {{"job1", 0}}}};
  limit.isDefaultLimitUnbounded() = true;
  spec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), spec);
  const auto numObjects = universe_->getObjects().getObjectIds().size();
  const auto numContainers =
      universe_->getContainers().getContainerIds().size();
  InvalidMoveFilter filter(numObjects, numContainers);

  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  std::set<InvalidPair> expectedInvalidPairs;
  for (const auto i : folly::irange(0, 5)) {
    expectedInvalidPairs.emplace(task(i), containerId("host1"));
  }
  EXPECT_EQ(expectedInvalidPairs, collectInvalidPairs(filter));
}

TEST_F(GroupCountSpecBuilderTest, FilterWorksForDuringDefinition) {
  interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "task_count";
  spec.bound() = interface::GroupCountSpecBound::MAX;
  spec.definition() = interface::GroupCountSpecDefinition::DURING;

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  limit.scopeItemToGroupLimits() = {{"host0", {{"job2", 0}}}};
  limit.isDefaultLimitUnbounded() = true;
  spec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), spec);
  const auto numObjects = universe_->getObjects().getObjectIds().size();
  const auto numContainers =
      universe_->getContainers().getContainerIds().size();
  InvalidMoveFilter filter(numObjects, numContainers);

  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  std::set<InvalidPair> expectedInvalidPairs;
  for (const auto i : folly::irange(5, 9)) {
    expectedInvalidPairs.emplace(task(i), containerId("host0"));
  }
  EXPECT_EQ(expectedInvalidPairs, collectInvalidPairs(filter));
}

TEST_F(
    GroupCountSpecBuilderTest,
    FilterDuringExemptsOriginalAndUpdatedScopeMembers) {
  interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "task_count";
  spec.bound() = interface::GroupCountSpecBound::MAX;
  spec.definition() = interface::GroupCountSpecDefinition::DURING;

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  limit.scopeItemToGroupLimits() = {{"host0", {{"job1", 0}, {"job2", 0}}}};
  limit.isDefaultLimitUnbounded() = true;
  spec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), spec);
  InvalidMoveFilter filter(
      universe_->getObjects().getObjectIds().size(),
      universe_->getContainers().getContainerIds().size());
  const Assignment updatedInitialAssignment(
      deltaFromInitial({{"task5", "host0"}}));
  specBuilder.populateInvalidMoveFilter(filter, updatedInitialAssignment);

  const std::set<InvalidPair> expected{
      {task(6), containerId("host0")},
      {task(7), containerId("host0")},
      {task(8), containerId("host0")},
  };
  EXPECT_EQ(expected, collectInvalidPairs(filter));
}

CO_TEST_F(GroupCountSpecBuilderTest, FilterHonorsFeasibilityTolerance) {
  co_await addObjectDimension(
      "near_zero", {{task(0), 0.00000000001}, {task(1), 0.00000000015}}, 0);

  interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "near_zero";
  spec.bound() = interface::GroupCountSpecBound::MAX;
  spec.definition() = interface::GroupCountSpecDefinition::DURING;

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  limit.scopeItemToGroupLimits() = {{"host1", {{"job1", 0}}}};
  limit.isDefaultLimitUnbounded() = true;
  spec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), spec);
  InvalidMoveFilter filter(
      universe_->getObjects().getObjectIds().size(),
      universe_->getContainers().getContainerIds().size());
  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  const std::set<InvalidPair> expected{{task(1), containerId("host1")}};
  EXPECT_EQ(expected, collectInvalidPairs(filter));
}

CO_TEST_F(
    GroupCountSpecBuilderTest,
    FilterSquaresUsesNormalizedSquaredPenalty) {
  co_await addObjectDimension(
      "near_zero", {{task(0), 0.00001}, {task(1), 0.0001}}, 0);

  interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "near_zero";
  spec.bound() = interface::GroupCountSpecBound::MAX;
  spec.definition() = interface::GroupCountSpecDefinition::DURING;
  spec.squares() = true;

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  limit.scopeItemToGroupLimits() = {{"host1", {{"job1", 0}}}};
  limit.isDefaultLimitUnbounded() = true;
  spec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), spec);
  InvalidMoveFilter filter(
      universe_->getObjects().getObjectIds().size(),
      universe_->getContainers().getContainerIds().size());
  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  const std::set<InvalidPair> expected{{task(1), containerId("host1")}};
  EXPECT_EQ(expected, collectInvalidPairs(filter));
}

CO_TEST_F(
    GroupCountSpecBuilderTest,
    FilterSquaresUsesUpdatedInitialPenaltyForAfter) {
  co_await addObjectDimension(
      "near_threshold",
      {{task(0), 1.0}, {task(1), 1.00000000015}, {task(2), 1.000000002}},
      0);

  interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "near_threshold";
  spec.bound() = interface::GroupCountSpecBound::MAX;
  spec.definition() = interface::GroupCountSpecDefinition::AFTER;
  spec.squares() = true;

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  limit.scopeItemToGroupLimits() = {{"host1", {{"job1", 0}}}};
  limit.isDefaultLimitUnbounded() = true;
  spec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), spec);
  InvalidMoveFilter filter(
      universe_->getObjects().getObjectIds().size(),
      universe_->getContainers().getContainerIds().size());
  const Assignment updatedInitialAssignment(
      deltaFromInitial({{"task0", "host1"}}));
  specBuilder.populateInvalidMoveFilter(filter, updatedInitialAssignment);

  const std::set<InvalidPair> expected{{task(2), containerId("host1")}};
  EXPECT_EQ(expected, collectInvalidPairs(filter));
}

TEST_F(GroupCountSpecBuilderTest, FilterNoOpForStayedDefinition) {
  interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "task_count";
  spec.bound() = interface::GroupCountSpecBound::MAX;
  spec.definition() = interface::GroupCountSpecDefinition::STAYED;

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  limit.scopeItemToGroupLimits() = {{"host0", {{"job2", 0}}}};
  limit.isDefaultLimitUnbounded() = true;
  spec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), spec);
  const auto numObjects = universe_->getObjects().getObjectIds().size();
  const auto numContainers =
      universe_->getContainers().getContainerIds().size();
  InvalidMoveFilter filter(numObjects, numContainers);

  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  EXPECT_TRUE(filter.empty());
}

TEST_F(GroupCountSpecBuilderTest, FilterSkipsInitiallyBrokenPairsForAfter) {
  interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "task_count";
  spec.bound() = interface::GroupCountSpecBound::MAX;
  spec.definition() = interface::GroupCountSpecDefinition::AFTER;

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  limit.globalLimit() = 0;
  spec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), spec);
  const auto numObjects = universe_->getObjects().getObjectIds().size();
  const auto numContainers =
      universe_->getContainers().getContainerIds().size();
  InvalidMoveFilter filter(numObjects, numContainers);

  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  // globalLimit=0 with AFTER definition:
  // (job1, host0): initial util L=5 (task0-4), threshold=5, all tasks have v=1
  // <= 5 → not blocked (job2, host1): initial util L=4 (task5-8), threshold=4,
  // all tasks have v=1 <= 4 → not blocked (job1, host1): initial util L=0,
  // threshold=0, block v > 0 → job1 tasks blocked (job2, host0): initial util
  // L=0, threshold=0, block v > 0 → job2 tasks blocked
  std::set<InvalidPair> expectedInvalidPairs;
  for (const auto i : folly::irange(0, 5)) {
    expectedInvalidPairs.emplace(task(i), containerId("host1"));
  }
  for (const auto i : folly::irange(5, 9)) {
    expectedInvalidPairs.emplace(task(i), containerId("host0"));
  }
  EXPECT_EQ(expectedInvalidPairs, collectInvalidPairs(filter));
}

TEST_F(GroupCountSpecBuilderTest, FilterNoOpForNonZeroGlobalNoOverrides) {
  interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "task_count";
  spec.bound() = interface::GroupCountSpecBound::MAX;

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  limit.globalLimit() = 1;
  spec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), spec);
  const auto numObjects = universe_->getObjects().getObjectIds().size();
  const auto numContainers =
      universe_->getContainers().getContainerIds().size();
  InvalidMoveFilter filter(numObjects, numContainers);

  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  // Non-zero global limit with no overrides → hasOverride() fast path
  EXPECT_TRUE(filter.empty());
}

CO_TEST_F(GroupCountSpecBuilderTest, FilterNoOpForNegativeDimensions) {
  co_await addObjectDimension(
      "weight", {{task(0), -1.0}, {task(1), 2.0}, {task(2), 3.0}}, 0);

  interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "weight";
  spec.bound() = interface::GroupCountSpecBound::MAX;

  interface::Limit limit;
  limit.type() = interface::LimitType::ABSOLUTE;
  limit.globalLimit() = 0;
  spec.limit() = limit;

  const GroupCountSpecBuilder specBuilder(buildUniverse(), spec);
  const auto numObjects = universe_->getObjects().getObjectIds().size();
  const auto numContainers =
      universe_->getContainers().getContainerIds().size();
  InvalidMoveFilter filter(numObjects, numContainers);

  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  // Dimension has negative values → filter is skipped entirely
  EXPECT_TRUE(filter.empty());
}

} // namespace facebook::rebalancer::materializer::tests
