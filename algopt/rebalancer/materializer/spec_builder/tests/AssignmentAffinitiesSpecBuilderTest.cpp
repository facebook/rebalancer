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
#include "algopt/rebalancer/materializer/spec_builder/AssignmentAffinitiesSpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"

#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::materializer::tests {

enum class AffinityInput { LIST, DIMENSION };

class AssignmentAffinitiesSpecBuilderTest
    : public SpecBuilderTestBase<AffinityInput> {
 protected:
  folly::coro::Task<void> setUpCoro() {
    setUpUniverse({
        {"host0", {"task0", "task1"}},
        {"host1", {"task2"}},
        {"host2", {}},
    });

    co_await addScope("rack", {{"rack0", {"host0"}}, {"rack1", {"host1"}}});

    co_return;
  }

  void SetUp() override {
    folly::coro::blockingWait(setUpCoro());
  }

  static interface::AssignmentAffinity makeAffinity(
      const std::string& objectName,
      const std::string& scopeItemName,
      double affinity) {
    interface::AssignmentAffinity result;
    result.objectName() = objectName;
    result.scopeItemName() = scopeItemName;
    result.affinity() = affinity;
    return result;
  }

  static interface::AssignmentAffinitiesSpec makeDimensionSpec() {
    interface::AssignmentAffinitiesSpec spec;
    spec.scope() = "host";
    spec.dimension() = "assignmentAffinity";
    return spec;
  }

  folly::coro::Task<void> useSelectedInput(
      interface::AssignmentAffinitiesSpec& spec) {
    switch (GetParam()) {
      case AffinityInput::LIST:
        co_return;
      case AffinityInput::DIMENSION:
        break;
    }

    entities::Map<std::string, entities::Map<std::string, double>>
        scopeItemNameToObjectNameToAffinity;
    for (const auto& affinity : *spec.affinities()) {
      scopeItemNameToObjectNameToAffinity[*affinity.scopeItemName()]
                                         [*affinity.objectName()] +=
          *affinity.affinity();
    }
    co_await addDynamicObjectDimension(
        "assignmentAffinity",
        scopeId(*spec.scope()),
        scopeItemNameToObjectNameToAffinity,
        0);
    spec.affinities()->clear();
    spec.dimension() = "assignmentAffinity";
    co_return;
  }
};

INSTANTIATE_TEST_CASE_P(
    Input,
    AssignmentAffinitiesSpecBuilderTest,
    ::testing::Values(AffinityInput::LIST, AffinityInput::DIMENSION));

CO_TEST_P(AssignmentAffinitiesSpecBuilderTest, Goal) {
  interface::AssignmentAffinitiesSpec spec;
  spec.scope() = "host";
  spec.affinities() = {
      makeAffinity("task0", "host0", 1),
      makeAffinity("task0", "host1", 2),
      makeAffinity("task1", "host0", -4),
      makeAffinity("task1", "host1", -8),
  };
  co_await useSelectedInput(spec);

  const AssignmentAffinitiesSpecBuilder specBuilder(buildUniverse(), spec);

  EXPECT_EQ("Assignment affinities of task to host", specBuilder.description());

  const auto expectedSpecInfo =
      SpecParameters{.name = "", .scope = "host", .size = 4};
  EXPECT_EQ(expectedSpecInfo, specBuilder.getSpecInfo());

  const auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // The total penalty is 3 in the initial assignment: task0 gets 1 point for
  // being in host0, while task1 gets -4 points for being in host0.
  EXPECT_NEAR(3, evaluate(goal, deltaFromInitial({})), 1e-8);

  // If task1 moves to host1, then the penalty increases by 4.
  EXPECT_NEAR(7, evaluate(goal, deltaFromInitial({{"task1", "host1"}})), 1e-8);

  // task1 no longer pays a penalty if moved to host2, and task0 still gets 1
  // point.
  EXPECT_NEAR(-1, evaluate(goal, deltaFromInitial({{"task1", "host2"}})), 1e-8);

  // task1 no longer pays a penalty if moved to host2, and task0 gets 2 points
  // in host1.
  EXPECT_NEAR(
      -2,
      evaluate(
          goal, deltaFromInitial({{"task0", "host1"}, {"task1", "host2"}})),
      1e-8);
}

