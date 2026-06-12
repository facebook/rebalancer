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

#include "algopt/rebalancer/solver/expressions/NthLargest.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"

#include <folly/Random.h>
#include <gtest/gtest.h>

#include <algorithm>

namespace facebook::rebalancer::packer::tests {

class NthLargestTest : public ExpressionTestsBase {};

TEST_F(NthLargestTest, NthLargest) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{{"container0", {}}});
  const auto universe = buildUniverse();
  std::vector<std::shared_ptr<Expression>> values = {
      const_expr(-10, universe),
      const_expr(-10, universe),
      const_expr(-5, universe),
      const_expr(0, universe),
      const_expr(0.5, universe),
      const_expr(50, universe),
      const_expr(50, universe),
      const_expr(100, universe),
  };
  std::shuffle(values.begin(), values.end(), folly::ThreadLocalPRNG());
  auto test = [&values, &universe](int n, bool unique, double expected) {
    NthLargest nthLargest(values, n, unique, universe);
    const double result = _apply(nthLargest, Assignment());
    EXPECT_EQ(expected, result);
  };
  test(0, false, 100);
  test(1, false, 50);
  test(2, false, 50);
  test(3, false, 0.5);
  test(4, false, 0);
  test(5, false, -5);
  test(6, false, -10);
  test(7, false, -10);
  test(8, false, -10);
  test(1000, false, -10);
  test(0, true, 100);
  test(1, true, 50);
  test(2, true, 0.5);
  test(3, true, 0);
  test(4, true, -5);
  test(5, true, -10);
  test(6, true, -10);
  test(7, true, -10);
  test(8, true, -10);
  test(1000, true, -10);
}

} // namespace facebook::rebalancer::packer::tests
