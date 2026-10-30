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

#include "algopt/rebalancer/solver/expressions/AnyPositive.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class AnyPositiveTest : public ExpressionTestsBase {
 protected:
  void setUpDefaultAssignment() {
    constexpr int kNumContainers = 20;
    entities::Map<std::string, std::vector<std::string>> initialAssignment;
    for (const auto i : folly::irange(kNumContainers)) {
      initialAssignment[fmt::format("container{}", i)] = {
          fmt::format("object{}", i)};
    }
    setInitialAssignment(initialAssignment);
  }
};

TEST_F(AnyPositiveTest, InitialValue) {
  setUpDefaultAssignment();
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  // v0c0 = 1 (object0 in container0); v0c1 = 0 (object0 not in container1);
  // v1c0 = 0 (object1 in container1, not container0).
  auto v0c0 = variable(object(0), container(0), universe, assignment);
  auto v0c1 = variable(object(0), container(1), universe, assignment);
  auto v1c0 = variable(object(1), container(0), universe, assignment);

  // Any positive child -> 1.
  EXPECT_DOUBLE_EQ(
      1.0, any_positive({v0c0, v1c0}, universe)->getInitialValue());
  // All-zero children -> 0.
  EXPECT_DOUBLE_EQ(
      0.0, any_positive({v0c1, v1c0}, universe)->getInitialValue());
  // No children -> 0.
  EXPECT_DOUBLE_EQ(0.0, any_positive({}, universe)->getInitialValue());
}

TEST_F(AnyPositiveTest, AddInitialValue) {
  setUpDefaultAssignment();
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  // Start with no violating children.
  auto v0c1 = variable(object(0), container(1), universe, assignment);
  auto expr = any_positive({v0c1}, universe);
  EXPECT_DOUBLE_EQ(0.0, expr->getInitialValue());

  // Add a positive child via any_positive_add -> should become 1.
  auto v0c0 = variable(object(0), container(0), universe, assignment);
  any_positive_add(expr, v0c0);
  EXPECT_DOUBLE_EQ(1.0, expr->getInitialValue());
}

TEST_F(AnyPositiveTest, ChildWithinToleranceAboveTheToleranceIsViolating) {
  setUpDefaultAssignment();
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  const auto tolerance =
      universe.getPrecision().getTolerances().absolute().value();
  const ObjectToNewContainer moveIn{{object(0), container(1)}};
  const auto movedAssignment = getModifiedAssignment(assignment, moveIn);

  const auto anyPositiveOf = [&](const Assignment& base, double weight) {
    return any_positive(
        {variable(object(0), container(1), universe, base) * weight}, universe);
  };
  // Incremental evaluation of the move.
  EXPECT_DOUBLE_EQ(
      1.0,
      evaluate(
          *anyPositiveOf(assignment, 1.5 * tolerance),
          getChangeSet(moveIn, assignment)));
  EXPECT_DOUBLE_EQ(
      0.0,
      evaluate(
          *anyPositiveOf(assignment, 0.5 * tolerance),
          getChangeSet(moveIn, assignment)));
  // Full evaluation of the assignment after the move.
  EXPECT_DOUBLE_EQ(
      1.0, anyPositiveOf(movedAssignment, 1.5 * tolerance)->getInitialValue());
  EXPECT_DOUBLE_EQ(
      0.0, anyPositiveOf(movedAssignment, 0.5 * tolerance)->getInitialValue());
}

} // namespace facebook::rebalancer::packer::tests
