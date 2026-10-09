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

#include <gtest/gtest.h>

#include <algorithm>

using namespace std;
using namespace facebook::algopt;
using namespace facebook::rebalancer::interface;
using ListFilterType = facebook::algopt::common::thrift::FilterType;

class EquivalentObjectsTest : public ::testing::TestWithParam<int> {};

INSTANTIATE_TEST_CASE_P(NumThreads, EquivalentObjectsTest, testThreadCounts());

namespace {
constexpr std::string_view kBalanceGoalName = "balance cpu";
constexpr std::string_view kDummyConstraint =
    "dummy constraint that makes all objects unique";

std::unique_ptr<ProblemSolver> buildProblem(
    int executorThreadCount,
    bool addDummyCapacityConstraint = false,
    bool addBalanceGoal = true,
    const std::optional<std::string>& toFreeHost = std::nullopt,
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
        initialAssignment = {
            {"h1", {"s1", "s2"}},
            {"h2", {"s3", "s4", "s5"}}}) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = executorThreadCount});
  solver->setObjectName("shard");
  solver->setContainerName("host");
  solver->setAssignment(initialAssignment);
  solver->addObjectDimension(
      "cpu",
      std::map<std::string, double>{
          {"s1", 10}, {"s2", 20}, {"s3", 10}, {"s4", 30}, {"s5", 30}});

  solver->addObjectDimension(
      "index",
      std::map<std::string, double>{
          {"s1", 1}, {"s2", 2}, {"s3", 3}, {"s4", 4}, {"s5", 5}});

  if (addDummyCapacityConstraint) {
    CapacitySpec capacitySpec;
    capacitySpec.name() = kDummyConstraint;
    capacitySpec.scope() = "host";
    capacitySpec.dimension() = "index";
    capacitySpec.limit()->type() = LimitType::ABSOLUTE;
    // chose a limity sufficiently large so that this constraint is trivially
    // satisfied
    capacitySpec.limit()->globalLimit() = 100;
    capacitySpec.bound() = CapacitySpecBound::MAX;
    solver->addConstraint(capacitySpec);
  }

  if (addBalanceGoal) {
    BalanceSpec balanceSpec;
    balanceSpec.name() = kBalanceGoalName;
    balanceSpec.scope() = "host";
    balanceSpec.dimension() = "cpu";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    solver->addGoal(balanceSpec);
  }

  if (toFreeHost) {
    ToFreeSpec toFreeSpec;
    toFreeSpec.name() = fmt::format("Free container {}", *toFreeHost);
    toFreeSpec.containers() = {*toFreeHost};
    solver->addGoal(toFreeSpec);
  }
  return solver;
}

// Three stages that each apply one move. Stages 0 and 2 put all objects in one
// equivalence set, and stage 1 puts each object in its own set.
LocalSearchStageSolverSpec makeStagesSwitchingEquivalenceSets(
    const MoveTypeSpec& moveTypeSpec) {
  // this will result in typical equivalence sets as per balance cpu goal
  // (excluding kDummyConstraint)
  CustomEquivalenceSetConfig onlyEquivalenceSetConfig;
  onlyEquivalenceSetConfig.constraintSelectionConfig()->stringsToFilter() = {};
  onlyEquivalenceSetConfig.constraintSelectionConfig()->filterType() =
      ListFilterType::ALLOWLIST;
  onlyEquivalenceSetConfig.goalSelectionConfig()->stringsToFilter() = {};
  onlyEquivalenceSetConfig.goalSelectionConfig()->filterType() =
      ListFilterType::ALLOWLIST;

  // this will result in trivial equivalence set where each object is in its own
  // equivalence set (due to kDummyConstraint)
  auto trivialEquivalenceSetConfig = onlyEquivalenceSetConfig;
  trivialEquivalenceSetConfig.constraintSelectionConfig()
      ->stringsToFilter()
      ->emplace_back(kDummyConstraint);

  LocalSearchStageSolverSpec stageSolver;
  // by default, all stages will have the standard equivalence sets
  stageSolver.customEquivalenceSetConfig() =
      std::move(onlyEquivalenceSetConfig);

  LocalSearchStageSpec stageTemplate;
  stageTemplate.begin() = 0;
  stageTemplate.end() = 1;
  stageTemplate.solverSpec()->stopAfterMoves() = 1;
  stageTemplate.solverSpec()->moveTypeList()->emplace_back(moveTypeSpec);

  auto stage0 = stageTemplate;
  stage0.name() = "Stage 0: only one equivalence set";
  stageSolver.stageSpecs()->emplace_back(std::move(stage0));

  // stage 1 will have as many equivalence sets as objects
  auto stage1 = stageTemplate;
  stage1.name() = "Stage 1: distinct equivalence sets";
  stage1.solverSpec()->customEquivalenceSetConfig() =
      std::move(trivialEquivalenceSetConfig);
  stageSolver.stageSpecs()->emplace_back(std::move(stage1));

  auto stage2 = stageTemplate;
  stage2.name() = "Stage 2: only one equivalence set";
  stageSolver.stageSpecs()->emplace_back(std::move(stage2));
  return stageSolver;
}
} // namespace

