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

#include "algopt/rebalancer/entities/tests/UniverseBuilderTestUtils.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/moves/MoveResult.h"
#include "algopt/rebalancer/solver/moves/MoveStatsAggregator.h"
#include "algopt/rebalancer/solver/tests/ExprProblemCreation.h"
#include "algopt/rebalancer/solver/utils/MovesSummaryHelper.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <gtest/gtest.h>

#include <memory>

namespace facebook::rebalancer::packer::tests {
namespace {

// Default absolute & relative tolerance from PrecisionTolerances.
constexpr double kTolerance = 1e-10;

// makeMovesSummary only consults the Problem for its precision (and for object/
// container names, which an empty MoveSet never touches), so a trivial Problem
// is enough.
std::unique_ptr<Problem> makeProblem() {
  entities::tests::UniverseBuilderTestUtils builder;
  builder.setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0"}}});
  const auto universe = builder.buildUniverse();
  return createTestProblem(
      universe, {const_expr(0, universe)}, const_expr(0, universe));
}

ObjectiveDeltaSet makeDeltaSet(int numObjectives, double perObjectiveChange) {
  ObjectiveDeltaSet deltaSet;
  deltaSet.reserve(numObjectives);
  for (const auto i : folly::irange(numObjectives)) {
    constexpr double kOldValue = 0.01;
    deltaSet.emplace_back(
        std::make_shared<LabeledExpression>(
            fmt::format("objective_{}", i), nullptr, 1),
        kOldValue,
        kOldValue - perObjectiveChange);
  }
  return deltaSet;
}

MoveResult makeMoveResult(const ObjectiveDeltaSet& deltaSet) {
  double oldSum = 0.0;
  double newSum = 0.0;
  for (const auto& delta : deltaSet) {
    oldSum += delta.oldValue;
    newSum += delta.newValue;
  }
  return MoveResult::makeValid(
      /*moveSet=*/{},
      GlobalObjectiveValue({oldSum}),
      GlobalObjectiveValue({newSum}),
      /*arbiterValue=*/std::nullopt,
      ObjectiveDeltaSets{deltaSet});
}

} // namespace

// A move is accepted because the AGGREGATE value at a tuple position (the sum
// of its labeled sub-objectives) improves by more than the precision tolerance,
// while every INDIVIDUAL sub-objective changes by less than the tolerance.
// makeMovesSummary must still record each changed sub-objective: filtering them
// with the precision tolerance would drop the deltas that explain the move.
TEST(
    MovesSummaryHelperTest,
    RecordsChangedSubObjectivesEvenWhenIndividuallySubTolerance) {
  const auto problem = makeProblem();
  const auto& precision = problem->getUniverse().getPrecision();

  // Five sub-objectives, each changing by 0.5 * tolerance (below tolerance), so
  // their summed change is 2.5 * tolerance (above tolerance).
  constexpr int kNumObjectives = 5;
  const auto deltaSet =
      makeDeltaSet(kNumObjectives, /*perObjectiveChange=*/0.5 * kTolerance);

  // Sanity-check the setup: each individual change is sub-tolerance ...
  for (const auto& delta : deltaSet) {
    EXPECT_EQ(0, precision.compare(delta.oldValue, delta.newValue));
  }

  const auto moveResult = makeMoveResult(deltaSet);

  // ... and the aggregated move is a strict improvement.
  EXPECT_TRUE(moveResult.isBetter(precision));

  const MoveStatsAggregator moveStats(precision);
  const auto summary =
      MovesSummaryHelper::makeMovesSummary(*problem, moveResult, moveStats);

  // All changed sub-objectives are recorded, despite each being sub-tolerance.
  EXPECT_EQ(kNumObjectives, summary.objectives()->size());
}

// when a sub-objective changes by more than the tolerance, it is
// recorded in the summary.
TEST(MovesSummaryHelperTest, RecordsObjectiveWhenChangeExceedsTolerance) {
  const auto problem = makeProblem();
  const auto& precision = problem->getUniverse().getPrecision();

  const auto deltaSet =
      makeDeltaSet(/*numObjectives=*/1, /*perObjectiveChange=*/10 * kTolerance);
  const auto moveResult = makeMoveResult(deltaSet);
  EXPECT_TRUE(moveResult.isBetter(precision));

  const MoveStatsAggregator moveStats(precision);
  const auto summary =
      MovesSummaryHelper::makeMovesSummary(*problem, moveResult, moveStats);

  ASSERT_TRUE(summary.objectives()->contains("objective_0"));
  EXPECT_EQ(0, summary.objectives()->at("objective_0").tuplePos().value());
}

} // namespace facebook::rebalancer::packer::tests
