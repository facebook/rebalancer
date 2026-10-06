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

#include <folly/container/irange.h>
#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

using namespace facebook::rebalancer::interface;
using facebook::algopt::isSolverUnavailable;
using facebook::algopt::solverName;
using facebook::algopt::testSolverPackages;

class ObjectiveTupleTest
    : public ::testing::TestWithParam<
          std::tuple<int, OptimalSolverPackage, bool /*simplified*/>> {
 protected:
  static int getThreadCount() {
    const auto [threadCount, solver, simplified] = GetParam();
    return threadCount;
  }

  static OptimalSolverPackage getSolverPackage() {
    const auto [threadCount, solver, simplified] = GetParam();
    return solver;
  }

  static bool isSimplified() {
    const auto [threadCount, solver, simplified] = GetParam();
    return simplified;
  }

  void SetUp() override {
    const auto solver = getSolverPackage();
    if (isSolverUnavailable(solver)) {
      GTEST_SKIP() << solverName(solver) << " solver not available";
    }

    solver_ =
        initializeTestProblemSolver({.executorThreadCount = getThreadCount()});
    solver_->setObjectName("task");
    solver_->setContainerName("host");

    solver_->setAssignment(
        std::vector<std::pair<std::string, std::vector<std::string>>>{
            {"host0", {"task0", "task1", "task2", "task3", "task4", "task5"}},
            {"host1", {"task6"}},
            {"host2", {"task7"}},
        });
  }

  static std::string getTimeLimitParam() {
    if (getSolverPackage() == OptimalSolverPackage::GUROBI) {
      return "GRB_TimeLimit";
    }
    if (getSolverPackage() == OptimalSolverPackage::HIGHS) {
      return "time_limit";
    }
    return "XPRS_TIMELIMIT";
  }

  static void checkProblemProfile(AssignmentSolution& solution, int nObj) {
    // check that there are k entries in solverStatsForObjectives(), where k =
    // number of objectives
    EXPECT_EQ(
        solution.problemProfile()
            ->optimalSolverProfile()
            ->solverStatsForObjectives()
            ->size(),
        nObj);
  }

  void setUpProblem() {
    solver_ =
        initializeTestProblemSolver({.executorThreadCount = getThreadCount()});

    solver_->setObjectName("shard");
    solver_->setContainerName("host");

    const std::map<std::string, std::vector<std::string>> initial_assignment = {
        {"dummy", {"s1", "s2", "s3", "s4", "s5"}},
        {"h1", {}},
        {"h2", {}},
        {"h3", {"s6"}},
    };
    solver_->setAssignment(initial_assignment);

    // h1 can have at most 4 shards
    {
      CapacitySpec capacitySpec;
      capacitySpec.scope() = "host";
      capacitySpec.dimension() = "shard_count";
      capacitySpec.definition() = CapacitySpecDefinition::AFTER;
      capacitySpec.bound() = CapacitySpecBound::MAX;
      capacitySpec.limit()->scopeItemLimits() = {{"h1", 4}};
      capacitySpec.filter()->itemsWhitelist() = {"h1"};

      solver_->addConstraint(capacitySpec);
    }

    // dummy cannot accept any new shards
    {
      NonAcceptingSpec nonAcceptingSpec;
      nonAcceptingSpec.scope() = "host";
      nonAcceptingSpec.items() = {"dummy"};
      solver_->addConstraint(std::move(nonAcceptingSpec));
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

      solver_->addGoal(std::move(capacitySpec), /*weight=*/1, /*tuplePos=*/0);
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

      solver_->addGoal(std::move(balanceSpec), /*weight=*/1, /*tuplePos=*/1);
    }

    // Balance h1 and h3
    {
      BalanceSpec balanceSpec;
      balanceSpec.scope() = "host";
      balanceSpec.dimension() = "shard_count";
      balanceSpec.formula() = BalanceSpecFormula::MAX;
      balanceSpec.filter()->itemsWhitelist() = {"h1", "h3"};
      balanceSpec.upperBound() = 0;
      balanceSpec.boundType() = BalanceSpecBoundType::RELATIVE;

      solver_->addGoal(std::move(balanceSpec), /*weight=*/1, /*tuplePos=*/2);
    }
  }

  void multiObjectiveSetup() {
    solver_ =
        initializeTestProblemSolver({.executorThreadCount = getThreadCount()});

    solver_->setObjectName("shard");
    solver_->setContainerName("host");

    const std::map<std::string, std::vector<std::string>> initialAssignment = {
        {"dummy", {}},
        {"h1", {"s1", "s2"}},
        {"h2", {"s3", "s4"}},
        {"h3", {"s5", "s6"}},
    };
    solver_->setAssignment(initialAssignment);

    {
      ToFreeSpec toFreeSpec;
      toFreeSpec.containers() = {"h1"};
      solver_->addGoal(std::move(toFreeSpec), /*weight=*/1, /*tuplePos=*/0);
    }

    {
      ToFreeSpec toFreeSpec;
      toFreeSpec.containers() = {"h2"};
      solver_->addGoal(std::move(toFreeSpec), /*weight=*/1, /*tuplePos=*/1);
    }

    {
      ToFreeSpec toFreeSpec;
      toFreeSpec.containers() = {"h3"};
      solver_->addGoal(std::move(toFreeSpec), /*weight=*/1, /*tuplePos=*/2);
    }

    {
      CapacitySpec capacitySpec;
      capacitySpec.scope() = "host";
      capacitySpec.dimension() = "shard_count";
      capacitySpec.definition() = CapacitySpecDefinition::AFTER;
      capacitySpec.bound() = CapacitySpecBound::MIN;
      capacitySpec.limit()->globalLimit() = 2;
      capacitySpec.limit()->type() = LimitType::ABSOLUTE;
      capacitySpec.filter()->itemsWhitelist() = {"h1"};

      solver_->addGoal(capacitySpec, /*weight=*/1, /*tuplePos=*/3);
    }

    {
      CapacitySpec capacitySpec;
      capacitySpec.scope() = "host";
      capacitySpec.dimension() = "shard_count";
      capacitySpec.definition() = CapacitySpecDefinition::AFTER;
      capacitySpec.bound() = CapacitySpecBound::MIN;
      capacitySpec.limit()->globalLimit() = 2;
      capacitySpec.limit()->type() = LimitType::ABSOLUTE;
      capacitySpec.filter()->itemsWhitelist() = {"h2"};

      solver_->addGoal(capacitySpec, /*weight=*/1, /*tuplePos=*/4);
    }

    {
      CapacitySpec capacitySpec;
      capacitySpec.scope() = "host";
      capacitySpec.dimension() = "shard_count";
      capacitySpec.definition() = CapacitySpecDefinition::AFTER;
      capacitySpec.bound() = CapacitySpecBound::MIN;
      capacitySpec.limit()->globalLimit() = 2;
      capacitySpec.limit()->type() = LimitType::ABSOLUTE;
      capacitySpec.filter()->itemsWhitelist() = {"h3"};

      solver_->addGoal(capacitySpec, /*weight=*/1, /*tuplePos=*/5);
    }
  }

  std::unique_ptr<ProblemSolver> solver_;
};

