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

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/summary/GlobalLabeledObjectives.h"
#include "algopt/rebalancer/solver/summary/LabeledObjectives.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/Context.h"
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"

#include "gmock/gmock.h"
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <string>
#include <vector>

using namespace facebook::rebalancer;

TEST(GlobalLabeledObjectivesTest, Preconditions) {
  const entities::Universe universe{};
  auto c1 = const_expr(1, universe);
  auto c2 = const_expr(2, universe);

  EXPECT_EQ(0, GlobalLabeledObjectives().size());

  REBALANCER_EXPECT_RUNTIME_ERROR(
      [&]() {
        GlobalLabeledObjectives::Builder{}
            .addSingle(-1, "two", c2)
            .build(GlobalObjective(universe));
      }(),
      "pos: -1 must be >= 0");

  REBALANCER_EXPECT_RUNTIME_ERROR(
      [&]() {
        GlobalLabeledObjectives::Builder{}
            .addSingle(1, "two", c2)
            .addSingle(0, "one", c1)
            .build(GlobalObjective(universe));
      }(),
      "number of objectives 0 != number of labeled objectives 2");
}

TEST(GlobalLabeledObjectivesTest, Basic) {
  const entities::Universe universe{};
  GlobalObjective::Builder objective_builder;
  GlobalLabeledObjectives::Builder labeled_objective_builder;

  auto c1 = const_expr(2, universe);
  auto c2 = const_expr(4, universe);
  objective_builder.addToObjective(0, c1, universe)
      .addToObjective(0, 4 * c2, universe);
  labeled_objective_builder.setRoot(0, c1)
      .addSingle(0, "two", c1)
      .addSingle(0, "four", c2, 4);

  auto C1 = const_expr(3, universe);
  auto C2 = const_expr(6, universe);
  objective_builder.addToObjective(1, C2, universe)
      .addToObjective(1, 6 * C2, universe);
  labeled_objective_builder.addSingle(1, "three", C1)
      .addSingle(1, "six", C2, 6);

  auto global_objective = objective_builder.build(universe);
  Context context;
  const Assignment assignment;
  global_objective.fullApply(TopToBottomEvaluator(context), assignment);
  auto global_labeled_objectives =
      labeled_objective_builder.build(global_objective);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      labeled_objective_builder.addSingle(1, "three", C1),
      "cannot re-use already built global labeled objective builder");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      labeled_objective_builder.build(global_objective),
      "global labeled objective already built!");

  ASSERT_EQ(2, global_labeled_objectives.size());
  ASSERT_EQ(global_labeled_objectives.size(), global_objective.size());

  // check that all labeled objectives have two labeled expressions
  EXPECT_EQ(
      2,
      global_labeled_objectives.getFirstObjective()
          .getSummary()
          .objs()
          ->size());
  EXPECT_EQ(
      2,
      global_labeled_objectives.getObjectiveAt(1).getSummary().objs()->size());

  // check that root value = objective value at all positions
  int pos = 0;
  for (const auto& labeled_objectives_elem : global_labeled_objectives) {
    EXPECT_EQ(
        *labeled_objectives_elem.getSummary().value(),
        global_objective.getObjectiveAt(pos)->value);
    ++pos;
  }
}
