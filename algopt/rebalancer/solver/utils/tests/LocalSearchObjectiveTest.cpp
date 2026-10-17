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

#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/Orchestrator.h"
#include "algopt/rebalancer/solver/tests/IdConverterTestUtils.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/LocalSearchObjective.h"

#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

TEST(LocalSearchObjectiveTest, EvaluateStopsAfterPenaltyWorsens) {
  const entities::Universe universe;
  const Assignment initialAssignment(
      {{container(1), {object(1)}}, {container(2), {}}});

  GlobalObjective::Builder globalBuilder;
  GlobalObjective::Builder penaltyBuilder;
  for (int pos = 0; pos < 3; ++pos) {
    globalBuilder.setObjective(pos, const_expr(0, universe));
    penaltyBuilder.setObjective(pos, const_expr(0, universe));
  }
  penaltyBuilder.setObjective(
      1, variable(object(1), container(2), universe, initialAssignment));

  const auto globalObjective = globalBuilder.build(universe);
  const auto penaltyObjective = penaltyBuilder.build(universe);
  const LocalSearchObjective objective(
      globalObjective, penaltyObjective, 0, globalObjective.size(), universe);
  std::vector<Expression*> roots;
  for (const auto& expression : globalObjective) {
    roots.push_back(expression.get());
  }
  for (const auto& expression : penaltyObjective) {
    roots.push_back(expression.get());
  }
  Orchestrator orchestrator;
  orchestrator.init(roots, AffectedByChangeDecisionData(1, 2));

  Context context;
  context.changes() = ChangeSet(
      {Change(object(1), container(1), -1),
       Change(object(1), container(2), 1)});
  const auto value =
      objective.evaluate(context, orchestrator, /*evaluateAllTuples=*/false);

  EXPECT_EQ("(0 (0), 0 (1), __)", value.toString());
}

TEST(LocalSearchObjectiveTest, RejectsMismatchedObjectiveSizes) {
  const entities::Universe universe;
  GlobalObjective::Builder globalBuilder;
  globalBuilder.setObjective(0, const_expr(0, universe));
  GlobalObjective::Builder penaltyBuilder;
  penaltyBuilder.setObjective(0, const_expr(0, universe));
  penaltyBuilder.setObjective(1, const_expr(0, universe));

  EXPECT_THROW(
      LocalSearchObjective(
          globalBuilder.build(universe),
          penaltyBuilder.build(universe),
          0,
          1,
          universe),
      std::runtime_error);
}

} // namespace facebook::rebalancer::packer::tests
