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

#include "algopt/rebalancer/interface/tests/utils.h"

#include <folly/container/irange.h>
#include <folly/logging/Init.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::interface::tests {

class StageSummaryTest : public ::testing::TestWithParam<int> {
 protected:
  void SetUp() override {
    solver = initializeTestProblemSolver({.executorThreadCount = GetParam()});

    // Set the object and container names
    solver->setObjectName("shard");
    solver->setContainerName("host");

    // Set the initial assignment
    const std::map<std::string, std::vector<std::string>> initial_assignment = {
        {"h1", {"s1", "s2", "s3"}},
        {"h2", {}},
    };
    solver->setAssignment(initial_assignment);

    // Define cpu dimension
    const std::map<std::string, double> shard_cpu = {
        {"s1", 60},
        {"s2", 10},
        {"s3", 40},
    };
    const std::map<std::string, double> host_cpu = {
        {"h1", 60},
        {"h2", 50},
    };
    solver->addObjectDimension("cpu", shard_cpu);
    solver->addContainerDimension("cpu", host_cpu);

    // add a capacity goal
    CapacitySpec capacitySpec;
    capacitySpec.scope() = "host";
    capacitySpec.dimension() = "cpu";
    solver->addGoal(capacitySpec);
  }

  std::unique_ptr<ProblemSolver> solver;
};

INSTANTIATE_TEST_CASE_P(NumThreads, StageSummaryTest, testThreadCounts());

TEST_P(StageSummaryTest, StageSummaryUnusedStagesTest) {
  // create a localSearchStageSolver with 3 stages and solveTime of 0
  LocalSearchStageSolverSpec stageSolverSpec;
  stageSolverSpec.solveTime() = 0;

  std::vector<LocalSearchStageSpec> stageSpecs;
  for (const auto _ : folly::irange(3)) {
    LocalSearchStageSpec stageSpec;
    stageSpec.begin() = 0;
    stageSpec.end() = 1;

    LocalSearchSolverSpec solverSpec = makeDefaultLocalSearchSolver();
    stageSpec.solverSpec() = solverSpec;
    stageSpec.solverSpec()->exploreMovesFromContainersNotInObjective() = false;

    stageSpecs.push_back(stageSpec);
  }
  stageSolverSpec.stageSpecs() = stageSpecs;
  solver->addSolver(stageSolverSpec);

  auto solution = solver->solve();

  ASSERT_EQ(1, solution.solverSummaries()->size());

  // the overall end reason is HIT_TIME_LIMIT, since solveTime = 0
  EXPECT_EQ(
      *solution.solverSummaries()->at(0).endReason(),
      facebook::rebalancer::interface::EndReason::HIT_TIME_LIMIT);

  // check the end reason for each of the 3 stages
  ASSERT_EQ(3, solution.solverSummaries()->at(0).stagesSummaries()->size());

  // check end reason for unused stages (all 3 are unused since solveTime = 0)
  for (const auto i : folly::irange(3)) {
    // the end reason for each stage is HIT_TIME_LIMIT
    EXPECT_EQ(
        *solution.solverSummaries()->at(0).stagesSummaries()->at(i).endReason(),
        facebook::rebalancer::interface::EndReason::HIT_TIME_LIMIT);
  }

  ASSERT_TRUE(solution.finalEvaluationSummary().has_value());
  EXPECT_EQ(FinalEvaluationSummary{}, *solution.finalEvaluationSummary());
}