INSTANTIATE_TEST_CASE_P(
    AllSolvers,
    ObjectiveTupleTest,
    ::testing::Combine(
        testThreadCounts(),
        testSolverPackages(),
        ::testing::Values(false, true)));

TEST_P(ObjectiveTupleTest, AllLinearExprs) {
  // SetUp() sets up the hosts, tasks, and initial assignment

  {
    // add a constraint saying host0 can have at most 1 task, host1 can contain
    // at most 3 tasks, and host2 can contain at most 4 tasks.
    CapacitySpec capacitySpec;
    capacitySpec.scope() = "host";
    capacitySpec.dimension() = "task_count";

    Limit limit;
    limit.type() = LimitType::ABSOLUTE;
    limit.scopeItemLimits() = {{"host0", 1}, {"host1", 3}, {"host2", 4}};
    capacitySpec.limit() = limit;

    solver_->addConstraint(capacitySpec);
  }

  {
    // add a goal at tupleIndex 1 to make "host0" as empty as possible"
    ToFreeSpec toFreeSpec;
    toFreeSpec.name() = "drain host0";
    toFreeSpec.containers() = {"host0"};

    solver_->addGoal(toFreeSpec, 1, 1);
  }

  {
    // add a goal at tupleIndex3 to maximize the number of objects in host2
    CapacitySpec capacitySpec;
    capacitySpec.scope() = "host";
    capacitySpec.dimension() = "task_count";
    capacitySpec.bound() = CapacitySpecBound::MIN;

    Limit limit;
    limit.type() = LimitType::ABSOLUTE;
    limit.globalLimit() = 100; // 100 here denotes infinity
    capacitySpec.limit() = limit;

    Filter filter;
    filter.itemsBlacklist() = {"host0", "host1"};
    capacitySpec.filter() = filter;

    solver_->addGoal(capacitySpec, 1, 3);
  }

  OptimalSolverSpec optimalSolver;
  optimalSolver.solverPackage() = getSolverPackage();
  if (isSimplified()) {
    optimalSolver.simplifyLpProblem() = true;
  }

  solver_->addSolver(optimalSolver);
  auto solution = solver_->solve();

  auto initialObjectives = solution.initialGlobalObjective()->goals();
  auto finalObjectives = solution.finalGlobalObjective()->goals();

  EXPECT_EQ(4, finalObjectives->size());

  // expect initial value for goal at tupleIndex 0 (corresponding to the broken
  // 'toFree' constraint) to be 10000 + 5*100 (i.e., violation of caapcity
  // w.r.t. host0)
  auto initialObj1Value = initialObjectives->at(0).value();
  EXPECT_EQ(*initialObj1Value, 10500);
  // expect final value for goal at tupleIndex 0 (corresponding to broken
  // constraints) to be 0
  auto finalObj1Value = finalObjectives->at(0).value();
  EXPECT_EQ(*finalObj1Value, 0);

  // expect initial value for goal at tupleIndex 1 to be 6 (number of tasks in
  // host0)
  auto initialObj2Value = initialObjectives->at(1).value();
  EXPECT_EQ(*initialObj2Value, 6);
  // expect final value for goal at tupleIndex 1 to be 1
  auto finalObj2Value = finalObjectives->at(1).value();
  EXPECT_EQ(*finalObj2Value, 1);

  // expect initial value for goal at tupleIndex 2 to be 0
  auto initialObj3Value = initialObjectives->at(2).value();
  EXPECT_EQ(*initialObj3Value, 0);
  // expect final value for goal at tupleIndex 2 to be 0
  auto finalObj3Value = finalObjectives->at(2).value();
  EXPECT_EQ(*finalObj3Value, 0);

  // expect initial value for goal at tupleIndex 3 (corresponding to the broken
  // 'toFree' constraint) to be 100 - 1 = 99 (w.r.t. host2)
  auto initialObj4Value = initialObjectives->at(3).value();
  EXPECT_EQ(*initialObj4Value, 99);
  // expect final value for goal at tupleIndex 3 to be (100 - 4)
  auto finalObj4Value = finalObjectives->at(3).value();
  EXPECT_EQ(*finalObj4Value, 96);

  // check optimalSolverProfile to see that length of solverStatsForObjectives()
  // is the same as number of objectives (4)
  checkProblemProfile(solution, 4);
}

