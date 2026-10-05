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

#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"

#include <gtest/gtest.h>

#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace facebook::rebalancer;

class GlobalObjectiveTest : public ::testing::Test {
 public:
  const entities::Universe universe{};
};

TEST_F(GlobalObjectiveTest, NullPtrIsZero) {
  // there is a gap in the objective
  {
    GlobalObjective::Builder objective_builder;
    objective_builder.addToObjective(0, const_expr(1, universe), universe);
    objective_builder.addToObjective(2, const_expr(1, universe), universe);
    auto objective = objective_builder.build(universe);
    ASSERT_EQ(3, objective.size());
    EXPECT_EQ(0, objective.getObjectiveAt(1)->value);

    EXPECT_THROW(
        objective_builder.addToObjective(1, const_expr(1, universe), universe),
        std::runtime_error);
  }

  // objective has a nullptr in the middle
  {
    GlobalObjective::Builder objective_builder;
    objective_builder.addToObjective(0, const_expr(1, universe), universe);
    objective_builder.addToObjective(1, nullptr, universe);
    EXPECT_EQ(0, objective_builder.build(universe).getObjectiveAt(1)->value);
  }

  //  objectives added in any order
  {
    GlobalObjective::Builder objective_builder;
    objective_builder.addToObjective(0, const_expr(1, universe), universe);
    objective_builder.addToObjective(2, const_expr(1, universe), universe);
    objective_builder.addToObjective(1, const_expr(2, universe), universe);
    objective_builder.addToObjective(2, const_expr(2, universe), universe);

    auto objective = objective_builder.build(universe);
    ASSERT_EQ(3, objective.size());

    EXPECT_EQ(
        2,
        objective.getObjectiveAt(1)
            ->get_sorted_children(true)
            .at(0)
            .first->value);

    const auto& children =
        objective.getObjectiveAt(2)->get_sorted_children(true);
    ASSERT_EQ(2, children.size());
    // Children order may vary by platform due to hash map iteration order
    std::set<double> childValues{
        children.at(0).first->value, children.at(1).first->value};
    EXPECT_EQ(childValues, (std::set<double>{1, 2}));
  }
}

TEST_F(GlobalObjectiveTest, AddToObjective) {
  // adding nullptr to a valid expression
  {
    GlobalObjective::Builder objective_builder;
    objective_builder.addToObjective(0, const_expr(1, universe), universe);
    objective_builder.addToObjective(0, nullptr, universe);
    auto objective = objective_builder.build(universe);
    EXPECT_NE(nullptr, objective.getFirstObjective());
    ASSERT_EQ(1, objective.size());
  }

  // replacing nullptr with a valid expression
  {
    GlobalObjective::Builder objective_builder;
    objective_builder.addToObjective(0, nullptr, universe);
    objective_builder.addToObjective(0, const_expr(1, universe), universe);
    auto objective = objective_builder.build(universe);
    EXPECT_NE(nullptr, objective.getFirstObjective());
    ASSERT_EQ(1, objective.size());
  }

  // in-place add of a non-nullptr expression with another valid expression
  {
    GlobalObjective::Builder objective_builder;
    objective_builder.addToObjective(0, const_expr(1, universe), universe);
    objective_builder.addToObjective(0, const_expr(2, universe), universe);

    auto objective = objective_builder.build(universe);

    // value is 3 (= const_expr(1) + const_expr(2))
    EXPECT_EQ(3, objective.getFirstObjective()->value);
    ASSERT_EQ(1, objective.size());

    const auto& children =
        objective.getFirstObjective()->get_sorted_children(true);
    ASSERT_EQ(2, children.size());
    // Children order may vary by platform due to hash map iteration order
    std::set<double> childValues2{
        children.at(0).first->value, children.at(1).first->value};
    EXPECT_EQ(childValues2, (std::set<double>{1, 2}));
  }
}
