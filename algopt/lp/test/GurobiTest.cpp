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

#ifndef REBALANCER_OSS_BUILD
#include "algopt/lp/environment/fb/SetupEnvironments.h"
#endif

#ifdef REBALANCER_USE_GUROBI

#include "algopt/lp/detail/generic/impl/thrift/gen-cpp2/model_types.h"
#include "algopt/lp/detail/gurobi/GurobiProblem.h"
#include "algopt/lp/factory/ProblemFactory.h"
#include "algopt/lp/generic/Expression.h"
#include "algopt/lp/generic/Operators.h"
#include "algopt/rebalancer/algopt_common/TestUtils.h"

#include <gtest/gtest.h>

#include <string>

using namespace facebook::algopt::lp;
using namespace facebook::algopt::lp::detail;

TEST(GurobiTest, LongVariableName) {
  auto problem = ProblemFactory::makeGurobiProblem();
  REBALANCER_EXPECT_RUNTIME_ERROR(
      problem.makeVar(std::string(256, 'a')),
      "Name too long (maximum name length is 255 characters)");
}

TEST(GurobiTest, ToThriftVariables) {
  auto problem = ProblemFactory::makeGurobiProblem();

  // Create variables of different types.
  auto x = problem.makeVar("x");
  x.setLB(-5);
  x.setUB(10);

  auto y = problem.makeIntVar("y");
  y.setLB(0);
  y.setUB(100);

  auto b = problem.makeBoolVar("b");

  auto sc = problem.makeSemiContVar("sc", 2.0);

  auto si = problem.makeSemiIntVar("si", 3.0);

  // Need an objective to call toThrift.
  problem.setObjective(Expression(0));

  const auto& gurobi = static_cast<const GurobiProblem&>(problem.get());
  auto model = gurobi.toThrift();

  ASSERT_EQ(5, model.variables()->size());

  // x: continuous, lb=-5, ub=10
  const auto& vx = model.variables()->at(0);
  EXPECT_EQ("x", *vx.name());
  EXPECT_EQ(thrift::VariableType::CONTINUOUS, *vx.type());
  EXPECT_EQ(-5, *vx.lowerBound());
  EXPECT_EQ(10, *vx.upperBound());
  EXPECT_FALSE(vx.threshold().has_value());

  // y: integer, lb=0, ub=100
  const auto& vy = model.variables()->at(1);
  EXPECT_EQ("y", *vy.name());
  EXPECT_EQ(thrift::VariableType::INTEGER, *vy.type());
  EXPECT_EQ(0, *vy.lowerBound());
  EXPECT_EQ(100, *vy.upperBound());

  // b: binary, lb=0, ub=1
  const auto& vb = model.variables()->at(2);
  EXPECT_EQ("b", *vb.name());
  EXPECT_EQ(thrift::VariableType::BINARY, *vb.type());
  EXPECT_EQ(0, *vb.lowerBound());
  EXPECT_EQ(1, *vb.upperBound());

  // sc: semi-continuous, threshold=2.0, no lowerBound
  const auto& vsc = model.variables()->at(3);
  EXPECT_EQ("sc", *vsc.name());
  EXPECT_EQ(thrift::VariableType::SEMI_CONTINUOUS, *vsc.type());
  EXPECT_FALSE(vsc.lowerBound().has_value());
  EXPECT_EQ(2.0, *vsc.threshold());

  // si: semi-integer, threshold=3.0, no lowerBound
  const auto& vsi = model.variables()->at(4);
  EXPECT_EQ("si", *vsi.name());
  EXPECT_EQ(thrift::VariableType::SEMI_INTEGER, *vsi.type());
  EXPECT_FALSE(vsi.lowerBound().has_value());
  EXPECT_EQ(3.0, *vsi.threshold());
}

TEST(GurobiTest, ToThriftUnboundedVariablesOmitBounds) {
  auto problem = ProblemFactory::makeGurobiProblem();

  // Create a variable without explicit bounds — should have no bounds in thrift
  // since Gurobi's default ±1e100 should be omitted.
  auto x = problem.makeVar("x");
  problem.setObjective(Expression(0));

  const auto& gurobi = static_cast<const GurobiProblem&>(problem.get());
  auto model = gurobi.toThrift();

  ASSERT_EQ(1, model.variables()->size());
  const auto& vx = model.variables()->at(0);
  EXPECT_FALSE(vx.lowerBound().has_value());
  EXPECT_FALSE(vx.upperBound().has_value());
}