TEST_P(ObjectiveTupleTest, QuadraticObjective) {
  // SetUp() sets up the hosts, tasks, and initial assignment

  {
    // add a constraint to drain "host0"
    ToFreeSpec toFreeSpec;
    toFreeSpec.name() = "drain host0";
    toFreeSpec.containers() = {"host0"};

    solver_->addConstraint(toFreeSpec);
  }

  {
    // add a goal at tupleIndex 1 to balance number of tasks in host1 and host2
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "host";
    balanceSpec.dimension() = "task_count";
    balanceSpec.formula() = BalanceSpecFormula::IDEAL;
    balanceSpec.boundType() = BalanceSpecBoundType::ABSOLUTE;

    solver_->addGoalBoundary();
    solver_->addGoal(balanceSpec);
  }

  OptimalSolverSpec optimalSolver;
  optimalSolver.solverPackage() = getSolverPackage();
  if (isSimplified()) {
    optimalSolver.simplifyLpProblem() = true;
    solver_->addSolver(optimalSolver);

    // non-linear models are currently not supported when using simplifier, so
    // expect an error
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver_->solve(),
        "getCoeffMap is unsupported for quadratic expressions");
    return;
  }

  solver_->addSolver(optimalSolver);

  // HiGHS supports quadratic objectives (QP) but not mixed-integer quadratic
  // (MIQP). The assignment variables are integer, so HiGHS cannot solve this.
  if (getSolverPackage() == OptimalSolverPackage::HIGHS) {
    EXPECT_THROW(solver_->solve(), std::exception);
    return;
  }

  auto solution = solver_->solve();

  auto initialObjectives = solution.initialGlobalObjective()->goals();
  auto finalObjectives = solution.finalGlobalObjective()->goals();
  EXPECT_EQ(2, finalObjectives->size());

  // expect initial value for goal at tupleIndex 0 (corresponding to the broken
  // 'toFree' constraint) to be (10000 + 6*100)
  auto initialObj1Value = initialObjectives->at(0).value();
  EXPECT_EQ(*initialObj1Value, 10600);
  // expect final value for goal at tupleIndex 0 to be 0
  auto finalObj1Value = finalObjectives->at(0).value();
  EXPECT_EQ(*finalObj1Value, 0);

  // expect initial value for goal at tupleIndex 1 to be 6^2 + 1^2 + 1^2 = 38
  auto initialObj2Value = initialObjectives->at(1).value();
  EXPECT_EQ(*initialObj2Value, 38);
  // expect final value for goal at tupleIndex 1 to be 0^2 + 4^2 + 4^2 = 32
  auto finalObj2Value = finalObjectives->at(1).value();
  EXPECT_EQ(*finalObj2Value, 32);

  std::map<std::string, int> taskCount;
  for (auto& [_, host] : *solution.assignment()) {
    ++taskCount[host];
  }

  // expect both host1 and host2 to have 4 tasks each because of the balance
  // goal
  EXPECT_EQ(taskCount["host1"], 4);
  EXPECT_EQ(taskCount["host2"], 4);

  // check optimalSolverProfile to see that length of solverStatsForObjectives()
  // is the same as number of objectives (2)
  checkProblemProfile(solution, 2);
}