CO_TEST_P(AssignmentAffinitiesSpecBuilderTest, OutOfScope) {
  interface::AssignmentAffinitiesSpec spec;
  spec.scope() = "rack";
  spec.affinities() = {
      makeAffinity("task0", "rack0", 1),
      makeAffinity("task1", "rack1", 2),
      makeAffinity("task2", "rack0", 4),
      makeAffinity("task2", "rack1", 8),
  };
  co_await useSelectedInput(spec);

  const AssignmentAffinitiesSpecBuilder specBuilder(buildUniverse(), spec);

  EXPECT_EQ("Assignment affinities of task to rack", specBuilder.description());
  const auto expectedSpecInfo =
      SpecParameters{.name = "", .scope = "rack", .size = 4};
  EXPECT_EQ(expectedSpecInfo, specBuilder.getSpecInfo());

  const auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // Overall score of initial assignment is 9.
  EXPECT_NEAR(-9, evaluate(goal, deltaFromInitial({})), 1e-8);

  // If task0 is moved from its preferred rack, the penalty increases by 1.
  EXPECT_NEAR(-8, evaluate(goal, deltaFromInitial({{"task0", "host1"}})), 1e-8);
  EXPECT_NEAR(-8, evaluate(goal, deltaFromInitial({{"task0", "host2"}})), 1e-8);

  // If task1 is moved to its preferred rack, the penalty decreases by 2.
  EXPECT_NEAR(
      -11, evaluate(goal, deltaFromInitial({{"task1", "host1"}})), 1e-8);

  // If task1 is moved out of scope, the overall penalty doesn't change, since
  // it has no affinity to the current rack.
  EXPECT_NEAR(-9, evaluate(goal, deltaFromInitial({{"task1", "host2"}})), 1e-8);

  // Moving task2 to rack0 increases penalty by 4, since the affinity to it is 4
  // points lower than rack1.
  EXPECT_NEAR(-5, evaluate(goal, deltaFromInitial({{"task2", "host0"}})), 1e-8);

  // Moving task2 out of scope increases penalty by 8, since it loses the
  // affinity to the current rack.
  EXPECT_NEAR(-1, evaluate(goal, deltaFromInitial({{"task2", "host2"}})), 1e-8);
}

CO_TEST_P(AssignmentAffinitiesSpecBuilderTest, Empty) {
  interface::AssignmentAffinitiesSpec spec;
  spec.scope() = "host";
  co_await useSelectedInput(spec);

  const AssignmentAffinitiesSpecBuilder specBuilder(buildUniverse(), spec);
  EXPECT_EQ(
      (SpecParameters{.name = "", .scope = "host", .size = 0}),
      specBuilder.getSpecInfo());

  const auto goal = co_await specBuilder.goalCoro(expressionBuilder());
  EXPECT_NEAR(0, evaluate(goal, deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(0, evaluate(goal, deltaFromInitial({{"task0", "host1"}})), 1e-8);
}

CO_TEST_F(
    AssignmentAffinitiesSpecBuilderTest,
    StaticDimensionMatchesAffinityList) {
  co_await addObjectDimension(
      "assignmentAffinity",
      {{objectId("task0"), 2}, {objectId("task1"), -3}},
      0);

  interface::AssignmentAffinitiesSpec listSpec;
  listSpec.scope() = "rack";
  listSpec.affinities() = {
      makeAffinity("task0", "rack0", 2),
      makeAffinity("task0", "rack1", 2),
      makeAffinity("task1", "rack0", -3),
      makeAffinity("task1", "rack1", -3),
  };
  auto dimensionSpec = makeDimensionSpec();
  dimensionSpec.scope() = "rack";

  const auto universe = buildUniverse();
  const AssignmentAffinitiesSpecBuilder listBuilder(universe, listSpec);
  const AssignmentAffinitiesSpecBuilder dimensionBuilder(
      universe, dimensionSpec);
  EXPECT_EQ(
      (SpecParameters{.name = "", .scope = "rack", .size = 4}),
      dimensionBuilder.getSpecInfo());
  const auto listGoal = co_await listBuilder.goalCoro(expressionBuilder());
  const auto dimensionGoal =
      co_await dimensionBuilder.goalCoro(expressionBuilder());
  for (const auto& assignmentDelta :
       std::vector<entities::Map<std::string, std::string>>{
           {},
           {{"task0", "host1"}},
           {{"task0", "host2"}},
           {{"task1", "host1"}},
           {{"task1", "host2"}},
       }) {
    EXPECT_NEAR(
        evaluate(listGoal, deltaFromInitial(assignmentDelta)),
        evaluate(dimensionGoal, deltaFromInitial(assignmentDelta)),
        1e-8);
  }
}

CO_TEST_F(AssignmentAffinitiesSpecBuilderTest, RejectsNonScalarDimension) {
  co_await addObjectDimension(
      "assignmentAffinity",
      {{{objectId("task0"), 1}}, {{objectId("task0"), 2}}},
      {0, 0});

  const auto spec = makeDimensionSpec();

  REBALANCER_EXPECT_RUNTIME_ERROR(
      const AssignmentAffinitiesSpecBuilder specBuilder(buildUniverse(), spec),
      "AssignmentAffinitiesSpec dimension 'assignmentAffinity' must be scalar, but has size 2");
}

CO_TEST_F(AssignmentAffinitiesSpecBuilderTest, RejectsNonzeroDefaultAffinity) {
  co_await addDynamicObjectDimension(
      "assignmentAffinity", scopeId("host"), {{"host0", {{"task0", 2}}}}, 1);

  const auto spec = makeDimensionSpec();

  REBALANCER_EXPECT_RUNTIME_ERROR(
      const AssignmentAffinitiesSpecBuilder specBuilder(buildUniverse(), spec),
      "AssignmentAffinitiesSpec dimension 'assignmentAffinity' must have default value 0, but has 1");
}

TEST_F(AssignmentAffinitiesSpecBuilderTest, Constraint) {
  const interface::AssignmentAffinitiesSpec spec;
  AssignmentAffinitiesSpecBuilder specBuilder(buildUniverse(), spec);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      folly::coro::blockingWait(specBuilder.constraints(expressionBuilder())),
      "not supported as a constraint");
}
} // namespace facebook::rebalancer::materializer::tests
