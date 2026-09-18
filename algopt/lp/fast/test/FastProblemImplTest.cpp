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
#include "algopt/lp/fast/FastConstraintImpl.h"
#include "algopt/lp/fast/FastExpressionImpl.h"
#include "algopt/lp/fast/FastProblemImpl.h"
#include "algopt/lp/fast/FastRelationImpl.h"
#include "algopt/lp/fast/FastVariableImpl.h"
#include "algopt/lp/generic/Operators.h"
#include "algopt/lp/generic/Problem.h"

#include <folly/container/irange.h>
#include <gtest/gtest.h>

#include <limits>
#include <memory>

namespace facebook::algopt::lp::tests {

// --- FastProblemImpl tests ---

TEST(FastProblemImplTest, MakeVar) {
  FastProblemImpl problem;
  auto var = problem.makeVar("x");
  EXPECT_EQ(problem.getVariableCount(), 1);
  const auto& inner = problem.getVariable(0);
  EXPECT_EQ(inner.name, "x");
  EXPECT_EQ(inner.type, VariableImpl::Type::CONTINUOUS);
  EXPECT_EQ(inner.lb, 0);
  EXPECT_EQ(inner.ub, std::numeric_limits<double>::infinity());
}

TEST(FastProblemImplTest, MakeIntVar) {
  FastProblemImpl problem;
  auto var = problem.makeIntVar("y");
  EXPECT_EQ(problem.getVariableCount(), 1);
  const auto& inner = problem.getVariable(0);
  EXPECT_EQ(inner.type, VariableImpl::Type::INTEGER);
}

TEST(FastProblemImplTest, MakeBoolVar) {
  FastProblemImpl problem;
  auto var = problem.makeBoolVar("b");
  EXPECT_EQ(problem.getVariableCount(), 1);
  const auto& inner = problem.getVariable(0);
  EXPECT_EQ(inner.type, VariableImpl::Type::BINARY);
  EXPECT_EQ(inner.lb, 0);
  EXPECT_EQ(inner.ub, 1);
}

TEST(FastProblemImplTest, StoresStartValue) {
  FastProblemImpl problem;
  auto var = problem.makeBoolVar("b");

  problem.addStartValue(var, 1.0);

  ASSERT_TRUE(problem.getVariable(0).initialValue.has_value());
  EXPECT_EQ(*problem.getVariable(0).initialValue, 1.0);
}

TEST(FastProblemImplTest, MakeSemiContVar) {
  FastProblemImpl problem;
  auto var = problem.makeSemiContVar("sc", 5.0);
  EXPECT_EQ(problem.getVariableCount(), 1);
  const auto& inner = problem.getVariable(0);
  EXPECT_EQ(inner.type, VariableImpl::Type::SEMI_CONTINUOUS);
  ASSERT_TRUE(inner.threshold.has_value());
  EXPECT_EQ(*inner.threshold, 5.0);
}

TEST(FastProblemImplTest, MakeSemiIntVar) {
  FastProblemImpl problem;
  auto var = problem.makeSemiIntVar("si", 3.0);
  EXPECT_EQ(problem.getVariableCount(), 1);
  const auto& inner = problem.getVariable(0);
  EXPECT_EQ(inner.type, VariableImpl::Type::SEMI_INTEGER);
  ASSERT_TRUE(inner.threshold.has_value());
  EXPECT_EQ(*inner.threshold, 3.0);
}

TEST(FastProblemImplTest, MakeExpression) {
  const FastProblemImpl problem;
  auto expr = problem.makeExpression(42.0);
  auto fastExpr = std::dynamic_pointer_cast<FastExpressionImpl>(expr);
  ASSERT_NE(fastExpr, nullptr);
  EXPECT_EQ(fastExpr->getConstant(), 42.0);
  EXPECT_TRUE(fastExpr->getTerms().empty());
}

TEST(FastProblemImplTest, NewConstraint) {
  FastProblemImpl problem;
  auto var = problem.makeVar("x");
  auto expr = var->makeExpression(2.0);
  auto relation = expr->makeLessEqualZeroRelation();
  auto constraint = problem.newConstraint(relation, "ctr1");
  EXPECT_EQ(problem.getConstraintCount(), 1);
  const auto& inner = problem.getConstraint(0);
  ASSERT_EQ(inner.terms.size(), 1);
  EXPECT_EQ(inner.terms[0].coefficient, 2.0);
  EXPECT_EQ(inner.terms[0].variableId, 0);
  EXPECT_EQ(inner.rhs, 0);
  EXPECT_EQ(inner.sense, Relation::LE_ZERO);
  EXPECT_EQ(inner.name, "ctr1");
  EXPECT_FALSE(inner.deleted);
}

TEST(FastProblemImplTest, DeleteConstraint) {
  FastProblemImpl problem;
  auto var = problem.makeVar("x");
  auto expr = var->makeExpression(1.0);
  auto relation = expr->makeEqualZeroRelation();
  auto constraint = problem.newConstraint(relation, "ctr");
  EXPECT_FALSE(problem.getConstraint(0).deleted);
  problem.deleteConstraint(constraint);
  EXPECT_TRUE(problem.getConstraint(0).deleted);
}

TEST(FastProblemImplTest, ObjectiveLifecycle) {
  FastProblemImpl problem;
  EXPECT_EQ(problem.getObjectiveSize(), 0);

  auto expr1 = problem.makeExpression(1.0);
  problem.addObjective(expr1);
  EXPECT_EQ(problem.getObjectiveSize(), 1);

  auto expr2 = problem.makeExpression(2.0);
  problem.addObjective(expr2);
  EXPECT_EQ(problem.getObjectiveSize(), 2);

  auto retrieved = problem.getObjectiveAt(0);
  ASSERT_NE(retrieved, nullptr);

  problem.clearObjectives();
  EXPECT_EQ(problem.getObjectiveSize(), 0);
}

TEST(FastProblemImplTest, SolveThrows) {
  // Test through the Problem wrapper which calls solveForObjectiveAt internally
  auto problem = ProblemFactory::makeFastProblem();
  auto x = problem.makeVar("x");
  problem.setObjective(x);
  EXPECT_THROW(problem.solve(), std::runtime_error);
}

TEST(FastProblemImplTest, GetProblemAttributes) {
  FastProblemImpl problem;
  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");

  auto expr1 = std::make_shared<FastExpressionImpl>(0);
  // Build: 2*x + 3*y <= 0
  auto xExpr = x->makeExpression(2.0);
  auto yExpr = y->makeExpression(3.0);
  xExpr->add(yExpr);
  auto rel = xExpr->makeLessEqualZeroRelation();
  problem.newConstraint(rel, "c1");

  // Build: x == 0
  auto xExpr2 = x->makeExpression(1.0);
  auto rel2 = xExpr2->makeEqualZeroRelation();
  auto c2 = problem.newConstraint(rel2, "c2");

  auto attrs = problem.getProblemAttributes();
  EXPECT_EQ(attrs.numVariables(), 2);
  EXPECT_EQ(attrs.numConstraints(), 2);
  EXPECT_EQ(attrs.numOfNonZeros(), 3);

  // Delete one constraint
  problem.deleteConstraint(c2);
  attrs = problem.getProblemAttributes();
  EXPECT_EQ(attrs.numConstraints(), 1);
  EXPECT_EQ(attrs.numOfNonZeros(), 2);
}

TEST(FastProblemImplTest, ParameterGetSet) {
  FastProblemImpl problem;
  problem.setParameter("foo", 42.0);
  EXPECT_EQ(problem.getParameter("foo"), 42.0);
  EXPECT_THROW(problem.getParameter("bar"), std::runtime_error);
}

TEST(FastProblemImplTest, TolerancesGetSet) {
  FastProblemImpl problem;
  auto tol = Tolerances{.constraint = 1e-6, .integer = 1e-7};
  problem.setTolerances(tol);
  auto got = problem.getTolerances();
  EXPECT_EQ(got.constraint, 1e-6);
  EXPECT_EQ(got.integer, 1e-7);
}

TEST(FastProblemImplTest, NoOpMethods) {
  FastProblemImpl problem;
  // These should not crash
  problem.setLogfile("test.log");
  problem.setDebugPath("/tmp");
  problem.disableLogs();
  problem.print({});
  problem.setCallback([](const ProblemCallbackData&) {
    return ProblemCallbackAction::CONTINUE;
  });
}

TEST(FastProblemImplTest, SaveToFileThrows) {
  FastProblemImpl problem;
  EXPECT_THROW(problem.saveToFile("test.mps"), std::runtime_error);
}

TEST(FastProblemImplTest, SaveToFileWithObjectiveAtThrows) {
  FastProblemImpl problem;
  EXPECT_THROW(
      problem.saveToFileWithObjectiveAt(0, "test.mps"), std::runtime_error);
}

TEST(FastProblemImplTest, GetIISReturnsNullopt) {
  FastProblemImpl problem;
  EXPECT_FALSE(problem.getIIS().has_value());
}

TEST(FastProblemImplTest, MultipleVariables) {
  FastProblemImpl problem;
  problem.makeVar("a");
  problem.makeIntVar("b");
  problem.makeBoolVar("c");
  EXPECT_EQ(problem.getVariableCount(), 3);
  EXPECT_EQ(problem.getVariable(0).name, "a");
  EXPECT_EQ(problem.getVariable(1).name, "b");
  EXPECT_EQ(problem.getVariable(2).name, "c");
}

TEST(FastProblemImplTest, MultipleConstraints) {
  FastProblemImpl problem;
  auto x = problem.makeVar("x");
  for (const auto i : folly::irange(5)) {
    auto expr = x->makeExpression(1.0);
    auto rel = expr->makeLessEqualZeroRelation();
    problem.newConstraint(rel, "c" + std::to_string(i));
  }
  EXPECT_EQ(problem.getConstraintCount(), 5);
}

// --- FastVariableImpl tests ---

TEST(FastVariableImplTest, SetBounds) {
  FastProblemImpl problem;
  auto var = problem.makeVar("x");
  var->setLB(-10);
  var->setUB(100);
  const auto& inner = problem.getVariable(0);
  EXPECT_EQ(inner.lb, -10);
  EXPECT_EQ(inner.ub, 100);
}

TEST(FastVariableImplTest, SetThreshold) {
  FastProblemImpl problem;
  auto var = problem.makeSemiContVar("x", 5.0);
  EXPECT_TRUE(var->hasThreshold());
  var->setThreshold(10.0);
  EXPECT_EQ(*problem.getVariable(0).threshold, 10.0);
}

TEST(FastVariableImplTest, GetValueThrowsWhenNotSet) {
  FastProblemImpl problem;
  auto var = problem.makeVar("x");
  EXPECT_THROW(var->getValue(), std::runtime_error);
}

TEST(FastVariableImplTest, GetVariableId) {
  FastProblemImpl problem;
  auto var0 = problem.makeVar("a");
  auto var1 = problem.makeVar("b");
  auto fast0 = std::dynamic_pointer_cast<FastVariableImpl>(var0);
  auto fast1 = std::dynamic_pointer_cast<FastVariableImpl>(var1);
  EXPECT_EQ(fast0->getVariableId(), 0);
  EXPECT_EQ(fast1->getVariableId(), 1);
}

TEST(FastVariableImplTest, MakeExpression) {
  FastProblemImpl problem;
  auto var = problem.makeVar("x");
  auto expr = var->makeExpression(3.5);
  auto fastExpr = std::dynamic_pointer_cast<FastExpressionImpl>(expr);
  ASSERT_NE(fastExpr, nullptr);
  ASSERT_EQ(fastExpr->getTerms().size(), 1);
  EXPECT_EQ(fastExpr->getTerms()[0].coefficient, 3.5);
  EXPECT_EQ(fastExpr->getTerms()[0].variableId, 0);
  EXPECT_EQ(fastExpr->getConstant(), 0);
}

// --- FastExpressionImpl tests ---

TEST(FastExpressionImplTest, ConstantConstructor) {
  const FastExpressionImpl expr(7.0);
  EXPECT_TRUE(expr.getTerms().empty());
  EXPECT_EQ(expr.getConstant(), 7.0);
}

TEST(FastExpressionImplTest, SingleTermConstructor) {
  const FastExpressionImpl expr(2.5, 42);
  ASSERT_EQ(expr.getTerms().size(), 1);
  EXPECT_EQ(expr.getTerms()[0].coefficient, 2.5);
  EXPECT_EQ(expr.getTerms()[0].variableId, 42);
  EXPECT_EQ(expr.getConstant(), 0);
}

TEST(FastExpressionImplTest, Clone) {
  FastExpressionImpl original(3.0, 1);
  original.add(5.0);
  auto cloned = original.clone();
  auto fastCloned = std::dynamic_pointer_cast<FastExpressionImpl>(cloned);
  ASSERT_NE(fastCloned, nullptr);
  EXPECT_EQ(fastCloned->getConstant(), 5.0);
  ASSERT_EQ(fastCloned->getTerms().size(), 1);
  // Modify clone, original should be unaffected
  fastCloned->add(10.0);
  EXPECT_EQ(original.getConstant(), 5.0);
  EXPECT_EQ(fastCloned->getConstant(), 15.0);
}

TEST(FastExpressionImplTest, AddConstant) {
  FastExpressionImpl expr(1.0);
  expr.add(2.5);
  EXPECT_EQ(expr.getConstant(), 3.5);
}

TEST(FastExpressionImplTest, AddExpression) {
  auto expr1 = std::make_shared<FastExpressionImpl>(2.0, 0);
  expr1->add(1.0);
  auto expr2 = std::make_shared<FastExpressionImpl>(3.0, 1);
  expr2->add(4.0);
  expr1->add(expr2);
  EXPECT_EQ(expr1->getTerms().size(), 2);
  EXPECT_EQ(expr1->getConstant(), 5.0);
}

TEST(FastExpressionImplTest, MultiplyConstant) {
  FastExpressionImpl expr(2.0, 0);
  expr.add(3.0);
  expr.multiply(4.0);
  EXPECT_EQ(expr.getTerms()[0].coefficient, 8.0);
  EXPECT_EQ(expr.getConstant(), 12.0);
}

TEST(FastExpressionImplTest, MultiplyConstantExpression) {
  auto expr1 = std::make_shared<FastExpressionImpl>(2.0, 0);
  auto constExpr = std::make_shared<FastExpressionImpl>(3.0);
  expr1->multiply(constExpr);
  EXPECT_EQ(expr1->getTerms()[0].coefficient, 6.0);
}

TEST(FastExpressionImplTest, MultiplyTwoNonConstantThrows) {
  auto expr1 = std::make_shared<FastExpressionImpl>(2.0, 0);
  auto expr2 = std::make_shared<FastExpressionImpl>(3.0, 1);
  EXPECT_THROW(expr1->multiply(expr2), std::runtime_error);
}

TEST(FastExpressionImplTest, MakeEqualZeroRelation) {
  FastExpressionImpl expr(2.0, 0);
  expr.add(5.0);
  auto rel = expr.makeEqualZeroRelation();
  auto fastRel = std::dynamic_pointer_cast<FastRelationImpl>(rel);
  ASSERT_NE(fastRel, nullptr);
  EXPECT_EQ(fastRel->getSense(), Relation::EQ_ZERO);
  ASSERT_EQ(fastRel->getTerms().size(), 1);
  EXPECT_EQ(fastRel->getConstant(), 5.0);
}

TEST(FastExpressionImplTest, MakeLessEqualZeroRelation) {
  const FastExpressionImpl expr(1.0, 0);
  auto rel = expr.makeLessEqualZeroRelation();
  auto fastRel = std::dynamic_pointer_cast<FastRelationImpl>(rel);
  EXPECT_EQ(fastRel->getSense(), Relation::LE_ZERO);
}

TEST(FastExpressionImplTest, MakeGreaterEqualZeroRelation) {
  const FastExpressionImpl expr(1.0, 0);
  auto rel = expr.makeGreaterEqualZeroRelation();
  auto fastRel = std::dynamic_pointer_cast<FastRelationImpl>(rel);
  EXPECT_EQ(fastRel->getSense(), Relation::GE_ZERO);
}

TEST(FastExpressionImplTest, GetValueThrows) {
  const FastExpressionImpl expr(1.0);
  EXPECT_THROW(expr.getValue(), std::runtime_error);
}

TEST(FastExpressionImplTest, ComputeValueThrows) {
  const FastExpressionImpl expr(1.0);
  EXPECT_THROW(expr.computeValue(), std::runtime_error);
}

TEST(FastExpressionImplTest, PrintThrows) {
  const FastExpressionImpl expr(1.0);
  EXPECT_THROW(expr.print(), std::runtime_error);
}

// --- FastRelationImpl tests ---

TEST(FastRelationImplTest, Constructor) {
  const std::vector<Term> terms = {
      {.coefficient = 2.0, .variableId = 0},
      {.coefficient = 3.0, .variableId = 1}};
  const FastRelationImpl rel(terms, 5.0, Relation::LE_ZERO);
  EXPECT_EQ(rel.getTerms().size(), 2);
  EXPECT_EQ(rel.getConstant(), 5.0);
  EXPECT_EQ(rel.getSense(), Relation::LE_ZERO);
}

// --- FastConstraintImpl tests ---

TEST(FastConstraintImplTest, Constructor) {
  const FastConstraintImpl constraint(42);
  EXPECT_EQ(constraint.getConstraintId(), 42);
}

// --- Integration tests using Problem wrapper ---

TEST(FastProblemImplTest, IntegrationBuildModel) {
  auto problem = ProblemFactory::makeFastProblem();
  auto x = problem.makeVar("x");
  x.setLB(0);
  x.setUB(10);
  auto y = problem.makeIntVar("y");
  y.setLB(0);
  y.setUB(20);

  problem.newConstraint(2 * x + 3 * y <= 10, "c1");
  problem.newConstraint(x + y >= 1, "c2");

  auto attrs = problem.getProblemAttributes();
  EXPECT_EQ(attrs.numVariables(), 2);
  EXPECT_EQ(attrs.numConstraints(), 2);
  EXPECT_EQ(attrs.numOfNonZeros(), 4);
}

TEST(FastProblemImplTest, IntegrationDeleteConstraint) {
  auto problem = ProblemFactory::makeFastProblem();
  auto x = problem.makeVar("x");
  auto c1 = problem.newConstraint(x <= 10, "c1");
  problem.newConstraint(x >= 0, "c2");

  auto attrs = problem.getProblemAttributes();
  EXPECT_EQ(attrs.numConstraints(), 2);

  problem.deleteConstraint(c1);
  attrs = problem.getProblemAttributes();
  EXPECT_EQ(attrs.numConstraints(), 1);
}

TEST(FastProblemImplTest, IntegrationObjectives) {
  auto problem = ProblemFactory::makeFastProblem();
  auto x = problem.makeIntVar("x");

  EXPECT_EQ(problem.getObjectiveSize(), 0);

  problem.setObjective({x, 2 * x});
  EXPECT_EQ(problem.getObjectiveSize(), 2);

  problem.setObjective(3 * x);
  EXPECT_EQ(problem.getObjectiveSize(), 1);
}

TEST(FastProblemImplTest, IntegrationSolveThrows) {
  auto problem = ProblemFactory::makeFastProblem();
  auto x = problem.makeVar("x");
  problem.setObjective(x);
  EXPECT_THROW(problem.solve(), std::runtime_error);
}

TEST(FastProblemImplTest, IntegrationConstantConstraint) {
  // This exercises the Problem::newConstraint path with a constant relation
  auto problem = ProblemFactory::makeFastProblem();
  problem.newConstraint(Expression(5.0) <= 10.0, "const_ctr");
  auto attrs = problem.getProblemAttributes();
  EXPECT_EQ(attrs.numConstraints(), 1);
}

} // namespace facebook::algopt::lp::tests