TEST_P(ObjectiveTupleTest, BlendedObjectives) {
  // SetUp() sets up the hosts, tasks, and initial assignment
  // add a constraint to drain "host0"
  ToFreeSpec toFreeSpec;
  toFreeSpec.name() = "drain host0";
  toFreeSpec.containers() = {"host0"};
  solver_->addGoal(toFreeSpec);
  solver_->addGoalBoundary();

  // add a goal saying all hosts can have at most 3 tasks
  CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "task_count";
  capacitySpec.limit()->type() = LimitType::ABSOLUTE;
  capacitySpec.limit()->globalLimit() = 3;
  solver_->addGoal(capacitySpec, 10);
  solver_->addGoalBoundary();

  // with hierarchical objective, we will always end up not meeting the max
  // capacity goal with blended objectives, we should fix capacity goal as it
  // contributes more to objective
  OptimalSolverSpec solverSpec;
  solverSpec.solverPackage() = getSolverPackage();
  solverSpec.multiObjSolveSettings()->solveType() =
      MultiObjectiveSolveType::BLENDED;
  solver_->addSolver(solverSpec);
  auto solution = solver_->solve();
  auto initialObjectives = solution.initialGlobalObjective()->goals();
  auto finalObjectives = solution.finalGlobalObjective()->goals();
  EXPECT_EQ(2, finalObjectives->size());
  for (const auto i : folly::irange(2)) {
    XLOG(INFO) << fmt::format(
        "obj {} improved from {} to {}",
        i,
        *initialObjectives->at(i).value(),
        *finalObjectives->at(i).value());
  }
  EXPECT_EQ(2, *finalObjectives->at(0).value());
  EXPECT_EQ(0, *finalObjectives->at(1).value());
}