TEST_P(EquivalentObjectsTest, Basic) {
  auto solver = buildProblem(GetParam());

  solver->addSolver(makeDefaultLocalSearchSolver());
  solver->publishEquivalenceSetInfo();

  auto solution = solver->solve();

  auto& equivSets = *solution.equivalenceSetInfo()->equivalenceSets();
  vector<vector<string>> equivObjectsList;
  for (auto& equivSet : equivSets) {
    auto equivObjects = *equivSet.objectNames();
    sort(equivObjects.begin(), equivObjects.end());
    equivObjectsList.emplace_back(std::move(equivObjects));
  }
  sort(equivObjectsList.begin(), equivObjectsList.end());

  const vector<vector<string>> expected = {
      {"s1", "s3"},
      {"s2"},
      {"s4", "s5"},
  };
  EXPECT_EQ(expected, equivObjectsList);
}

TEST_P(EquivalentObjectsTest, CustomEquivalenceSetsAllowList) {
  auto solver = buildProblem(GetParam());
  auto localSearchSpec = makeDefaultLocalSearchSolver();
  CustomEquivalenceSetConfig customEquivalenceSetConfig;
  customEquivalenceSetConfig.goalSelectionConfig()
      ->stringsToFilter()
      ->emplace_back(kBalanceGoalName);
  customEquivalenceSetConfig.goalSelectionConfig()->filterType() =
      ListFilterType::ALLOWLIST;
  localSearchSpec.customEquivalenceSetConfig() =
      std::move(customEquivalenceSetConfig);

  solver->addSolver(localSearchSpec);
  solver->publishEquivalenceSetInfo();

  auto solution = solver->solve();

  auto& equivSets = *solution.equivalenceSetInfo()->equivalenceSets();
  vector<vector<string>> equivObjectsList;
  for (auto& equivSet : equivSets) {
    auto equivObjects = *equivSet.objectNames();
    sort(equivObjects.begin(), equivObjects.end());
    equivObjectsList.emplace_back(std::move(equivObjects));
  }
  sort(equivObjectsList.begin(), equivObjectsList.end());

  const vector<vector<string>> expected = {
      {"s1", "s3"},
      {"s2"},
      {"s4", "s5"},
  };
  EXPECT_EQ(expected, equivObjectsList);
}

TEST_P(EquivalentObjectsTest, CustomEquivalenceSetsDenyList) {
  auto solver = buildProblem(GetParam(), /*addDummyCapacityConstraint=*/true);
  auto localSearchSpec = makeDefaultLocalSearchSolver();
  CustomEquivalenceSetConfig customEquivalenceSetConfig;
  // block dummy capacity constraint
  customEquivalenceSetConfig.constraintSelectionConfig()->filterType() =
      ListFilterType::BLOCKLIST;
  customEquivalenceSetConfig.constraintSelectionConfig()
      ->stringsToFilter()
      ->emplace_back(kDummyConstraint);
  // do not block any goals
  customEquivalenceSetConfig.goalSelectionConfig()->filterType() =
      ListFilterType::BLOCKLIST;
  customEquivalenceSetConfig.goalSelectionConfig()->stringsToFilter() = {};

  localSearchSpec.customEquivalenceSetConfig() =
      std::move(customEquivalenceSetConfig);

  solver->addSolver(localSearchSpec);
  solver->publishEquivalenceSetInfo();

  auto solution = solver->solve();

  auto& equivSets = *solution.equivalenceSetInfo()->equivalenceSets();
  vector<vector<string>> equivObjectsList;
  for (auto& equivSet : equivSets) {
    auto equivObjects = *equivSet.objectNames();
    sort(equivObjects.begin(), equivObjects.end());
    equivObjectsList.emplace_back(std::move(equivObjects));
  }
  sort(equivObjectsList.begin(), equivObjectsList.end());

  const vector<vector<string>> expected = {
      {"s1", "s3"},
      {"s2"},
      {"s4", "s5"},
  };
  EXPECT_EQ(expected, equivObjectsList);
}

