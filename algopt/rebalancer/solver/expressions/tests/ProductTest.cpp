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
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class ProductTest : public ExpressionTestsBase {
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

TEST_F(ProductTest, NeitherBinary) {
  setUpDefaultAssignment();
  const auto universe = buildUniverse();
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  // we have objects 1, 2 and containers: 0, 1
  // initially container 1 contains both objects
  // so variable(1, 0) = variable(2, 0) = 0
  auto binaryOperation = product(
      4 * variable(object(1), container(0), universe, initialAssignment) - 2,
      2 * variable(object(2), container(0), universe, initialAssignment) + 1,
      universe);
  const Assignment assignment(
      {{container(0), {}}, {container(1), {object(1), object(2)}}});

  // skip lp expr evaluation because lp() is ony supported when one the operands
  // is binary
  LpAssertOptions lpAssertOptions = {
      .exceptionForLpExpr =
          "At least one of the operands must be a binary variable"};

  EXPECT_DOUBLE_EQ(-2, binaryOperation->getInitialValue());
  EXPECT_EQ(-2, apply(binaryOperation, assignment, lpAssertOptions));
  EXPECT_TRUE(descendingChildPotentialsAsExpected(*binaryOperation, {0, 0}));

  // move object 1 to container 0
  auto changes = ObjectToNewContainer{{object(1), container(0)}};
  EXPECT_EQ(2, evaluate(binaryOperation, changes, assignment, lpAssertOptions));

  EXPECT_EQ(6, upper_bound(*binaryOperation));
  EXPECT_EQ(-6, lower_bound(*binaryOperation));

  // apply the move
  EXPECT_EQ(
      2, applyChanges(binaryOperation, changes, assignment, lpAssertOptions));
  EXPECT_TRUE(
      descendingChildPotentialsAsExpected(*binaryOperation, {4.0, 0.0}));
}

TEST_F(ProductTest, LhsBinary) {
  setUpDefaultAssignment();
  const auto universe = buildUniverse();
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  auto m =
      2 * variable(object(1), container(0), universe, initialAssignment) - 1;
  auto binaryOperation = product(step(m, universe), m - 2, universe);
  const Assignment assignment({
      {container(0), {}},
      {container(1), {object(1)}},
  });

  // m init = 2*0 - 1 = -1; step(-1) = 0; (m-2) init = -3; product = 0 * -3 = 0.
  EXPECT_DOUBLE_EQ(0, binaryOperation->getInitialValue());
  EXPECT_EQ(0, apply(binaryOperation, assignment));

  EXPECT_EQ(
      -1, evaluate(binaryOperation, {{object(1), container(0)}}, assignment));

  EXPECT_EQ(
      -1,
      applyChanges(binaryOperation, {{object(1), container(0)}}, assignment));
}

TEST_F(ProductTest, EquivalenceSets) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1", {"object1", "object2"}},
          {"container2", {"object3", "object4"}}});
  const auto universe = buildUniverse();
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  auto o1c1 = variable(object(1), container(1), universe, assignment);
  auto o3c2 = variable(object(3), container(2), universe, assignment);
  auto b = product(o1c1, o3c2, universe);
  // o1c1 init = 1, o3c2 init = 1, so product init = 1.
  EXPECT_DOUBLE_EQ(1.0, b->getInitialValue());

  EquivalenceSets equivalenceSets(*universe);
  equivalenceSets.combine(
      std::vector<entities::ObjectId>{
          object(1), object(2), object(3), object(4)});

  updateEquivalenceSets(equivalenceSets, *b);

  EXPECT_EQ(equivalenceSets.size(), 3);
  EXPECT_NE(equivalenceSets.at(object(1)), equivalenceSets.at(object(2)));
  EXPECT_EQ(equivalenceSets.at(object(2)), equivalenceSets.at(object(4)));
  EXPECT_NE(equivalenceSets.at(object(1)), equivalenceSets.at(object(3)));
}

} // namespace facebook::rebalancer::packer::tests
