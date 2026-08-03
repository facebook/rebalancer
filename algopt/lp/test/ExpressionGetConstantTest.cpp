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

// Tests for Expression::getConstant(). The value returned must reflect the
// constant/offset term of the expression regardless of whether the expression
// carries variables (in which case the constant lives inside the backend
// ExpressionImpl) or is a pure constant (stored in the wrapper). These tests
// are solver-parameterized so every backend's ExpressionImpl::getConstant()
// override is exercised.

#include "algopt/lp/factory/ProblemFactory.h"
#include "algopt/lp/generic/Expression.h"
#include "algopt/lp/generic/Operators.h"
#include "algopt/lp/generic/Problem.h"

#include <gtest/gtest.h>

#include <functional>

namespace facebook::algopt::lp::tests {

// A constant small enough to be lost when a variable-bearing expression only
// reported the wrapper's outer constant_. This is the exact value the Max
// linearization was leaking into LP row RHS.
constexpr double kTiny = 1e-19;

// Linear-only cases, safe for every backend.
void testGetConstant(const std::function<Problem()>& factory) {
  Problem problem = factory();
  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");

  // Pure constants (no variables) go through the wrapper's own scalar.
  EXPECT_DOUBLE_EQ(0.0, Expression().getConstant());
  EXPECT_DOUBLE_EQ(5.0, Expression(5.0).getConstant());
  EXPECT_DOUBLE_EQ(-2.5, Expression(-2.5).getConstant());

  // Backend-native constant expressions built via Problem::makeExpression must
  // report their constant. For Xpress this is the makeExpression(constant)
  // constructor path, whose shadow constant would otherwise default to 0.
  EXPECT_DOUBLE_EQ(0.0, problem.makeExpression(0.0).getConstant());
  EXPECT_DOUBLE_EQ(7.0, problem.makeExpression(7.0).getConstant());
  EXPECT_DOUBLE_EQ(-3.5, problem.makeExpression(-3.5).getConstant());

  // A bare variable is variable-bearing (built via Variable::makeExpression,
  // i.e. coeff * var) but has no constant term.
  EXPECT_DOUBLE_EQ(0.0, Expression(x).getConstant());
  EXPECT_DOUBLE_EQ(0.0, (x + y).getConstant());

  // A backend-native constant folded into a variable expression keeps its
  // constant -- exercises add(ExpressionImpl) with a variable-backed,
  // constant-only operand (not just the add(double) path above).
  EXPECT_DOUBLE_EQ(
      7.0, (Expression(x) + problem.makeExpression(7.0)).getConstant());

  // Variable-bearing expression with a constant: the regression case. Before
  // the fix these all returned 0 because the constant lived in the impl.
  EXPECT_DOUBLE_EQ(3.0, (x + 3.0).getConstant());
  EXPECT_DOUBLE_EQ(3.0, (3.0 + x).getConstant());
  EXPECT_DOUBLE_EQ(-4.0, (x - 4.0).getConstant());
  EXPECT_DOUBLE_EQ(5.0, (5.0 - x).getConstant());
  EXPECT_DOUBLE_EQ(2.5, (x + y + 2.5).getConstant());

  // A direct add(double) of a non-tiny constant lands in the impl.
  auto direct = Expression(x);
  direct.add(2.0);
  EXPECT_DOUBLE_EQ(2.0, direct.getConstant());

  // The arithmetic operator+ snaps within-tolerance constants to zero, so a
  // tiny constant added that way is dropped before it ever reaches the impl.
  EXPECT_EQ(0.0, (x + kTiny).getConstant());

  // The real leak path is add(Expression), which has no tolerance guard: this
  // is how OPAL's Sum folds a Constant child (e.g. 1e-19) into a
  // variable-bearing expression. getConstant() must surface that impl-resident
  // constant exactly
  // -- this is the observation the Option B clamp depends on and the case the
  // pre-fix getConstant() (outer scalar only) reported as 0.
  auto tinySum = x + y;
  tinySum.add(Expression(kTiny));
  EXPECT_EQ(kTiny, tinySum.getConstant());

  // Exact cancellation of an impl-resident tiny constant.
  auto tinyExpr = x + y;
  tinyExpr.add(Expression(kTiny));
  tinyExpr.add(-kTiny);
  EXPECT_EQ(0.0, tinyExpr.getConstant());

  // Subtracting two equal constants collapses the constant to exactly zero.
  EXPECT_EQ(0.0, ((x + 5.0) - (y + 5.0)).getConstant());

  // Combining constants across sub-expressions.
  EXPECT_DOUBLE_EQ(5.0, ((x + 2.0) + (y + 3.0)).getConstant());
  EXPECT_DOUBLE_EQ(5.0, ((x + 7.0) - (y + 2.0)).getConstant());

  // Scaling scales the constant term.
  EXPECT_DOUBLE_EQ(6.0, ((x + 2.0) * 3.0).getConstant());
  EXPECT_DOUBLE_EQ(-2.0, ((x + 2.0) * -1.0).getConstant());
  EXPECT_DOUBLE_EQ(0.0, ((x + 4.0) * 0.0).getConstant());
  EXPECT_DOUBLE_EQ(3.0, ((x + 6.0) / 2.0).getConstant());
  EXPECT_DOUBLE_EQ(-3.0, (-(x + 3.0)).getConstant());

  // In-place mutation.
  auto chained = Expression(x);
  chained += 1.0;
  chained += 2.0;
  chained -= 0.5;
  EXPECT_DOUBLE_EQ(2.5, chained.getConstant());

  // Adding a variable does not change the constant term.
  auto plusVar = x + 3.0;
  plusVar += y;
  EXPECT_DOUBLE_EQ(3.0, plusVar.getConstant());

  // Copies are independent: mutating a copy must not change the original's
  // constant (exercises each backend's clone()).
  auto original = x + 9.0;
  auto copy = original;
  copy.add(1.0);
  EXPECT_DOUBLE_EQ(9.0, original.getConstant());
  EXPECT_DOUBLE_EQ(10.0, copy.getConstant());
}

// Quadratic cases: only run on backends that support building expr * expr.
void testGetConstantQuadratic(const std::function<Problem()>& factory) {
  Problem problem = factory();
  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");

  // (x + 2)(y + 3) = xy + 3x + 2y + 6 -> constant term is 6.
  EXPECT_DOUBLE_EQ(6.0, ((x + 2.0) * (y + 3.0)).getConstant());
  // (x + 2)(y - 2) -> constant term is -4.
  EXPECT_DOUBLE_EQ(-4.0, ((x + 2.0) * (y - 2.0)).getConstant());
  // A zero constant on one side zeroes the product's constant term.
  EXPECT_DOUBLE_EQ(0.0, ((x + 5.0) * Expression(y)).getConstant());
}

TEST(ExpressionGetConstantTest, Fast) {
  testGetConstant(ProblemFactory::makeFastProblem);
}

#ifdef REBALANCER_USE_XPRESS

TEST(ExpressionGetConstantTest, Xpress) {
  testGetConstant(ProblemFactory::makeXpressProblem);
}

TEST(ExpressionGetConstantTest, XpressQuadratic) {
  testGetConstantQuadratic(ProblemFactory::makeXpressProblem);
}

#endif

#ifdef REBALANCER_USE_GUROBI

TEST(ExpressionGetConstantTest, Gurobi) {
  testGetConstant(ProblemFactory::makeGurobiProblem);
}

TEST(ExpressionGetConstantTest, Generic) {
  testGetConstant([]() {
    return ProblemFactory::makeGenericProblem(
        ProblemFactory::makeGurobiProblem);
  });
}

TEST(ExpressionGetConstantTest, GurobiQuadratic) {
  testGetConstantQuadratic(ProblemFactory::makeGurobiProblem);
}

TEST(ExpressionGetConstantTest, GenericQuadratic) {
  testGetConstantQuadratic([]() {
    return ProblemFactory::makeGenericProblem(
        ProblemFactory::makeGurobiProblem);
  });
}

#endif

#ifdef REBALANCER_USE_HIGHS

TEST(ExpressionGetConstantTest, HiGHS) {
  testGetConstant(ProblemFactory::makeHiGHSProblem);
}

#endif

} // namespace facebook::algopt::lp::tests
