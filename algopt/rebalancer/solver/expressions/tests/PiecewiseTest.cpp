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
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class PiecewiseTest : public ExpressionTestsBase {
 protected:
  void SetUp() override {
    constexpr int kNumContainers = 20;
    entities::Map<std::string, std::vector<std::string>> initialAssignment;
    for (const auto i : folly::irange(kNumContainers)) {
      initialAssignment[fmt::format("container{}", i)] = {
          fmt::format("object{}", i)};
    }
    setInitialAssignment(initialAssignment);
  }
};

TEST_F(PiecewiseTest, PiecewiseNonContinuous) {
  const auto universe = buildUniverse();
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  // x = 6*var(obj1,c0) + var(obj2,c0) - 1; initial value -1, out of [0, 5].
  auto x = 6 * variable(object(1), container(0), universe, assignment) +
      variable(object(2), container(0), universe, assignment) - 1;
  REBALANCER_EXPECT_RUNTIME_ERROR_CONTAINS(
      piecewise({{0, 0}, {0, 5}, {5, 0}}, x, universe, false),
      "cannot be smaller than first point");

  // x2 has initial value 6*1 + 0 - 1 = 5 (in domain).
  auto x2 = 6 * variable(object(0), container(0), universe, assignment) +
      variable(object(2), container(0), universe, assignment) - 1;
  auto y = piecewise({{0, 0}, {0, 5}, {5, 0}}, x2, universe, false);
  EXPECT_DOUBLE_EQ(0, apply(y, assignment));

  // x2/5 = 1, so piecewise interpolates to 4 between (0, 5) and (5, 0).
  auto z = piecewise({{0, 0}, {0, 5}, {5, 0}}, x2 / 5, universe, false);
  EXPECT_DOUBLE_EQ(4, apply(z, assignment));
}

TEST_F(PiecewiseTest, PiecewiseNonContinuousDecreasing) {
  const auto universe = buildUniverse();
  const Assignment assignment(
      {{container(0), {}}, {container(1), {object(1)}}});

  auto x = 2 *
          step(variable(object(1), container(0), universe, assignment),
               universe) +
      variable(object(1), container(1), universe, assignment) - 1;
  auto y = piecewise({{0, 5}, {0, 1}, {5, 1}, {10, 0}}, x, universe, false);

  // x = 0
  EXPECT_EQ(5, apply(y, assignment));

  // x = 1
  EXPECT_EQ(1.0, evaluate(y, {{object(1), container(0)}}, assignment));

  // x = 1; just apply changes
  EXPECT_EQ(1.0, applyChanges(y, {{object(1), container(0)}}, assignment));
}

TEST_F(PiecewiseTest, PiecewiseNonMontonic) {
  const auto universe = buildUniverse();
  const Assignment assignment(
      {{container(0), {}}, {container(1), {object(1)}}});

  auto x = 2 *
          step(variable(object(1), container(0), universe, assignment),
               universe) +
      1;
  auto y = piecewise({{0, 0}, {1, 1}, {5, 0}}, x, universe);

  // x = 1
  EXPECT_EQ(1.0, apply(y, assignment));

  // x = 3, so y should be (5-3)/4 = 0.5
  EXPECT_EQ(0.5, evaluate(y, {{object(1), container(0)}}, assignment));
}

TEST_F(PiecewiseTest, PiecewiseNonDecreasing) {
  const auto universe = buildUniverse();
  const Assignment assignment(
      {{container(0), {}}, {container(1), {object(1)}}});

  auto x = 2 *
          step(variable(object(1), container(0), universe, assignment),
               universe) +
      0.5;
  auto y = piecewise({{0, 0}, {1, 1}, {5, 1}}, x, universe);

  // x = 0.5
  EXPECT_EQ(0.5, apply(y, assignment));

  // x = 2.5, so y should be 1
  EXPECT_EQ(1.0, evaluate(y, {{object(1), container(0)}}, assignment));
}

TEST_F(PiecewiseTest, PiecewiseDecreasing) {
  const auto universe = buildUniverse();
  const Assignment assignment(
      {{container(0), {}}, {container(1), {object(1)}}});

  auto x = 2 *
          step(variable(object(1), container(0), universe, assignment),
               universe) +
      0.5;
  auto y = piecewise({{0, 5}, {5, 0}}, x, universe);

  // x = 0.5, so y is 5-0.5
  EXPECT_EQ(4.5, apply(y, assignment));

  // x = 2.5, so y should be 5-2.5
  EXPECT_EQ(2.5, evaluate(y, {{object(1), container(0)}}, assignment));
}

TEST_F(PiecewiseTest, PiecewiseInitialValue) {
  const auto universe = buildUniverse();
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  auto v = variable(object(0), container(0), universe, assignment);
  // v = 1, so piecewise({(1, 9), (2, 5)})(1) = 9.
  EXPECT_DOUBLE_EQ(
      9.0, piecewise({{1.0, 9.0}, {2.0, 5.0}}, v, universe)->getInitialValue());
}

} // namespace facebook::rebalancer::packer::tests
