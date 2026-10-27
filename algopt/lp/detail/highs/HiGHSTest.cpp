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

#ifdef REBALANCER_USE_HIGHS

#include "algopt/lp/detail/highs/HiGHSProblem.h"
#include "algopt/lp/generic/Operators.h"

#include <gtest/gtest.h>

#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace facebook::algopt::lp;
using namespace facebook::algopt::lp::detail;

TEST(HiGHSTest, SimpleTest) {
  auto model = Problem(std::make_unique<HiGHSProblem>());
  auto x = model.makeVar("x");
  auto y = model.makeVar("y");
  model.newConstraint(x <= 0, "xneg");
  model.newConstraint(y <= 0, "yneg");
  model.newConstraint(5 * x + 2 * y >= -45, "con1");
  model.newConstraint(x - y <= 3, "con2");
  model.newConstraint(x + 4 * y >= -27, "con3");
  model.setObjective(x + y);
  model.solve();
  EXPECT_EQ(model.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_EQ(x.getValue(), -7);
  EXPECT_EQ(y.getValue(), -5);

  const auto attrib = model.getProblemAttributes();
  EXPECT_EQ(attrib.numVariables(), 2);
  EXPECT_EQ(attrib.numConstraints(), 5);
}

TEST(HiGHSTest, MipTest) {
  auto model = Problem(std::make_unique<HiGHSProblem>());
  auto x = model.makeIntVar("x");
  auto y = model.makeIntVar("y");
  model.newConstraint(x + 7 * y <= 17.5);
  model.newConstraint(0 <= x);
  model.newConstraint(0 <= y);
  model.newConstraint(x <= 3.5);
  model.setObjective(-x - 10 * y);
  model.solve();
  EXPECT_EQ(model.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_EQ(x.getValue(), 3);
  EXPECT_EQ(y.getValue(), 2);

  const auto attrib = model.getProblemAttributes();
  EXPECT_EQ(attrib.numVariables(), 2);
  EXPECT_EQ(attrib.numConstraints(), 4);
}

TEST(HiGHSTest, ReportsSolutionFoundWhenStoppedWithSolution) {
  auto model = Problem(std::make_unique<HiGHSProblem>());
  auto x = model.makeIntVar("x");
  auto y = model.makeIntVar("y");
  x.setBounds(0, 3.5);
  y.setLB(0);
  model.newConstraint(x + 7 * y <= 17.5);
  model.setObjective(-x - 10 * y);
  // No presolve and no search, so HiGHS stops with the start as its solution.
  model.addStartValue(x, 0);
  model.addStartValue(y, 0);
  model.setParameter("presolve_reduction_limit", 0);
  model.setParameter("mip_max_nodes", 0);
  model.solve();
  EXPECT_EQ(model.getStatus(), thrift::ProblemStatus::SOLUTION_FOUND);
}

TEST(HiGHSTest, IISExcludesUninvolvedRows) {
  auto model = Problem(std::make_unique<HiGHSProblem>());
  auto x = model.makeVar("x");
  auto y = model.makeVar("y");
  model.newConstraint(x + y >= 10, "sum_lb");
  model.newConstraint(x + y <= 5, "sum_ub");
  model.newConstraint(x - y <= 100, "slack");
  model.setObjective(x);
  model.solve();
  ASSERT_EQ(model.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);

  const auto iis = model.getIIS();
  ASSERT_TRUE(iis.has_value());
  EXPECT_EQ(
      std::set<std::string>(
          iis->constraintIds.begin(), iis->constraintIds.end()),
      (std::set<std::string>{"sum_lb", "sum_ub"}));
}

TEST(HiGHSTest, IISReportsBoundSides) {
  auto model = Problem(std::make_unique<HiGHSProblem>());
  auto x = model.makeIntVar("x");
  x.setLB(0);
  x.setUB(3);
  model.newConstraint(x >= 5, "x_ge_5");
  model.setObjective(x);
  model.solve();
  ASSERT_EQ(model.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);

  const auto iis = model.getIIS();
  ASSERT_TRUE(iis.has_value());
  EXPECT_EQ(iis->constraintIds, std::vector<std::string>{"x_ge_5"});
  EXPECT_EQ(iis->upperBoundVars, std::vector<std::string>{"x"});
  EXPECT_TRUE(iis->lowerBoundVars.empty());
}

TEST(HiGHSTest, DeleteConstraintAfterEarlierDeletion) {
  // Deleting `first` shifts every later row down one, so a row index captured
  // when `huge` was created would now point at `x_le_5`.
  auto model = Problem(std::make_unique<HiGHSProblem>());
  auto x = model.makeVar("x");
  auto first = model.newConstraint(x >= 100, "first");
  model.newConstraint(x <= 1000, "loose");
  model.newConstraint(x >= 10, "x_ge_10");
  auto huge = model.newConstraint(x >= 200, "huge");
  model.newConstraint(x <= 5, "x_le_5");
  model.deleteConstraint(first);
  model.deleteConstraint(huge);
  model.setObjective(x);
  model.solve();
  ASSERT_EQ(model.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);
  EXPECT_EQ(model.getProblemAttributes().numConstraints(), 3);

  const auto iis = model.getIIS();
  ASSERT_TRUE(iis.has_value());
  EXPECT_EQ(
      std::set<std::string>(
          iis->constraintIds.begin(), iis->constraintIds.end()),
      (std::set<std::string>{"x_ge_10", "x_le_5"}));
}

TEST(HiGHSTest, IISEmptyWhenOnlyIntegralityConflicts) {
  // The LP relaxation is feasible (x in [0.5, 0.75]); only integrality makes
  // these rows conflict, which HiGHS's LP-based IIS validation rejects.
  auto model = Problem(std::make_unique<HiGHSProblem>());
  auto x = model.makeIntVar("x");
  x.setLB(0);
  x.setUB(10);
  model.newConstraint(2 * x >= 1, "x_ge_half");
  model.newConstraint(4 * x <= 3, "x_le_three_quarters");
  model.setObjective(x);
  model.solve();
  ASSERT_EQ(model.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);

  EXPECT_FALSE(model.getIIS().has_value());
}

TEST(HiGHSTest, IISForMipWithLpConflict) {
  // Integer columns whose rows also conflict in the LP relaxation.
  auto model = Problem(std::make_unique<HiGHSProblem>());
  auto x = model.makeIntVar("x");
  auto y = model.makeIntVar("y");
  x.setLB(0);
  x.setUB(10);
  y.setLB(0);
  y.setUB(10);
  model.newConstraint(x + y >= 8, "sum_lb");
  model.newConstraint(x + y <= 3, "sum_ub");
  model.newConstraint(x - y <= 100, "slack");
  model.setObjective(x + y);
  model.solve();
  ASSERT_EQ(model.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);

  const auto iis = model.getIIS();
  ASSERT_TRUE(iis.has_value());
  EXPECT_EQ(
      std::set<std::string>(
          iis->constraintIds.begin(), iis->constraintIds.end()),
      (std::set<std::string>{"sum_lb", "sum_ub"}));
}

TEST(HiGHSTest, IISEmptyForUnbounded) {
  auto model = Problem(std::make_unique<HiGHSProblem>());
  auto x = model.makeVar("x");
  model.newConstraint(x <= 5, "x_le_5");
  model.setObjective(x);
  model.solve();
  ASSERT_EQ(model.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);

  EXPECT_FALSE(model.getIIS().has_value());
}

TEST(HiGHSTest, SensitivityTest) {
  // min x + y over a non-degenerate vertex, so the duals below are unique.
  auto model = Problem(std::make_unique<HiGHSProblem>());
  auto x = model.makeVar("x");
  auto y = model.makeVar("y");
  // Deleted before the solve so every row below is read after shifting down.
  auto shifted = model.newConstraint(x + y <= 1000, "shifted");
  auto xneg = model.newConstraint(x <= 0, "xneg");
  model.newConstraint(y <= 0, "yneg");
  auto con1 = model.newConstraint(5 * x + 2 * y >= -45, "con1");
  model.newConstraint(x - y <= 3, "con2");
  auto con3 = model.newConstraint(x + 4 * y >= -27, "con3");
  model.deleteConstraint(shifted);
  model.setObjective(x + y);
  model.solve();
  ASSERT_EQ(model.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);

  // con1 and con3 are the binding rows at (-7, -5). Both are `>=` rows, so
  // under the minimization convention their duals are positive.
  EXPECT_NEAR(con1.getDual(), 1.0 / 6.0, 1e-6);
  EXPECT_NEAR(con3.getDual(), 1.0 / 6.0, 1e-6);
  EXPECT_NEAR(con1.getSlack(), 0.0, 1e-9);
  EXPECT_NEAR(con1.getRHS(), -45.0, 1e-9);

  // xneg is slack at the optimum, so it prices at zero.
  EXPECT_NEAR(xneg.getDual(), 0.0, 1e-9);
  EXPECT_NEAR(xneg.getSlack(), 7.0, 1e-9);
  EXPECT_NEAR(xneg.getRHS(), 0.0, 1e-9);

  // Both columns are basic at the optimum.
  EXPECT_NEAR(x.getReducedCost(), 0.0, 1e-9);
  EXPECT_NEAR(y.getReducedCost(), 0.0, 1e-9);
}

TEST(HiGHSTest, RowLookupSurvivesDeletions) {
  // Each row's RHS is its creation order, so a lookup that lands on the wrong
  // row returns the wrong number.
  auto model = Problem(std::make_unique<HiGHSProblem>());
  auto x = model.makeVar("x");
  std::vector<Constraint> rows;
  rows.reserve(6);
  for (int i = 0; i < 5; ++i) {
    rows.push_back(model.newConstraint(x <= i, "r" + std::to_string(i)));
  }
  // Cache r4's row before the rows below it move.
  EXPECT_EQ(rows[4].getRHS(), 4.0);

  model.deleteConstraint(rows[1]);
  rows.push_back(model.newConstraint(x <= 5, "r5"));
  model.deleteConstraint(rows[3]);
  model.deleteConstraint(rows[0]);

  EXPECT_EQ(rows[4].getRHS(), 4.0);
  EXPECT_EQ(rows[2].getRHS(), 2.0);
  EXPECT_EQ(rows[5].getRHS(), 5.0);
  EXPECT_THROW(rows[1].getRHS(), std::runtime_error);
}

#endif