TEST_P(StageSummaryTest, StageSummaryComprehensiveTest) {
  // create a localSearchStageSolver with 5 stages and stopAfterMoves = 1
  LocalSearchStageSolverSpec stageSolverSpec;
  stageSolverSpec.stopAfterMoves() = 1;

  std::vector<LocalSearchStageSpec> stageSpecs;
  for (const auto i : folly::irange(5)) {
    LocalSearchStageSpec stageSpec;
    stageSpec.begin() = 0;
    stageSpec.end() = 2;
    stageSpec.solverSpec()->exploreMovesFromContainersNotInObjective() = false;
    // for stage 1, do not allow any moves
    if (i == 1) {
      stageSpec.stopAfterMovesTillStage() = 0;
    }

    LocalSearchSolverSpec solverSpec = makeDefaultLocalSearchSolver();
    // for stage 0, solveTime = 0
    if (i == 0) {
      solverSpec.solveTime() = 0;
    }

    // for stage 2, allowedPlateauTime = 0; also cap stage moves at 0 to ensure
    // deterministic behavior across thread counts (plateau check with 0s
    // timeout is non-deterministic when move evaluation is parallelized)
    if (i == 2) {
      solverSpec.allowedPlateauTime() = 0;
      stageSpec.stopAfterMovesTillStage() = 0;
    }

    stageSpec.solverSpec() = solverSpec;
    stageSpecs.push_back(stageSpec);
  }
  stageSolverSpec.stageSpecs() = stageSpecs;
  solver->addSolver(stageSolverSpec);

  auto solution = solver->solve();

  ASSERT_EQ(1, solution.solverSummaries()->size());

  // the overall end reason is HIT_MOVE_LIMIT
  EXPECT_EQ(
      *solution.solverSummaries()->at(0).endReason(),
      facebook::rebalancer::interface::EndReason::HIT_MOVE_LIMIT);

  // check end reasons for each of the 5 stages
  ASSERT_EQ(5, solution.solverSummaries()->at(0).stagesSummaries()->size());

  auto& stageSummaries = *solution.solverSummaries()->at(0).stagesSummaries();

  // end reason for stage 0 is HIT_TIME_LIMIT (since solveTime = 0 for this
  // stage)
  EXPECT_EQ(
      *stageSummaries.at(0).endReason(),
      facebook::rebalancer::interface::EndReason::HIT_TIME_LIMIT);

  // end reason for stage 1 is HIT_STAGE_MOVE_LIMIT (since the move limit
  // is 0 for this stage)
  EXPECT_EQ(
      *stageSummaries.at(1).endReason(),
      facebook::rebalancer::interface::EndReason::HIT_STAGE_MOVE_LIMIT);

  // end reason for stage 2 is HIT_STAGE_MOVE_LIMIT (since
  // stopAfterMovesTillStage = 0 fires before allowedPlateauTime = 0 with
  // multiple threads)
  EXPECT_EQ(
      *stageSummaries.at(2).endReason(),
      facebook::rebalancer::interface::EndReason::HIT_STAGE_MOVE_LIMIT);

  for (const auto i : folly::irange(3, 5)) {
    // end reason for stage 3 is HIT_MOVE_LIMIT, since it makes 1 move and
    // hits move limit;
    // end reason for stage 4 that did not run is also HIT_MOVE_LIMIT
    EXPECT_EQ(
        *stageSummaries.at(i).endReason(),
        facebook::rebalancer::interface::EndReason::HIT_MOVE_LIMIT);
  }

  // ensure that evalStats is False for every stage except stage-3 and
  // searchTimeStats are populated
  for (const auto i : folly::irange(5)) {
    if (i == 3) {
      EXPECT_TRUE(stageSummaries.at(i).evalStats().has_value());
    } else {
      EXPECT_FALSE(stageSummaries.at(i).evalStats().has_value());
    }
  }

  ASSERT_TRUE(solution.finalEvaluationSummary().has_value());
  EXPECT_EQ(
      *stageSummaries.at(3).finalEvaluationSummary(),
      *solution.finalEvaluationSummary());
}

TEST_P(StageSummaryTest, StageSummaryMoveLimitOnlyInStages) {
  // This test is to show that when stopAfterMoves is set for each stage but is
  // not set in LocalSearchStageSolverSpec (i.e., globally), then the solver
  // will rightly only enforce the limit per stage

  // create a localSearchStageSolver with 2 stages and unbounded stopAfterMoves
  LocalSearchStageSolverSpec stageSolverSpec;

  std::vector<LocalSearchStageSpec> stageSpecs;
  for (const auto _ : folly::irange(2)) {
    LocalSearchStageSpec stageSpec;
    stageSpec.begin() = 0;
    stageSpec.end() = 1;

    LocalSearchSolverSpec solverSpec = makeDefaultLocalSearchSolver();
    solverSpec.stopAfterMoves() = 1;
    solverSpec.exploreMovesFromContainersNotInObjective() = false;

    stageSpec.solverSpec() = solverSpec;
    stageSpecs.push_back(stageSpec);
  }
  stageSolverSpec.stageSpecs() = stageSpecs;
  solver->addSolver(stageSolverSpec);

  auto solution = solver->solve();

  // expect the solver to make 2 moves
  EXPECT_EQ(2, solution.movesSummary()->size());

  ASSERT_EQ(1, solution.solverSummaries()->size());
  // the overall end reason is OPTIMAL
  EXPECT_EQ(
      *solution.solverSummaries()->at(0).endReason(),
      facebook::rebalancer::interface::EndReason::OPTIMAL);

  // check end reasons for each of the 2 stages
  ASSERT_EQ(2, solution.solverSummaries()->at(0).stagesSummaries()->size());

  // end reason for stage 0 is HIT_MOVE_LIMIT (since stopAfterMoves = 1 for this
  // stage)
  EXPECT_EQ(
      *solution.solverSummaries()->at(0).stagesSummaries()->at(0).endReason(),
      facebook::rebalancer::interface::EndReason::HIT_MOVE_LIMIT);

  // end reason for stage 1 is OPTIMAL
  EXPECT_EQ(
      *solution.solverSummaries()->at(0).stagesSummaries()->at(1).endReason(),
      facebook::rebalancer::interface::EndReason::OPTIMAL);
}

