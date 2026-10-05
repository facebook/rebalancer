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
#include "algopt/rebalancer/algopt_common/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/solver/utils/ContainerPotential.h"
#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"
#include "algopt/rebalancer/solver/utils/Precision.h"

#include "gmock/gmock.h"
#include <gtest/gtest.h>

#include <string>

using namespace std;
using namespace facebook::rebalancer;

namespace {
Precision createPrecision() {
  facebook::algopt::common::thrift::PrecisionTolerances tolerances;
  return Precision(tolerances);
}
} // namespace

TEST(ContainerPotentialTest, BasicComparisonTest) {
  const auto precision = createPrecision();
  // potential1 < potential2 based on contributionToGoal
  const ContainerPotential potential1(
      GlobalObjectiveValue({0, -1, 2}), 1, precision);
  const ContainerPotential potential2(
      GlobalObjectiveValue({0, 1, 2}), 1, precision);
  EXPECT_EQ(
      ContainerPotential::precisionCompare(potential1, potential2, precision),
      -1);

  // potential3 < potential4 since although they are equal based on
  // contributionToGoal, potential3 has lesser objects than potential4
  const ContainerPotential potential3(
      GlobalObjectiveValue({0, 1, 2}), 1, precision);
  const ContainerPotential potential4(
      GlobalObjectiveValue({0, 1, 2}), 10, precision);
  EXPECT_EQ(
      ContainerPotential::precisionCompare(potential3, potential4, precision),
      -1);

  // potential5 < potential6 based on contributionToGoal
  const ContainerPotential potential5(
      GlobalObjectiveValue({-1, 1, 2}), 1, precision);
  const ContainerPotential potential6(
      GlobalObjectiveValue({0, -100, 2}), 1, precision);
  EXPECT_EQ(
      ContainerPotential::precisionCompare(potential5, potential6, precision),
      -1);

  // potential7 == potential8, since they are equivalent w.r.t. to both
  // contributionToGoal and nObjects
  const ContainerPotential potential7(
      GlobalObjectiveValue({0, 1, 2}), 1, precision);
  const ContainerPotential potential8(
      GlobalObjectiveValue({0, 1, 2}), 1, precision);
  EXPECT_EQ(
      ContainerPotential::precisionCompare(potential7, potential8, precision),
      0);

  // potential9 > potential10, since although they are equivalent w.r.t. to
  // contributionToGoal, potential9 has more objects
  const ContainerPotential potential9(
      GlobalObjectiveValue({0, 1, 2}), 100, precision);
  const ContainerPotential potential10(
      GlobalObjectiveValue({0, 1, 2}), 1, precision);
  EXPECT_EQ(
      ContainerPotential::precisionCompare(potential9, potential10, precision),
      1);

  // potential11 > potential12, based on contributionToGoal
  const ContainerPotential potential11(
      GlobalObjectiveValue({0, 1, 20}), 1, precision);
  const ContainerPotential potential12(
      GlobalObjectiveValue({0, 1, 2}), 100, precision);
  EXPECT_EQ(
      ContainerPotential::precisionCompare(potential11, potential12, precision),
      1);
}

TEST(ContainerPotentialTest, BasicAdditionTest) {
  const auto precision = createPrecision();
  // raise an error if the objective values have different length
  ContainerPotential potential1(GlobalObjectiveValue({0, 1}), 8, precision);
  ContainerPotential potential2(GlobalObjectiveValue({0}), 14, precision);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      potential1 + potential2,
      "addition of GlobalObjective values can only be done if both have the same length");

  // check addition result of potential3 + potential4
  const ContainerPotential potential3(
      GlobalObjectiveValue({-1, 2, 9, -13, 99, -7}), 1, precision);
  const ContainerPotential potential4(
      GlobalObjectiveValue({1, 2, 3, 89, -4, -67}), 6, precision);

  auto computedSum = potential3 + potential4;
  const ContainerPotential expectedSum(
      GlobalObjectiveValue({0, 4, 12, 76, 95, -74}), 7, precision);

  EXPECT_EQ(
      ContainerPotential::precisionCompare(computedSum, expectedSum, precision),
      0);
}
