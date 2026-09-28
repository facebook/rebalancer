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

#include "algopt/lp/factory/ProblemFactory.h"
#include "algopt/lp/generic/Expression.h"
#include "algopt/lp/generic/Operators.h"
#include "algopt/lp/generic/Problem.h"
#include <algopt/lp/detail/generic/impl/GenericExpressionImpl.h>

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <gtest/gtest.h>

#include <stdexcept>

namespace facebook::algopt::lp::tests {
using namespace facebook::algopt::lp::detail;

enum SolverType {
  Gurobi,
  Xpress,
  SimplifiedGurobi,
  SimplifiedXpress,
  GenericGurobi,
  HiGHS,
  SimplifiedHiGHS,
};

class MultipleObjectivesTest : public ::testing::TestWithParam<SolverType> {
 protected:
  void setUpProblem(Problem& problem, int numVars) {
    // create numVars variables, where each variable has a lowerBound of 0, and
    // upper bound of 2.
    for (const auto i : folly::irange(numVars)) {
      auto varI = problem.makeIntVar(fmt::format("var{}", i));
      variable_.push_back(varI);
      variable_[i].setLB(0);
      variable_[i].setUB(2);
    }
    EXPECT_EQ(variable_.size(), numVars);

    // for every pair of variables i, j, where i != j and they are not both
    // even, add a constraint saying variable_[i] + variable_[j] >= 2
    for (const auto i : folly::irange(numVars)) {
      for (const auto j : folly::irange(numVars)) {
        const bool bothEven = (i % 2 == 0 && j % 2 == 0);
        if (i != j && !bothEven) {
          problem.newConstraint(variable_[i] + variable_[j] >= 2);
        }
      }
    }

    for (const auto i : folly::irange(numVars)) {
      sumOfVars_ += variable_.at(i);

      // add the variables with even index
      if (i % 2 == 0) {
        sumOfEvenVars_ += variable_.at(i);
      }
    }
  }

  void verifyRecomputedValue(int expectedValue, auto& expr) {
    // when using computeValue() and the solver is not XPRESS (since
    // computeValue() is not implemented accurately in Xpress), we expect that
    // the value of expr is an integer since all the variables used here are
    // integers
    if (GetParam() == SolverType::Xpress) {
      EXPECT_NEAR(expectedValue, expr.computeValue(), 1e-8);
    } else {
      EXPECT_EQ(expectedValue, expr.computeValue());
    }
  }

  static Problem initializeProblem() {
    if (GetParam() == SolverType::Gurobi) {
      return ProblemFactory::makeGurobiProblem();
    } else if (GetParam() == SolverType::Xpress) {
      return ProblemFactory::makeXpressProblem();
    } else if (GetParam() == SolverType::SimplifiedXpress) {
      return ProblemFactory::makeSimplifiedProblem(
          ProblemFactory::makeXpressProblem);
    } else if (GetParam() == SolverType::SimplifiedGurobi) {
      return ProblemFactory::makeSimplifiedProblem(
          ProblemFactory::makeGurobiProblem);
    } else if (GetParam() == SolverType::GenericGurobi) {
      return ProblemFactory::makeGenericProblem(
          ProblemFactory::makeGurobiProblem);
    } else if (GetParam() == SolverType::HiGHS) {
      return ProblemFactory::makeHiGHSProblem();
    } else if (GetParam() == SolverType::SimplifiedHiGHS) {
      return ProblemFactory::makeSimplifiedProblem(
          ProblemFactory::makeHiGHSProblem);
    } else {
      throw std::runtime_error("unexpected solver type");
    }
  }