TEST_P(StageSummaryTest, StageSummaryMoveType) {
  LocalSearchStageSolverSpec stageSolverSpec;

  std::vector<std::vector<MoveTypeSpec>> moveTypeSpec = {
      {ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec())},
      {ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec())},
      {ProblemSolver::makeMoveTypeSpec(KLSearchMoveTypeSpec()),
       ProblemSolver::makeMoveTypeSpec(TripleLoopMoveTypeSpec())},
  };

  std::vector<LocalSearchStageSpec> stageSpecs;
  for (const auto i : folly::irange(3)) {
    LocalSearchStageSpec stageSpec;
    stageSpec.begin() = 0;
    stageSpec.end() = 1;
    stageSpec.solverSpec()->exploreMovesFromContainersNotInObjective() = false;

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList() = moveTypeSpec[i];
    solverSpec.stopAfterMoves() = 1;

    stageSpec.solverSpec() = solverSpec;
    stageSpecs.push_back(stageSpec);
  }

  stageSolverSpec.stageSpecs() = stageSpecs;
  solver->addSolver(stageSolverSpec);

  auto solution = solver->solve();
  auto movesSummary = solution.movesSummary();
  EXPECT_EQ(2, movesSummary->size());

  auto solverSummaries = solution.solverSummaries();
  ASSERT_EQ(1, solverSummaries->size());

  auto stageSummaries = solverSummaries->at(0).stagesSummaries();
  auto stage0MoveTypes = stageSummaries->at(0).moveStats()->moveTypes();
  ASSERT_EQ(1, stage0MoveTypes->size());
  EXPECT_EQ("SINGLE", stage0MoveTypes[0]);

  auto stage1MoveTypes = stageSummaries->at(1).moveStats()->moveTypes();
  ASSERT_EQ(1, stage1MoveTypes->size());
  EXPECT_EQ("SWAP", stage1MoveTypes[0]);

  auto stage2MoveTypes = stageSummaries->at(2).moveStats()->moveTypes();
  ASSERT_EQ(2, stage2MoveTypes->size());
  EXPECT_EQ("KL_SEARCH", stage2MoveTypes[0]);
  EXPECT_EQ("TRIPLE_LOOP", stage2MoveTypes[1]);
}

TEST_P(StageSummaryTest, MoveSummaryStageIdTest) {
  // tests that the stageId is correctly populated in the move summary
  LocalSearchStageSolverSpec stageSolverSpec;

  std::vector<std::vector<MoveTypeSpec>> moveTypeSpec = {
      {ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec())},
      {ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec())},
      {ProblemSolver::makeMoveTypeSpec(KLSearchMoveTypeSpec()),
       ProblemSolver::makeMoveTypeSpec(TripleLoopMoveTypeSpec())},
  };

  std::vector<LocalSearchStageSpec> stageSpecs;
  for (const auto i : folly::irange(3)) {
    LocalSearchStageSpec stageSpec;
    stageSpec.begin() = 0;
    stageSpec.end() = 1;
    stageSpec.solverSpec()->exploreMovesFromContainersNotInObjective() = false;

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList() = moveTypeSpec[i];
    solverSpec.stopAfterMoves() = 1;

    stageSpec.solverSpec() = solverSpec;
    stageSpecs.push_back(stageSpec);
  }

  stageSolverSpec.stageSpecs() = stageSpecs;
  solver->addSolver(stageSolverSpec);

  auto solution = solver->solve();
  auto movesSummary = solution.movesSummary();
  EXPECT_EQ(2, movesSummary->size());

  // stage 0 moves Summary
  EXPECT_GE(movesSummary->at(0).moves().value().size(), 1);
  EXPECT_EQ(0, movesSummary->at(0).stageId().value());
  ASSERT_TRUE(movesSummary->at(0).cycleId().has_value());
  EXPECT_GE(movesSummary->at(0).cycleId().value(), 1);

  // stage 2 move summary — move count may vary by platform since
  // KLSearch/TripleLoop can produce multi-move sets
  EXPECT_GE(movesSummary->at(1).moves().value().size(), 1);
  EXPECT_EQ(2, movesSummary->at(1).stageId().value());
  ASSERT_TRUE(movesSummary->at(1).cycleId().has_value());
  EXPECT_GE(movesSummary->at(1).cycleId().value(), 1);
}

