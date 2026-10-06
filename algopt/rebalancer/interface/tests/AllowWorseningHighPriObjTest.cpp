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

#include <tuple>

namespace facebook::rebalancer::interface::tests {

// Base class with shared setup logic
class AllowWorseningHighPriObjTestBase {
 protected:
  void setUpProblem(int threadCount) {
    solver = initializeTestProblemSolver({.executorThreadCount = threadCount});

    solver->setObjectName("shard");
    solver->setContainerName("host");

    const std::map<std::string, std::vector<std::string>> initial_assignment = {
        {"dummy", {"s1", "s2", "s3", "s4", "s5"}},
        {"h1", {}},
        {"h2", {}},
        {"h3", {"s6"}},
    };
    solver->setAssignment(initial_assignment);

    // h1 can have at most 4 shards
    {
      CapacitySpec capacitySpec;
      capacitySpec.scope() = "host";
      capacitySpec.dimension() = "shard_count";
      capacitySpec.definition() = CapacitySpecDefinition::AFTER;
      capacitySpec.bound() = CapacitySpecBound::MAX;
      capacitySpec.limit()->scopeItemLimits() = {{"h1", 4}};
      capacitySpec.filter()->itemsWhitelist() = {"h1"};

      solver->addConstraint(capacitySpec);
    }

    // dummy cannot accept any new shards
    {
      NonAcceptingSpec nonAcceptingSpec;
      nonAcceptingSpec.scope() = "host";
      nonAcceptingSpec.items() = {"dummy"};
      solver->addConstraint(std::move(nonAcceptingSpec));
    }

    // maximize the number of shards on h1
    {
      CapacitySpec capacitySpec;
      capacitySpec.scope() = "host";
      capacitySpec.dimension() = "shard_count";
      capacitySpec.definition() = CapacitySpecDefinition::AFTER;
      capacitySpec.bound() = CapacitySpecBound::MIN;
      capacitySpec.limit()->globalLimit() = 4;
      capacitySpec.filter()->itemsWhitelist() = {"h1"};

      solver->addGoal(std::move(capacitySpec), /*weight=*/1, /*tuplePos=*/0);
    }

    // Minimize the max number of shards on hosts h1 and h2
    {
      BalanceSpec balanceSpec;
      balanceSpec.scope() = "host";
      balanceSpec.dimension() = "shard_count";
      balanceSpec.formula() = BalanceSpecFormula::MAX;
      balanceSpec.filter()->itemsWhitelist() = {"h1", "h2"};
      auto& upperBounds = balanceSpec.upperBounds().ensure();
      upperBounds.type() = LimitType::ABSOLUTE;
      upperBounds.globalLimit() = 0;
      balanceSpec.boundType() = BalanceSpecBoundType::RELATIVE;

      solver->addGoal(std::move(balanceSpec), /*weight=*/1, /*tuplePos=*/1);
    }

    // Balance h1 and h3
    {
      BalanceSpec balanceSpec;
      balanceSpec.scope() = "host";
      balanceSpec.dimension() = "shard_count";
      balanceSpec.formula() = BalanceSpecFormula::MAX;
      balanceSpec.filter()->itemsWhitelist() = {"h1", "h3"};
      auto& upperBounds = balanceSpec.upperBounds().ensure();
      upperBounds.type() = LimitType::ABSOLUTE;
      upperBounds.globalLimit() = 0;
      balanceSpec.boundType() = BalanceSpecBoundType::RELATIVE;

      solver->addGoal(std::move(balanceSpec), /*weight=*/1, /*tuplePos=*/2);
    }
  }

