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

#include "algopt/rebalancer/algopt_common/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/solver/utils/Precision.h"

#include <gtest/gtest.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <string>

namespace facebook::rebalancer::tests {

const inline double kEpsilon =
    *algopt::common::thrift::PrecisionTolerances().absolute();

algopt::common::thrift::PrecisionTolerances createTolerances(
    double absolute,
    double relative) {
  algopt::common::thrift::PrecisionTolerances tolerances;
  tolerances.absolute() = absolute;
  tolerances.relative() = relative;
  return tolerances;
}

TEST(PrecisionTest, CompareLeq) {
  auto precision = Precision(algopt::common::thrift::PrecisionTolerances());

  constexpr double num1 = -3;
  constexpr double num2 = 5;
  EXPECT_TRUE(precision.isStrictlyLesser(num1, num2));
  EXPECT_FALSE(precision.isEqual(num1, num2));
}

TEST(PrecisionTest, CompareGeq) {
  auto precision = Precision(algopt::common::thrift::PrecisionTolerances());
  constexpr double num1 = 3;
  constexpr double num2 = -5;
  EXPECT_TRUE(precision.isstrictlyGreater(num1, num2));
  EXPECT_FALSE(precision.isEqual(num1, num2));
}

TEST(PrecisionTest, CompareEqEpsilon) {
  auto precision = Precision(algopt::common::thrift::PrecisionTolerances());
  constexpr double num1 = 3;
  const double num2 = 3 - 1e-11;
  EXPECT_TRUE(precision.isEqual(num1, num2));
}

TEST(PrecisionTest, CompareEq) {
  auto precision = Precision(algopt::common::thrift::PrecisionTolerances());
  constexpr double num1 = 3;
  constexpr double num2 = 3;
  EXPECT_TRUE(precision.isEqual(num1, num2));
}

TEST(PrecisionTest, IsZeroTrue) {
  auto precision = Precision(algopt::common::thrift::PrecisionTolerances());
  constexpr double num = -1e-11;
  EXPECT_TRUE(precision.isZero(num));
}

TEST(PrecisionTest, IsZeroFalse) {
  auto precision = Precision(algopt::common::thrift::PrecisionTolerances());
  const double num = 3 - 1e-11;
  EXPECT_FALSE(precision.isZero(num));
}

TEST(PrecisionTest, IsIntegerTrue) {
  auto precision = Precision(algopt::common::thrift::PrecisionTolerances());
  const double num = 3 - 1e-11;
  EXPECT_TRUE(precision.isInteger(num));
}

TEST(PrecisionTest, IsIntegerFalse) {
  auto precision = Precision(algopt::common::thrift::PrecisionTolerances());
  constexpr double num = 3.14;
  EXPECT_FALSE(precision.isInteger(num));
}

TEST(PrecisionTest, IsZeroOrOneTrue) {
  auto precision = Precision(algopt::common::thrift::PrecisionTolerances());
  const double num = 1 - 1e-11;
  EXPECT_TRUE(precision.isZeroOrOne(num));
  EXPECT_TRUE(precision.isEqual(num, 1.0));
  EXPECT_FALSE(precision.isEqual(num, 0.0));
}

TEST(PrecisionTest, IsZeroOrOneFalse) {
  auto precision = Precision(algopt::common::thrift::PrecisionTolerances());
  constexpr double num = 0.3;
  EXPECT_FALSE(precision.isZeroOrOne(num));
}

TEST(PrecisionTest, IsStrictlyGreater) {
  auto precision = Precision(algopt::common::thrift::PrecisionTolerances());
  constexpr double greater = 10.0;
  constexpr double lesser = 5.0;
  EXPECT_TRUE(precision.isstrictlyGreater(greater, lesser));
  EXPECT_FALSE(precision.isstrictlyGreater(lesser, greater));

  constexpr double equal1 = 7.0;
  constexpr double equal2 = 7.0;
  EXPECT_FALSE(precision.isstrictlyGreater(equal1, equal2));

  const double almostEqual1 = 7.0;
  const double almostEqual2 = 7.0 - kEpsilon / 10;
  EXPECT_FALSE(precision.isstrictlyGreater(almostEqual1, almostEqual2));
  EXPECT_FALSE(precision.isstrictlyGreater(almostEqual2, almostEqual1));
}

TEST(PrecisionTest, IsStrictlyLesser) {
  auto precision = Precision(algopt::common::thrift::PrecisionTolerances());
  constexpr double greater = 10.0;
  constexpr double lesser = 5.0;
  EXPECT_TRUE(precision.isStrictlyLesser(lesser, greater));
  EXPECT_FALSE(precision.isStrictlyLesser(greater, lesser));

  constexpr double equal1 = 7.0;
  constexpr double equal2 = 7.0;
  EXPECT_FALSE(precision.isStrictlyLesser(equal1, equal2));

  const double almostEqual1 = 7.0;
  const double almostEqual2 = 7.0 - kEpsilon / 10;
  EXPECT_FALSE(precision.isStrictlyLesser(almostEqual1, almostEqual2));
  EXPECT_FALSE(precision.isStrictlyLesser(almostEqual2, almostEqual1));
}

TEST(PrecisionTest, IsLesserOrEqual) {
  auto precision = Precision(algopt::common::thrift::PrecisionTolerances());
  constexpr double greater = 10.0;
  constexpr double lesser = 5.0;
  EXPECT_TRUE(precision.isLesserOrEqual(lesser, greater));
  EXPECT_FALSE(precision.isLesserOrEqual(greater, lesser));

  constexpr double equal1 = 7.0;
  constexpr double equal2 = 7.0;
  EXPECT_TRUE(precision.isLesserOrEqual(equal1, equal2));

  const double almostEqual1 = 7.0;
  const double almostEqual2 = 7.0 - kEpsilon / 10;
  EXPECT_TRUE(precision.isLesserOrEqual(almostEqual1, almostEqual2));
  EXPECT_TRUE(precision.isLesserOrEqual(almostEqual2, almostEqual1));
}

TEST(PrecisionTest, IsGreaterOrEqual) {
  auto precision = Precision(algopt::common::thrift::PrecisionTolerances());
  constexpr double greater = 10.0;
  constexpr double lesser = 5.0;
  EXPECT_TRUE(precision.isGreaterOrEqual(greater, lesser));
  EXPECT_FALSE(precision.isGreaterOrEqual(lesser, greater));

  constexpr double equal1 = 7.0;
  constexpr double equal2 = 7.0;
  EXPECT_TRUE(precision.isGreaterOrEqual(equal1, equal2));

  const double almostEqual1 = 7.0;
  const double almostEqual2 = 7.0 - kEpsilon / 10;
  EXPECT_TRUE(precision.isGreaterOrEqual(almostEqual1, almostEqual2));
  EXPECT_TRUE(precision.isGreaterOrEqual(almostEqual2, almostEqual1));
}

TEST(PrecisionTest, IsStrictlyGtZero) {
  auto precision = Precision(algopt::common::thrift::PrecisionTolerances());

  constexpr double positive = 5.0;
  EXPECT_TRUE(precision.isStrictlyGtZero(positive));

  constexpr double zero = 0.0;
  EXPECT_FALSE(precision.isStrictlyGtZero(zero));

  constexpr double negative = -5.0;
  EXPECT_FALSE(precision.isStrictlyGtZero(negative));

  const double smallPositive = 1e-11;
  EXPECT_FALSE(precision.isStrictlyGtZero(smallPositive));
}

TEST(PrecisionTest, CompareWithAbsoluteAndRelativeEpsilon) {
  {
    auto precision = Precision(createTolerances(1e-10, 1e-10));
    constexpr double value1 = 0;
    constexpr double value2 = 1e-11;
    EXPECT_TRUE(precision.isEqual(value1, value2));
  }

  {
    auto precision = Precision(createTolerances(1e-11, 1e-10));
    constexpr double value1 = 0;
    constexpr double value2 = 1e-11;
    EXPECT_TRUE(precision.isStrictlyLesser(value1, value2));
  }

  {
    auto precision = Precision(createTolerances(1e-10, 1e-10));
    const double value1 = 1e11;
    const double value2 = 1e11 + 1;
    EXPECT_TRUE(precision.isEqual(value1, value2));
  }

  {
    auto precision = Precision(createTolerances(1e-10, 1e-12));
    const double value1 = 1e11;
    const double value2 = 1e11 + 1;
    EXPECT_TRUE(precision.isStrictlyLesser(value1, value2));
  }
}

TEST(PrecisionTest, CompareWithDifferentEpsilons) {
  auto precision = Precision(createTolerances(1e-10, 1e-10));
  {
    constexpr double value1 = 0;
    constexpr double value2 = 1e-11;
    EXPECT_TRUE(precision.isEqual(value1, value2));
  }

  {
    auto precision2 = Precision(createTolerances(1e-11, 1e-10));
    constexpr double value1 = 0;
    constexpr double value2 = 1e-11;
    EXPECT_TRUE(precision2.isStrictlyLesser(value1, value2));
  }

  {
    auto precision3 = Precision(createTolerances(1e-11, 1e-10));
    constexpr double value1 = 1e-11;
    constexpr double value2 = 0;
    EXPECT_TRUE(precision3.isstrictlyGreater(value1, value2));
  }

  {
    auto precision4 = Precision(createTolerances(1e-10, 1e-10));
    const double value1 = 1e11;
    const double value2 = 1e11 + 1;
    EXPECT_TRUE(precision4.isEqual(value1, value2));
  }

  {
    auto precision5 = Precision(createTolerances(1e-10, 1e-12));
    const double value1 = 1e11;
    const double value2 = 1e11 + 1;
    EXPECT_TRUE(precision5.isStrictlyLesser(value1, value2));
  }

  {
    auto precision6 = Precision(createTolerances(1e-10, 1e-12));
    const double value1 = 1e11 + 1;
    const double value2 = 1e11;
    EXPECT_TRUE(precision6.isstrictlyGreater(value1, value2));
  }
}

} // namespace facebook::rebalancer::tests
