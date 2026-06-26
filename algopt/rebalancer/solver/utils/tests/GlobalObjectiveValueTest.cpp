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
#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"

#include <folly/container/irange.h>
#include <gtest/gtest.h>

using namespace facebook::rebalancer;

namespace {
Precision createPrecision() {
  facebook::algopt::common::thrift::PrecisionTolerances tolerances;
  return Precision(tolerances);
}
} // namespace

TEST(GlobalObjectiveValueTest, Basic) {
  GlobalObjectiveValue val1({1.0, 2.0}), val2({-1.0});
  ASSERT_EQ(2, val1.size());
  EXPECT_EQ(2.0, val1.get(1));

  val1.append(3.0);
  ASSERT_EQ(3, val1.size());
  EXPECT_EQ(3.0, val1.get(2));

  EXPECT_EQ("(1, 2, 3)", val1.toString());
  EXPECT_EQ("-1", val2.toString());

  ASSERT_EQ(1, val2.size());
  EXPECT_EQ(-1.0, val2.get(0));

  val2 = val1;
  ASSERT_EQ(3, val2.size());
  EXPECT_EQ(1.0, val2.get(0));
}

// Regression test for local-search cycling. These are real objective tuples
// from a cycle A->B->C->A: each step looked "better" under the old comparison
// (the higher-priority component tied within tolerance while the lower-priority
// one improved), letting the higher-priority component silently drift worse so
// the solver cycled forever. isStrictlyBetter forbids worsening a tied
// component, so the drift steps are rejected while a genuine (significant)
// improvement is still accepted.
TEST(GlobalObjectiveValueTest, IsStrictlyBetterForbidsTiedWorsening) {
  const auto precision = createPrecision(); // absolute == relative == 1e-10
  const GlobalObjectiveValue a({4.04519466830, 2.01836050018});
  const GlobalObjectiveValue b({4.04519466852, 2.01836049997});
  const GlobalObjectiveValue c({4.04519466872, 2.01836049905});

  // Drift steps worsen the higher-priority component within tolerance.
  EXPECT_FALSE(GlobalObjectiveValue::isStrictlyBetter(b, a, precision)); // A->B
  EXPECT_FALSE(GlobalObjectiveValue::isStrictlyBetter(c, b, precision)); // B->C

  // A genuine, significant improvement is still accepted.
  EXPECT_TRUE(GlobalObjectiveValue::isStrictlyBetter(a, c, precision)); // C->A

  // Sanity: clear improvement / worsening.
  const GlobalObjectiveValue lo({1.0});
  const GlobalObjectiveValue hi({2.0});
  EXPECT_TRUE(GlobalObjectiveValue::isStrictlyBetter(lo, hi, precision));
  EXPECT_FALSE(GlobalObjectiveValue::isStrictlyBetter(hi, lo, precision));
  // Equal tuples are not strictly better.
  EXPECT_FALSE(GlobalObjectiveValue::isStrictlyBetter(lo, lo, precision));
}

TEST(GlobalObjectiveValueTest, Comparators) {
  const auto precision = createPrecision();
  GlobalObjectiveValue val1, val2;

  // uninitialized are equal
  EXPECT_EQ(0, GlobalObjectiveValue::precisionCompare(val1, val2, precision));

  // initialized < uninitialized
  val1.append(1.0);
  EXPECT_LT(GlobalObjectiveValue::precisionCompare(val1, val2, precision), 0);
  EXPECT_GT(GlobalObjectiveValue::precisionCompare(val2, val1, precision), 0);

  val2.append(-1.0);
  EXPECT_LT(GlobalObjectiveValue::precisionCompare(val2, val1, precision), 0);
  EXPECT_GT(GlobalObjectiveValue::precisionCompare(val1, val2, precision), 0);
}

TEST(GlobalObjectiveValueTest, Partial) {
  const auto precision = createPrecision();
  GlobalObjectiveValue val1;

  val1.append(1);
  val1.append(2);
  val1.append(3);

  auto val2 = GlobalObjectiveValue::makeWithFixedSize(2);
  val2.append(0);

  EXPECT_LT(GlobalObjectiveValue::precisionCompare(val2, val1, precision), 0);
  EXPECT_GT(GlobalObjectiveValue::precisionCompare(val1, val2, precision), 0);

  EXPECT_EQ("(0, __)", val2.toString());
}

TEST(GlobalObjectiveValueTest, AdditionAndSubtractionError) {
  const auto precision = createPrecision();
  GlobalObjectiveValue val1;
  val1.append(1);
  val1.append(2);
  val1.append(3);

  auto val2 = GlobalObjectiveValue::makeWithFixedSize(2);
  val2.append(0);

  EXPECT_EQ(val1.size(), 3);
  EXPECT_EQ(val2.size(), 1);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      GlobalObjectiveValue::add(val1, val2, precision),
      "addition of GlobalObjective values can only be done if both have the same length");

  REBALANCER_EXPECT_RUNTIME_ERROR(
      GlobalObjectiveValue::subtract(val1, val2, precision),
      "subtraction of GlobalObjective values can only be done if both have the same length");
}

TEST(GlobalObjectiveValueTest, BasicAdditionAndSubtraction) {
  const auto precision = createPrecision();
  const GlobalObjectiveValue val1({1, 20, 30, -10, 6, -7, -5, 1});
  const GlobalObjectiveValue val2({-1, -33, 3, 0, 6, 3, -5, 9});

  const GlobalObjectiveValue computedSum =
      GlobalObjectiveValue::add(val1, val2, precision);
  const GlobalObjectiveValue expectedSum({0, -13, 33, -10, 12, -4, -10, 10});
  // check that addition is correct
  EXPECT_EQ(computedSum.size(), expectedSum.size());
  for (const auto i : folly::irange(expectedSum.size())) {
    EXPECT_EQ(computedSum.get(i), expectedSum.get(i));
  }

  const GlobalObjectiveValue computedDiff =
      GlobalObjectiveValue::subtract(val1, val2, precision);
  const GlobalObjectiveValue expectedDiff({2, 53, 27, -10, 0, -10, 0, -8});
  // check that subtraction is correct
  EXPECT_EQ(computedDiff.size(), expectedDiff.size());
  for (const auto i : folly::irange(expectedDiff.size())) {
    EXPECT_EQ(computedDiff.get(i), expectedDiff.get(i));
  }
}
