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
#include "algopt/rebalancer/solver/utils/LocalSearchObjective.h"

#include <gtest/gtest.h>

using namespace facebook::rebalancer;

namespace {
Precision createPrecision() {
  const facebook::algopt::common::thrift::PrecisionTolerances tolerances;
  return Precision(tolerances);
}

LocalSearchObjectiveValue makeSeparatedValue(
    std::initializer_list<double> global,
    std::initializer_list<double> penalty) {
  const entities::Universe universe;
  GlobalObjective::Builder globalBuilder;
  GlobalObjective::Builder penaltyBuilder;
  int pos = 0;
  for (const auto value : global) {
    globalBuilder.setObjective(pos++, const_expr(value, universe));
  }
  pos = 0;
  for (const auto value : penalty) {
    penaltyBuilder.setObjective(pos++, const_expr(value, universe));
  }
  return LocalSearchObjective(
             globalBuilder.build(universe),
             penaltyBuilder.build(universe),
             0,
             static_cast<int>(global.size()),
             universe)
      .getValue();
}
} // namespace

TEST(LocalSearchObjectiveValueTest, InterleavesGlobalAndPenaltyValues) {
  const auto precision = createPrecision();
  const auto current = makeSeparatedValue({1, 2}, {10, 10});
  const auto earlierPenaltyImprovement = makeSeparatedValue({1, 3}, {9, 100});
  const auto earlierPenaltyRegression = makeSeparatedValue({1, 0}, {11, 0});

  EXPECT_TRUE(
      LocalSearchObjectiveValue::isStrictlyBetter(
          makeSeparatedValue({0, 100}, {1000000, 1000000}),
          current,
          precision));
  EXPECT_TRUE(
      LocalSearchObjectiveValue::isStrictlyBetter(
          earlierPenaltyImprovement, current, precision));
  EXPECT_LT(
      LocalSearchObjectiveValue::unsafeCompare(
          earlierPenaltyImprovement, current),
      0);
  EXPECT_LT(
      LocalSearchObjectiveValue::precisionCompare(
          earlierPenaltyImprovement, current, precision),
      0);
  EXPECT_TRUE(
      LocalSearchObjectiveValue::isStrictlyBetter(
          makeSeparatedValue({1, 2}, {9, 100}), current, precision));

  EXPECT_FALSE(
      LocalSearchObjectiveValue::isStrictlyBetter(
          earlierPenaltyRegression, current, precision));
  EXPECT_GT(
      LocalSearchObjectiveValue::unsafeCompare(
          earlierPenaltyRegression, current),
      0);
  EXPECT_GT(
      LocalSearchObjectiveValue::precisionCompare(
          earlierPenaltyRegression, current, precision),
      0);
}

TEST(LocalSearchObjectiveValueTest, LegacyModeComparesOnlyGlobalValues) {
  const auto precision = createPrecision();
  const auto current = LocalSearchObjectiveValue(GlobalObjectiveValue({1, 2}));
  const auto candidate =
      LocalSearchObjectiveValue(GlobalObjectiveValue({1, 1}));

  EXPECT_TRUE(
      LocalSearchObjectiveValue::isStrictlyBetter(
          candidate, current, precision));
  EXPECT_FALSE(
      LocalSearchObjectiveValue::isStrictlyBetter(
          current, candidate, precision));
  EXPECT_EQ("(1, 2)", current.toString());
}

TEST(LocalSearchObjectiveValueTest, PrintsPenaltyAfterItsGlobalPosition) {
  const auto value = makeSeparatedValue({1, 2}, {3, 4});

  EXPECT_EQ("(1 (3), 2 (4))", value.toString());
}

TEST(LocalSearchObjectiveValueTest, EmptyValueIsWorseThanAnyValue) {
  const auto precision = createPrecision();
  const LocalSearchObjectiveValue empty;
  const auto withPenalty = makeSeparatedValue({5}, {9});

  EXPECT_GT(
      LocalSearchObjectiveValue::precisionCompare(
          empty, withPenalty, precision),
      0);
  EXPECT_TRUE(
      LocalSearchObjectiveValue::isStrictlyBetter(
          withPenalty, empty, precision));
}

TEST(LocalSearchObjectiveValueTest, ForbidsToleranceCycleAcrossTuples) {
  const auto precision = createPrecision();
  const auto a = makeSeparatedValue({4.04519466830}, {2.01836050018});
  const auto b = makeSeparatedValue({4.04519466852}, {2.01836049997});
  const auto c = makeSeparatedValue({4.04519466872}, {2.01836049905});

  EXPECT_FALSE(LocalSearchObjectiveValue::isStrictlyBetter(b, a, precision));
  EXPECT_FALSE(LocalSearchObjectiveValue::isStrictlyBetter(c, b, precision));
  EXPECT_TRUE(LocalSearchObjectiveValue::isStrictlyBetter(a, c, precision));
}