TEST(GurobiTest, ToThriftLinearConstraints) {
  auto problem = ProblemFactory::makeGurobiProblem();

  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");

  // Add constraint: 2x + 3y <= 10, i.e. (2x + 3y - 10) <= 0
  problem.newConstraint(2 * x + 3 * y <= 10, "c1");

  // Add constraint: x + y == 5, i.e. (x + y - 5) == 0
  problem.newConstraint(x + y == 5, "c2");

  // Add constraint: x >= 1, i.e. (x - 1) >= 0
  problem.newConstraint(x >= 1, "c3");

  problem.setObjective(Expression(0));

  const auto& gurobi = static_cast<const GurobiProblem&>(problem.get());
  auto model = gurobi.toThrift();

  ASSERT_EQ(3, model.constraints()->size());

  // c1: 2x + 3y - 10 <= 0
  const auto& c1 = model.constraints()->at(0);
  EXPECT_EQ("c1", *c1.name());
  EXPECT_EQ(thrift::ConstraintType::LEQ_ZERO, *c1.type());
  EXPECT_EQ(-10, *c1.expr()->constant());
  EXPECT_EQ(2, c1.expr()->linearCoeffs()->size());
  EXPECT_EQ(2.0, c1.expr()->linearCoeffs()->at(0)); // x index=0
  EXPECT_EQ(3.0, c1.expr()->linearCoeffs()->at(1)); // y index=1

  // c2: x + y - 5 == 0
  const auto& c2 = model.constraints()->at(1);
  EXPECT_EQ("c2", *c2.name());
  EXPECT_EQ(thrift::ConstraintType::EQUALS_ZERO, *c2.type());
  EXPECT_EQ(-5, *c2.expr()->constant());
  EXPECT_EQ(1.0, c2.expr()->linearCoeffs()->at(0));
  EXPECT_EQ(1.0, c2.expr()->linearCoeffs()->at(1));

  // c3: x - 1 >= 0
  const auto& c3 = model.constraints()->at(2);
  EXPECT_EQ("c3", *c3.name());
  EXPECT_EQ(thrift::ConstraintType::GEQ_ZERO, *c3.type());
  EXPECT_EQ(-1, *c3.expr()->constant());
  EXPECT_EQ(1.0, c3.expr()->linearCoeffs()->at(0));
}

TEST(GurobiTest, ToThriftObjectives) {
  auto problem = ProblemFactory::makeGurobiProblem();

  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");

  // Multi-objective: first minimize 3x + 2, then minimize y.
  problem.setObjective({3 * x + 2, 1 * y});

  // Add a trivial constraint so the model is valid.
  problem.newConstraint(x + y >= 0, "trivial");

  const auto& gurobi = static_cast<const GurobiProblem&>(problem.get());
  auto model = gurobi.toThrift();

  ASSERT_EQ(2, model.objectives()->size());

  // Objective 0: 3x + 2
  const auto& obj0 = model.objectives()->at(0);
  EXPECT_EQ(2, *obj0.constant());
  EXPECT_EQ(1, obj0.linearCoeffs()->size());
  EXPECT_EQ(3.0, obj0.linearCoeffs()->at(0)); // x

  // Objective 1: y
  const auto& obj1 = model.objectives()->at(1);
  EXPECT_EQ(0, *obj1.constant());
  EXPECT_EQ(1, obj1.linearCoeffs()->size());
  EXPECT_EQ(1.0, obj1.linearCoeffs()->at(1)); // y
}

TEST(GurobiTest, ToThriftQuadraticConstraint) {
  auto problem = ProblemFactory::makeGurobiProblem();

  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");

  // Add quadratic constraint: x*y + 2x <= 5
  // In thrift form: x*y + 2x - 5 <= 0
  problem.newConstraint(x * y + 2 * x <= 5, "qc1");

  problem.setObjective(Expression(0));

  const auto& gurobi = static_cast<const GurobiProblem&>(problem.get());
  auto model = gurobi.toThrift();

  // Quadratic constraints come after linear constraints.
  ASSERT_GE(model.constraints()->size(), 1);

  // Find the quadratic constraint by name.
  const thrift::GenericConstraint* qc = nullptr;
  for (const auto& c : *model.constraints()) {
    if (*c.name() == "qc1") {
      qc = &c;
      break;
    }
  }
  ASSERT_NE(nullptr, qc);

  EXPECT_EQ(thrift::ConstraintType::LEQ_ZERO, *qc->type());
  EXPECT_EQ(-5, *qc->expr()->constant());

  // Linear part: 2x
  EXPECT_EQ(2.0, qc->expr()->linearCoeffs()->at(0)); // x index=0

  // Quadratic part: x*y (var indices 0 and 1)
  EXPECT_FALSE(qc->expr()->quadraticCoeffs()->empty());
  EXPECT_EQ(1.0, qc->expr()->quadraticCoeffs()->at(0).at(1)); // x*y
}

TEST(GurobiTest, UpdatePresolvedNonZerosParsesPresolvedLine) {
  EXPECT_EQ(
      707622,
      GurobiProblem::updatePresolvedNonZeros(
          -1, "Presolved: 45512 rows, 372758 columns, 707622 nonzeros\n"));
}

TEST(GurobiTest, UpdatePresolvedNonZerosIgnoresOtherMessages) {
  EXPECT_EQ(
      -1,
      GurobiProblem::updatePresolvedNonZeros(
          -1, "Presolve removed 16509 rows and 71187 columns\n"));
}

// A hierarchical multi-objective solve prints one "Presolved:" line for the
// initial presolve and another for each objective pass; the first must win.
TEST(GurobiTest, UpdatePresolvedNonZerosKeepsFirstPresolvedLine) {
  const int initial = GurobiProblem::updatePresolvedNonZeros(
      -1, "Presolved: 45512 rows, 372758 columns, 707622 nonzeros\n");

  const int afterObjectivePass = GurobiProblem::updatePresolvedNonZeros(
      initial, "Presolved: 76 rows, 280 columns, 735 nonzeros\n");

  EXPECT_EQ(707622, afterObjectivePass);
}

#endif
