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
#include "algopt/rebalancer/solver/expressions/Power.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"

#include <gtest/gtest.h>

#include <cmath>

namespace facebook::rebalancer::packer::tests {

class PowerTest : public ExpressionTestsBase {};

TEST_F(PowerTest, CubedBoundsTests) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1"}}});
  const auto universe = buildUniverse();
  auto vector = makeObjectVector({{object(0), 1}, {object(1), 4}}, universe);
  Power power(vector, 3, universe);
  EXPECT_EQ(125, upper_bound(power));
  EXPECT_EQ(0, lower_bound(power));
}

TEST_F(PowerTest, NegativeCubedBoundsTests) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1"}}});
  const auto universe = buildUniverse();
  auto vector = makeObjectVector({{object(0), -1}, {object(1), -4}}, universe);
  Power power(vector, 3, universe);
  EXPECT_EQ(0, upper_bound(power));
  EXPECT_EQ(-125, lower_bound(power));
}

TEST_F(PowerTest, NegativePowerBoundsTests) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1"}}});
  const auto universe = buildUniverse();
  auto vector = makeObjectVector({{object(0), .25}, {object(1), -5}}, universe);
  Power power(vector, -2, universe);
  EXPECT_EQ(pow(.25, -2), 16);
  EXPECT_EQ(16, upper_bound(power));
  EXPECT_EQ(.04, lower_bound(power));
}

} // namespace facebook::rebalancer::packer::tests
