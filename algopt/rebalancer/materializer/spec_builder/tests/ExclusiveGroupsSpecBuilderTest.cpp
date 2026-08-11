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

#include "algopt/rebalancer/materializer/spec_builder/ExclusiveGroupsSpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/tests/SolverTestUtils.h"
#include <algopt/rebalancer/materializer/utils/ExpressionBuilder.h>

#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::materializer::tests {

class ExclusiveGroupsSpecBuilderTest : public SpecBuilderTestBase<> {
 protected:
  folly::coro::Task<void> setUpCoro() {
    setUpUniverse({
        {"host0", {"task0", "task1", "task2", "task3"}},
        {"host1", {"task4", "task5"}},
    });

    co_await addPartition(
        "job",
        {{"job0", {"task0", "task1", "task2", "task3"}},
         {"job1", {"task4", "task5"}}});

    co_return;
  }

  void SetUp() override {
    folly::coro::blockingWait(setUpCoro());
  }

  entities::ScopeId host() const {
    return scopeId("host");
  }
};

CO_TEST_F(ExclusiveGroupsSpecBuilderTest, fixDeficit) {
  REBALANCER_CO_SKIP_IF_NO_MIP_SOLVER();
  co_await addScopeDimension(
      "task_count", host(), {{"host0", 2}, {"host1", 4}}, 1);

  interface::ExclusiveGroupsSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "task_count";
  spec.name() = "test";

  const auto universe = buildUniverse();
  auto& builder = expressionBuilder();

  const ExclusiveGroupsSpecBuilder specBuilder(
      universe, spec, std::make_shared<LogCollector>());
  // ScopeItemToGroupAssignment is computed when constraint is built, so tagging
  // would be initially unset
  EXPECT_EQ(
      "exclusive groups spec test on dimension task_count, scope host, partition job, tagging: unset",
      specBuilder.description());
  // deficit for a group: is defined as groupSize - capacity of scopeItem to
  // which it is mapped
  // deficit(job0) =  4 - 2 * mapped(job0, host0) - 4 * mapped(job0, host1)
  // deficit(job1) =  2 - 2 * mapped(job1, host0) - 4 * mapped(job1, host1)
  // Here, mapped(job, host) is a binary saying if the job is mapped to host
  // to  fix deficit we must map host0 => job1, host1 => job0
  const auto exprs = co_await specBuilder.constraints(builder);
  EXPECT_EQ(
      "exclusive groups spec test on dimension task_count, scope host, partition job, tagging: host0 -> job1, host1 -> job0",
      specBuilder.description());

  EXPECT_EQ(2, exprs.size());
  const auto goal = co_await specBuilder.goalCoro(builder);
  // initially, host0 is assigned tasks of job0 while it maps to job1
  // penalty(host0) = util(host0) - util(host0, job1) = 4 - 0 = 4
  // penalty(host1) = util(host1) - util(host1, job0) = 2 - 0 = 2
  // normalized penalty = 4/3 + 2/3 = 2, where 3 is avg capacity
  EXPECT_NEAR(2, evaluate(goal, deltaFromInitial({})), 1e-8);
  // after job0 moves to host1 and job1 moves to host0, penalty should be zero
  EXPECT_NEAR(
      0,
      evaluate(
          goal,
          deltaFromInitial(
              {{"task0", "host1"},
               {"task1", "host1"},
               {"task2", "host1"},
               {"task3", "host1"},
               {"task4", "host0"},
               {"task5", "host0"}})),
      1e-8);
}

CO_TEST_F(ExclusiveGroupsSpecBuilderTest, minimizeMoves) {
  REBALANCER_CO_SKIP_IF_NO_MIP_SOLVER();
  co_await addScopeDimension(
      "task_count", host(), {{"host0", 4}, {"host1", 4}}, 1);

  interface::ExclusiveGroupsSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "task_count";
  spec.name() = "test";

  const auto universe = buildUniverse();
  auto& builder = expressionBuilder();

  // deficit(job0) =  4 - 4 * mapped(job0, host0) - 4 * mapped(job0, host1)
  // deficit(job1) =  2 - 4 * mapped(job1, host0) - 4 * mapped(job1, host1)
  // The assignment of job to host should try to minimize moves from initial
  // state gives us the mapping: job0 => host0, job1 => host1
  const ExclusiveGroupsSpecBuilder specBuilder(
      universe, spec, std::make_shared<LogCollector>());
  const auto exprs = co_await specBuilder.constraints(builder);
  EXPECT_EQ(
      "exclusive groups spec test on dimension task_count, scope host, partition job, tagging: host0 -> job0, host1 -> job1",
      specBuilder.description());

  // The initial assignment is consistent with group to scope assignment
  EXPECT_NEAR(2, exprs.size(), 1e-5);
  for (const auto& ctr : exprs) {
    EXPECT_NEAR(0, evaluate(ctr, deltaFromInitial({})), 1e-8);
  }
  const auto goal = co_await specBuilder.goalCoro(builder);
  EXPECT_NEAR(0, evaluate(goal, deltaFromInitial({})), 1e-8);
  // host0 expr = 3 (util) - 3 (util for mapped group: job0) = 0
  // host1 expr = 3 (util) - 2 (util for mapped group: job1) = 1
  // normalized penalty = 0/4 + 1/4 = 0.25 where 4 is avg capacity
  EXPECT_NEAR(
      0.25, evaluate(goal, deltaFromInitial({{"task0", "host1"}})), 1e-5);
}

TEST_F(ExclusiveGroupsSpecBuilderTest, SpecInfo) {
  interface::ExclusiveGroupsSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "task_count";
  spec.name() = "test";

  const ExclusiveGroupsSpecBuilder specBuilder(
      buildUniverse(), spec, std::make_shared<LogCollector>());

  const auto expectedSpecInfo = SpecParameters{
      .name = "test",
      .scope = "host",
      .partition = "job",
      .dimension = "task_count"};
  EXPECT_EQ(expectedSpecInfo, specBuilder.getSpecInfo());
}
} // namespace facebook::rebalancer::materializer::tests
