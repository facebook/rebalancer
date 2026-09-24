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

#include "algopt/rebalancer/materializer/spec_builder/GroupDiversitySpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"

#include <folly/coro/GtestHelpers.h>

namespace facebook::rebalancer::materializer::tests {

class GroupDiversitySpecBuilderTest : public SpecBuilderTestBase<> {};

CO_TEST_F(GroupDiversitySpecBuilderTest, GoalAllowsNegativePenaltyLowerBound) {
  setUpUniverse({{"host0", {"task0"}}, {"host1", {"task1"}}});
  co_await addObjectDimension(
      "positive", {{objectId("task0"), 1}, {objectId("task1"), 1}}, 0);
  co_await addPartition(
      "group", {{"group0", {"task0"}}, {"group1", {"task1"}}});
  interface::GroupDiversitySpec spec;
  spec.scope() = "host";
  spec.partition() = "group";
  spec.dimension() = "positive";
  spec.bound() = interface::GroupDiversityBound::MAX;
  spec.limit()->globalLimit() = 0;

  const GroupDiversitySpecBuilder specBuilder(buildUniverse(), spec, true);
  auto& exprBuilder = expressionBuilder();
  const auto goalInfo = co_await specBuilder.goal(exprBuilder);

  CO_ASSERT_NE(nullptr, goalInfo.penaltyExpr);
  EXPECT_LT(exprBuilder.getLowerBound(*goalInfo.penaltyExpr), 0);
}

CO_TEST_F(GroupDiversitySpecBuilderTest, NegativeDimensionPenaltyIsPreserved) {
  setUpUniverse({{"host0", {"task0"}}, {"host1", {"task1"}}});
  co_await addObjectDimension(
      "signed", {{objectId("task0"), -1}, {objectId("task1"), 1}}, 0);
  co_await addPartition("group", {{"group0", {"task0", "task1"}}});
  interface::GroupDiversitySpec spec;
  spec.scope() = "host";
  spec.partition() = "group";
  spec.dimension() = "signed";
  spec.bound() = interface::GroupDiversityBound::MAX;
  spec.limit()->globalLimit() = 0;

  const auto universe = buildUniverse();
  const GroupDiversitySpecBuilder specBuilder(universe, spec, true);
  auto& exprBuilder = expressionBuilder();
  const auto goalInfo = co_await specBuilder.goal(exprBuilder);

  CO_ASSERT_NE(nullptr, goalInfo.penaltyExpr);
  EXPECT_LT(exprBuilder.getLowerBound(*goalInfo.penaltyExpr), 0);
}

} // namespace facebook::rebalancer::materializer::tests