  std::unique_ptr<interface::ProblemSolver> solver;
};

class AllowWorseningHighPriObjTest : public AllowWorseningHighPriObjTestBase,
                                     public ::testing::TestWithParam<int> {
 protected:
  void setUpProblem() {
    AllowWorseningHighPriObjTestBase::setUpProblem(GetParam());
  }
};

INSTANTIATE_TEST_CASE_P(
    NumThreads,
    AllowWorseningHighPriObjTest,
    testThreadCounts());

TEST_P(AllowWorseningHighPriObjTest, LocalSearchStageSolver) {
  setUpProblem();

  // create a localSearchStageSolver with 3 stages
  LocalSearchStageSolverSpec stageSolverSpec;
  // stage1: goals: [0, 1)
  {
    LocalSearchStageSpec stageSpec;
    stageSpec.begin() = 0;
    stageSpec.end() = 1;
    stageSpec.solverSpec()->moveTypeList() = {
        ProblemSolver::makeMoveTypeSpec(SingleFastMoveTypeSpec())};
    stageSolverSpec.stageSpecs()->push_back(std::move(stageSpec));
  }

  // stage2: goals: [1, 2)
  {
    LocalSearchStageSpec stageSpec;
    stageSpec.begin() = 1;
    stageSpec.end() = 2;
    stageSpec.solverSpec()->moveTypeList() = {
        ProblemSolver::makeMoveTypeSpec(SingleFastMoveTypeSpec())};

    // allow 20% worsening of goal 0 value from stage0 or by absolute value
    // of 1
    algopt::common::thrift::AllowedWorsening allowedWorsening;
    allowedWorsening.percent() = 20;
    allowedWorsening.absolute() = 1;

    algopt::common::thrift::HigherPriorityObjectivesConfig
        higherPriorityObjConfig;
    higherPriorityObjConfig.tuplePosToAllowedWorsening() = {
        {0, allowedWorsening}};
    stageSpec.higherPriorityObjConfig() = std::move(higherPriorityObjConfig);

    stageSolverSpec.stageSpecs()->push_back(std::move(stageSpec));
  }

  // stage3: goals: [2, 3)
  {
    LocalSearchStageSpec stageSpec;
    stageSpec.begin() = 2;
    stageSpec.end() = 3;
    stageSpec.solverSpec()->moveTypeList() = {
        ProblemSolver::makeMoveTypeSpec(SingleFastMoveTypeSpec())};

    // allow worsening of goal0 value from stage1 by 100% (i.e., allow it to
    // double)
    algopt::common::thrift::AllowedWorsening allowedWorsening0;
    allowedWorsening0.percent() = 100;

    algopt::common::thrift::HigherPriorityObjectivesConfig
        higherPriorityObjConfig;
    higherPriorityObjConfig.tuplePosToAllowedWorsening() = {
        {0, allowedWorsening0}};
    stageSpec.higherPriorityObjConfig() = std::move(higherPriorityObjConfig);

    stageSolverSpec.stageSpecs()->push_back(std::move(stageSpec));
  }

  solver->addSolver(stageSolverSpec);

  auto solution = solver->solve();

  // initially objective is (4, 0, 1)
  auto& initialGoals = *solution.initialGlobalObjective()->goals();
  EXPECT_NEAR(4.0, *initialGoals.at(0).value(), 1e-8);
  EXPECT_NEAR(0.0, *initialGoals.at(1).value(), 1e-8);
  EXPECT_NEAR(1.0, *initialGoals.at(2).value(), 1e-8);

  // Note that if we do not allow any worsening of goal 0 in stage 1, the
  // final objective will be (0, 4, 4). However, we allow 20% or absolute 1 of
  // worsening of goal 0 from stage0, and also 100% worsening of goal0 from
  // stage1 in stage2. So, the final expected objective is (2, 2, 2)
  auto& finalGoals = *solution.finalGlobalObjective()->goals();
  EXPECT_NEAR(2.0, *finalGoals.at(0).value(), 1e-8);
  EXPECT_NEAR(2.0, *finalGoals.at(1).value(), 1e-8);
  EXPECT_NEAR(2.0, *finalGoals.at(2).value(), 1e-8);
}

class AllowWorseningHighPriObjOptimalSolverTest
    : public AllowWorseningHighPriObjTestBase,
      public ::testing::TestWithParam<std::tuple<int, OptimalSolverPackage>> {
 protected:
  static int getThreadCount() {
    return std::get<0>(GetParam());
  }
  static OptimalSolverPackage getSolverPackage() {
    return std::get<1>(GetParam());
  }

  void SetUp() override {
    switch (getSolverPackage()) {
      case OptimalSolverPackage::GUROBI:
        REBALANCER_SKIP_IF_NO_GUROBI();
        break;
      case OptimalSolverPackage::XPRESS:
        REBALANCER_SKIP_IF_NO_XPRESS();
        break;
      case OptimalSolverPackage::HIGHS:
        REBALANCER_SKIP_IF_NO_HIGHS();
        break;
    }
  }

  void setUpProblem() {
    AllowWorseningHighPriObjTestBase::setUpProblem(getThreadCount());
  }
};

INSTANTIATE_TEST_CASE_P(
    NumThreadsAndSolverTypes,
    AllowWorseningHighPriObjOptimalSolverTest,
    ::testing::Combine(
        testThreadCounts(),
        ::testing::Values(
            OptimalSolverPackage::GUROBI,
            OptimalSolverPackage::XPRESS,
            OptimalSolverPackage::HIGHS)));

TEST_P(AllowWorseningHighPriObjOptimalSolverTest, OptimalSolver) {
  setUpProblem();

  algopt::common::thrift::AllowedWorsening allowedWorsening0;
  allowedWorsening0.percent() = 100;
  allowedWorsening0.absolute() = 2;

  algopt::common::thrift::HigherPriorityObjectivesConfig
      higherPriorityObjConfig;
  higherPriorityObjConfig.tuplePosToAllowedWorsening() = {
      {0, allowedWorsening0}};

  interface::OptimalSolverSpec optimalSolverSpec;
  optimalSolverSpec.multiObjSolveSettings()->higherPriorityObjConfig() =
      std::move(higherPriorityObjConfig);
  optimalSolverSpec.solverPackage() = getSolverPackage();

  solver->addSolver(optimalSolverSpec);

  auto solution = solver->solve();

  // initially objective is (4, 0, 1)
  auto& initialGoals = *solution.initialGlobalObjective()->goals();
  EXPECT_NEAR(4.0, *initialGoals.at(0).value(), 1e-8);
  EXPECT_NEAR(0.0, *initialGoals.at(1).value(), 1e-8);
  EXPECT_NEAR(1.0, *initialGoals.at(2).value(), 1e-8);

  // Note that if we do not allow any worsening of goal0, the final objective
  // will be (0, 4, 4). However, we allow 100% or absolute 2 of worsening of
  // goal0. So, the final expected objective is (2, 2, 2)
  auto& finalGoals = *solution.finalGlobalObjective()->goals();
  EXPECT_NEAR(2.0, *finalGoals.at(0).value(), 1e-8);
  EXPECT_NEAR(2.0, *finalGoals.at(1).value(), 1e-8);
  EXPECT_NEAR(2.0, *finalGoals.at(2).value(), 1e-8);
}

TEST_P(
    AllowWorseningHighPriObjOptimalSolverTest,
    OptimalSolverFinalTupleWorseThanInitial) {
  solver =
      initializeTestProblemSolver({.executorThreadCount = getThreadCount()});

  solver->setObjectName("shard");
  solver->setContainerName("host");

  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"dummy", {"s1", "s2", "s3", "s4"}},
      {"h1", {}},
      {"h2", {}},
      {"h3", {"s5", "s6", "s7"}},
  };
  solver->setAssignment(initial_assignment);

  // h1, h2, h3 cannot accept any new shards
  {
    NonAcceptingSpec nonAcceptingSpec;
    nonAcceptingSpec.scope() = "host";
    nonAcceptingSpec.items() = {"h1", "h2", "h3"};
    solver->addConstraint(std::move(nonAcceptingSpec));
  }

  // free dummy container
  {
    ToFreeSpec toFreeSpec;
    toFreeSpec.name() = "to free dummy";
    toFreeSpec.containers() = {"dummy"};
    solver->addGoal(std::move(toFreeSpec), /*weight=*/1, /*tuplePos=*/0);
  }

  // free h3
  {
    ToFreeSpec toFreeSpec;
    toFreeSpec.name() = "to free h3";
    toFreeSpec.containers() = {"h3"};
    solver->addGoal(std::move(toFreeSpec), /*weight=*/1, /*tuplePos=*/1);
  }

  algopt::common::thrift::AllowedWorsening allowedWorsening0;
  allowedWorsening0.absolute() = 1;
  allowedWorsening0.percent() = 50;

  algopt::common::thrift::HigherPriorityObjectivesConfig
      higherPriorityObjConfig;
  higherPriorityObjConfig.tuplePosToAllowedWorsening() = {
      {0, allowedWorsening0}};

  interface::OptimalSolverSpec optimalSolverSpec;
  optimalSolverSpec.multiObjSolveSettings()->higherPriorityObjConfig() =
      std::move(higherPriorityObjConfig);
  optimalSolverSpec.solverPackage() = getSolverPackage();

  solver->addSolver(optimalSolverSpec);

  auto solution = solver->solve();

  // initially, objective is (4, 3)
  auto& initialGoals = *solution.initialGlobalObjective()->goals();
  EXPECT_NEAR(4.0, *initialGoals.at(0).value(), 1e-8);
  EXPECT_NEAR(3.0, *initialGoals.at(1).value(), 1e-8);

  // Note that if we do not allow any worsening then the solver cannot make any
  // move. But since we allow  worsening of goal 0 by 1 or 50%, the final
  // expected objective is (6, 1)
  auto& finalGoals = *solution.finalGlobalObjective()->goals();
  EXPECT_NEAR(6.0, *finalGoals.at(0).value(), 1e-8);
  EXPECT_NEAR(1.0, *finalGoals.at(1).value(), 1e-8);
}

} // namespace facebook::rebalancer::interface::tests