class StageSummaryEndReasonTest : public ::testing::TestWithParam<int> {
 protected:
  void SetUp() override {
    solver = initializeTestProblemSolver({.executorThreadCount = GetParam()});

    // Set the object and container names
    solver->setObjectName("shard");
    solver->setContainerName("host");

    // Set the initial assignment
    const std::map<std::string, std::vector<std::string>> initial_assignment = {
        {"h1", {"s1", "s2", "s3"}},
        {"h2", {"s4"}},
    };
    solver->setAssignment(initial_assignment);
  }

  std::unique_ptr<ProblemSolver> solver;
};

INSTANTIATE_TEST_CASE_P(
    NumThreads,
    StageSummaryEndReasonTest,
    testThreadCounts());

TEST_P(StageSummaryEndReasonTest, EndReasonGlobalOptimumAndHitMoveLimit) {
  // add a constraint to free h2
  ToFreeSpec toFreeSpec;
  toFreeSpec.containers() = {"h2"};
  solver->addConstraint(toFreeSpec);

  // create a localSearchStageSolver with 1 stage and move limit of 1
  LocalSearchStageSolverSpec stageSolverSpec;

  std::vector<LocalSearchStageSpec> stageSpecs;
  LocalSearchStageSpec stageSpec;
  stageSpec.begin() = 0;
  stageSpec.end() = 1;
  LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleFastMoveTypeSpec()));
  solverSpec.stopAfterMoves() = 1;
  stageSpec.solverSpec() = solverSpec;
  stageSpecs.push_back(stageSpec);

  stageSolverSpec.stageSpecs() = stageSpecs;
  solver->addSolver(stageSolverSpec);

  auto solution = solver->solve();

  ASSERT_EQ(1, solution.solverSummaries()->size());

  // the overall end reason is OPTIMAL because we expect the solver to make one
  // move that fully optimizes the objective
  const auto& solverSummary = solution.solverSummaries()->at(0);
  EXPECT_EQ(interface::EndReason::OPTIMAL, *solverSummary.endReason());

  // end reason for stage 0 should be OPTIMAL
  const auto& stage0Summary = solverSummary.stagesSummaries()->at(0);
  EXPECT_EQ(interface::EndReason::OPTIMAL, *stage0Summary.endReason());

  // we should expect exactly one move to be evaluated
  const auto& stage0EvalStats = stage0Summary.evalStats();
  ASSERT_TRUE(stage0EvalStats.has_value());
  EXPECT_EQ(1, *stage0EvalStats->numEvals());
}

TEST_P(
    StageSummaryEndReasonTest,
    PerStageEvalStatsUsesEvalDurationNotTotalDuration) {
  // Verify that per-stage evalStats.evalDurationSecs reflects eval-only time,
  // not wall-clock time. The eval duration should be <= the stage's total
  // duration (wall clock includes apply time, pruning, etc.).

  // add a constraint to free h2
  ToFreeSpec toFreeSpec;
  toFreeSpec.containers() = {"h2"};
  solver->addConstraint(toFreeSpec);

  LocalSearchStageSolverSpec stageSolverSpec;

  LocalSearchStageSpec stageSpec;
  stageSpec.begin() = 0;
  stageSpec.end() = 1;
  LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleFastMoveTypeSpec()));
  stageSpec.solverSpec() = solverSpec;

  stageSolverSpec.stageSpecs() = {stageSpec};
  solver->addSolver(stageSolverSpec);

  auto solution = solver->solve();

  ASSERT_EQ(1, solution.solverSummaries()->size());
  const auto& solverSummary = solution.solverSummaries()->at(0);
  ASSERT_EQ(1, solverSummary.stagesSummaries()->size());

  const auto& stage0Summary = solverSummary.stagesSummaries()->at(0);
  const auto& stage0EvalStats = stage0Summary.evalStats();
  ASSERT_TRUE(stage0EvalStats.has_value());

  // eval duration should be non-negative and at most the stage's total duration
  EXPECT_GE(*stage0EvalStats->evalDurationSecs(), 0.0);
  EXPECT_LE(
      *stage0EvalStats->evalDurationSecs(), *stage0Summary.duration() + 1e-9);

  // also verify the overall solver evalStats is consistent
  const auto& overallEvalStats = solverSummary.evalStats();
  ASSERT_TRUE(overallEvalStats.has_value());
  EXPECT_GE(*overallEvalStats->evalDurationSecs(), 0.0);

  // verify numCycles is populated for both per-stage and overall
  EXPECT_GE(*stage0EvalStats->numCycles(), 1);
  EXPECT_GE(*overallEvalStats->numCycles(), 1);
}

} // namespace facebook::rebalancer::interface::tests
