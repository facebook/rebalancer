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
#include "algopt/rebalancer/materializer/spec_builder/SpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"

#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

#include <limits>

namespace facebook::rebalancer::materializer::tests {

class SpecBuilderTest : public SpecBuilderTestBase<> {
 protected:
  void setUpTestUniverse() {
    setUpUniverse({{"host0", {"task0"}}});
  }
};

CO_TEST_F(SpecBuilderTest, SeparatedPenaltyUsesViolationFloor) {
  setUpTestUniverse();
  const auto universe = buildUniverse();
  const auto constraints = std::vector<ConstraintInfo>{
      {boundsOverride(const_expr(1, *universe), 1, 2),
       const_expr(7, *universe)},
      {boundsOverride(const_expr(2, *universe), 1, 2),
       const_expr(7, *universe)}};

  const auto goalInfo = SpecBuilder::getSeparatedConstraintViolation(
      constraints, expressionBuilder(), *universe);
  EXPECT_NEAR(3, evaluate(goalInfo.objectiveExpr, deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(7, evaluate(goalInfo.penaltyExpr, deltaFromInitial({})), 1e-8);
  co_return;
}

CO_TEST_F(SpecBuilderTest, NegativeConstraintLowerBoundUsesZeroViolationFloor) {
  setUpTestUniverse();
  const auto universe = buildUniverse();
  const auto constraints = std::vector<ConstraintInfo>{
      {boundsOverride(const_expr(0, *universe), -1, 1),
       const_expr(7, *universe)},
      {boundsOverride(const_expr(1, *universe), -1, 1),
       const_expr(7, *universe)}};

  const auto goalInfo = SpecBuilder::getSeparatedConstraintViolation(
      constraints, expressionBuilder(), *universe);
  EXPECT_NEAR(1, evaluate(goalInfo.objectiveExpr, deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(7, evaluate(goalInfo.penaltyExpr, deltaFromInitial({})), 1e-8);
  co_return;
}

CO_TEST_F(SpecBuilderTest, NegativePenaltyIsRejectedByDefault) {
  setUpTestUniverse();
  const auto universe = buildUniverse();
  const auto constraints = std::vector<ConstraintInfo>{
      {boundsOverride(const_expr(1, *universe), 0.0, 1.0),
       const_expr(-1, *universe)}};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      (void)SpecBuilder::getSeparatedConstraintViolation(
          constraints, expressionBuilder(), *universe),
      "Additional penalty expression has invalid lower bound -1");
  co_return;
}

CO_TEST_F(SpecBuilderTest, NegativePenaltyIsAcceptedWhenUnrestricted) {
  setUpTestUniverse();
  const auto universe = buildUniverse();
  const auto constraints = std::vector<ConstraintInfo>{
      {boundsOverride(const_expr(1, *universe), 0.0, 1.0),
       const_expr(-1, *universe)}};

  const auto goalInfo = SpecBuilder::getSeparatedConstraintViolation(
      constraints, expressionBuilder(), *universe, ValueRequirement::NONE);
  EXPECT_NE(nullptr, goalInfo.penaltyExpr);
  EXPECT_NEAR(-1, evaluate(goalInfo.penaltyExpr, deltaFromInitial({})), 1e-8);
  co_return;
}

CO_TEST_F(SpecBuilderTest, InfinitePenaltyUpperBoundIsAccepted) {
  setUpTestUniverse();
  const auto universe = buildUniverse();
  const auto constraints = std::vector<ConstraintInfo>{
      {boundsOverride(const_expr(1, *universe), 0.0, 1.0),
       boundsOverride(
           const_expr(1, *universe),
           0.0,
           std::numeric_limits<double>::infinity())}};

  const auto goalInfo = SpecBuilder::getSeparatedConstraintViolation(
      constraints, expressionBuilder(), *universe);
  EXPECT_NEAR(1, evaluate(goalInfo.penaltyExpr, deltaFromInitial({})), 1e-8);
  co_return;
}

CO_TEST_F(SpecBuilderTest, UnsetConstraintIsRejected) {
  setUpTestUniverse();
  const auto universe = buildUniverse();
  const auto constraints =
      std::vector<ConstraintInfo>{{nullptr, const_expr(7, *universe)}};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      (void)SpecBuilder::getSeparatedConstraintViolation(
          constraints, expressionBuilder(), *universe),
      "Constraint expression is not set");
  co_return;
}

} // namespace facebook::rebalancer::materializer::tests
