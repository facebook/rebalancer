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

#include "algopt/lp/detail/generic/impl/GenericExpressionImpl.h"
#include "algopt/lp/detail/generic/impl/GenericVariableImpl.h"
#include "algopt/lp/factory/ProblemFactory.h"
#include "algopt/lp/generic/Constraint.h"
#include "algopt/lp/generic/Expression.h"
#include "algopt/lp/generic/Operators.h"
#include "algopt/lp/generic/Problem.h"
#include "algopt/rebalancer/tests/SolverTestUtils.h"

#include <fmt/core.h>
#include <folly/container/irange.h>
#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>

using namespace facebook::algopt::lp;
using namespace facebook::algopt::lp::detail;

namespace {

std::shared_ptr<const GenericExpressionImpl> getGeneric(Expression expr) {
  return std::static_pointer_cast<const GenericExpressionImpl>(expr.get());
}

std::shared_ptr<const GenericVariableImpl> getGeneric(Variable var) {
  return std::static_pointer_cast<const GenericVariableImpl>(var.get());
}

} // namespace

TEST(GenericExpressionTest, onlyLinearTerms) {
  REBALANCER_SKIP_IF_NO_GUROBI();
  auto problem =
      ProblemFactory::makeGenericProblem(ProblemFactory::makeGurobiProblem);
  auto sum = Expression(0);
  folly::F14FastMap<ConstGenericVarPtr, int> expectedCoeffs;
  constexpr int numVars = 5;
  for (const auto i : folly::irange(1, numVars + 1)) {
    auto x_i = problem.makeVar(fmt::format("x_{}", i));
    sum += i * x_i;
    expectedCoeffs.emplace(getGeneric(x_i), i);
  }

  // Adding small coefficients should not affect the number of variables in the
  // expression
  for (const auto i : folly::irange(numVars)) {
    auto eps_i = problem.makeVar(fmt::format("eps_{}", i));
    // choose a coefficient strictly smaller than EPSILON
    auto smallCoef = 0.99 * EPSILON;
    sum += smallCoef * eps_i;
  }

  auto expr = getGeneric(sum);
  EXPECT_EQ(numVars, expr->getLinearCoeffMap().size());

  EXPECT_EQ(
      "0 + (1 · x_1) + (2 · x_2) + (3 · x_3) + (4 · x_4) + (5 · x_5)",
      expr->digest());

  for (const auto& [var, coef] : expr->getLinearCoeffMap()) {
    EXPECT_EQ(expectedCoeffs.at(var), coef);
  }
  EXPECT_EQ(std::nullopt, expr->getQuadraticCoeffMap());

  // multiplying the expression with a very small value must drop all terms
  sum *= 0.99 * EPSILON;
  EXPECT_EQ("0", getGeneric(sum)->digest());
}

TEST(GenericExpressionTest, onlyQuadraticTerms) {
  REBALANCER_SKIP_IF_NO_GUROBI();
  auto problem =
      ProblemFactory::makeGenericProblem(ProblemFactory::makeGurobiProblem);
  auto sum = Expression(0);
  folly::F14FastMap<ConstGenericVarPtrPair, int> expectedCoeffs;
  constexpr int numVarPairs = 5;
  for (const auto i : folly::irange(1, numVarPairs + 1)) {
    auto x_i = problem.makeVar(fmt::format("x_{}", i));
    auto y_i = problem.makeVar(fmt::format("y_{}", i));
    // check that ordered variables get added together
    sum += i * x_i * y_i;
    sum += i * y_i * x_i;
    expectedCoeffs.emplace(
        GenericExpressionImpl::makeOrderedPair(
            getGeneric(x_i), getGeneric(y_i)),
        2 * i);
  }

  // Adding small coefficients should not affect the number of variables in the
  // expression
  for (const auto i : folly::irange(numVarPairs)) {
    auto eps_i = problem.makeVar(fmt::format("eps_{}", i));
    // choose a coefficient strictly smaller than EPSILON
    auto smallCoef = 0.99 * EPSILON;
    sum += smallCoef * eps_i * eps_i;
  }

  auto expr = getGeneric(sum);
  EXPECT_EQ(0, expr->getLinearCoeffMap().size());
  auto quadraticCoeffMap = expr->getQuadraticCoeffMap();
  EXPECT_NE(std::nullopt, quadraticCoeffMap);
  EXPECT_EQ(numVarPairs, quadraticCoeffMap->size());
  for (const auto& [varPair, coef] : *quadraticCoeffMap) {
    EXPECT_EQ(expectedCoeffs.at(varPair), coef);
  }
  EXPECT_EQ(
      "0 + (2 · x_1 · y_1) + (4 · x_2 · y_2) + (6 · x_3 · y_3) + (8 · x_4 · y_4) + (10 · x_5 · y_5)",
      expr->digest());

  // multiplying the expression with a very small value must drop all terms
  sum *= 0.99 * EPSILON;
  EXPECT_EQ("0", getGeneric(sum)->digest());
}

TEST(GenericExpressionTest, linearAndQuadraticTerms) {
  REBALANCER_SKIP_IF_NO_GUROBI();
  auto problem =
      ProblemFactory::makeGenericProblem(ProblemFactory::makeGurobiProblem);
  // 2a + 4b + 6c + 8
  auto abcExpr = 2 * problem.makeVar("a") + 4 * problem.makeVar("b") +
      6 * problem.makeVar("c") + 8;
  // x + 3y + 5z + 7
  auto xyzExpr = 1 * problem.makeVar("x") + 3 * problem.makeVar("y") +
      5 * problem.makeVar("z") + 7;

  auto quadExpr = getGeneric(abcExpr * xyzExpr);
  EXPECT_EQ(6, quadExpr->getLinearCoeffMap().size());
  EXPECT_NE(std::nullopt, quadExpr->getQuadraticCoeffMap());
  EXPECT_EQ(9, quadExpr->getQuadraticCoeffMap()->size());
  EXPECT_EQ(
      "56 + (14 · a) + (28 · b) + (42 · c) + "
      "(8 · x) + (24 · y) + (40 · z) + "
      "(2 · a · x) + (6 · a · y) + (10 · a · z) + "
      "(4 · b · x) + (12 · b · y) + (20 · b · z) + "
      "(6 · c · x) + (18 · c · y) + (30 · c · z)",
      quadExpr->digest());
}

TEST(GenericExpressionTest, termCancellation) {
  REBALANCER_SKIP_IF_NO_GUROBI();
  auto problem =
      ProblemFactory::makeGenericProblem(ProblemFactory::makeGurobiProblem);
  auto a = problem.makeVar("a");
  auto b = problem.makeVar("b");
  auto quadExpr = (a + b) * (a - b);
  EXPECT_EQ("0 + (1 · a · a) + (-1 · b · b)", getGeneric(quadExpr)->digest());
}

TEST(GenericExpressionTest, SensitivityUnsupportedBackendThrows) {
  // The base ConstraintImpl stands in for FastConstraintImpl /
  // GenericConstraintImpl, which do not override the sensitivity accessors.
  const ConstraintImpl unsupported{};
  EXPECT_THROW(unsupported.getDual(), std::runtime_error);
  EXPECT_THROW(unsupported.getSlack(), std::runtime_error);
  EXPECT_THROW(unsupported.getRHS(), std::runtime_error);
}
