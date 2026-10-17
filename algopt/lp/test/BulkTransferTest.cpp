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

#include "algopt/lp/environment/Environment.h" // NOLINT

#ifdef REBALANCER_USE_GUROBI

#include "algopt/lp/detail/gurobi/GurobiProblem.h"
#include "algopt/lp/factory/ProblemFactory.h"
#include "algopt/lp/fast/FastProblemImpl.h"
#include "algopt/lp/generic/Operators.h"
#include "algopt/lp/generic/Problem.h"

#include <gtest/gtest.h>

namespace facebook::algopt::lp::tests {

// Builds a small LP into FastProblemImpl:
//   min x + 2y
//   s.t. x + y <= 10, x >= 2
//   x continuous [0,100], y integer [0,100]
struct FastModel {
  Problem fastLp{ProblemFactory::makeFastProblem()};
  Variable xVar{fastLp.makeVar("x")};
  Variable yVar{fastLp.makeIntVar("y")};
  FastProblemImpl* impl{nullptr};

  FastModel() {
    xVar.setLB(0);
    xVar.setUB(100);
    yVar.setLB(0);
    yVar.setUB(100);
    fastLp.newConstraint(xVar + yVar <= 10, "capacity");
    fastLp.newConstraint(xVar >= 2, "min_x");
    fastLp.setObjective(xVar + 2 * yVar);
    impl =
        dynamic_cast<FastProblemImpl*>(&const_cast<ProblemImpl&>(fastLp.get()));
  }
};

TEST(BulkTransferTest, ModelFingerprintIndependentOfInsertionOrder) {
  // loadFromFastProblem sorts by name, so the resulting Gurobi model -- and its
  // fingerprint -- must be identical regardless of the order variables and
  // constraints were added to the FastProblemImpl (which under a parallel build
  // is non-deterministic). We do NOT require matching a direct GurobiProblem
  // build; only that the bulk-built order is stable across insertion orders.
  auto fingerprintFor = [](const Problem& fastLp) {
    auto* impl =
        dynamic_cast<FastProblemImpl*>(&const_cast<ProblemImpl&>(fastLp.get()));
    auto gurobiLp = ProblemFactory::makeGurobiProblem();
    auto* gurobiImpl = dynamic_cast<detail::GurobiProblem*>(
        &const_cast<ProblemImpl&>(gurobiLp.get()));
    gurobiImpl->loadFromFastProblem(*impl);
    return gurobiLp.getModelFingerprint();
  };

  // Insertion order x, y.
  const FastModel forward;
  const auto forwardFingerprint = fingerprintFor(forward.fastLp);

  // Same model, variables inserted in the reverse order y, x.
  auto reversed = ProblemFactory::makeFastProblem();
  auto y = reversed.makeIntVar("y");
  y.setLB(0);
  y.setUB(100);
  auto x = reversed.makeVar("x");
  x.setLB(0);
  x.setUB(100);
  reversed.newConstraint(x + y <= 10, "capacity");
  reversed.newConstraint(x >= 2, "min_x");
  reversed.setObjective(x + 2 * y);
  const auto reversedFingerprint = fingerprintFor(reversed);

  ASSERT_TRUE(forwardFingerprint.has_value());
  ASSERT_TRUE(reversedFingerprint.has_value());
  EXPECT_EQ(*forwardFingerprint, *reversedFingerprint);
}

TEST(BulkTransferTest, DeletedConstraintsAreSkipped) {
  auto fastLp = ProblemFactory::makeFastProblem();
  auto xVar = fastLp.makeVar("x");
  xVar.setLB(0);
  xVar.setUB(100);
  fastLp.newConstraint(xVar <= 50, "kept");
  auto deleted = fastLp.newConstraint(xVar <= 10, "deleted");
  fastLp.deleteConstraint(deleted);
  fastLp.setObjective(-1.0 * xVar);

  auto* impl =
      dynamic_cast<FastProblemImpl*>(&const_cast<ProblemImpl&>(fastLp.get()));

  auto gurobiLp = ProblemFactory::makeGurobiProblem();
  auto* gurobiImpl = dynamic_cast<detail::GurobiProblem*>(
      &const_cast<ProblemImpl&>(gurobiLp.get()));
  gurobiImpl->loadFromFastProblem(*impl);
  gurobiLp.solve();
  const auto values = gurobiImpl->getSolvedVariableValues();
  EXPECT_NEAR(values.at("x"), 50.0, 1e-6);
}

TEST(BulkTransferTest, PermutedVariableOrder) {
  // Create a model with variables added in reverse order (y before x) to verify
  // that buildSortedVarIds produces a deterministic sorted order regardless of
  // insertion order. The variables are added as y then x, but the solution
  // should still be x=2.0, y=0.0.
  auto fastLp = ProblemFactory::makeFastProblem();
  auto yVar = fastLp.makeIntVar("y");
  yVar.setLB(0);
  yVar.setUB(100);
  auto xVar = fastLp.makeVar("x");
  xVar.setLB(0);
  xVar.setUB(100);
  fastLp.newConstraint(xVar + yVar <= 10, "capacity");
  fastLp.newConstraint(xVar >= 2, "min_x");
  fastLp.setObjective(xVar + 2 * yVar);

  auto* impl =
      dynamic_cast<FastProblemImpl*>(&const_cast<ProblemImpl&>(fastLp.get()));
  ASSERT_NE(impl, nullptr);

  auto gurobiLp = ProblemFactory::makeGurobiProblem();
  auto* gurobiImpl = dynamic_cast<detail::GurobiProblem*>(
      &const_cast<ProblemImpl&>(gurobiLp.get()));
  gurobiImpl->loadFromFastProblem(*impl);
  gurobiLp.solve();

  EXPECT_EQ(gurobiLp.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);

  const auto values = gurobiImpl->getSolvedVariableValues();
  EXPECT_NEAR(values.at("x"), 2.0, 1e-6);
  EXPECT_NEAR(values.at("y"), 0.0, 1e-6);
}

TEST(BulkTransferTest, MultiObjective) {
  auto fastLp = ProblemFactory::makeFastProblem();
  auto x = fastLp.makeVar("x");
  auto y = fastLp.makeVar("y");
  x.setLB(0);
  x.setUB(10);
  y.setLB(0);
  y.setUB(10);
  fastLp.newConstraint(x + y <= 8, "capacity");
  fastLp.setObjective({x + y, x - y});

  auto* impl =
      dynamic_cast<FastProblemImpl*>(&const_cast<ProblemImpl&>(fastLp.get()));

  auto gurobiLp = ProblemFactory::makeGurobiProblem();
  auto* gurobiImpl = dynamic_cast<detail::GurobiProblem*>(
      &const_cast<ProblemImpl&>(gurobiLp.get()));
  gurobiImpl->loadFromFastProblem(*impl);
  gurobiLp.solve();

  EXPECT_EQ(gurobiLp.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);

  const auto values = gurobiImpl->getSolvedVariableValues();
  EXPECT_NEAR(values.at("x"), 0.0, 1e-6);
  EXPECT_NEAR(values.at("y"), 0.0, 1e-6);
}

TEST(BulkTransferTest, InfeasibleModelSolveStatus) {
  auto fastLp = ProblemFactory::makeFastProblem();
  auto x = fastLp.makeVar("x");
  x.setLB(5);
  x.setUB(10);
  fastLp.newConstraint(x <= 3, "impossible");
  fastLp.setObjective(x);

  auto* impl =
      dynamic_cast<FastProblemImpl*>(&const_cast<ProblemImpl&>(fastLp.get()));

  auto gurobiLp = ProblemFactory::makeGurobiProblem();
  auto* gurobiImpl = dynamic_cast<detail::GurobiProblem*>(
      &const_cast<ProblemImpl&>(gurobiLp.get()));
  gurobiImpl->loadFromFastProblem(*impl);
  gurobiLp.solve();

  EXPECT_EQ(gurobiLp.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);
}

TEST(BulkTransferTest, BinaryAndBoolVariables) {
  auto fastLp = ProblemFactory::makeFastProblem();
  auto b = fastLp.makeBoolVar("b");
  auto x = fastLp.makeVar("x");
  x.setLB(0);
  x.setUB(100);
  fastLp.newConstraint(x - 50.0 * b <= 0, "link");
  fastLp.setObjective(-1.0 * x);

  auto* impl =
      dynamic_cast<FastProblemImpl*>(&const_cast<ProblemImpl&>(fastLp.get()));

  auto gurobiLp = ProblemFactory::makeGurobiProblem();
  auto* gurobiImpl = dynamic_cast<detail::GurobiProblem*>(
      &const_cast<ProblemImpl&>(gurobiLp.get()));
  gurobiImpl->loadFromFastProblem(*impl);
  gurobiLp.solve();

  EXPECT_EQ(gurobiLp.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);

  const auto values = gurobiImpl->getSolvedVariableValues();
  EXPECT_NEAR(values.at("b"), 1.0, 1e-6);
  EXPECT_NEAR(values.at("x"), 50.0, 1e-6);
}

TEST(BulkTransferTest, NormalLpShadowPricesPreservesRowNamesAndSigns) {
  auto fastLp = ProblemFactory::makeFastProblem();
  auto y = fastLp.makeVar("z_y");
  auto x = fastLp.makeVar("a_x");
  y.setLB(0);
  y.setUB(100);
  x.setLB(0);
  x.setUB(100);
  fastLp.newConstraint(x >= 1.5, "z_demand");
  fastLp.newConstraint(x <= 10, "a_capacity");
  fastLp.setObjective(2 * x + 5 * y);
  const auto& fast = static_cast<const FastProblemImpl&>(fastLp.get());

  auto problem = ProblemFactory::makeGurobiProblem();
  auto& gurobi = static_cast<detail::GurobiProblem&>(
      const_cast<ProblemImpl&>(problem.get()));
  gurobi.loadFromFastProblem(fast);
  gurobi.enableLpShadowPrices();
  problem.solve();
  const auto values = gurobi.takeLpShadowPrices();
  ASSERT_TRUE(values);
  EXPECT_EQ(
      std::vector<std::string>({"a_capacity", "z_demand"}),
      values->constraintNames);
  ASSERT_EQ(1, values->objectives.size());
  EXPECT_NEAR(3, values->objectives.front().objectiveValue, 1e-6);
  EXPECT_EQ(
      std::vector<double>({0, 2}), values->objectives.front().shadowPrices);
}

TEST(BulkTransferTest, BarrierShadowPricesWithoutCrossover) {
  auto problem = ProblemFactory::makeGurobiProblem();
  auto x = problem.makeVar("x");
  x.setLB(0);
  x.setUB(100);
  problem.newConstraint(x >= 1.5, "demand");
  problem.setObjective(2 * x);
  problem.setParameter("GRB_Method", 2);
  problem.setParameter("GRB_Crossover", 0);
  problem.setParameter("GRB_Presolve", 0);
  auto& gurobi = static_cast<detail::GurobiProblem&>(
      const_cast<ProblemImpl&>(problem.get()));
  gurobi.enableLpShadowPrices();

  problem.solve();

  const auto values = gurobi.takeLpShadowPrices();
  ASSERT_TRUE(values);
  EXPECT_EQ(std::vector<std::string>({"demand"}), values->constraintNames);
  ASSERT_EQ(1, values->objectives.size());
  EXPECT_NEAR(3, values->objectives.front().objectiveValue, 1e-6);
  ASSERT_EQ(1, values->objectives.front().shadowPrices.size());
  EXPECT_NEAR(2, values->objectives.front().shadowPrices.front(), 1e-6);
}

TEST(BulkTransferTest, NormalLpShadowPricesUnavailableForInfeasibleModel) {
  auto fastLp = ProblemFactory::makeFastProblem();
  auto x = fastLp.makeVar("x");
  x.setLB(5);
  x.setUB(10);
  fastLp.newConstraint(x <= 3, "impossible");
  fastLp.setObjective(x);
  const auto& fast = static_cast<const FastProblemImpl&>(fastLp.get());
  auto gurobiProblem = ProblemFactory::makeGurobiProblem();
  auto& gurobi = static_cast<detail::GurobiProblem&>(
      const_cast<ProblemImpl&>(gurobiProblem.get()));
  gurobi.loadFromFastProblem(fast);
  gurobi.enableLpShadowPrices();
  gurobiProblem.solve();
  EXPECT_EQ(
      thrift::ProblemStatus::NO_SOLUTION_EXISTS, gurobiProblem.getStatus());
  EXPECT_FALSE(gurobi.takeLpShadowPrices());
}

} // namespace facebook::algopt::lp::tests

#endif // REBALANCER_USE_GUROBI