  Expression sumOfVars_;
  Expression sumOfEvenVars_;
  std::vector<facebook::algopt::lp::Variable> variable_;
  std::vector<Expression> objectives_;
};

#if !defined(REBALANCER_USE_GUROBI) && !defined(REBALANCER_USE_XPRESS) && \
    !defined(REBALANCER_USE_HIGHS)
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(MultipleObjectivesTest);
#endif

#ifdef REBALANCER_USE_HIGHS
INSTANTIATE_TEST_CASE_P(
    HiGHSTest,
    MultipleObjectivesTest,
    ::testing::Values(SolverType::HiGHS));

INSTANTIATE_TEST_CASE_P(
    HiGHSWithSimplifierTest,
    MultipleObjectivesTest,
    ::testing::Values(SolverType::SimplifiedHiGHS));
#endif

#ifdef REBALANCER_USE_GUROBI
INSTANTIATE_TEST_CASE_P(
    GurobiTest,
    MultipleObjectivesTest,
    ::testing::Values(SolverType::Gurobi));

INSTANTIATE_TEST_CASE_P(
    GurobiWithSimplifierTest,
    MultipleObjectivesTest,
    ::testing::Values(SolverType::SimplifiedGurobi));

INSTANTIATE_TEST_CASE_P(
    GenericTest,
    MultipleObjectivesTest,
    ::testing::Values(SolverType::GenericGurobi));
#endif

#ifdef REBALANCER_USE_XPRESS
INSTANTIATE_TEST_CASE_P(
    XpressTest,
    MultipleObjectivesTest,
    ::testing::Values(SolverType::Xpress));

INSTANTIATE_TEST_CASE_P(
    XpressWithSimplifierTest,
    MultipleObjectivesTest,
    ::testing::Values(SolverType::SimplifiedXpress));
#endif

TEST_P(MultipleObjectivesTest, AllLinearExprs) {
  // pick even numVars to ensure correctness below
  constexpr int numVars = 100;
  auto problem = initializeProblem();

  // This creates numVars variables and also adds some constraints. In
  // particular, for every pair of variables i, j, where i < j and they are not
  // both even, it adds a constraint saying variable_[i] + variable_[j] >= 2
  setUpProblem(problem, numVars);

  // Add three objectives; first, is just a 0 objective, other two are linear
  // expressions; first non-zero objective is to minimize sum of all the
  // variables and the second non-zero objective is to maximize the sum of all
  // even variables
  std::vector<Expression> objectives = {0, sumOfVars_, -sumOfEvenVars_};
  problem.setObjective(objectives);

  problem.solve();

  // Since the goal is to minimize sumOfVars, we expect it's value to be numVars
  EXPECT_NEAR(numVars, objectives.at(1).getValue(), 1e-8);
  verifyRecomputedValue(numVars, objectives.at(1));

  EXPECT_NEAR(-numVars / 2, objectives.at(2).getValue(), 1e-8);
  verifyRecomputedValue(-numVars / 2, objectives.at(2));

  // Expect all even variables to be 1 because of the objective to maximize
  // their sum
  for (int i = 0; i < numVars; i = i + 2) {
    EXPECT_NEAR(1, variable_.at(i).getValue(), 1e-8);
  }

  // Expect all odd variables to be 1
  for (int i = 1; i < numVars; i = i + 2) {
    EXPECT_NEAR(1, variable_.at(i).getValue(), 1e-8);
  }

  EXPECT_EQ(problem.getResultPerObjective().size(), objectives.size());
  EXPECT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
}

TEST_P(MultipleObjectivesTest, QuadraticObjective) {
  // pick even numVars to ensure correctness below
  constexpr int numVars = 10;
  auto problem = initializeProblem();

  // This creates numVars variables and also adds some constraints. In
  // particular, for every pair of variables i, j, where i < j and they are not
  // both even, it adds a constraint saying variable_[i] + variable_[j] >= 2
  setUpProblem(problem, numVars);

  // Add two objectives; first is to maximize square of the sum of all the
  // even variables and the second objective is to minimize the sum of all
  // variables
  auto getObjectives = [&]() -> std::vector<Expression> {
    return {-sumOfEvenVars_ * sumOfEvenVars_, sumOfVars_, sumOfVars_};
  };

  if (GetParam() == SolverType::SimplifiedGurobi ||
      GetParam() == SolverType::SimplifiedXpress) {
    // quadratic expressions are not supported in SimplifiedProblem, so skip
    // those cases.
    return;
  }

  if (GetParam() == SolverType::HiGHS ||
      GetParam() == SolverType::SimplifiedHiGHS) {
    // The HiGHS backend does not support quadratic objectives.
    return;
  }

  auto objectives = getObjectives();
  problem.setObjective(objectives);
  problem.solve();

  // Since we want to minimize the sqaure of sumOfEvenVars, we expect it's value
  // to be -1 * numVars^2, since all even variables are upperBounded by 2
  EXPECT_NEAR(-numVars * numVars, objectives.at(0).getValue(), 1e-8);
  verifyRecomputedValue(-numVars * numVars, objectives.at(0));

  // Since the second objective is to minimize the sum of all the variables,
  // and all even variables have value = 2 from above, in order to satisfy
  // constraints of the kind variable[i] + variable[j] >= 2, where i and j
  // are odd, their sum (i.e., sumOfOddVars) has to be at least numVars/2
  EXPECT_NEAR(1.5 * numVars, objectives.at(1).getValue(), 1e-8);
  verifyRecomputedValue(1.5 * numVars, objectives.at(1));

  // Expect all even variables to be 2 because of the first objective
  for (int i = 0; i < numVars; i = i + 2) {
    EXPECT_NEAR(2, variable_.at(i).getValue(), 1e-8);
  }

  EXPECT_EQ(problem.getResultPerObjective().size(), objectives.size());
  EXPECT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
}

TEST_P(MultipleObjectivesTest, ForceSimplifierToWork) {
  auto problem = initializeProblem();

  auto x = problem.makeIntVar("x");
  auto y = problem.makeIntVar("y");

  x.setUB(5);
  y.setUB(8);

  problem.newConstraint(x - 5 >= 0);
  problem.newConstraint(y >= 8);

  problem.setObjective({x, y + 5});

  problem.solve();

  // Ensure that simplifier removes both the constraints and sets the
  // first goal to be 5 and second goal to be 13
  if (GetParam() == SolverType::SimplifiedGurobi ||
      GetParam() == SolverType::SimplifiedXpress) {
    auto obj1 = std::static_pointer_cast<GenericExpressionImpl>(
        problem.getObjectiveAt(0)->clone());
    auto obj2 = std::static_pointer_cast<GenericExpressionImpl>(
        problem.getObjectiveAt(1)->clone());

    EXPECT_EQ(obj1->isConstant(), true);
    EXPECT_EQ(obj2->isConstant(), true);
  }

  EXPECT_EQ(x.getValue(), 5);
  EXPECT_EQ(y.getValue(), 8);

  EXPECT_EQ(problem.getResultPerObjective().size(), 2);
  EXPECT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
}

TEST_P(MultipleObjectivesTest, InfeasibleObjective) {
  auto problem = initializeProblem();

  auto x = problem.makeIntVar("x");
  auto y = problem.makeIntVar("y");
  y.setLB(0);

  problem.newConstraint(x <= 5);
  problem.newConstraint(x >= 8);

  problem.setObjective({y, x + 5});
  problem.solve();

  // Ensure that the solve does not proceed if the problem is found to be
  // infeasible (note that getResultPerObjective().size() is 1 and not 2,
  // although there are 2 objectives)
  EXPECT_EQ(problem.getResultPerObjective().size(), 1);
  EXPECT_EQ(problem.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);
}

TEST_P(MultipleObjectivesTest, TestTimeLimit) {
  // this test is just for testing that time limits are set correctly, so not
  // checking solutions
  auto problem = initializeProblem();

  auto x = problem.makeIntVar("x");
  auto y = problem.makeIntVar("y");
  x.setLB(0);
  y.setLB(0);

  problem.newConstraint(x <= 5);
  problem.newConstraint(y >= 8);

  problem.setObjective({y, x + 5});

  problem.setMaxSolveTime(1e-12);
  problem.solve();

  // Ensure that the solve does not proceed since maxSolveTime is set to
  // 10^(-12) (note that getResultPerObjective().size() is 1 and not 2)
  EXPECT_EQ(problem.getResultPerObjective().size(), 1);
}

#ifdef REBALANCER_USE_GUROBI
TEST_P(MultipleObjectivesTest, IndividualTimeLimitCustom) {
  // this test is just for testing that time limits are set correctly, so not
  // checking solutions
  auto problem = ProblemFactory::makeGurobiProblem();

  auto x = problem.makeIntVar("x");
  auto y = problem.makeIntVar("y");
  x.setLB(0);
  y.setLB(0);

  problem.newConstraint(x <= 5);
  problem.newConstraint(y >= 8);

  // Quadratic objective so it will be called by the custom multiobjective
  problem.setObjective({y * y, x + 5});

  folly::F14FastMap<std::string, algopt::common::thrift::PerObjectiveValue>
      multiObjectiveParams;
  algopt::common::thrift::PerObjectiveValue grbTimeLimit;
  grbTimeLimit.defaultValue() = 0;
  grbTimeLimit.objectiveIndexToValue() = {{0, 0.947}, {1, 0.678}};

  multiObjectiveParams["GRB_TimeLimit"] = grbTimeLimit;

  problem.setMultiObjConfig(multiObjectiveParams);
  problem.solve();

  // get the last time limit set by the solver
  EXPECT_EQ(problem.getParameter("GRB_TimeLimit"), 0.678);
}

TEST_P(MultipleObjectivesTest, IndividualTimeLimitGurobi) {
  // this test is just for testing that time limits are set correctly, so not
  // checking solutions
  auto problem = ProblemFactory::makeGurobiProblem();

  auto x = problem.makeIntVar("x");
  auto y = problem.makeIntVar("y");
  x.setLB(0);
  y.setLB(0);

  problem.newConstraint(x <= 5);
  problem.newConstraint(y >= 8);

  // linear objectives so it will be called by the gurobi multiobjective
  problem.setObjective({y, x + 5});
  problem.setMaxSolveTime(1e-12);

  folly::F14FastMap<std::string, algopt::common::thrift::PerObjectiveValue>
      multiObjectiveParams;
  algopt::common::thrift::PerObjectiveValue grbTimeLimit;
  grbTimeLimit.defaultValue() = 0;
  grbTimeLimit.objectiveIndexToValue() = {{0, 947}, {1, 678}};

  multiObjectiveParams["GRB_TimeLimit"] = grbTimeLimit;

  problem.setMultiObjConfig(multiObjectiveParams);
  problem.solve();

  // since multi objective environments are destroyed after the solve
  // we will get the master time limit by the maxSolvetime
  EXPECT_EQ(problem.getParameter("GRB_TimeLimit"), 1e-12);
}
#endif

#ifdef REBALANCER_USE_XPRESS
TEST_P(MultipleObjectivesTest, IndividualTimeLimitXpress) {
  // this test is just for testing that time limits are set correctly, so not
  // checking solutions
  auto problem = ProblemFactory::makeXpressProblem();

  auto x = problem.makeIntVar("x");
  auto y = problem.makeIntVar("y");
  x.setLB(0);
  y.setLB(0);

  problem.newConstraint(x <= 5);
  problem.newConstraint(y >= 8);

  problem.setObjective({y, x + 5});

  folly::F14FastMap<std::string, algopt::common::thrift::PerObjectiveValue>
      multiObjectiveParams;
  algopt::common::thrift::PerObjectiveValue xprsMaxtime;
  xprsMaxtime.defaultValue() = 0;
  xprsMaxtime.objectiveIndexToValue() = {{0, 947}, {1, 678}};
  multiObjectiveParams["XPRS_TIMELIMIT"] = xprsMaxtime;

  problem.setMultiObjConfig(multiObjectiveParams);
  problem.solve();

  // we will get the last time limit.
  EXPECT_DOUBLE_EQ(problem.getParameter("XPRS_TIMELIMIT"), 678);
}
#endif

#ifdef REBALANCER_USE_GUROBI
TEST_P(MultipleObjectivesTest, IndividualParamSetCustom) {
  // this test is just for testing that time limits are set correctly, so not
  // checking solutions
  auto problem = ProblemFactory::makeGurobiProblem();

  auto x = problem.makeIntVar("x");
  auto y = problem.makeIntVar("y");
  x.setLB(0);
  y.setLB(0);

  problem.newConstraint(x <= 5);
  problem.newConstraint(y >= 8);

  // Quadratic objective so it will be called by the custom multiobjective
  problem.setObjective({y * y, x + 5});

  folly::F14FastMap<std::string, algopt::common::thrift::PerObjectiveValue>
      multiObjectiveParams;
  algopt::common::thrift::PerObjectiveValue grbTimeLimit;
  grbTimeLimit.defaultValue() = 0;
  grbTimeLimit.objectiveIndexToValue() = {{0, 947}, {1, 678}};

  multiObjectiveParams["GRB_TimeLimit"] = grbTimeLimit;

  algopt::common::thrift::PerObjectiveValue grbMipGap;
  grbMipGap.defaultValue() = 1.0;
  grbMipGap.objectiveIndexToValue() = {{0, 1.0}, {1, 2.0}};
  multiObjectiveParams["GRB_MIPGap"] = grbMipGap;

  problem.setMultiObjConfig(multiObjectiveParams);
  problem.solve();

  // Will get the last value of the parameter
  EXPECT_EQ(problem.getParameter("GRB_MIPGap"), 2);
}

TEST_P(MultipleObjectivesTest, IndividualParamSetGurobi) {
  // this test is just for testing that time limits are set correctly, so not
  // checking solutions
  auto problem = ProblemFactory::makeGurobiProblem();

  auto x = problem.makeIntVar("x");
  auto y = problem.makeIntVar("y");
  x.setLB(0);
  y.setLB(0);

  problem.newConstraint(x <= 5);
  problem.newConstraint(y >= 8);

  // linear objectives so it will be called by the gurobi multiobjective
  problem.setObjective({y, x + 5});

  folly::F14FastMap<std::string, algopt::common::thrift::PerObjectiveValue>
      multiObjectiveParams;
  algopt::common::thrift::PerObjectiveValue grbTimeLimit;
  grbTimeLimit.defaultValue() = 0;
  grbTimeLimit.objectiveIndexToValue() = {{0, 947}, {1, 678}};

  multiObjectiveParams["GRB_TimeLimit"] = grbTimeLimit;

  algopt::common::thrift::PerObjectiveValue grbMipGap;
  grbMipGap.defaultValue() = 1.0;
  grbMipGap.objectiveIndexToValue() = {{0, 1.0}, {1, 2.0}};
  multiObjectiveParams["GRB_MIPGap"] = grbMipGap;

  problem.setMultiObjConfig(multiObjectiveParams);
  // MASTER PARAMETER
  problem.setParameter("GRB_MIPGap", 1e-5);
  problem.solve();

  // Will get the value of the master parameter
  EXPECT_EQ(problem.getParameter("GRB_MIPGap"), 1e-5);
}
#endif

} // namespace facebook::algopt::lp::tests
