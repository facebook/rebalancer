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
#include "algopt/lp/generic/Operators.h"
#include "algopt/rebalancer/tests/SolverTestUtils.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

using namespace facebook::algopt::lp;

TEST(XpressIISTest, ExcludesUninvolvedRows) {
  REBALANCER_SKIP_IF_NO_XPRESS();
  auto model = ProblemFactory::makeXpressProblem();
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

TEST(XpressIISTest, ReportsBoundSides) {
  REBALANCER_SKIP_IF_NO_XPRESS();
  auto model = ProblemFactory::makeXpressProblem();
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

TEST(XpressIISTest, IntegralityOnlyConflict) {
  // The LP relaxation is feasible (x in [0.5, 0.75]); only integrality makes
  // these rows conflict.
  REBALANCER_SKIP_IF_NO_XPRESS();
  auto model = ProblemFactory::makeXpressProblem();
  auto x = model.makeIntVar("x");
  x.setLB(0);
  x.setUB(10);
  model.newConstraint(2 * x >= 1, "x_ge_half");
  model.newConstraint(4 * x <= 3, "x_le_three_quarters");
  model.newConstraint(x <= 100, "slack");
  model.setObjective(x);
  model.solve();
  ASSERT_EQ(model.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);

  const auto iis = model.getIIS();
  ASSERT_TRUE(iis.has_value());
  EXPECT_EQ(
      std::set<std::string>(
          iis->constraintIds.begin(), iis->constraintIds.end()),
      (std::set<std::string>{"x_ge_half", "x_le_three_quarters"}));
}

TEST(XpressIISTest, EmptyForUnbounded) {
  REBALANCER_SKIP_IF_NO_XPRESS();
  auto model = ProblemFactory::makeXpressProblem();
  auto x = model.makeVar("x");
  x.setLB(-1e30);
  model.newConstraint(x <= 5, "x_le_5");
  model.setObjective(x);
  model.solve();
  ASSERT_EQ(model.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);

  EXPECT_FALSE(model.getIIS().has_value());
}
