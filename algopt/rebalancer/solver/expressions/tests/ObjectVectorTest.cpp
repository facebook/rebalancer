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

#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"

#include <gtest/gtest.h>

#include <stdexcept>

namespace facebook::rebalancer::packer::tests {

class ObjectVectorTest : public ExpressionTestsBase {};

TEST_F(ObjectVectorTest, VectorBounds) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1"}}});
  const auto universe = buildUniverse();
  auto vec = makeObjectVector({{object(0), 5}, {object(1), -3}}, universe);
  EXPECT_EQ(5, upper_bound(*vec));
  EXPECT_EQ(-3, lower_bound(*vec));
}

TEST_F(ObjectVectorTest, VectorTotalTooSmall) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1"}}});
  const auto universe = buildUniverse();
  EXPECT_THROW(
      makeObjectVector({{object(0), 5}, {object(1), -3}}, -3, 1, universe),
      std::runtime_error);
}

} // namespace facebook::rebalancer::packer::tests
