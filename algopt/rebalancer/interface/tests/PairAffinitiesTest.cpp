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
#include "algopt/rebalancer/tests/SolverTestUtils.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <tuple>
#include <vector>

using facebook::algopt::isSolverUnavailable;
using facebook::algopt::solverName;
using facebook::algopt::testSolverPackages;

namespace facebook::rebalancer::interface::tests {

class PairAffinitiesTest
    : public ::testing::TestWithParam<
          std::tuple<int, SolverAlgoType, OptimalSolverPackage>> {
 protected:
  static int getThreadCount() {
    const auto [threadCount, algoType, solver] = GetParam();
    return threadCount;
  }
  static SolverAlgoType getSolverAlgoType() {
    const auto [threadCount, algoType, solver] = GetParam();
    return algoType;
  }
  static OptimalSolverPackage getSolverPackage() {
    const auto [threadCount, algoType, solver] = GetParam();
    return solver;
  }
  void SetUp() override {
    if (getSolverAlgoType() == OPTIMAL) {
      const auto solverPkg = getSolverPackage();
      if (isSolverUnavailable(solverPkg)) {
        GTEST_SKIP() << solverName(solverPkg) << " solver not available";
      }
    }
    solver =
        initializeTestProblemSolver({.executorThreadCount = getThreadCount()});
    solver->setObjectName("task");
    solver->setContainerName("host");

    switch (getSolverAlgoType()) {
      case OPTIMAL: {
        auto optimalSpec = OptimalSolverSpec{};
        optimalSpec.solverPackage() = getSolverPackage();
        solver->addSolver(optimalSpec);
        break;
      }
      case LOCALSEARCH: {
        auto spec = LocalSearchSolverSpec();
        spec.moveTypeList() = {
            ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec{}),
            ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec{})};
        solver->addSolver(spec);
        break;
      }
    }
  }

  std::unique_ptr<ProblemSolver> solver = nullptr;
};

INSTANTIATE_TEST_CASE_P(
    LocalSearch,
    PairAffinitiesTest,
    ::testing::Combine(
        testThreadCounts(),
        ::testing::Values(LOCALSEARCH),
        ::testing::Values(
            OptimalSolverPackage::GUROBI))); // ignored but required

INSTANTIATE_TEST_CASE_P(
    Optimal,
    PairAffinitiesTest,
    ::testing::Combine(
        testThreadCounts(),
        ::testing::Values(OPTIMAL),
        testSolverPackages()));

TEST_P(PairAffinitiesTest, Constraint) {
  /* In this example we have 3 hosts and 4 tasks. We will add pair affinity spec
  which will trigger the move and bring obects in same cotainer.
  */
  // There are 3 hosts and 4 tasks.
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1"}},
          {"host1", {"task2"}},
          {"host2", {"task3"}},
      });

  // We want task0, task2 on same host and task1, task3 on same host
  PairAffinitiesSpec pairAffinitiesSpec;
  pairAffinitiesSpec.name() = "test";
  pairAffinitiesSpec.scope() = "host";

  std::vector<PairAffinity> affinities;
  {
    PairAffinity affinity;
    ObjectPair objectPair;
    objectPair.object1() = "task0";
    objectPair.object2() = "task2";
    affinity.pair() = objectPair;
    affinity.affinity() = 1;
    affinities.push_back(std::move(affinity));
  }
  {
    PairAffinity affinity;
    ObjectPair objectPair;
    objectPair.object1() = "task1";
    objectPair.object2() = "task3";
    affinity.pair() = objectPair;
    affinity.affinity() = 1;
    affinities.push_back(std::move(affinity));
  }
  pairAffinitiesSpec.affinities() = std::move(affinities);

  solver->addConstraint(pairAffinitiesSpec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // ensure task1, task3 are on same host
  EXPECT_EQ(assignment["task1"], assignment["task3"]);

  // ensure task0, task2 are on same host
  EXPECT_EQ(assignment["task0"], assignment["task2"]);
}

TEST_P(PairAffinitiesTest, Goal) {
  const std::map<std::string, std::vector<std::string>> initialAssignment = {
      {"h1", {"t1", "t2", "t3"}}, {"h2", {"t4"}}};
  solver->setAssignment(initialAssignment);

  PairAffinity p1;
  ObjectPair objectPair1;
  objectPair1.object1() = "t1";
  objectPair1.object2() = "t2";
  p1.pair() = objectPair1;
  p1.affinity() = -2;

  PairAffinity p2;
  ObjectPair objectPair2;
  objectPair2.object1() = "t3";
  objectPair2.object2() = "t4";
  p2.pair() = objectPair2;
  p2.affinity() = 1;

  PairAffinitiesSpec pairAffinitiesSpec;
  pairAffinitiesSpec.affinities() = {p1, p2};
  solver->addGoal(pairAffinitiesSpec);

  AvoidMovingSpec avoidMovingSpec;
  avoidMovingSpec.objects() = {"t1", "t3"};
  solver->addConstraint(avoidMovingSpec);

  const auto solution = solver->solve();

  const std::map<std::string, std::vector<std::string>> expectedAssignment = {
      {"h1", {"t1", "t3", "t4"}}, {"h2", {"t2"}}};
  REBALANCER_EXPECT_EQ_ASSIGNMENT(expectedAssignment, solution);

  EXPECT_EQ(0, *solution.initialConstraint()->brokenCount());
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());

  switch (getSolverAlgoType()) {
    case OPTIMAL:
      EXPECT_NEAR(1, *solution.initialObjective()->value(), 1e-8);
      break;
    case LOCALSEARCH:
      // Without separated penalty objectives, local search also reports the
      // continuous penalty terms.
      EXPECT_NEAR(
          expectedLocalSearchGlobalObjective(1, 0.95),
          *solution.initialObjective()->value(),
          1e-8);
      break;
  }
  EXPECT_NEAR(-2, *solution.finalObjective()->value(), 1e-8);
}

TEST_P(PairAffinitiesTest, MultiplePairAffinities) {
  /* This test is the same as Constraint test but with multiple pair affinities
   * to make sure they are both converted to ColocateGroupSpec correctly
   */
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1"}},
          {"host1", {"task2"}},
          {"host2", {"task3"}},
      });

  // We want task0, task2 on same host
  {
    PairAffinitiesSpec pairAffinitiesSpec;
    pairAffinitiesSpec.name() = "pairAffinitiesSpec1";
    pairAffinitiesSpec.scope() = "host";
    PairAffinity affinity;
    ObjectPair objectPair;
    objectPair.object1() = "task0";
    objectPair.object2() = "task2";
    affinity.pair() = objectPair;
    affinity.affinity() = 1;
    pairAffinitiesSpec.affinities()->push_back(std::move(affinity));
    solver->addConstraint(pairAffinitiesSpec);
  }

  {
    // we want task1, task3 on same host
    PairAffinitiesSpec pairAffinitiesSpec;
    pairAffinitiesSpec.name() = "pairAffinitiesSpec2";
    pairAffinitiesSpec.scope() = "host";
    PairAffinity affinity;
    ObjectPair objectPair;
    objectPair.object1() = "task1";
    objectPair.object2() = "task3";
    affinity.pair() = objectPair;
    affinity.affinity() = 1;
    pairAffinitiesSpec.affinities()->push_back(std::move(affinity));
    solver->addConstraint(pairAffinitiesSpec);
  }

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // ensure task1, task3 are on same host
  EXPECT_EQ(assignment["task1"], assignment["task3"]);

  // ensure task0, task2 are on same host
  EXPECT_EQ(assignment["task0"], assignment["task2"]);
}

} // namespace facebook::rebalancer::interface::tests
