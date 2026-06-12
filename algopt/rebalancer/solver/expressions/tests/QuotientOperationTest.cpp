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

#include <folly/container/irange.h>
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace facebook::rebalancer::packer::tests {

class QuotientOperationTest : public ExpressionTestsBase {
 protected:
  void setUpDefaultAssignment() {
    setInitialAssignment(
        entities::Map<std::string, std::vector<std::string>>{
            {"container0", {}}, {"container1", {"object1"}}});
  }
};

TEST_F(QuotientOperationTest, LpExactWhenQuotientInUnitInterval) {
  setUpDefaultAssignment();
  // a/b = 1 / (0 + 2) = 0.5, inside the [0, 1] range that lp() can
  // represent.
  const auto universe = buildUniverse();
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  auto x = variable(object(1), container(0), universe, assignment);
  auto q = quotient(const_expr(1, universe), x + 2, universe);

  EXPECT_NEAR(0.5, apply(q, assignment), 1e-8);
}

TEST_F(QuotientOperationTest, LpInfeasibleWhenQuotientExceedsOne) {
  setUpDefaultAssignment();
  // a/b = 3 / (0 + 2) = 1.5 is outside [0, 1]; the expression's apply
  // still gets the exact value, but lp()'s relaxation hardcodes the
  // quotient's bound to [0, 1] (see `approximate_quotient_log` in
  // QuotientOperation.cpp) and the MIP comes back infeasible.
  const auto universe = buildUniverse();
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  auto x = variable(object(1), container(0), universe, assignment);
  auto q = quotient(const_expr(3, universe), x + 2, universe);

  LpAssertOptions lpAssertOptions{
      .exceptionForLpExpr =
          "LP problem has no solution (infeasible or unbounded)"};
  EXPECT_NEAR(1.5, apply(q, assignment, lpAssertOptions), 1e-8);
}

TEST_F(QuotientOperationTest, Bounds) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {}}, {"container1", {"object1", "object2"}}});
  const auto universe = buildUniverse();
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  const double inf = std::numeric_limits<double>::infinity();
  const double nan = 0 * inf;
  std::array<double, 8> v1coeff = {4, 2, 2, 2, -3, 5, 5, 5};
  std::array<double, 8> v1const = {3, -3, -3, 3, -2, -3, 0, -3};
  std::array<double, 8> v2coeff = {2, 2, 2, 3, 3, 3, 3, 3};
  std::array<double, 8> v2const = {1, 2, -5, -2, -2, -2, 0, 0};
  std::array<double, 8> eval = {
      3, -1.5, 3.0 / 5, -3.0 / 2, 1, 3.0 / 2, nan, -inf};
  std::array<double, 8> ub = {7, -0.25, 1, inf, inf, inf, inf, inf};
  std::array<double, 8> lb = {1, -1.5, 1.0 / 5, -inf, -inf, -inf, 0, -inf};

  for (const auto i : folly::irange(8)) {
    auto binary_operation = quotient(
        v1coeff[i] *
                variable(object(1), container(0), universe, initialAssignment) +
            v1const[i],
        v2coeff[i] *
                variable(object(2), container(0), universe, initialAssignment) +
            v2const[i],
        universe);

    const Assignment assignment({{container(1), {object(1), object(2)}}});
    // we have variable(1, 0) = variable(2, 0) = 0
    _apply(*binary_operation, assignment);

    if (std::isinf(eval[i])) {
      EXPECT_EQ(eval[i], evaluate(*binary_operation, {})) << "eval " << i;
    } else if (std::isnan(eval[i])) {
      EXPECT_TRUE(std::isnan(evaluate(*binary_operation, {}))) << "eval " << i;
    } else {
      EXPECT_NEAR(eval[i], evaluate(*binary_operation, {}), 0.001)
          << "eval " << i;
    }
    const double ubv = upper_bound(*binary_operation);
    if (std::isinf(ub[i])) {
      EXPECT_EQ(ub[i], ubv) << "ub" << i;
    } else if (std::isnan(ub[i])) {
      EXPECT_TRUE(std::isnan(ubv)) << "ub" << i;
    } else {
      EXPECT_NEAR(ub[i], ubv, 0.001) << "ub" << i;
    }

    const double lbv = lower_bound(*binary_operation);
    if (std::isinf(lb[i])) {
      EXPECT_EQ(lb[i], lbv) << "lb" << i;
    } else if (std::isnan(lb[i])) {
      EXPECT_TRUE(std::isnan(lbv)) << "lb" << i;
    } else {
      EXPECT_NEAR(lb[i], lbv, 0.001) << "lb" << i;
    }
  }
}

} // namespace facebook::rebalancer::packer::tests
