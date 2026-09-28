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

#include "algopt/lp/detail/decomposable/DecomposableProblem.h"
#include "algopt/lp/detail/gurobi/GurobiVariable.h"
#include "algopt/lp/factory/ProblemFactory.h"
#include "algopt/lp/generic/Expression.h"
#include "algopt/lp/generic/Operators.h"
#include "algopt/lp/generic/Problem.h"
#include "algopt/rebalancer/algopt_common/TestUtils.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <folly/FileUtil.h>
#include <folly/logging/xlog.h>
#include <folly/Random.h>
#include <folly/String.h>
#include <folly/testing/TestUtil.h>
#include <gtest/gtest.h>

#include <functional>
#include <memory>

namespace facebook::algopt::lp::tests {
using namespace facebook::algopt::lp::detail;

Problem makeDecomposableGurobiProblem() {
  return Problem(
      std::make_unique<DecomposableProblem>(ProblemFactory::makeGurobiProblem));
}

Problem makeGenericGurobiProblem() {
  return Problem(
      std::make_unique<GenericProblemImpl>(ProblemFactory::makeGurobiProblem));
}

void testTolerances(const std::function<Problem()>& factory) {
  Problem problem = factory();
  auto tol = Tolerances{};
  problem.setTolerances(tol);
  tol = problem.getTolerances();
  // Expect no crashes / failures with default tolerances
}

void testCompactPrinting(const std::function<Problem()>& factory) {
  Problem problem = factory();
  auto tol = Tolerances{.constraint = 1e-6};
  problem.setTolerances(tol);
  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");
  y.setLB(1);
  problem.newConstraint(x + y >= 0, "trivially satisfied, not printed");
  problem.newConstraint(x <= 0, "numerically tight ctr1");
  problem.newConstraint(x >= 0.1 * tol.constraint, "numerically tight ctr2");
  problem.solve();
  XLOG(INFO) << fmt::format(
      "Solution found with values x: {}, y: {}", x.getValue(), y.getValue());
  XLOG(INFO) << "Printing numerically sensitive constraints";
  ::testing::internal::CaptureStderr();
  problem.print(/*substringsToMatch=*/{"numerically tight"});
  const auto output = ::testing::internal::GetCapturedStderr();
  // expect both of the numerically tight constraints to be printed
  EXPECT_TRUE(
      output.find("ctr1") != std::string::npos &&
      output.find("ctr2") != std::string::npos);
  EXPECT_TRUE(
      output.find("trivially satisfied, not printed") == std::string::npos);
}

// some result attributes such as ObjBound in case of Gurobi is not accesible
// for only quadratic LP (non integer) models. This test checks that we don't
// crash in that case
void testQuadraticLPModel(const std::function<Problem()>& factory) {
  auto problem = factory();
  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");
  problem.setObjective(y * y);
  problem.newConstraint(y - x - 0.5 >= 0);
  problem.newConstraint(y + x - 0.5 >= 0);
  problem.solve();
  EXPECT_NEAR(0, x.getValue(), 1e-8);
  EXPECT_NEAR(0.5, y.getValue(), 1e-8);

  // ensure that computeValues are correct
  auto xSquare = x * x;
  auto ySquare = y * y;
  EXPECT_NEAR(0, xSquare.computeValue(), 1e-8);
  EXPECT_NEAR(0.25, ySquare.computeValue(), 1e-8);
}

void testSemiContinuous(const std::function<Problem()>& factory) {
  auto problem = factory();
  auto a = problem.makeVar("a");
  a.setLB(0);
  a.setUB(10);
  auto b = problem.makeSemiContVar("b");
  b.setThreshold(5);
  b.setUB(10);
  problem.newConstraint(a + b == 10);
  problem.setObjective(b);
  problem.solve();
  EXPECT_NEAR(0, b.getValue(), 1e-8);

  problem.newConstraint(b >= 6.5);
  problem.solve();
  EXPECT_NEAR(6.5, b.getValue(), 1e-8);
}

void testSemiInteger(const std::function<Problem()>& factory) {
  auto problem = factory();
  auto a = problem.makeVar("a");
  a.setLB(0);
  a.setUB(10);
  auto b = problem.makeSemiIntVar("b");
  b.setThreshold(5);
  b.setUB(10);
  problem.newConstraint(a + b == 10);
  problem.setObjective(b);
  problem.solve();
  EXPECT_NEAR(0, b.getValue(), 1e-8);

  problem.newConstraint(b >= 6.5);
  problem.solve();
  EXPECT_NEAR(7, b.getValue(), 1e-8);
}

Problem testExample(const std::function<Problem()>& factory) {
  // Example from: https://en.wikipedia.org/wiki/Integer_programming#Example
  // With a small change to the objective which makes the solution unique.
  auto problem = factory();
  auto x = problem.makeIntVar("x");
  x.setLB(0);
  auto y = problem.makeIntVar("y");
  y.setLB(0);
  problem.newConstraint(-x + y <= 1);
  problem.newConstraint(3 * x + 2 * y <= 12);
  problem.newConstraint(2 * x + 3 * y <= 12);
  auto maximize = -0.5 * x + y;
  problem.setObjective(-maximize);
  problem.setCallback([&](const auto& callbackData) {
    EXPECT_GE(callbackData.walltime, 0);
    EXPECT_LE(callbackData.bound, -1.5 + 1e-8);
    EXPECT_GE(callbackData.objective, -1.5 - 1e-8);
    return ProblemCallbackAction::CONTINUE;
  });
  problem.solve();
  EXPECT_NEAR(1, x.getValue(), 1e-8);
  EXPECT_NEAR(2, y.getValue(), 1e-8);

  // ensure that computeValues() are correct
  auto expr1 = x + 1;
  auto expr2 = y + 1;
  EXPECT_NEAR(2, expr1.computeValue(), 1e-8);
  EXPECT_NEAR(3, expr2.computeValue(), 1e-8);

  thrift::ProblemResult result = *problem.getResultPerObjective().begin();
  auto problemAttributes = result.problemAttributes();
  EXPECT_EQ(2, problemAttributes->numVariables());
  EXPECT_EQ(3, problemAttributes->numConstraints());
  EXPECT_EQ(6, problemAttributes->numOfNonZeros());

  return problem;
}

void testInfeasible(const std::function<Problem()>& factory) {
  auto problem = factory();
  auto x = problem.makeIntVar("x");
  x.setUB(3);
  problem.newConstraint(x >= 4);
  problem.setObjective(x);
  problem.solve();
  EXPECT_EQ(thrift::ProblemStatus::NO_SOLUTION_EXISTS, problem.getStatus());

#ifdef REBALANCER_USE_GUROBI
  if (std::dynamic_pointer_cast<const GurobiVariable>(x.get())) {
    REBALANCER_EXPECT_RUNTIME_ERROR(x.getValue(), "Var::get");
    REBALANCER_EXPECT_RUNTIME_ERROR((x + 1).computeValue(), "Var::get");
    REBALANCER_EXPECT_RUNTIME_ERROR((x + 1).getValue(), "Var::get");
  }
#endif
}

void testNoConstraintSolve(const std::function<Problem()>& factory) {
  auto problem = factory();
  auto x = problem.makeIntVar("x");
  x.setLB(3);
  problem.setObjective(x);
  problem.solve();
  EXPECT_NEAR(3, x.getValue(), 1e-8);
  thrift::ProblemResult result = *problem.getResultPerObjective().begin();
  EXPECT_EQ(0, result.problemAttributes()->numConstraints());
}

void testUnboundedSolve(const std::function<Problem()>& factory) {
  auto problem = factory();
  auto x = problem.makeIntVar("x");
  problem.setObjective(x);
  problem.solve();
  EXPECT_EQ(thrift::ProblemStatus::NO_SOLUTION_EXISTS, problem.getStatus());
}

void testVariables(const std::function<Problem()>& factory) {
  Problem problem = factory();
  auto var1 = problem.makeVar();
  var1.setLB(-10);
  var1.setUB(100);
  problem.makeVar("continuous variable");
  problem.makeIntVar();
  problem.makeIntVar("integer variable");
  problem.makeBoolVar();
  problem.makeBoolVar("boolean variable");
  problem.makeSemiContVar();
  auto semiCont = problem.makeSemiContVar("semi continuous variable", 10);
  semiCont.setThreshold(20);
  problem.makeSemiIntVar();
  auto semiInt = problem.makeSemiIntVar("semi integer variable", 10);
  semiInt.setThreshold(20);
}

void testNoSolutionFound(const std::function<Problem()>& factory) {
  auto problem = factory();
  // We want to build a model that is not too easy to solve and
  // the solver times out, so we write the MIP model for set cover
  constexpr int numElements = 1000;
  std::vector<Variable> vars;
  Expression setCoverSize;
  for (const auto i : folly::irange(numElements)) {
    vars.push_back(problem.makeBoolVar(fmt::format("x_{}", i)));
    setCoverSize += vars.at(i);
  }

  auto rng = folly::Random::DefaultGenerator(0); // NOLINT: use fixed seed

  // Add a constraint for every set
  constexpr int numSets = 20000;
  constexpr int width = 10;
  for (const auto _ : folly::irange(numSets)) {
    Expression ctr;
    for (const auto _ : folly::irange(width)) {
      const int idx = folly::Random::rand32(vars.size(), rng);
      ctr += vars.at(idx);
    }
    problem.newConstraint(ctr >= 1);
  }

  // this constraint is important or else Gurobi presolve finds a (sub-optimal)
  // solution quickly, even when timeout is set to zero
  problem.newConstraint(setCoverSize <= vars.size() * 0.25);
  problem.setObjective(setCoverSize);
  // For Xpress, time limit of 0 means unlimited time, so we set this to next
  // integer
  problem.setMaxSolveTime(1);
  // stop whenever the first callback is scheduled or the time limit is hit,
  // whichever is earlier
  problem.setCallback(
      [&](const auto& /* callbackData */) -> ProblemCallbackAction {
        return ProblemCallbackAction::ABORT;
      });
  problem.solve();
  // the solver should time out and should not be able to find any solution or
  // prove that the model is infeasible
  // However, on some machines the solver might find "one" solution and the
  // following assert will be flaky:
  // EXPECT_EQ(ProblemStatus::SOLUTION_NOT_FOUND, problem.getStatus());
  // Therefore, we verify that solve status is correctly set indirectly
  // by checking variable values. if no solution found, and status was set to
  // found, accessing the variable values will throw
  const auto status = problem.getStatus();
  const bool success = status == thrift::ProblemStatus::SOLUTION_FOUND ||
      status == thrift::ProblemStatus::OPTIMAL_FOUND;
  if (success) {
    bool exceptionAccessingVars = false;
    for (auto& v : vars) {
      try {
        std::ignore = v.getValue();
      } catch (...) {
        exceptionAccessingVars = true;
      }
    }
    EXPECT_FALSE(exceptionAccessingVars)
        << "unable to access variable values even when solve succeeded";
  } else {
    const auto desc = status == thrift::ProblemStatus::NO_SOLUTION_EXISTS
        ? "infeasible"
        : "solution not found";
    XLOG(INFO) << "Failed solve with status " << desc;
  }
}

void testOperators(const std::function<Problem()>& factory) {
  // Below, a 'term' is used to refer a simple expression which is of the form
  // coeff * variable
  auto boundVal = 3.0;
  Problem problem = factory();
  auto var = problem.makeVar();
  // just setting the var to be a constant so that we can evaluate
  var.setBounds(boundVal, boundVal);
  problem.setObjective(var);
  problem.solve();

  auto makeVar = [&]() -> Variable { return var; };
  auto makeTerm = [&makeVar]() -> Expression { return 2 * makeVar(); };
  auto makeExpr = [&makeVar]() -> Expression { return 4 + makeVar(); };

  auto expectedVarVal = boundVal;
  EXPECT_NEAR(makeVar().getValue(), expectedVarVal, 1e-8);
  auto expectedTermVal = 2 * boundVal;
  EXPECT_NEAR(makeTerm().getValue(), expectedTermVal, 1e-8);
  auto expectedExprVal = 4 + boundVal;
  EXPECT_NEAR(makeExpr().getValue(), expectedExprVal, 1e-8);

  // test all combinations of operators
  const Expression term1 = 8 * makeVar(); // constant * variable
  EXPECT_NEAR(term1.getValue(), 8 * expectedVarVal, 1e-8);
  const Expression term2 = makeVar() * 8; // variable * constant
  EXPECT_NEAR(term2.getValue(), expectedVarVal * 8, 1e-8);
  const Expression term3 = makeVar() / 8; // variable / constant
  EXPECT_NEAR(term3.getValue(), expectedVarVal / 8, 1e-8);
  const Expression term4 = -makeVar(); // -variable
  EXPECT_NEAR(term4.getValue(), -expectedVarVal, 1e-8);
  Expression expr1 = -makeTerm(); // -term
  EXPECT_NEAR(expr1.getValue(), -expectedTermVal, 1e-8);
  const Expression expr2 = -makeExpr(); // -expression
  EXPECT_NEAR(expr2.getValue(), -expectedExprVal, 1e-8);
  const Expression expr3 = 8 + makeVar(); // constant + variable
  EXPECT_NEAR(expr3.getValue(), 8 + expectedVarVal, 1e-8);
  const Expression expr4 = makeVar() + 8; // variable + constant
  EXPECT_NEAR(expr4.getValue(), expectedVarVal + 8, 1e-8);
  const Expression expr5 = 8 + makeTerm(); // constant + term
  EXPECT_NEAR(expr5.getValue(), 8 + expectedTermVal, 1e-8);
  const Expression expr6 = makeTerm() + 8; // term + constant
  EXPECT_NEAR(expr6.getValue(), expectedTermVal + 8, 1e-8);
  const Expression expr7 = 8 + makeExpr(); // constant + expression
  EXPECT_NEAR(expr7.getValue(), 8 + expectedExprVal, 1e-8);
  const Expression expr8 = makeExpr() + 8; // expression + constant
  EXPECT_NEAR(expr8.getValue(), expectedExprVal + 8, 1e-8);
  const Expression expr9 = makeVar() + makeVar(); // variable + variable
  EXPECT_NEAR(expr9.getValue(), expectedVarVal + expectedVarVal, 1e-8);
  const Expression expr10 = makeVar() + makeTerm(); // variable + term
  EXPECT_NEAR(expr10.getValue(), expectedVarVal + expectedTermVal, 1e-8);
  const Expression expr11 = makeTerm() + makeVar(); // term + variable
  EXPECT_NEAR(expr11.getValue(), expectedTermVal + expectedVarVal, 1e-8);
  const Expression expr12 = makeVar() + makeExpr(); // variable + expression
  EXPECT_NEAR(expr12.getValue(), expectedVarVal + expectedExprVal, 1e-8);
  const Expression expr13 = makeExpr() + makeVar(); // expression + variable
  EXPECT_NEAR(expr13.getValue(), expectedExprVal + expectedVarVal, 1e-8);
  const Expression expr14 = makeTerm() + makeTerm(); // term + term
  EXPECT_NEAR(expr14.getValue(), expectedTermVal + expectedTermVal, 1e-8);
  const Expression expr15 = makeTerm() + makeExpr(); // term + expression
  EXPECT_NEAR(expr15.getValue(), expectedTermVal + expectedExprVal, 1e-8);
  const Expression expr16 = makeExpr() + makeTerm(); // expression + term
  EXPECT_NEAR(expr16.getValue(), expectedExprVal + expectedTermVal, 1e-8);
  const Expression expr17 = makeExpr() + makeExpr(); // expression + expression
  EXPECT_NEAR(expr17.getValue(), expectedExprVal + expectedExprVal, 1e-8);
  const Expression expr18 = 8 - makeVar(); // constant - variable
  EXPECT_NEAR(expr18.getValue(), 8 - expectedVarVal, 1e-8);
  const Expression expr19 = makeVar() - 8; // variable - constant
  EXPECT_NEAR(expr19.getValue(), expectedVarVal - 8, 1e-8);
  const Expression expr20 = 8 - makeTerm(); // constant - term
  EXPECT_NEAR(expr20.getValue(), 8 - expectedTermVal, 1e-8);
  const Expression expr21 = makeTerm() - 8; // term - constant
  EXPECT_NEAR(expr21.getValue(), expectedTermVal - 8, 1e-8);
  const Expression expr22 = 8 - makeExpr(); // constant - expression
  EXPECT_NEAR(expr22.getValue(), 8 - expectedExprVal, 1e-8);
  const Expression expr23 = makeExpr() - 8; // expression - constant
  EXPECT_NEAR(expr23.getValue(), expectedExprVal - 8, 1e-8);
  const Expression expr24 = makeVar() - makeVar(); // variable - variable
  EXPECT_NEAR(expr24.getValue(), expectedVarVal - expectedVarVal, 1e-8);
  const Expression expr25 = makeVar() - makeTerm(); // variable - term
  EXPECT_NEAR(expr25.getValue(), expectedVarVal - expectedTermVal, 1e-8);
  const Expression expr26 = makeTerm() - makeVar(); // term - variable
  EXPECT_NEAR(expr26.getValue(), expectedTermVal - expectedVarVal, 1e-8);
  const Expression expr27 = makeVar() - makeExpr(); // variable - expression
  EXPECT_NEAR(expr27.getValue(), expectedVarVal - expectedExprVal, 1e-8);
  const Expression expr28 = makeExpr() - makeVar(); // expression - variable
  EXPECT_NEAR(expr28.getValue(), expectedExprVal - expectedVarVal, 1e-8);
  const Expression expr29 = makeTerm() - makeTerm(); // term - term
  EXPECT_NEAR(expr29.getValue(), expectedTermVal - expectedTermVal, 1e-8);
  const Expression expr30 = makeTerm() - makeExpr(); // term - expression
  EXPECT_NEAR(expr30.getValue(), expectedTermVal - expectedExprVal, 1e-8);
  const Expression expr31 = makeExpr() - makeTerm(); // expression - term
  EXPECT_NEAR(expr31.getValue(), expectedExprVal - expectedTermVal, 1e-8);
  const Expression expr32 = makeExpr() - makeExpr(); // expression - expression
  EXPECT_NEAR(expr32.getValue(), expectedExprVal - expectedExprVal, 1e-8);
  const Expression expr33 = 8 * makeVar(); // constant * variable
  EXPECT_NEAR(expr33.getValue(), 8 * expectedVarVal, 1e-8);
  const Expression expr34 = makeVar() * 8; // variable * constant
  EXPECT_NEAR(expr34.getValue(), expectedVarVal * 8, 1e-8);
  const Expression expr35 = 8 * makeTerm(); // constant * term
  EXPECT_NEAR(expr35.getValue(), 8 * expectedTermVal, 1e-8);
  const Expression expr36 = makeTerm() * 8; // term * constant
  EXPECT_NEAR(expr36.getValue(), expectedTermVal * 8, 1e-8);
  const Expression expr37 = 8 * makeExpr(); // constant * expression
  EXPECT_NEAR(expr37.getValue(), 8 * expectedExprVal, 1e-8);
  const Expression expr38 = makeExpr() * 8; // expression * constant
  EXPECT_NEAR(expr38.getValue(), expectedExprVal * 8, 1e-8);
  const Expression expr39 = makeTerm() / 8; // term / constant
  EXPECT_NEAR(expr39.getValue(), expectedTermVal / 8, 1e-8);
  const Expression expr40 = makeExpr() / 8; // expression / constant
  EXPECT_NEAR(expr40.getValue(), expectedExprVal / 8, 1e-8);
  const Expression expr41 = makeVar(); // expression = variable
  EXPECT_NEAR(expr41.getValue(), expectedVarVal, 1e-8);
  const Expression expr42 = makeTerm(); // expression = term
  EXPECT_NEAR(expr42.getValue(), expectedTermVal, 1e-8);
  const Expression expr43 = makeExpr(); // expression = expression
  EXPECT_NEAR(expr43.getValue(), expectedExprVal, 1e-8);
  const Expression expr44 = 2; // constant
  EXPECT_NEAR(expr44.getValue(), 2, 1e-8);
  const Expression expr45 = 3; // constant
  EXPECT_NEAR(expr45.getValue(), 3, 1e-8);
  const Expression expr46 = expr44 * expr45; // expression * expression
  EXPECT_NEAR(expr46.getValue(), 6, 1e-8);

  auto prevExpr1 = expr1.getValue();
  expr1 += 8; // expression += constant
  EXPECT_NEAR(expr1.getValue(), prevExpr1 + 8, 1e-8);
  prevExpr1 = expr1.getValue();
  expr1 += makeVar(); // expression += variable
  EXPECT_NEAR(expr1.getValue(), prevExpr1 + expectedVarVal, 1e-8);
  prevExpr1 = expr1.getValue();
  expr1 += makeTerm(); // expression += term
  EXPECT_NEAR(expr1.getValue(), prevExpr1 + expectedTermVal, 1e-8);
  prevExpr1 = expr1.getValue();
  expr1 += makeExpr(); // expression += expression
  EXPECT_NEAR(expr1.getValue(), prevExpr1 + expectedExprVal, 1e-8);
  prevExpr1 = expr1.getValue();
  expr1 -= 8; // expression -= constant
  EXPECT_NEAR(expr1.getValue(), prevExpr1 - 8, 1e-8);
  prevExpr1 = expr1.getValue();
  expr1 -= makeVar(); // expression -= variable
  EXPECT_NEAR(expr1.getValue(), prevExpr1 - expectedVarVal, 1e-8);
  prevExpr1 = expr1.getValue();
  expr1 -= makeTerm(); // expression -= term
  EXPECT_NEAR(expr1.getValue(), prevExpr1 - expectedTermVal, 1e-8);
  prevExpr1 = expr1.getValue();
  expr1 -= makeExpr(); // expression -= expression
  EXPECT_NEAR(expr1.getValue(), prevExpr1 - expectedExprVal, 1e-8);
  prevExpr1 = expr1.getValue();
  expr1 *= 8; // expression *= constant
  EXPECT_NEAR(expr1.getValue(), prevExpr1 * 8, 1e-8);
  prevExpr1 = expr1.getValue();
  expr1 /= 8; // expression /= constant
  EXPECT_NEAR(expr1.getValue(), prevExpr1 / 8, 1e-8);
  const Relation rel1 = 8 == makeVar(); // constant == variable
  const Relation rel2 = makeVar() == 8; // variable == constant
  const Relation rel3 = 8 == makeTerm(); // constant == term
  const Relation rel4 = makeTerm() == 8; // term == constant
  const Relation rel5 = 8 == makeExpr(); // constant == expression
  const Relation rel6 = makeExpr() == 8; // expression == constant
  const Relation rel7 = makeVar() == makeVar(); // variable == variable
  const Relation rel8 = makeVar() == makeTerm(); // variable == term
  const Relation rel9 = makeTerm() == makeVar(); // term == variable
  const Relation rel10 = makeVar() == makeExpr(); // variable == expression
  const Relation rel11 = makeExpr() == makeVar(); // expression == variable
  const Relation rel12 = makeTerm() == makeTerm(); // term == term
  const Relation rel13 = makeTerm() == makeExpr(); // term == expression
  const Relation rel14 = makeExpr() == makeTerm(); // expression == term;
  const Relation rel15 = makeExpr() == makeExpr(); // expression == expression
  const Relation rel16 = 8 <= makeVar(); // constant <= variable
  const Relation rel17 = makeVar() <= 8; // variable <= constant
  const Relation rel18 = 8 <= makeTerm(); // constant <= term
  const Relation rel19 = makeTerm() <= 8; // term <= constant
  const Relation rel20 = 8 <= makeExpr(); // constant <= expression
  const Relation rel21 = makeExpr() <= 8; // expression <= constant
  const Relation rel22 = makeVar() <= makeVar(); // variable <= variable
  const Relation rel23 = makeVar() <= makeTerm(); // variable <= term
  const Relation rel24 = makeTerm() <= makeVar(); // term <= variable
  const Relation rel25 = makeVar() <= makeExpr(); // variable <= expression
  const Relation rel26 = makeExpr() <= makeVar(); // expression <= variable
  const Relation rel27 = makeTerm() <= makeTerm(); // term <= term
  const Relation rel28 = makeTerm() <= makeExpr(); // term <= expression
  const Relation rel29 = makeExpr() <= makeTerm(); // expression <= term;
  const Relation rel30 = makeExpr() <= makeExpr(); // expression <= expression
  const Relation rel31 = 8 >= makeVar(); // constant >= variable
  const Relation rel32 = makeVar() >= 8; // variable >= constant
  const Relation rel33 = 8 >= makeTerm(); // constant >= term
  const Relation rel34 = makeTerm() >= 8; // term >= constant
  const Relation rel35 = 8 >= makeExpr(); // constant >= expression
  const Relation rel36 = makeExpr() >= 8; // expression >= constant
  const Relation rel37 = makeVar() >= makeVar(); // variable >= variable
  const Relation rel38 = makeVar() >= makeTerm(); // variable >= term
  const Relation rel39 = makeTerm() >= makeVar(); // term >= variable
  const Relation rel40 = makeVar() >= makeExpr(); // variable >= expression
  const Relation rel41 = makeExpr() >= makeVar(); // expression >= variable
  const Relation rel42 = makeTerm() >= makeTerm(); // term >= term
  const Relation rel43 = makeTerm() >= makeExpr(); // term >= expression
  const Relation rel44 = makeExpr() >= makeTerm(); // expression >= term;
  const Relation rel45 = makeExpr() >= makeExpr(); // expression >= expression
}

void testQuadraticExpressions(const std::function<Problem()>& factory) {
  Problem problem = factory();
  Expression expr1 = 2 + problem.makeVar();
  const Expression expr2 = 4 + problem.makeVar();
  const Expression expr3 = expr1 * expr2; // expression * expression
  expr1 *= expr2; // expression *= expression
}

void testConstraints(const std::function<Problem()>& factory) {
  Problem problem = factory();
  Constraint constraint = problem.newConstraint(problem.makeVar() <= 42);
  problem.deleteConstraint(constraint);
}

void testLogging(const std::function<Problem()>& factory) {
  Problem problem = factory();
  const folly::test::TemporaryFile tempfile("algopt_logging_test");
  problem.setLogfile(tempfile.path().string());
  problem.solve();
  const auto logfilePath = tempfile.path().string();
  std::string loggedOutput;
  folly::readFile(logfilePath.c_str(), loggedOutput);
  std::vector<std::string_view> lines;
  folly::split('\n', loggedOutput, lines);
  // TODO: T210504389  setting the individual environment for multiobjective is
  // impacting the logging. Need to fix this back to EXPECT_GE(lines.size(), 10)
  // << loggedOutput;
  EXPECT_GE(lines.size(), 1) << loggedOutput;
  thrift::ProblemResult result = *problem.getResultPerObjective().begin();
  EXPECT_EQ(0, result.problemAttributes()->numVariables());
  EXPECT_EQ(0, result.problemAttributes()->numConstraints());
  EXPECT_EQ(0, result.problemAttributes()->numOfNonZeros());
}

void testNoVariables(const std::function<Problem()>& factory) {
  Problem problem = factory();
  problem.setObjective(problem.makeExpression(3.14));
  problem.solve();
}

void testQuadraticConstraints(const std::function<Problem()>& factory) {
  Problem problem = factory();
  auto x = problem.makeIntVar("x");
  x.setLB(0);
  auto y = problem.makeIntVar("y");
  y.setLB(0);
  y.setUB(4);

  problem.newConstraint(x * x <= 25);

  auto minimize = 5 - 2 * x - 4 * y;
  problem.setObjective(minimize);

  problem.solve();

  EXPECT_EQ(minimize.getValue(), -21);
  EXPECT_EQ(x.getValue(), 5);
  EXPECT_EQ(y.getValue(), 4);
}

void testSolveWithQuadraticObjectiveAndConstraint(
    const std::function<Problem()>& factory) {
  Problem problem = factory();
  auto x = problem.makeIntVar("x");
  x.setLB(0);
  auto y = problem.makeIntVar("y");
  y.setLB(0);

  problem.newConstraint(y >= 5);
  problem.newConstraint(x * x <= 16);

  // minimize y - x^2
  auto goal = y - x * x;
  problem.setObjective(goal);
  problem.solve();

  // expect value to be 5  + (-16) = -11
  EXPECT_NEAR(goal.getValue(), -11, 1e-8);
  EXPECT_NEAR(x.getValue(), 4, 1e-8);
  EXPECT_NEAR(y.getValue(), 5, 1e-8);
}

void testMultipleSetObjectiveCalls(const std::function<Problem()>& factory) {
  Problem problem = factory();
  auto x = problem.makeIntVar("x");

  EXPECT_EQ(problem.getObjectiveSize(), 0);

  problem.setObjective({x, 2 * x});
  EXPECT_EQ(problem.getObjectiveSize(), 2);

  // if the objective is set again, then it should clear existing objectives,
  // so the new objectiveSize should be 1
  problem.setObjective(3 * x);
  EXPECT_EQ(problem.getObjectiveSize(), 1);
}

void testIIS(const std::function<Problem()>& factory) {
  // Tests that getting the IIS does not crash nor results in memory leaks. This
  // example used to leak memory due to a bug caught by the sanitizer.
  auto problem = factory();
  auto x = problem.makeVar("x");
  x.setBounds(1, 10);
  problem.newConstraint(x <= 0);
  problem.solve();
  ASSERT_EQ(thrift::ProblemStatus::NO_SOLUTION_EXISTS, problem.getStatus());
  problem.getIIS();
}

void testMultiObjective(const std::function<Problem()>& factory) {
  auto problem = factory();
  auto x = problem.makeVar("x");
  x.setBounds(1.0, 2.0);
  auto y = problem.makeVar("y");
  y.setBounds(10.0, 20.0);
  auto z = problem.makeVar("z");
  z.setBounds(100.0, 200.0);
  problem.setObjective({x, y, -x - y - z});
  problem.solve();
  EXPECT_EQ(thrift::ProblemStatus::OPTIMAL_FOUND, problem.getStatus());
  EXPECT_NEAR(x.getValue(), 1.0, 1e-8);
  EXPECT_NEAR(y.getValue(), 10.0, 1e-8);
  EXPECT_NEAR(z.getValue(), 200.0, 1e-8);

  for (const auto i : folly::irange(3)) {
    auto result = problem.getResultPerObjective().at(i);
    EXPECT_EQ(thrift::ProblemStatus::OPTIMAL_FOUND, result.status());
    // check that problemAttributes have been set
    EXPECT_EQ(result.problemAttributes()->numVariables() != -1, true);
  }
}

void runTest(const std::function<Problem()>& factory) {
  testTolerances(factory);
  testExample(factory);
  testVariables(factory);
  testOperators(factory);
  testConstraints(factory);
  testInfeasible(factory);
  testNoConstraintSolve(factory);
  testUnboundedSolve(factory);
  testNoVariables(factory);
  testMultipleSetObjectiveCalls(factory);
  testNoSolutionFound(factory);
  testMultiObjective(factory);
}

void verifyModelFingerprint(
    const std::function<Problem()>& factory,
    bool expectedToExist) {
  auto problem = testExample(factory);
  EXPECT_EQ(
      problem.getOnlyResult()
          .problemAttributes()
          ->modelFingerprint()
          .has_value(),
      expectedToExist);
}

#ifdef REBALANCER_USE_XPRESS

TEST(EndToEndTest, Xpress) {
  runTest(ProblemFactory::makeXpressProblem);
  testLogging(ProblemFactory::makeXpressProblem);
  testQuadraticExpressions(ProblemFactory::makeXpressProblem);
  testQuadraticConstraints(ProblemFactory::makeXpressProblem);
  testQuadraticLPModel(ProblemFactory::makeXpressProblem);
  verifyModelFingerprint(ProblemFactory::makeXpressProblem, false);
}

TEST(EndToEndTest, XpressSimplified) {
  auto factory = []() {
    return ProblemFactory::makeSimplifiedProblem(
        ProblemFactory::makeXpressProblem);
  };
  runTest(factory);
  verifyModelFingerprint(factory, false);
}

TEST(EndToEndTest, SimplifierQuadraticNotSupportedXpress) {
  testQuadraticExpressions([&]() {
    return ProblemFactory::makeSimplifiedProblem(
        ProblemFactory::makeXpressProblem);
  });

  REBALANCER_EXPECT_RUNTIME_ERROR(
      testQuadraticConstraints([&]() {
        return ProblemFactory::makeSimplifiedProblem(
            ProblemFactory::makeXpressProblem);
      }),
      "getCoeffMap is unsupported for quadratic expressions");
}

#endif

#ifdef REBALANCER_USE_GUROBI

TEST(EndToEndTest, Gurobi) {
  runTest(ProblemFactory::makeGurobiProblem);
  testLogging(ProblemFactory::makeGurobiProblem);
  testQuadraticExpressions(ProblemFactory::makeGurobiProblem);
  testQuadraticConstraints(ProblemFactory::makeGurobiProblem);
  testQuadraticLPModel(ProblemFactory::makeGurobiProblem);
  testIIS(ProblemFactory::makeGurobiProblem);
  verifyModelFingerprint(ProblemFactory::makeGurobiProblem, true);
  testCompactPrinting(ProblemFactory::makeGurobiProblem);
}

TEST(EndToEndTest, GenericImpl) {
  runTest(makeGenericGurobiProblem);
  testQuadraticExpressions(makeGenericGurobiProblem);
  testQuadraticConstraints(makeGenericGurobiProblem);
  testQuadraticLPModel(makeGenericGurobiProblem);
  verifyModelFingerprint(makeGenericGurobiProblem, true);
}

TEST(EndToEndTest, GurobiSimplified) {
  auto factory = []() {
    return ProblemFactory::makeSimplifiedProblem(
        ProblemFactory::makeGurobiProblem);
  };
  runTest(factory);
  verifyModelFingerprint(factory, true);
}

// NOTE: the test below is not done with Xpress on purpose because it
// currently gives an incorrect solution. Have reached out to Xpress support.
TEST(EndToEndTest, QuadraticSolveGurobi) {
  testSolveWithQuadraticObjectiveAndConstraint(
      ProblemFactory::makeGurobiProblem);
}

TEST(EndToEndTest, SimplifierQuadraticNotSupportedGurobi) {
  testQuadraticExpressions([&]() {
    return ProblemFactory::makeSimplifiedProblem(
        ProblemFactory::makeGurobiProblem);
  });

  REBALANCER_EXPECT_RUNTIME_ERROR(
      testQuadraticConstraints([&]() {
        return ProblemFactory::makeSimplifiedProblem(
            ProblemFactory::makeGurobiProblem);
      }),
      "getCoeffMap is unsupported for quadratic expressions");
}

TEST(EndToEndTest, DecomposableGurobi) {
  runTest(makeDecomposableGurobiProblem);
  verifyModelFingerprint(makeDecomposableGurobiProblem, true);
}

#endif

#ifdef REBALANCER_USE_HIGHS

// HiGHS ignores setCallback rather than invoking it, so the callback bodies in
// testExample and testNoSolutionFound never run. Both tests still assert on the
// solve itself, so they are included.
TEST(EndToEndTest, HiGHS) {
  runTest(ProblemFactory::makeHiGHSProblem);
  testLogging(ProblemFactory::makeHiGHSProblem);
  verifyModelFingerprint(ProblemFactory::makeHiGHSProblem, false);
}

TEST(EndToEndTest, HiGHSSimplified) {
  auto factory = []() {
    return ProblemFactory::makeSimplifiedProblem(
        ProblemFactory::makeHiGHSProblem);
  };
  runTest(factory);
  verifyModelFingerprint(factory, false);
}

#endif

TEST(EndToEndTest, Fast) {
  testTolerances(ProblemFactory::makeFastProblem);
  testVariables(ProblemFactory::makeFastProblem);
  testConstraints(ProblemFactory::makeFastProblem);
  testMultipleSetObjectiveCalls(ProblemFactory::makeFastProblem);
}

} // namespace facebook::algopt::lp::tests