TEST_P(EquivalentObjectsTest, CustomEquivalenceSetsStageSolver) {
  // start with an optimal assignment so that there is no progress needed
  // that way we can count, how many evaluations were done in each stage
  const std::vector<std::pair<std::string, std::vector<std::string>>>
      initialAssignment = {{"h1", {"s1", "s2", "s3", "s4", "s5"}}, {"h2", {}}};

  // the problem is deliberately chosen so that "h1" is always the hot container
  // and the "optimization" simply entails moving objects out of "h1".
  auto solver = buildProblem(
      GetParam(),
      /*addDummyCapacityConstraint=*/true,
      /*addBalanceGoal=*/false,
      /*toFreeHost=*/"h1",
      initialAssignment);

  solver->addSolver(makeStagesSwitchingEquivalenceSets(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec())));
  auto solution = solver->solve();
  folly::F14FastMap<std::string, int> containerToObjectCount;

  for (auto [task, host] : *solution.assignment()) {
    XLOG(INFO) << fmt::format("{} -> {}", task, host);
    containerToObjectCount[host]++;
  }

  // verify that after solve 2 objects remain in h1
  EXPECT_EQ(2, containerToObjectCount.at("h1"));
  // and three objects have moved to h2
  EXPECT_EQ(3, containerToObjectCount.at("h2"));

  assert(solution.movesSummary());
  const auto& movesSummary = *solution.movesSummary();
  EXPECT_EQ(3, movesSummary.size());

  // Stage-0 : since all objects are in one equivalence set, this stage
  //  evaluates 1 move (out of 5 objects available), applies one move
  auto stage0Moves = movesSummary.at(0);
  EXPECT_EQ(1, *stage0Moves.evalsCount());
  EXPECT_EQ(1, stage0Moves.moves()->size());

  // Stage-1 : since every object is in its own equivalence set, this stage
  //  evaluates 4 moves (out of 4 objects available), applies one move
  auto stage1Moves = movesSummary.at(1);
  EXPECT_EQ(4, *stage1Moves.evalsCount());
  EXPECT_EQ(1, stage1Moves.moves()->size());

  // Stage-2 : since all objects are in one equivalence set, this stage
  //  evaluates 1 move (out of 3 objects available), applies one move
  auto stage2Moves = movesSummary.at(2);
  EXPECT_EQ(1, *stage2Moves.evalsCount());
  EXPECT_EQ(1, stage2Moves.moves()->size());
}

// SINGLE_FIXED_SOURCE lists objects through the assignment's equivalence index,
// so every stage must index its own equivalence sets.
TEST_P(EquivalentObjectsTest, CustomEquivalenceSetsStageSolverFixedSource) {
  const std::vector<std::pair<std::string, std::vector<std::string>>>
      initialAssignment = {{"h1", {"s1", "s2", "s3", "s4", "s5"}}, {"h2", {}}};
  auto solver = buildProblem(
      GetParam(),
      /*addDummyCapacityConstraint=*/true,
      /*addBalanceGoal=*/false,
      /*toFreeHost=*/"h1",
      initialAssignment);
  // h1 is not its own source while it is hot, so objects move to h2 when h2 is
  SingleFixedSourceMoveTypeSpec fixedSourceSpec;
  fixedSourceSpec.specialContainer() = "h1";
  solver->addSolver(makeStagesSwitchingEquivalenceSets(
      ProblemSolver::makeMoveTypeSpec(fixedSourceSpec)));

  const auto solution = solver->solve();

  std::vector<int64_t> evalsPerStage;
  for (const auto& stageMoves : *solution.movesSummary()) {
    EXPECT_EQ(1, stageMoves.moves()->size());
    evalsPerStage.push_back(*stageMoves.evalsCount());
  }
  // one object per equivalence set: 1 of 5, then 4 of 4, then 1 of 3
  const std::vector<int64_t> expectedEvalsPerStage = {1, 4, 1};
  EXPECT_EQ(expectedEvalsPerStage, evalsPerStage);
}
