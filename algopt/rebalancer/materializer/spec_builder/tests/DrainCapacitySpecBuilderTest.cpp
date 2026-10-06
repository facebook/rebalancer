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
#include "algopt/rebalancer/materializer/spec_builder/DrainCapacitySpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"

#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::materializer::tests {

class DrainCapacitySpecBuilderTest : public SpecBuilderTestBase<> {
 protected:
  folly::coro::Task<void> setUpCoro() {
    setUpUniverse({
        {"host0", {"task0", "task1", "task2"}},
        {"host1", {}},
        {"host2", {}},
        {"host3", {}},
    });

    co_await addObjectDimension(
        "cpu", {{task(0), 8}, {task(1), 5}, {task(2), 9}}, 0);
    co_await addScopeDimension("cpu", scopeId("host"), {}, 10);

    co_await addObjectDimension(
        "load",
        {{{task(0), 8}, {task(1), 2}, {task(2), 6}},
         {{task(0), 2}, {task(1), 8}, {task(2), 6}}},
        {0, 0});
    co_await addScopeDimension("load", scopeId("host"), {}, 10);

    co_return;
  }

  void SetUp() override {
    folly::coro::blockingWait(setUpCoro());
  }

  entities::ObjectId task(int index) const {
    return objectId(fmt::format("task{}", index));
  }
};

TEST_F(DrainCapacitySpecBuilderTest, Description) {
  interface::DrainCapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";

  const DrainCapacitySpecBuilder specBuilder(buildUniverse(), spec);
  EXPECT_EQ(
      "limit drain capacity (cpu) on scope host", specBuilder.description());
}

TEST_F(DrainCapacitySpecBuilderTest, SpecInfo) {
  interface::DrainCapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";

  const DrainCapacitySpecBuilder specBuilder(buildUniverse(), spec);

  const auto expectedSpecInfo =
      SpecParameters{.name = "", .scope = "host", .dimension = "cpu"};
  EXPECT_EQ(expectedSpecInfo, specBuilder.getSpecInfo());
}

CO_TEST_F(DrainCapacitySpecBuilderTest, Goal) {
  interface::DrainCapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.spillDistribution() = {
      {"host1", {{"host3", 0.5}}}, {"host2", {{"host3", 0.2}}}};

  const DrainCapacitySpecBuilder specBuilder(buildUniverse(), spec);
  const auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  EXPECT_NEAR(0.0, evaluate(goal, deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(
      0.0, evaluate(goal, deltaFromInitial({{"task2", "host3"}})), 1e-8);
  EXPECT_NEAR(
      1.4,
      evaluate(
          goal, deltaFromInitial({{"task0", "host3"}, {"task2", "host3"}})),
      1e-8);
  EXPECT_NEAR(
      0.3,
      evaluate(
          goal, deltaFromInitial({{"task0", "host1"}, {"task2", "host3"}})),
      1e-8);
  EXPECT_NEAR(
      0.06,
      evaluate(
          goal, deltaFromInitial({{"task0", "host2"}, {"task2", "host3"}})),
      1e-8);
  EXPECT_NEAR(
      0.15,
      evaluate(
          goal, deltaFromInitial({{"task1", "host1"}, {"task2", "host3"}})),
      1e-8);
  EXPECT_NEAR(
      0.0,
      evaluate(
          goal, deltaFromInitial({{"task1", "host2"}, {"task2", "host3"}})),
      1e-8);
  EXPECT_NEAR(
      0.3,
      evaluate(
          goal,
          deltaFromInitial(
              {{"task0", "host1"}, {"task1", "host2"}, {"task2", "host3"}})),
      1e-8);
  EXPECT_NEAR(
      0.21,
      evaluate(
          goal,
          deltaFromInitial(
              {{"task0", "host2"}, {"task1", "host1"}, {"task2", "host3"}})),
      1e-8);
}

CO_TEST_F(DrainCapacitySpecBuilderTest, GoalWithVectorDimension) {
  interface::DrainCapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "load";
  spec.spillDistribution() = {{"host1", {{"host3", 0.5}}}};

  const DrainCapacitySpecBuilder specBuilder(buildUniverse(), spec);
  const auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // host1 = {8, 2}, host3 = {2, 8}. Each index fits (2 + 0.5 * 8 = 6 and
  // 8 + 0.5 * 2 = 9), even though the peaks add up to 8 + 0.5 * 8 = 12.
  EXPECT_NEAR(
      0.0,
      evaluate(
          goal, deltaFromInitial({{"task0", "host1"}, {"task1", "host3"}})),
      1e-8);
  // host1 = {8, 2}, host3 = {8, 14}. Index 0 is over by 2 (8 + 4 = 12) and
  // index 1 by 5 (14 + 1 = 15); only the worst index counts.
  EXPECT_NEAR(
      0.5,
      evaluate(
          goal,
          deltaFromInitial(
              {{"task0", "host1"}, {"task1", "host3"}, {"task2", "host3"}})),
      1e-8);
}
} // namespace facebook::rebalancer::materializer::tests
