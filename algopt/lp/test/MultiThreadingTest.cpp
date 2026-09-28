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
#include "algopt/rebalancer/interface/tests/utils.h"
#include "algopt/rebalancer/tests/SolverTestUtils.h"
#include <algopt/lp/detail/generic/impl/GenericExpressionImpl.h>

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <gtest/gtest.h>

namespace lp = facebook::algopt::lp;
namespace interface = facebook::rebalancer::interface;

// NOTE: this test intentionally only tests for problems that go through
// GenericProblemImpl, since ones that directly go through XpressProblem and
// GurobiProblem are not thread-safe
enum SolverType { SimplifiedGurobi, SimplifiedXpress, SimplifiedHiGHS };

class MultiThreadingTest : public ::testing::TestWithParam<SolverType> {
 protected:
  static lp::Problem initializeProblem() {
    if (GetParam() == SolverType::SimplifiedXpress) {
      return lp::ProblemFactory::makeSimplifiedProblem(
          lp::ProblemFactory::makeXpressProblem);
    } else if (GetParam() == SolverType::SimplifiedHiGHS) {
      return lp::ProblemFactory::makeSimplifiedProblem(
          lp::ProblemFactory::makeHiGHSProblem);
    } else {
      return lp::ProblemFactory::makeSimplifiedProblem(
          lp::ProblemFactory::makeGurobiProblem);
    }
  }
};

INSTANTIATE_TEST_CASE_P(
    GurobiWithSimplifierTest,
    MultiThreadingTest,
    ::testing::Values(SolverType::SimplifiedGurobi));

INSTANTIATE_TEST_CASE_P(
    XpressWithSimplifierTest,
    MultiThreadingTest,
    ::testing::Values(SolverType::SimplifiedXpress));

INSTANTIATE_TEST_CASE_P(
    HiGHSWithSimplifierTest,
    MultiThreadingTest,
    ::testing::Values(SolverType::SimplifiedHiGHS));

TEST_P(MultiThreadingTest, Basic) {
  if (GetParam() == SolverType::SimplifiedGurobi) {
    REBALANCER_SKIP_IF_NO_GUROBI();
  }
  if (GetParam() == SolverType::SimplifiedXpress) {
    REBALANCER_SKIP_IF_NO_XPRESS();
  }
  // use 'false' below to use a single thread
  auto executor = interface::getTestExecutor(
      /*numThreads=*/interface::defaultExecutorThreadCount);
  auto problem = initializeProblem();

  const int nFunc = 1e2;
  int varCount = 1e3;

  // create some variables serially
  lp::Expression commonVarSum;
  std::vector<lp::Variable> vars;
  for (const auto i : folly::irange(nFunc)) {
    auto var = problem.makeIntVar(fmt::format("common_var{}", i));
    vars.push_back(var);
    commonVarSum += var;
  }

  // Launch several functions in parallel, that all create several variables,
  // expressions, and add several constraints to the model. Note that no
  // two functions modify the same variable or expression (doing so is NOT
  // thread-safe).
  for (const auto i : folly::irange(nFunc)) {
    executor->add([&, i]() {
      lp::Expression internalSum;
      for (const auto j : folly::irange(varCount)) {
        auto var = problem.makeIntVar(fmt::format("var{}_{}", i, j));
        var.setLB(2);
        var.setUB(2);

        internalSum += var;
      }

      // add a constraint saying that the sum of vars inside each function i is
      // at most the i-th common variable
      problem.newConstraint(internalSum <= vars.at(i));
    });
  }
  problem.setObjective(commonVarSum);
  executor->join();

  // make sure executor has joined before calling solve
  problem.solve();

  // expect the value of each commonVar to be 2*varCount (since each variable
  // inside a function takes only integral values, it's upper and lower bounds
  // are 2, vars.at(i) >= "internal sum", where "internal sum" = 2*varCount, and
  // we are minimizing commonVarSum)
  for (const auto i : folly::irange(nFunc)) {
    EXPECT_EQ(vars.at(i).getValue(), 2 * varCount);
  }

  EXPECT_EQ(commonVarSum.getValue(), nFunc * 2 * varCount);
}