TEST_P(ObjectiveTupleTest, OptimalSolverPerObjectiveValue) {
  setUpProblem();

  std::string timeLimitString = getTimeLimitParam();

  facebook::algopt::common::thrift::PerObjectiveValue timeObjectiveValue;
  timeObjectiveValue.objectiveIndexToValue() = {{0, 0.05}, {1, 0.02}, {2, 0.1}};
  timeObjectiveValue.defaultValue() = 1;

  std::map<std::string, facebook::algopt::common::thrift::PerObjectiveValue>
      perObjectiveValue;
  perObjectiveValue[timeLimitString] = timeObjectiveValue;

  OptimalSolverSpec optimalSolverSpec;
  optimalSolverSpec.multiObjSolveSettings()->paramNamesToValues() =
      perObjectiveValue;

  optimalSolverSpec.solverPackage() = getSolverPackage();
  solver_->addSolver(optimalSolverSpec);

  // Verify that per-objective solver parameters are accepted and the solve
  // completes without error. The time limits are very short, so the solver
  // may not find a feasible solution — we only check that it doesn't crash.
  EXPECT_NO_THROW(solver_->solve());
}

TEST_P(ObjectiveTupleTest, GurobiMultiObjectiveSolve) {
  REBALANCER_SKIP_IF_NO_GUROBI();
  multiObjectiveSetup();

  auto param = OptimalSolverPackage::GUROBI;
  const std::string timeLimitString = "GRB_TimeLimit";

  facebook::algopt::common::thrift::PerObjectiveValue timeObjectiveValue;
  timeObjectiveValue.objectiveIndexToValue() = {
      {0, 0}, {1, 100}, {2, 100}, {3, 100}, {4, 100}, {5, 100}};
  timeObjectiveValue.defaultValue() = 1;

  std::map<std::string, facebook::algopt::common::thrift::PerObjectiveValue>
      perObjectiveValue;
  perObjectiveValue[timeLimitString] = timeObjectiveValue;

  OptimalSolverSpec optimalSolverSpec;
  optimalSolverSpec.multiObjSolveSettings()->paramNamesToValues() =
      perObjectiveValue;

  optimalSolverSpec.solverPackage() = param;
  solver_->addSolver(optimalSolverSpec);

  auto solution = solver_->solve();

  // initially, objective is (2,2,2,0,0,0)
  auto& initialGoals = *solution.initialGlobalObjective()->goals();
  EXPECT_NEAR(2.0, *initialGoals.at(0).value(), 1e-8);
  EXPECT_NEAR(2.0, *initialGoals.at(1).value(), 1e-8);
  EXPECT_NEAR(2.0, *initialGoals.at(2).value(), 1e-8);
  EXPECT_NEAR(0.0, *initialGoals.at(3).value(), 1e-8);
  EXPECT_NEAR(0.0, *initialGoals.at(4).value(), 1e-8);
  EXPECT_NEAR(0.0, *initialGoals.at(5).value(), 1e-8);

  // if time limit for objective 0 is 100, the solver will find the optimal
  // solution, however, if the time limit for objective 0 is 0, the solver
  // cannot make any move even though other objectives can improve

  // Note: In Gurobi, if the time limit is 0, the solver will not make any move
  // This is expected behavior in Gurobi because if there is not incumbent
  // solution available for the first objective, there cannot be a solution for
  // the second objective
  // https://support.gurobi.com/hc/en-us/articles/4405371052689-What-does-Gurobi-return-when-the-first-pass-in-a-hierarchical-multi-objective-optimization-terminates-with-no-solution?fbclid=IwY2xjawMS58BleHRuA2FlbQIxMQBicmlkETFPeFh4dk8ySWdmWlA3WElVAR5UIpRKWjVyuhqVA6KtTMvDho7Cc642e22-qlDHTR9oPVallNd6GhctBNLtsQ_aem_6Jasdg-32zgOFm2vlKxC0Q
  auto& finalGoals = *solution.finalGlobalObjective()->goals();
  EXPECT_NEAR(2.0, *finalGoals.at(0).value(), 1e-8);
  EXPECT_NEAR(2.0, *finalGoals.at(1).value(), 1e-8);
  EXPECT_NEAR(2.0, *finalGoals.at(2).value(), 1e-8);
  EXPECT_NEAR(0.0, *finalGoals.at(3).value(), 1e-8);
  EXPECT_NEAR(0.0, *finalGoals.at(4).value(), 1e-8);
  EXPECT_NEAR(0.0, *finalGoals.at(5).value(), 1e-8);
}
