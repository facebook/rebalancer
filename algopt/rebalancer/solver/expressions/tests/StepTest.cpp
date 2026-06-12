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

#include "algopt/lp/generic/Operators.h"
#include "algopt/rebalancer/solver/expressions/LpEvaluator.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/tests/ExprProblemCreation.h"
#include "algopt/rebalancer/tests/SolverTestUtils.h"

#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class StepTest : public ExpressionTestsBase {};

TEST_F(StepTest, Lp) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0"}}});
  const auto universe = buildUniverse();

  const Assignment assignment(universe->getContainers().getInitialAssignment());
  auto var = 0.6 * variable(object(0), container(0), universe, assignment);
  auto stepExpr = step(var, universe);

  auto p_ptr = createTestProblem(
      universe, {const_expr(0, universe)}, const_expr(0, universe));
  auto& p = *p_ptr;

  const PackerSet<entities::ContainerId> dynamicContainers = {container(0)};
  LpContext context(
      dynamicContainers,
      p.getDynamicEquivalentSets(dynamicContainers),
      p.getOrchestrator().getDynamicChildren(dynamicContainers));

  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  p.lp_store.reset(
      facebook::algopt::getAvailableMIPSolver().value(), false, context);
  const auto nodesPriority = folly::F14FastMap<Expression*, PriorityInfo>();
  const LpEvaluator evaluator(context, p, nodesPriority);
  p.lp_store.setObjective({-1 * stepExpr->lp(evaluator, false, {})});

  p.lp_store.solve();

  EXPECT_NEAR(
      -1.0, *p.lp_store.getLpProblem().getOnlyResult().bestObjective(), 1e-8);
}

} // namespace facebook::rebalancer::packer::tests
