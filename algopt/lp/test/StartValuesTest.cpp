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
#include "algopt/rebalancer/tests/SolverTestUtils.h"

#include <gtest/gtest.h>

using namespace facebook::algopt::lp;

TEST(StartValuesTest, XpressFeasible) {
  REBALANCER_SKIP_IF_NO_XPRESS();
  auto problem = ProblemFactory::makeXpressProblem();
  auto var = problem.makeVar();
  var.setLB(0);
  var.setUB(10);
  problem.addStartValue(var, 5);
  problem.setObjective(var);
  problem.solve();

  auto result = problem.getOnlyResult();
  auto optionalWarmStartResult = result.warmStartResult();
  EXPECT_TRUE(optionalWarmStartResult.has_value());

  auto& warmStartResult = *optionalWarmStartResult;
  // XPRESS may either accept the solution directly (FEASIBLE_SOLUTION_PROVIDED)
  // or optimize from it (FEASIBLE_SOLUTION_OBTAINED), depending on the version.
  EXPECT_TRUE(
      *warmStartResult.status() ==
          thrift::WarmStartStatus::FEASIBLE_SOLUTION_PROVIDED ||
      *warmStartResult.status() ==
          thrift::WarmStartStatus::FEASIBLE_SOLUTION_OBTAINED);
}

// HiGHS applies the start values but does not report a WarmStartStatus, so this
// only checks that warm starting does not perturb the solve.
TEST(StartValuesTest, HiGHSFeasible) {
  auto problem = ProblemFactory::makeHiGHSProblem();
  auto var = problem.makeVar();
  var.setLB(0);
  var.setUB(10);
  problem.addStartValue(var, 5);
  problem.setObjective(var);
  problem.solve();

  EXPECT_EQ(thrift::ProblemStatus::OPTIMAL_FOUND, problem.getStatus());
  EXPECT_NEAR(0, var.getValue(), 1e-8);
}

TEST(StartValuesTest, XpressInfeasible) {
  REBALANCER_SKIP_IF_NO_XPRESS();
  auto problem = ProblemFactory::makeXpressProblem();
  auto var = problem.makeVar();
  var.setLB(0);
  var.setUB(10);
  problem.addStartValue(var, 20);
  problem.setObjective(var);
  problem.solve();

  auto result = problem.getOnlyResult();
  auto optionalWarmStartResult = result.warmStartResult();
  EXPECT_TRUE(optionalWarmStartResult.has_value());

  auto& warmStartResult = *optionalWarmStartResult;
  // With direct addMipSol, Xpress repairs the infeasible solution rather than
  // rejecting it. The repaired solution is accepted as OBTAINED, not PROVIDED.
  EXPECT_EQ(
      thrift::WarmStartStatus::FEASIBLE_SOLUTION_OBTAINED,
      *warmStartResult.status());
}
