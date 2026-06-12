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
#include "algopt/rebalancer/solver/expressions/Square.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"

#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class SquareTest : public ExpressionTestsBase {};

TEST_F(SquareTest, BoundsTests) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1"}}});
  const auto universe = buildUniverse();
  const PackerMap<entities::ObjectId, double> input = {
      {object(0), 4}, {object(1), 3}};
  auto vector = makeObjectVector(input, universe);
  Square square(vector, universe);
  EXPECT_EQ(49, upper_bound(square));
  EXPECT_EQ(0, lower_bound(square));

  // check children's unconstrained bounds
  EXPECT_EQ(0, lower_bound(*vector));
  EXPECT_EQ(7, upper_bound(*vector));
}

TEST_F(SquareTest, VariableBoundsTests) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0"}}});
  const auto universe = buildUniverse();
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  auto var = variable(object(0), container(0), universe, assignment);

  auto sum = var * 3 + 8;
  EXPECT_EQ(8, lower_bound(*sum));
  EXPECT_EQ(11, upper_bound(*sum));
  Square square(sum, universe);

  EXPECT_EQ(64, lower_bound(square));
  EXPECT_EQ(121, upper_bound(square));

  auto negative_sum = var * 2 - 7;
  EXPECT_EQ(-7, lower_bound(*negative_sum));
  EXPECT_EQ(-5, upper_bound(*negative_sum));
  Square square2(negative_sum, universe);
  EXPECT_EQ(25, lower_bound(square2));
  EXPECT_EQ(49, upper_bound(square2));

  auto zero_between_sum = var * 2 - 1;
  EXPECT_EQ(-1, lower_bound(*zero_between_sum));
  EXPECT_EQ(1, upper_bound(*zero_between_sum));
  Square square3(zero_between_sum, universe);
  EXPECT_EQ(0, lower_bound(square3));
  EXPECT_EQ(1, upper_bound(square3));
}

TEST_F(SquareTest, EquivalenceSets) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1", {"object1", "object2", "object3", "object4"}}});
  const auto universe = buildUniverse();
  auto allObjectIds = std::vector<entities::ObjectId>{
      object(1), object(2), object(3), object(4)};
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  auto var = variable(object(1), container(1), universe, assignment);
  auto sum = 10 * var + 5;
  auto b = square(sum, universe);

  EquivalenceSets equivalenceSets(*universe);
  equivalenceSets.combine(allObjectIds);

  updateEquivalenceSetsRecursive(
      equivalenceSets,
      *b,
      static_cast<entities::EntityIdType>(allObjectIds.size()));

  EXPECT_EQ(equivalenceSets.size(), 2);
  EXPECT_NE(equivalenceSets.at(object(1)), equivalenceSets.at(object(2)));
  EXPECT_EQ(equivalenceSets.at(object(2)), equivalenceSets.at(object(4)));
  EXPECT_NE(equivalenceSets.at(object(1)), equivalenceSets.at(object(3)));
}

} // namespace facebook::rebalancer::packer::tests
