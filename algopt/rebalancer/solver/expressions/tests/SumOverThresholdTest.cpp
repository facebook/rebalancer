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

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class SumOverThresholdTest : public ExpressionTestsBase {
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

TEST_F(SumOverThresholdTest, SumOverThreshold) {
  const auto universe = buildUniverse();
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  auto var_t0 = variable(object(0), container(1), universe, initialAssignment);
  auto var_t1 = variable(object(1), container(1), universe, initialAssignment);
  auto var_t2 = variable(object(2), container(1), universe, initialAssignment);
  auto var_t3 = variable(object(3), container(1), universe, initialAssignment);

  auto sum_t0 = 0.25 * var_t0;
  auto sum_t1 = 0.3 + 0.3 * var_t1;
  auto sum_t2 = 0.8 * var_t2;

  auto threshold = 0.5 + 0.1 * var_t3;
  auto sot =
      sum_over_threshold(threshold, {sum_t0, sum_t1, sum_t2}, false, universe);
  // only var_t1 (= var(o1, c1)) is 1; the others are 0, so sum_t1 = 0.6 and the
  // rest are 0. With threshold = 0.5, sum = max(0, 0.6 - 0.5) + 0 + 0 = 0.1.
  EXPECT_NEAR(0.1, sot->getInitialValue(), 1e-8);
  EXPECT_NEAR(0.0, lower_bound(*sot), 1e-8);
  EXPECT_NEAR(0.4, upper_bound(*sot), 1e-8);

  auto assignment = makeAssignment(
      {{object(0), container(1)},
       {object(1), container(1)},
       {object(2), container(1)},
       {object(3), container(0)}});

  EXPECT_NEAR(0.4, apply(sot, assignment), 1e-8);
  EXPECT_NEAR(0.4, sot->value, 1e-8);

  EXPECT_NEAR(
      0.4, evaluate(sot, {{object(0), container(0)}}, assignment), 1e-8);

  EXPECT_NEAR(
      0.3, evaluate(sot, {{object(1), container(0)}}, assignment), 1e-8);

  EXPECT_NEAR(
      0.1, evaluate(sot, {{object(2), container(0)}}, assignment), 1e-8);

  EXPECT_NEAR(
      0.1,
      evaluate(
          sot,
          {{object(0), container(0)}, {object(2), container(0)}},
          assignment),
      1e-8);

  EXPECT_NEAR(
      0.0,
      evaluate(
          sot,
          {{object(1), container(0)}, {object(2), container(0)}},
          assignment),
      1e-8);

  EXPECT_NEAR(
      0.2, evaluate(sot, {{object(3), container(1)}}, assignment), 1e-8);

  EXPECT_NEAR(
      0.2,
      evaluate(
          sot,
          {{object(1), container(0)}, {object(3), container(1)}},
          assignment),
      1e-8);

  EXPECT_NEAR(
      0.0,
      evaluate(
          sot,
          {{object(2), container(0)}, {object(3), container(1)}},
          assignment),
      1e-8);
}

TEST_F(SumOverThresholdTest, SumOverThreshold2) {
  const auto universe = buildUniverse();
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  auto var_t0 = variable(object(0), container(1), universe, initialAssignment);
  auto var_t1 = variable(object(1), container(1), universe, initialAssignment);
  auto var_t2 = variable(object(2), container(1), universe, initialAssignment);
  auto var_t3 = variable(object(3), container(1), universe, initialAssignment);

  auto sum_t0 = 0.25 * var_t0;
  auto sum_t1 = 0.3 + 0.3 * var_t1;
  auto sum_t2 = 0.8 * var_t2;

  auto threshold = 0.5 + 0.1 * var_t3;

  auto sot =
      sum_over_threshold(threshold, {sum_t0, sum_t1, sum_t2}, true, universe);
  // (max(0, 0.6 - 0.5))^2 + 0 + 0 = 0.01.
  EXPECT_NEAR(0.01, sot->getInitialValue(), 1e-8);
  EXPECT_NEAR(0.0, lower_bound(*sot), 1e-8);
  EXPECT_NEAR(0.1, upper_bound(*sot), 1e-8);

  // skip lp expr evaluation because lp() is not currently supported when
  // squares is true
  LpAssertOptions lpAssertOptions = {
      .exceptionForLpExpr =
          "SumOverThreshold with squares is not supported yet when using lp()"};

  auto assignment = makeAssignment(
      {{object(0), container(1)},
       {object(1), container(1)},
       {object(2), container(1)},
       {object(3), container(0)}});

  EXPECT_NEAR(0.1, apply(sot, assignment, lpAssertOptions), 1e-8);
  EXPECT_NEAR(0.1, sot->value, 1e-8);

  EXPECT_NEAR(
      0.1,
      evaluate(sot, {{object(0), container(0)}}, assignment, lpAssertOptions),
      1e-8);

  EXPECT_NEAR(
      0.09,
      evaluate(sot, {{object(1), container(0)}}, assignment, lpAssertOptions),
      1e-8);

  EXPECT_NEAR(
      0.01,
      evaluate(sot, {{object(2), container(0)}}, assignment, lpAssertOptions),
      1e-8);

  EXPECT_NEAR(
      0.01,
      evaluate(
          sot,
          {{object(0), container(0)}, {object(2), container(0)}},
          assignment,
          lpAssertOptions),
      1e-8);

  EXPECT_NEAR(
      0.0,
      evaluate(
          sot,
          {{object(1), container(0)}, {object(2), container(0)}},
          assignment,
          lpAssertOptions),
      1e-8);

  EXPECT_NEAR(
      0.04,
      evaluate(sot, {{object(3), container(1)}}, assignment, lpAssertOptions),
      1e-8);

  EXPECT_NEAR(
      0.04,
      evaluate(
          sot,
          {{object(1), container(0)}, {object(3), container(1)}},
          assignment,
          lpAssertOptions),
      1e-8);

  EXPECT_NEAR(
      0.0,
      evaluate(
          sot,
          {{object(2), container(0)}, {object(3), container(1)}},
          assignment,
          lpAssertOptions),
      1e-8);
}

} // namespace facebook::rebalancer::packer::tests
