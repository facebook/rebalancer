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
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/materializer/spec_builder/CapacityWithGroupPresenceSpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"

#include <folly/container/irange.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace entities = facebook::rebalancer::entities;
namespace interface = facebook::rebalancer::interface;

namespace facebook::rebalancer::materializer::tests {

namespace {
struct ConstraintAndPenaltyValue {
  double constraintValue = 0.0;
  std::optional<double> penaltyValue = std::nullopt;
};

struct ExpectedInfo {
  std::vector<ConstraintAndPenaltyValue> constraintAndPenaltyValues;
  double goalValue = 0.0;
};

#define VERIFY_CONSTRAINT_COMPONENTS_AND_GOAL_VALUES(                                                                       \
    expectedInfo, constraintComponents, localSearchGoalExpr, assignment)                                                    \
  do {                                                                                                                      \
    const auto& testIntent = apache::thrift::util::enumNameSafe(GetParam());                                                \
    const packer::tests::LpAssertOptions lpAssertOptions = {                                                                \
        .exceptionForLpExpr = GetParam() ==                                                                                 \
                interface::CapacityWithGroupPresenceUsageIntent::                                                           \
                    PER_SCOPE_ITEM                                                                                          \
            ? "LP expressions are not yet implemented for ObjectPartitionWithMinPresence"                                   \
            : "",                                                                                                           \
        .lpTolerances =                                                                                                     \
            algopt::lp::Tolerances{.constraint = 1e-7, .integer = 1e-6}};                                                   \
    for (const auto i :                                                                                                     \
         folly::irange(expectedInfo.constraintAndPenaltyValues.size())) {                                                   \
      const auto expectedConstraintValue =                                                                                  \
          expectedInfo.constraintAndPenaltyValues[i].constraintValue;                                                       \
      const auto actualConstraintValue = evaluate(                                                                          \
          constraintComponents[i].constraintExpr,                                                                           \
          assignment,                                                                                                       \
          lpAssertOptions);                                                                                                 \
      EXPECT_NEAR(expectedConstraintValue, actualConstraintValue, 1e-8)                                                     \
          << fmt::format(                                                                                                   \
                 "mismatch between expected constraint value {} and actual {} w.r.t. component index {} | GetParam() = {}", \
                 expectedConstraintValue,                                                                                   \
                 actualConstraintValue,                                                                                     \
                 i,                                                                                                         \
                 testIntent);                                                                                               \
                                                                                                                            \
      if (expectedInfo.constraintAndPenaltyValues[i]                                                                        \
              .penaltyValue.has_value()) {                                                                                  \
        const auto expectedPenaltyValue =                                                                                   \
            expectedInfo.constraintAndPenaltyValues[i].penaltyValue.value();                                                \
        const auto actualPenaltyValue = evaluate(                                                                           \
            constraintComponents[i].additionalPenaltyExpr,                                                                  \
            assignment,                                                                                                     \
            lpAssertOptions);                                                                                               \
        EXPECT_NEAR(expectedPenaltyValue, actualPenaltyValue, 1e-8) << fmt::format(                                         \
            "mismatch between expected penalty value {} and actual {} w.r.t. component index {} | GetParam() = {}",         \
            expectedPenaltyValue,                                                                                           \
            actualPenaltyValue,                                                                                             \
            i,                                                                                                              \
            testIntent);                                                                                                    \
      } else {                                                                                                              \
        EXPECT_TRUE(constraintComponents[i].additionalPenaltyExpr == nullptr);                                              \
      }                                                                                                                     \
    }                                                                                                                       \
    EXPECT_NEAR(                                                                                                            \
        expectedInfo.goalValue,                                                                                             \
        evaluate(localSearchGoalExpr, assignment, lpAssertOptions),                                                         \
        1e-8);                                                                                                              \
  } while (0)

} // namespace

class CapacityWithGroupPresenceSpecBuilderTest
    : public SpecBuilderTestBase<
          interface::CapacityWithGroupPresenceUsageIntent> {
 protected:
  // Scale factors for the default NORMALIZED_CONTINUOUS_UTILIZATION penalty
  // (= penaltyBound / (numObjects * maxDimValue)). Fixture has 10 objects
  // (tenant1=7, tenant2=3) and maxDimValue = 1.88. penaltyBound = 0.5 when
  // roundUp=true and 0.5 * 0.115 (minPositiveDimValue) when roundUp=false.
  static constexpr double kMaxDimValue = 1.88;
  static constexpr double kNormPerScopeItem = 0.5 / (10 * kMaxDimValue);
  static constexpr double kNormPerScopeItemNoRoundUp =
      0.5 * 0.115 / (10 * kMaxDimValue);
  static constexpr double kNormTenant1 = 0.5 / (7 * kMaxDimValue);
  static constexpr double kNormTenant2 = 0.5 / (3 * kMaxDimValue);
  static constexpr double kNormTenant1NoRoundUp =
      0.5 * 0.115 / (7 * kMaxDimValue);
  static constexpr double kNormTenant2NoRoundUp =
      0.5 * 0.115 / (3 * kMaxDimValue);

  folly::coro::Task<void> setUpCoro() {
    setUpUniverse(
        {
            {"host1", {"trafficObject8"}},
            {"host2", {"trafficObject1", "trafficObject5", "trafficObject9"}},
            {"host3", {"trafficObject2", "trafficObject6"}},
            {"host4", {"trafficObject3"}},
            {"host5", {"trafficObject4", "trafficObject7"}},
            {"host6", {"trafficObject10"}},
        },
        "trafficObject",
        "host");

    // host6 is not part of any region
    co_await addScope(
        "region",
        {{"region1", {"host1", "host2"}},
         {"region2", {"host3", "host4", "host5"}}});

    // dynamic objectDimension scoped by region
    const entities::Map<entities::ObjectId, double> baseObjectToValue = {
        {objectId("trafficObject1"), 1.85},
        {objectId("trafficObject2"), 0.4},
        {objectId("trafficObject3"), 0.6},
        {objectId("trafficObject4"), 0.5},
        {objectId("trafficObject5"), 0.13},
        {objectId("trafficObject6"), 0.115},
        {objectId("trafficObject7"), 0.88},
        {objectId("trafficObject8"), 1},
        {objectId("trafficObject9"), 1.2},
        {objectId("trafficObject10"), 0.3},
    };

    // only difference between object dimension values for region1 and region2
    // is the value of trafficObject7
    auto region2DimensionValues = baseObjectToValue;
    region2DimensionValues[objectId("trafficObject7")] = 1.88;

    co_await addDynamicObjectDimension(
        "replicaCount",
        scopeId("region"),
        {{"region1", makeSharedPtrEntityToValueMap(baseObjectToValue)},
         {"region2", makeSharedPtrEntityToValueMap(region2DimensionValues)}},
        0.0);

    co_await addPartition(
        "tenantTrafficObjects",
        {{"tenant1-trafficObjects",
          {"trafficObject1",
           "trafficObject2",
           "trafficObject3",
           "trafficObject4",
           "trafficObject5",
           "trafficObject9",
           "trafficObject10"}},
         {"tenant2-trafficObjects",
          {"trafficObject6", "trafficObject7", "trafficObject8"}}});

    co_await addPartition(
        "tenantGroups",
        {{"allTenants-trafficObjects",
          {"trafficObject1",
           "trafficObject2",
           "trafficObject3",
           "trafficObject4",
           "trafficObject5",
           "trafficObject6",
           "trafficObject7",
           "trafficObject8",
           "trafficObject9",
           "trafficObject10"}}});

    co_return;
  }

  void SetUp() override {
    folly::coro::blockingWait(setUpCoro());
  }
};

INSTANTIATE_TEST_CASE_P(
    PER_SCOPE_ITEM,
    CapacityWithGroupPresenceSpecBuilderTest,
    ::testing::Values(
        interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM));

INSTANTIATE_TEST_CASE_P(
    PER_GROUP_AND_SCOPE_ITEM,
    CapacityWithGroupPresenceSpecBuilderTest,
    ::testing::Values(
        interface::CapacityWithGroupPresenceUsageIntent::
            PER_GROUP_AND_SCOPE_ITEM));

CO_TEST_P(CapacityWithGroupPresenceSpecBuilderTest, WithRoundUpAndMaxBound) {
  interface::CapacityWithGroupPresenceSpec spec;
  spec.dimension() = "replicaCount";
  spec.partition() = "tenantTrafficObjects";
  spec.scope() = "region";
  spec.roundUpGroupUtilOnScopeItem() = true;
  spec.intent() = GetParam();

  auto& scopeItemLimits = *spec.scopeItemToLimit();
  scopeItemLimits.type() = interface::LimitType::ABSOLUTE;
  scopeItemLimits.globalLimit() = 5;
  scopeItemLimits.scopeItemLimits() = {{"region1", 2}};
  if (GetParam() ==
      interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM) {
    scopeItemLimits.scopeItemToGroupLimits() = {
        {"region2", {{"tenant1-trafficObjects", 1}}},
    };
  }

  auto& groupToPresenceWeight = *spec.groupToPresenceWeight();
  groupToPresenceWeight.globalLimit() = 2;
  groupToPresenceWeight.groupLimits() = {{"tenant1-trafficObjects", 3}};

  const auto universe = buildUniverse();
  auto& builder = expressionBuilder();

  {
    // local search constraint and goal
    const CapacityWithGroupPresenceSpecBuilder localSearchSpecBuilder(
        universe, spec, true);
    auto localSearchComponents =
        co_await localSearchSpecBuilder.constraints(builder);

    assertConstraintViolationBounds(localSearchComponents);

    // local search goal
    const CapacityWithGroupPresenceSpecBuilder localSearchGoalSpecBuilder(
        universe, spec, true);
    auto localSearchGoalExpr =
        co_await localSearchGoalSpecBuilder.goalCoro(builder);

    // verify expected number of components
    switch (GetParam()) {
      case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
        //  2 constraints (one per scope item)
        EXPECT_EQ(2, localSearchComponents.size());
        break;
      }
      case interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM: {
        // / 4 constraints (one per group-scope item pair)
        EXPECT_EQ(4, localSearchComponents.size());
        break;
      }
    }

    // verify initial values
    const auto initial = deltaFromInitial({});
    ExpectedInfo initialExpectedInfo;
    switch (GetParam()) {
      case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
        // 2 constraints (one per scope item)
        initialExpectedInfo.constraintAndPenaltyValues = {
            // Region1 component (limit of region1 = 2)
            // tenant1-trafficObject's non-rounded-up contribution to region1 is
            // = max(1.85+0.13+1.2, 3) = 3.18
            // with roundUp = ceil(1.85+0.13+1.2) = ceil(3.18) = 4
            // tenant2-trafficObject's non-rounded-up contribution to region1 is
            // = max(1, 2) = 2
            // with roundUp = ceil(2) = 2
            // raw continuous util (without roundUp/presenceWeight) = 3.18
            // + 1.0;
            // stored value scales this by kNormPerScopeItem.
            {.constraintValue = 4.0 + 2.0 - 2.0,
             .penaltyValue = (3.18 + 1.0) * kNormPerScopeItem},
            // Region2 component (limit of region2 = 5)
            // tenant1-trafficObject's non-rounded-up contribution to region2 is
            // = max(0.4+0.6+0.5, 3) = 3.0
            // with roundUp = ceil(3.0) = 3.0
            // tenant2-trafficObject's non-rounded-up contribution to region2 is
            // = max(0.115+1.88, 2) = 2
            // with roundUp = ceil(2) = 2
            // raw continuous util (without roundUp/presenceWeight) = 1.5
            // + 1.995;
            // stored value scales this by kNormPerScopeItem.
            {.constraintValue = 3.0 + 2.0 - 5.0,
             .penaltyValue = (1.5 + 1.995) * kNormPerScopeItem},
        };

        // goal value is the sum of max(0, constExpr) + step(constExpr) *
        // additionalPenaltyExpr per constraint. Only region1 is broken; its
        // constraint (=4) and scaled penalty ((3.18 + 1.0) * kNormPerScopeItem)
        // contribute.
        initialExpectedInfo.goalValue = 4.0 + (3.18 + 1.0) * kNormPerScopeItem;
        break;
      }
      case interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM: {
        // 4 constraints (one per group-scope item pair)
        // (tenant1,region1) constr: ceil(max(3.18,3)) - limit(2) = 4-2 = 2
        // (tenant2,region1) constr: ceil(max(1,2)) - limit(2) = 2-2 = 0
        // (tenant1,region2) constr: ceil(max(1.5,3)) - limit(1) = 3-1 = 2
        // (tenant2,region2) constr: ceil(max(1.995,2)) - limit(5) = 2-5 = -3
        initialExpectedInfo.constraintAndPenaltyValues = {
            {.constraintValue = 2.0, .penaltyValue = 3.18 * kNormTenant1},
            {.constraintValue = 0.0,
             .penaltyValue = 1.0 * kNormTenant2}, // not broken
            {.constraintValue = 2.0, .penaltyValue = 1.5 * kNormTenant1},
            {.constraintValue = -3.0,
             .penaltyValue = 1.995 * kNormTenant2}, // not broken
        };

        initialExpectedInfo.goalValue =
            (2 + 3.18 * kNormTenant1) + (2 + 1.5 * kNormTenant1);
        break;
      }
    }
    VERIFY_CONSTRAINT_COMPONENTS_AND_GOAL_VALUES(
        initialExpectedInfo,
        localSearchComponents,
        localSearchGoalExpr,
        initial);

    auto delta1 = deltaFromInitial({{"trafficObject5", "host4"}});
    ExpectedInfo delta1ExpectedInfo;
    switch (GetParam()) {
      case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
        // 2 constraints (one per scope item)
        delta1ExpectedInfo.constraintAndPenaltyValues = {
            // Region1 component (limit of region1 = 2)
            // tenant1-trafficObject's non-rounded-up contribution to region1 is
            // = max(1.85 + 1.2, 3) = 3.05 (trafficObject5 moved from host2 to
            // host4)
            // with roundUp = ceil(3.05) = 4
            // tenant2-trafficObject's non-rounded-up contribution to region1 is
            // = max(1, 2) = 2
            // with roundUp = ceil(2) = 2
            // raw continuous util (without roundUp/presenceWeight) = 3.05
            // + 1.0;
            // stored value scales this by kNormPerScopeItem.
            {.constraintValue = 4.0 + 2.0 - 2.0,
             .penaltyValue = (3.05 + 1.0) * kNormPerScopeItem},
            // Region2 component (limit of region2 = 5)
            // tenant1-trafficObject's non-rounded-up contribution to region2 is
            // = max(0.4+0.6+0.5+0.13, 3) = 1.63
            // with roundUp = ceil(1.63) = 2
            // tenant2-trafficObject's non-rounded-up contribution to region2 is
            // = max(0.115+1.88, 2) = 2
            // with roundUp = ceil(2) = 2
            // raw continuous util (without roundUp/presenceWeight) = 1.63
            // + 1.995;
            // stored value scales this by kNormPerScopeItem.
            {.constraintValue = 3.0 + 2.0 - 5.0,
             .penaltyValue = (1.63 + 1.995) * kNormPerScopeItem},
        };

        // goal value is the sum of max(0, constExpr) + step(constExpr) *
        // additionalPenaltyExpr per constraint. Only region1 is broken; its
        // constraint (=4) and scaled penalty ((3.05 + 1.0) * kNormPerScopeItem)
        // contribute.
        delta1ExpectedInfo.goalValue = 4.0 + (3.05 + 1.0) * kNormPerScopeItem;
        break;
      }
      case interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM: {
        // 4 constraints (one per group-scope item pair)
        // Similar calculations as initial but with trafficObject5 moved
        delta1ExpectedInfo.constraintAndPenaltyValues = {
            {.constraintValue = 2.0, .penaltyValue = 3.05 * kNormTenant1},
            {.constraintValue = 0.0,
             .penaltyValue = 1.0 * kNormTenant2}, // not broken
            {.constraintValue = 2.0, .penaltyValue = 1.63 * kNormTenant1},
            {.constraintValue = -3.0,
             .penaltyValue = 1.995 * kNormTenant2}, // not broken
        };

        delta1ExpectedInfo.goalValue =
            (2 + 3.05 * kNormTenant1) + (2 + 1.63 * kNormTenant1);
        break;
      }
    }
    VERIFY_CONSTRAINT_COMPONENTS_AND_GOAL_VALUES(
        delta1ExpectedInfo, localSearchComponents, localSearchGoalExpr, delta1);

    // remove tenant1-trafficObjects from region1 and move trafficObject7 to
    // host1
    auto delta2 = deltaFromInitial(
        {{"trafficObject1", "host4"},
         {"trafficObject5", "host5"},
         {"trafficObject9", "host5"},
         {"trafficObject7", "host1"}});
    ExpectedInfo delta2ExpectedInfo;
    switch (GetParam()) {
      case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
        // 2 constraints (one per scope item)
        delta2ExpectedInfo.constraintAndPenaltyValues = {
            // Region1 component (limit of region1 = 2)
            // tenant1-trafficObject's non-rounded-up contribution to region1 is
            // = 0
            // (all moved to region2)
            // with roundUp = ceil(0) = 0
            // tenant2-trafficObject's non-rounded-up contribution to region1 is
            // = max(1+0.88, 2) = 2 (trafficObject7 moved to host1)
            // with roundUp = ceil(2) = 2
            // raw continuous util (without roundUp/presenceWeight) = 0 + 1.88;
            // stored value scales this by kNormPerScopeItem.
            {.constraintValue = 0.0 + 2.0 - 2.0,
             .penaltyValue = (0 + 1.88) * kNormPerScopeItem},
            // Region2 component (limit of region2 = 5)
            // tenant1-trafficObject's non-rounded-up contribution to region2 is
            // = max(1.85+0.4+0.6+0.5+0.13+1.2, 3) = 4.68 (all moved here)
            // with roundUp = ceil(4.68) = 5
            // tenant2-trafficObject's non-rounded-up contribution to region2 is
            // = max(0.115, 2) = 2 (trafficObject7 moved away)
            // with roundUp = ceil(2) = 2
            // raw continuous util (without roundUp/presenceWeight) = 4.68 +
            // 0.115;
            // stored value scales this by kNormPerScopeItem.
            {.constraintValue = 5.0 + 2.0 - 5.0,
             .penaltyValue = (4.68 + 0.115) * kNormPerScopeItem},
        };

        // goal value is the sum of max(0, constExpr) + step(constExpr) *
        // additionalPenaltyExpr per constraint. Only region2 is broken; its
        // constraint (=2) and scaled penalty ((4.68 + 0.115) *
        // kNormPerScopeItem) contribute.
        delta2ExpectedInfo.goalValue = 2.0 + (4.68 + 0.115) * kNormPerScopeItem;
        break;
      }
      case interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM: {
        // 4 constraints (one per group-scope item pair)
        // Similar calculations as above but for individual group-scope pairs
        delta2ExpectedInfo.constraintAndPenaltyValues = {
            // tenant1,region1: not broken
            {.constraintValue = 0.0 - 2.0, .penaltyValue = 0.0 * kNormTenant1},
            // tenant2,region1: not broken
            {.constraintValue = 2.0 - 2.0, .penaltyValue = 1.88 * kNormTenant2},
            // tenant1,region2 (note limit is 1.0)
            {.constraintValue = 5 - 1.0, .penaltyValue = 4.68 * kNormTenant1},
            // tenant2,region2: not broken
            {.constraintValue = 2.0 - 5.0,
             .penaltyValue = 0.115 * kNormTenant2},
        };

        delta2ExpectedInfo.goalValue = 4.0 + 4.68 * kNormTenant1;
        break;
      }
    }
    VERIFY_CONSTRAINT_COMPONENTS_AND_GOAL_VALUES(
        delta2ExpectedInfo, localSearchComponents, localSearchGoalExpr, delta2);
  }

  {
    // for the same example with optimal solver, just check that
    // additionalPenlty is nullptr and constraintExpr have the same values as
    // with localSearch
    const CapacityWithGroupPresenceSpecBuilder optimalSolverSpecBuilder(
        universe, spec, false);
    auto optimalSolverComponents =
        co_await optimalSolverSpecBuilder.constraints(builder);

    auto optimalSolverGoalExpr =
        co_await optimalSolverSpecBuilder.goalCoro(builder);

    auto initial = deltaFromInitial({});
    ExpectedInfo optimalSolverExpectedInfo;
    switch (GetParam()) {
      case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
        // 2 constraints (one per scope item)
        // For optimal solver, additionalPenaltyExpr should be nullptr
        // but constraintExpr should have same values as localSearch
        EXPECT_EQ(2, optimalSolverComponents.size());
        optimalSolverExpectedInfo.constraintAndPenaltyValues = {
            // Region1 component - same constraint value as localSearch initial
            {.constraintValue = 4.0 + 2.0 - 2.0, .penaltyValue = std::nullopt},
            // Region2 component - same constraint value as localSearch initial
            {.constraintValue = 3.0 + 2.0 - 5.0, .penaltyValue = std::nullopt},
        };

        // goal value is just the sum of constraint expressions
        optimalSolverExpectedInfo.goalValue =
            (4.0 + 2.0 - 2.0) + (3.0 + 2.0 - 5.0);
        break;
      }
      case interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM: {
        // 4 constraints (one per group-scope item pair)
        // For optimal solver, additionalPenaltyExpr should be nullptr
        EXPECT_EQ(4, optimalSolverComponents.size());
        optimalSolverExpectedInfo.constraintAndPenaltyValues = {
            {.constraintValue = 2.0, .penaltyValue = std::nullopt},
            {.constraintValue = 0.0, .penaltyValue = std::nullopt},
            {.constraintValue = 2.0, .penaltyValue = std::nullopt},
            {.constraintValue = -3.0, .penaltyValue = std::nullopt},
        };

        optimalSolverExpectedInfo.goalValue = 2.0 + 0.0 + 2.0 + 0.0;
        break;
      }
    }
    VERIFY_CONSTRAINT_COMPONENTS_AND_GOAL_VALUES(
        optimalSolverExpectedInfo,
        optimalSolverComponents,
        optimalSolverGoalExpr,
        initial);
  }
}

// always-present: for (group, scope item) pairs listed in always-present, the
// presence floor is honored even when the group has zero actual utilization in
// that scope item (step(util) is forced to 1). Verifies the optimal-solver path
// (getGroupUtilContributionToScopeItemUtil), with a side-by-side against a spec
// that omits always-present. roundUp=false and all limits=0 so each constraint
// value equals the computed utilization.
CO_TEST_P(CapacityWithGroupPresenceSpecBuilderTest, AlwaysPresentHonorsFloor) {
  auto makeSpec = [&](bool withAlwaysPresent) {
    interface::CapacityWithGroupPresenceSpec spec;
    spec.dimension() = "replicaCount";
    spec.partition() = "tenantTrafficObjects";
    spec.scope() = "region";
    spec.roundUpGroupUtilOnScopeItem() = false;
    spec.intent() = GetParam();
    spec.scopeItemToLimit()->type() = interface::LimitType::ABSOLUTE;
    spec.scopeItemToLimit()->globalLimit() = 0;
    spec.groupToPresenceWeight()->globalLimit() = 2;
    spec.groupToPresenceWeight()->groupLimits() = {
        {"tenant1-trafficObjects", 3}};
    if (withAlwaysPresent) {
      // Force presence only for (region1, tenant2). The floor magnitude comes
      // from groupToPresenceWeight (2.0).
      spec.scopeItemToAlwaysPresentGroups() = {
          {"region1", {"tenant2-trafficObjects"}}};
    }
    return spec;
  };

  const auto universe = buildUniverse();
  auto& builder = expressionBuilder();

  // Move tenant2's only region1 object (trafficObject8, host1) out to region2
  // (host3) so tenant2 has zero actual util in region1.
  auto postMove = deltaFromInitial({{"trafficObject8", "host3"}});

  // After the move:
  //   region1: tenant1 util = max(1.85+0.13+1.2, 3.0) = 3.18; tenant2 util = 0
  //   region2: tenant1 util = max(0.4+0.6+0.5, 3.0) = 3.0;
  //            tenant2 util = max(0.115+1.88+1.0, 2.0) = 2.995
  // tenant2's region1 contribution is the only value always-present changes:
  //   without always-present -> step(0) gates it to 0
  //   with always-present    -> presence floor 2.0 is honored
  const CapacityWithGroupPresenceSpecBuilder withoutFp(
      universe,
      makeSpec(/*withAlwaysPresent=*/false),
      /*needsContinuousExpressions=*/false);
  const CapacityWithGroupPresenceSpecBuilder withFp(
      universe,
      makeSpec(/*withAlwaysPresent=*/true),
      /*needsContinuousExpressions=*/false);
  auto componentsWithout = co_await withoutFp.constraints(builder);
  auto componentsWith = co_await withFp.constraints(builder);
  auto goalWithout = co_await withoutFp.goalCoro(builder);
  auto goalWith = co_await withFp.goalCoro(builder);

  ExpectedInfo expectedWithout;
  ExpectedInfo expectedWith;
  switch (GetParam()) {
    case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
      // 2 components: [region1, region2]. Optimal solver -> penalty is nullptr.
      expectedWithout.constraintAndPenaltyValues = {
          {.constraintValue = 3.18,
           .penaltyValue = std::nullopt}, // tenant2 -> 0
          {.constraintValue = 5.995, .penaltyValue = std::nullopt},
      };
      expectedWithout.goalValue = 3.18 + 5.995;
      expectedWith.constraintAndPenaltyValues = {
          {.constraintValue = 5.18, .penaltyValue = std::nullopt}, // +2.0 floor
          {.constraintValue = 5.995, .penaltyValue = std::nullopt},
      };
      expectedWith.goalValue = 5.18 + 5.995;
      break;
    }
    case interface::CapacityWithGroupPresenceUsageIntent::
        PER_GROUP_AND_SCOPE_ITEM: {
      // 4 components: (r1,t1),(r1,t2),(r2,t1),(r2,t2).
      expectedWithout.constraintAndPenaltyValues = {
          {.constraintValue = 3.18, .penaltyValue = std::nullopt},
          {.constraintValue = 0.0,
           .penaltyValue = std::nullopt}, // tenant2 -> 0
          {.constraintValue = 3.0, .penaltyValue = std::nullopt},
          {.constraintValue = 2.995, .penaltyValue = std::nullopt},
      };
      expectedWithout.goalValue = 3.18 + 0.0 + 3.0 + 2.995;
      expectedWith.constraintAndPenaltyValues = {
          {.constraintValue = 3.18, .penaltyValue = std::nullopt},
          {.constraintValue = 2.0,
           .penaltyValue = std::nullopt}, // floor honored
          {.constraintValue = 3.0, .penaltyValue = std::nullopt},
          {.constraintValue = 2.995, .penaltyValue = std::nullopt},
      };
      expectedWith.goalValue = 3.18 + 2.0 + 3.0 + 2.995;
      break;
    }
  }

  VERIFY_CONSTRAINT_COMPONENTS_AND_GOAL_VALUES(
      expectedWithout, componentsWithout, goalWithout, postMove);
  VERIFY_CONSTRAINT_COMPONENTS_AND_GOAL_VALUES(
      expectedWith, componentsWith, goalWith, postMove);
}

CO_TEST_P(
    CapacityWithGroupPresenceSpecBuilderTest,
    MaxBoundTypeContinuousPenaltyGatedAtLowerBound) {
  if (GetParam() !=
      interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM) {
    co_return;
  }

  interface::CapacityWithGroupPresenceSpec spec;
  spec.dimension() = "replicaCount";
  spec.partition() = "tenantTrafficObjects";
  spec.scope() = "region";
  spec.roundUpGroupUtilOnScopeItem() = false;
  spec.bound() = interface::CapacityWithGroupPresenceBound::MAX;
  spec.intent() = GetParam();
  spec.scopeItemToLimit()->type() = interface::LimitType::ABSOLUTE;
  spec.scopeItemToLimit()->globalLimit() = 0; // MAX limit 0 -> always broken
  // Presence floor of 1.0 for every group; force tenant2 always-present in
  // region1 so its contribution has a static lower bound of 1.0 there.
  spec.groupToPresenceWeight()->globalLimit() = 1.0;
  spec.scopeItemToAlwaysPresentGroups() = {
      {"region1", {"tenant2-trafficObjects"}}};

  const auto universe = buildUniverse();
  auto& builder = expressionBuilder();
  const CapacityWithGroupPresenceSpecBuilder specBuilder(
      universe, spec, /*needsContinuousExpressions=*/true);
  auto components = co_await specBuilder.constraints(builder);

  EXPECT_EQ(4, components.size());
  // Component order is (region1,tenant1),(region1,tenant2),(region2,tenant1),
  // (region2,tenant2); index 1 is the always-present (region1, tenant2) group.
  const auto& r1t2 = components[1];
  EXPECT_TRUE(r1t2.additionalPenaltyExpr != nullptr);

  // Pinned at the lower bound with NONZERO raw util: keep tenant2 present in
  // region1 but below the floor. Move its only region1 object (trafficObject8)
  // out to region2 and bring trafficObject6 (value 0.115) in. tenant2's actual
  // util in region1 is then 0.115 < floor, so finalUtil = max(floor 1.0, 0.115)
  // = 1.0 = its static lower bound -- the contribution cannot be reduced
  // further. Because the raw util (0.115) is nonzero, the penalty is 0 only
  // because the gate fires; an ungated penalty would be 0.115 * normFactor.
  // This is what makes the assertion actually guard the gate.
  auto atLowerBound = deltaFromInitial(
      {{"trafficObject8", "host3"}, {"trafficObject6", "host2"}});
  EXPECT_NEAR(
      0.0,
      evaluate(r1t2, atLowerBound, /*evaluateConstraintExpr=*/false),
      1e-8);

  // Above the lower bound: bring trafficObject6 into region1 (host2) so
  // tenant2's actual util in region1 is 1.0 + 0.115 = 1.115 > floor. finalUtil
  // = 1.115 > lower bound, so the penalty is active (normalized raw util).
  auto aboveLowerBound = deltaFromInitial({{"trafficObject6", "host2"}});
  EXPECT_NEAR(
      1.115 * kNormTenant2NoRoundUp,
      evaluate(r1t2, aboveLowerBound, /*evaluateConstraintExpr=*/false),
      1e-8);
}

// Fused-path (optimized PER_SCOPE_ITEM) counterpart of the MAX gate. Here the
// per-scope-item util is built by ObjectPartitionLookupWithMinPresence, which
// sums each group's penalty internally, so the gate lives inside that node. An
// always-present group pinned at its floor must contribute zero continuous
// penalty. Targets PER_SCOPE_ITEM (the only intent that uses the fused node).
CO_TEST_P(
    CapacityWithGroupPresenceSpecBuilderTest,
    ContinuousPenaltyGatedAtLowerBoundMaxBoundFusedPerScopeItem) {
  if (GetParam() !=
      interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM) {
    co_return;
  }

  interface::CapacityWithGroupPresenceSpec spec;
  spec.dimension() = "replicaCount";
  spec.partition() = "tenantTrafficObjects";
  spec.scope() = "region";
  spec.roundUpGroupUtilOnScopeItem() = false;
  spec.bound() = interface::CapacityWithGroupPresenceBound::MAX;
  spec.intent() = GetParam();
  spec.scopeItemToLimit()->type() = interface::LimitType::ABSOLUTE;
  spec.scopeItemToLimit()->globalLimit() = 0; // MAX limit 0 -> always broken
  spec.groupToPresenceWeight()->globalLimit() = 1.0; // floor 1.0
  spec.scopeItemToAlwaysPresentGroups() = {
      {"region1", {"tenant2-trafficObjects"}}};

  const auto universe = buildUniverse();
  auto& builder = expressionBuilder();
  const CapacityWithGroupPresenceSpecBuilder specBuilder(
      universe, spec, /*needsContinuousExpressions=*/true);
  auto components = co_await specBuilder.constraints(builder);
  EXPECT_EQ(2, components.size()); // one per scope item (region1, region2)
  const auto& region1 = components[0];
  EXPECT_TRUE(region1.additionalPenaltyExpr != nullptr);

  // ObjectPartitionLookupWithMinPresence has no LP form yet.
  const packer::tests::LpAssertOptions lpOpts = {
      .exceptionForLpExpr =
          "LP expressions are not yet implemented for ObjectPartitionWithMinPresence"};

  // region1 holds only tenant2, pinned below its floor: move tenant1 out of
  // region1 and leave tenant2 with just trafficObject6 (0.115 < floor 1.0)
  // there. finalUtil = max(1.0, 0.115) = 1.0 = its lower bound, so tenant2's
  // fused penalty is gated to 0 -> region1's continuous penalty is 0.
  auto pinned = deltaFromInitial({
      {"trafficObject1", "host3"},
      {"trafficObject5", "host3"},
      {"trafficObject9", "host3"},
      {"trafficObject8", "host3"},
      {"trafficObject6", "host2"},
  });
  EXPECT_NEAR(
      0.0,
      evaluate(region1, pinned, /*evaluateConstraintExpr=*/false, lpOpts),
      1e-8);

  // Above the floor: keep trafficObject8 (1.0) in region1 too, so tenant2's
  // util is 1.0 + 0.115 = 1.115 > floor -> not pinned -> penalty active
  // (normalized raw util).
  auto aboveFloor = deltaFromInitial({
      {"trafficObject1", "host3"},
      {"trafficObject5", "host3"},
      {"trafficObject9", "host3"},
      {"trafficObject6", "host2"},
  });
  EXPECT_NEAR(
      1.115 * kNormPerScopeItemNoRoundUp,
      evaluate(region1, aboveFloor, /*evaluateConstraintExpr=*/false, lpOpts),
      1e-8);
}

CO_TEST_P(
    CapacityWithGroupPresenceSpecBuilderTest,
    WithRoundUpMaxBoundAndMultipliersBasic) {
  interface::CapacityWithGroupPresenceSpec spec;
  spec.dimension() = "replicaCount";
  spec.partition() = "tenantTrafficObjects";
  spec.scope() = "region";
  spec.roundUpGroupUtilOnScopeItem() = true;
  spec.intent() = GetParam();

  auto& scopeItemLimits = *spec.scopeItemToLimit();
  scopeItemLimits.type() = interface::LimitType::ABSOLUTE;
  scopeItemLimits.globalLimit() = 22.0;
  scopeItemLimits.scopeItemLimits() = {{"region1", 20.0}};

  auto& groupToPresenceWeight = *spec.groupToPresenceWeight();
  groupToPresenceWeight.globalLimit() = 2;
  groupToPresenceWeight.groupLimits() = {{"tenant1-trafficObjects", 3}};

  auto& multiplierList = *spec.multiplierList();
  interface::Limit multiplier1;
  multiplier1.type() = interface::LimitType::ABSOLUTE;
  multiplier1.globalLimit() = 1.1;

  interface::Limit multiplier2;
  multiplier2.type() = interface::LimitType::ABSOLUTE;
  multiplier2.groupLimits() = {
      {"tenant1-trafficObjects", 4}, {"tenant2-trafficObjects", 8}};
  multiplier2.scopeItemToGroupLimits() = {
      {"region2", {{"tenant2-trafficObjects", 2}}},
  };

  multiplierList.emplace_back(std::move(multiplier1));
  multiplierList.emplace_back(std::move(multiplier2));

  const auto universe = buildUniverse();
  auto& builder = expressionBuilder();

  {
    // local search constraint
    const CapacityWithGroupPresenceSpecBuilder localSearchSpecBuilder(
        universe, spec, true);
    auto localSearchComponents =
        co_await localSearchSpecBuilder.constraints(builder);

    assertConstraintViolationBounds(localSearchComponents);

    // local search goal
    const CapacityWithGroupPresenceSpecBuilder localSearchGoalSpecBuilder(
        universe, spec, true);
    auto localSearchGoalExpr =
        co_await localSearchGoalSpecBuilder.goalCoro(builder);

    // verify expected number of components
    switch (GetParam()) {
      case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
        // 2 constraints (one per scope item)
        EXPECT_EQ(2, localSearchComponents.size());
        break;
      }
      case interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM: {
        // 4 constraints (one per group-scope item pair)
        EXPECT_EQ(4, localSearchComponents.size());
        break;
      }
    }

    auto initial = deltaFromInitial({});
    ExpectedInfo initialExpectedInfo;
    switch (GetParam()) {
      case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
        // 2 constraints (one per scope item)
        /*
        w.r.t. region1:
            tenant1-trafficObjects: non-rounded-up contribution to region1
        (without multpliers) is  = max(1.85+0.13+1.2, 3) = max(3.18, 3) = 3.18

            tenant1-trafficObjects: after rounding up and applying multiplier1
        is = ceil(3.18) * 1.1 = 4*1.1

            tenant1-trafficObjects: after rounding up and applying multiplier2
        is = ceil(4*1.1) * 4 = 5*4 = 20

            tenant2-trafficObjects: non-rounded-up contribution to region1
        (without multpliers) is  = max(1, 2)

            tenant2-trafficObjects: after rounding up and applying multiplier1
        is = ceil(2) * 1.1 = 2*1.1

            tenant2-trafficObjects: after rounding up and applying multiplier2
        is = ceil(2*1.1) * 8 = 24
        */
        initialExpectedInfo.constraintAndPenaltyValues = {
            // Region1 component (limit of region1 = 20.0)
            {.constraintValue = 20.0 + 24.0 - 20.0,
             .penaltyValue =
                 (3.18 * 1.1 * 4 + 1.0 * 1.1 * 8) * kNormPerScopeItem},
            // Region2 component (limit of region2 = 22.0)
            /*
            w.r.t. region2:
                tenant1-trafficObjects: non-rounded-up contribution to region2
            (without multpliers) is  = max(0.4+0.6+0.5, 3) = max(1.5, 3) = 3

                tenant1-trafficObjects: after rounding up and applying
            multiplier1 is = ceil(3) * 1.1 = 3*1.1= 3.3

                tenant1-trafficObjects: after rounding up and applying
            multiplier2 is = ceil(3.3) * 4 = 4*4 = 16

                tenant2-trafficObjects: non-rounded-up contribution to region2
            (without multpliers) is  = max(0.115+1.88, 2) = max(1.995, 2)
            = 1.995

                tenant2-trafficObjects: after rounding up and applying
            multiplier1 is = ceil(1.995) * 1.1 = 2*1.1 = 2.2

                tenant2-trafficObjects: after rounding up and applying
            multiplier2 is = ceil(2.2) * 2 = 6
                // NOTE that multiplier2 for tenant2-trafficObjects w.r.t
            region2 is 2
            */
            {.constraintValue = 16.0 + 6.0 - 22.0,
             .penaltyValue =
                 (1.5 * 1.1 * 4 + 1.995 * 1.1 * 2) * kNormPerScopeItem},
        };

        // goal value is the sum of max(0, constExpr) + step(constExpr) *
        // additionalPenaltyExpr per constraint. Only region1 is broken; its
        // constraint (=24) and scaled penalty
        // ((3.18 * 1.1 * 4 + 1.0 * 1.1 * 8) * kNormPerScopeItem = 22.792 *
        // kNormPerScopeItem) contribute.
        initialExpectedInfo.goalValue =
            24.0 + (3.18 * 1.1 * 4 + 1.0 * 1.1 * 8) * kNormPerScopeItem;
        break;
      }
      case interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM: {
        // 4 constraints (one per group-scope item pair)
        initialExpectedInfo.constraintAndPenaltyValues = {
            // tenant1-region1
            {.constraintValue = 20.0 - 20.0,
             .penaltyValue = 3.18 * 1.1 * 4 * kNormTenant1},
            // tenant2-region1
            {.constraintValue = 24.0 - 20.0,
             .penaltyValue = 1.0 * 1.1 * 8 * kNormTenant2},
            // tenant1-region2
            {.constraintValue = 16.0 - 22.0,
             .penaltyValue = 1.5 * 1.1 * 4 * kNormTenant1},
            // tenant2-region2
            {.constraintValue = 6.0 - 22.0,
             .penaltyValue = 1.995 * 1.1 * 2 * kNormTenant2},
        };

        // only (tenant2, region1) broken
        initialExpectedInfo.goalValue = 4.0 + 1.0 * 1.1 * 8 * kNormTenant2;
        break;
      }
    }
    VERIFY_CONSTRAINT_COMPONENTS_AND_GOAL_VALUES(
        initialExpectedInfo,
        localSearchComponents,
        localSearchGoalExpr,
        initial);
  }
}

CO_TEST_P(
    CapacityWithGroupPresenceSpecBuilderTest,
    WithRoundUpMaxBoundAndGroupUtilMultipliers) {
  // This test verifies that groupUtilMultipliers with target works correctly.
  // When target=UTILIZATION, the multiplier is applied to the actual util.
  // When target=PRESENCE_WEIGHT, the multiplier is applied to the presence
  // weight.
  // When target=COMMON, the multiplier is applied to both.
  interface::CapacityWithGroupPresenceSpec spec;
  spec.dimension() = "replicaCount";
  spec.partition() = "tenantTrafficObjects";
  spec.scope() = "region";
  spec.roundUpGroupUtilOnScopeItem() = false;
  spec.intent() = GetParam();

  auto& scopeItemLimits = *spec.scopeItemToLimit();
  scopeItemLimits.type() = interface::LimitType::ABSOLUTE;
  scopeItemLimits.globalLimit() = 100.0;

  auto& groupToPresenceWeight = *spec.groupToPresenceWeight();
  groupToPresenceWeight.globalLimit() = 5.0;
  groupToPresenceWeight.groupLimits() = {{"tenant1-trafficObjects", 10.0}};

  // Set up groupUtilMultipliers - one applied before presence weight, one after
  auto& groupUtilMultipliers = *spec.groupUtilMultipliers();

  // This multiplier (2.0) is applied to actual util (target = UTILIZATION)
  interface::GroupUtilMultiplier utilMultiplier;
  utilMultiplier.value() = interface::Limit{};
  utilMultiplier.value()->type() = interface::LimitType::ABSOLUTE;
  utilMultiplier.value()->globalLimit() = 2.0;
  utilMultiplier.target() = interface::GroupUtilMultiplierTarget::UTILIZATION;
  groupUtilMultipliers.emplace_back(std::move(utilMultiplier));

  // This multiplier (3.0) is applied to presence weight (target =
  // PRESENCE_WEIGHT)
  interface::GroupUtilMultiplier presenceWeightMultiplier;
  presenceWeightMultiplier.value() = interface::Limit{};
  presenceWeightMultiplier.value()->type() = interface::LimitType::ABSOLUTE;
  presenceWeightMultiplier.value()->globalLimit() = 3.0;
  presenceWeightMultiplier.target() =
      interface::GroupUtilMultiplierTarget::PRESENCE_WEIGHT;
  groupUtilMultipliers.emplace_back(std::move(presenceWeightMultiplier));

  // This multiplier (1.5) is applied after (target = COMMON)
  interface::GroupUtilMultiplier commonMultiplier;
  commonMultiplier.value() = interface::Limit{};
  commonMultiplier.value()->type() = interface::LimitType::ABSOLUTE;
  commonMultiplier.value()->globalLimit() = 1.5;
  commonMultiplier.target() = interface::GroupUtilMultiplierTarget::COMMON;
  groupUtilMultipliers.emplace_back(std::move(commonMultiplier));

  const auto universe = buildUniverse();
  auto& builder = expressionBuilder();

  {
    // local search constraint and goal
    const CapacityWithGroupPresenceSpecBuilder localSearchSpecBuilder(
        universe, spec, true);
    auto localSearchComponents =
        co_await localSearchSpecBuilder.constraints(builder);

    assertConstraintViolationBounds(localSearchComponents);

    // local search goal
    const CapacityWithGroupPresenceSpecBuilder localSearchGoalSpecBuilder(
        universe, spec, true);
    auto localSearchGoalExpr =
        co_await localSearchGoalSpecBuilder.goalCoro(builder);

    // verify expected number of components
    switch (GetParam()) {
      case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
        // 2 constraints (one per scope item)
        EXPECT_EQ(2, localSearchComponents.size());
        break;
      }
      case interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM: {
        // 4 constraints (one per group-scope item pair)
        EXPECT_EQ(4, localSearchComponents.size());
        break;
      }
    }

    // verify initial values
    const auto initial = deltaFromInitial({});
    ExpectedInfo initialExpectedInfo;
    switch (GetParam()) {
      case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
        // 2 constraints (one per scope item)
        /*
        w.r.t. region1:
          tenant1-trafficObjects:
            actual util = 1.85 + 0.13 + 1.2 = 3.18
            after beforeMultiplier (2.0) = 3.18 * 2.0 = 6.36
            minPresence = 10.0 * 3.0 (presenceWeightMultiplier) = 30.0
            util = max(6.36, 30.0) = 30.0  <- min presence kicks in!
            after afterMultiplier (1.5) = 30.0 * 1.5 = 45.0

          tenant2-trafficObjects:
            actual util = 1.0
            after beforeMultiplier (2.0) = 1.0 * 2.0 = 2.0
            minPresence = 5.0 * 3.0 (presenceWeightMultiplier) = 15.0
            util = max(2.0, 15.0) = 15.0  <- min presence kicks in!
            after afterMultiplier (1.5) = 15.0 * 1.5 = 22.5

          total region1 util = 45.0 + 22.5 = 67.5
          penalty = 3.18 * 2.0 * 3.0 * 1.5 + 1.0 * 2.0 * 3.0 * 1.5
                  = 28.62 + 9.0 = 37.62

        w.r.t. region2:
          tenant1-trafficObjects:
            actual util = 0.4 + 0.6 + 0.5 = 1.5
            after beforeMultiplier (2.0) = 1.5 * 2.0 = 3.0
            minPresence = 10.0 * 3.0 (presenceWeightMultiplier) = 30.0
            util = max(3.0, 30.0) = 30.0  <- min presence kicks in!
            after afterMultiplier (1.5) = 30.0 * 1.5 = 45.0

          tenant2-trafficObjects:
            actual util = 0.115 + 1.88 = 1.995
            after beforeMultiplier (2.0) = 1.995 * 2.0 = 3.99
            minPresence = 5.0 * 3.0 (presenceWeightMultiplier) = 15.0
            util = max(3.99, 15.0) = 15.0  <- min presence kicks in!
            after afterMultiplier (1.5) = 15.0 * 1.5 = 22.5

          total region2 util = 45.0 + 22.5 = 67.5
          penalty = 1.5 * 2.0 * 3.0 * 1.5 + 1.995 * 2.0 * 3.0 * 1.5
                  = 13.5 + 17.955 = 31.455
        */
        // roundUp=false so penalties scale by kNormPerScopeItemNoRoundUp.
        initialExpectedInfo.constraintAndPenaltyValues = {
            // Region1 component (limit = 100.0)
            {.constraintValue = 67.5 - 100.0,
             .penaltyValue = 37.62 * kNormPerScopeItemNoRoundUp},
            // Region2 component (limit = 100.0)
            {.constraintValue = 67.5 - 100.0,
             .penaltyValue = 31.455 * kNormPerScopeItemNoRoundUp},
        };

        // Both constraints are not broken (negative values)
        initialExpectedInfo.goalValue = 0.0;
        break;
      }
      case interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM: {
        // 4 constraints (one per group-scope item pair)
        initialExpectedInfo.constraintAndPenaltyValues = {
            // tenant1-region1 (not broken)
            {.constraintValue = 45.0 - 100.0,
             .penaltyValue = 3.18 * 2.0 * 3.0 * 1.5 * kNormTenant1NoRoundUp},
            // tenant2-region1 (not broken)
            {.constraintValue = 22.5 - 100.0,
             .penaltyValue = 1.0 * 2.0 * 3.0 * 1.5 * kNormTenant2NoRoundUp},
            // tenant1-region2 (not broken)
            {.constraintValue = 45.0 - 100.0,
             .penaltyValue = 1.5 * 2.0 * 3.0 * 1.5 * kNormTenant1NoRoundUp},
            // tenant2-region2 (not broken)
            {.constraintValue = 22.5 - 100.0,
             .penaltyValue = 1.995 * 2.0 * 3.0 * 1.5 * kNormTenant2NoRoundUp},
        };

        initialExpectedInfo.goalValue = 0.0;
        break;
      }
    }
    VERIFY_CONSTRAINT_COMPONENTS_AND_GOAL_VALUES(
        initialExpectedInfo,
        localSearchComponents,
        localSearchGoalExpr,
        initial);
  }
}

CO_TEST_P(
    CapacityWithGroupPresenceSpecBuilderTest,
    WithRoundUpMaxBoundMultipliersAggregationScope) {
  interface::CapacityWithGroupPresenceSpec spec;
  spec.dimension() = "replicaCount";
  spec.partition() = "tenantTrafficObjects";
  spec.scope() = "region";
  spec.aggregationScope() = "host";
  spec.roundUpGroupUtilOnScopeItem() = true;
  spec.intent() = GetParam();

  auto& scopeItemLimits = *spec.scopeItemToLimit();
  scopeItemLimits.type() = interface::LimitType::ABSOLUTE;
  scopeItemLimits.globalLimit() = 22.0;
  scopeItemLimits.scopeItemLimits() = {{"region1", 20.0}};

  auto& groupToPresenceWeight = *spec.groupToPresenceWeight();
  groupToPresenceWeight.globalLimit() = 2;
  groupToPresenceWeight.groupLimits() = {{"tenant1-trafficObjects", 3}};

  auto& multiplierList = *spec.multiplierList();
  interface::Limit multiplier1;
  multiplier1.type() = interface::LimitType::ABSOLUTE;
  multiplier1.globalLimit() = 1.1;

  interface::Limit multiplier2;
  multiplier2.type() = interface::LimitType::ABSOLUTE;
  multiplier2.groupLimits() = {
      {"tenant1-trafficObjects", 4}, {"tenant2-trafficObjects", 8}};
  multiplier2.scopeItemToGroupLimits() = {
      {"host2", {{"tenant2-trafficObjects", 2}}},
      {"host5", {{"tenant2-trafficObjects", 2}}},
  };

  multiplierList.emplace_back(std::move(multiplier1));
  multiplierList.emplace_back(std::move(multiplier2));

  const auto universe = buildUniverse();
  auto& builder = expressionBuilder();

  {
    // local search constraint
    const CapacityWithGroupPresenceSpecBuilder localSearchSpecBuilder(
        universe, spec, true);
    auto localSearchComponents =
        co_await localSearchSpecBuilder.constraints(builder);

    assertConstraintViolationBounds(localSearchComponents);

    // local search goal
    const CapacityWithGroupPresenceSpecBuilder localSearchGoalSpecBuilder(
        universe, spec, true);
    auto localSearchGoalExpr =
        co_await localSearchGoalSpecBuilder.goalCoro(builder);

    // verify expected number of components
    switch (GetParam()) {
      case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
        // 2 constraints (one per scope item)
        EXPECT_EQ(2, localSearchComponents.size());
        break;
      }
      case interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM: {
        // 4 constraints (one per group-scope item pair)
        EXPECT_EQ(4, localSearchComponents.size());
        break;
      }
    }

    auto initial = deltaFromInitial({});
    ExpectedInfo initialExpectedInfo;
    switch (GetParam()) {
      case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
        // 2 constraints (one per scope item)
        /*
        w.r.t. region1:
            w.r.t. host1:
                tenant1-trafficObjects: non-rounded-up contribution to host1
                (without multpliers) is  = max(0, 3*0) = max(0, 0) = 0

                tenant1-trafficObjects: after rounding up and applying
        multiplier1 is  = ceil(0)*1.1 = 0

                tenant1-trafficObjects: after rounding up and applying
        multiplier2 is  = ceil(0)*4 = 0

                tenant2-trafficObjects: non-rounded-up contribution to host1
                (without multpliers) is  = max(1, 2)

                tenant2-trafficObjects: after rounding up and applying
        multiplier1 is  = ceil(2)*1.1 = 2*1.1

                tenant2-trafficObjects: after rounding up and applying
        multiplier2 is  = ceil(2*1.1)*8 = 24

        w.r.t. host2:
                tenant1-trafficObjects: non-rounded-up contribution to host2
                (without multpliers) is  = max(1.85+0.13+1.2, 3) = max(3.18, 3)
                = 3.18

                tenant1-trafficObjects: after rounding up and applying
        multiplier1 is  = ceil(3.18)*1.1 = 4*1.1

                tenant1-trafficObjects: after rounding up and applying
        multiplier2 is  = ceil(4*1.1)*4 = 5*4 = 20

                tenant2-trafficObjects: non-rounded-up contribution to host2
                (without multpliers) is  = max(0, 0)

                tenant2-trafficObjects: after rounding up and applying
        multiplier1 is  = ceil(0)*1.1 = 0

                tenant2-trafficObjects: after rounding up and applying
        multiplier2 is  = ceil(0)*2 = 0
        */
        initialExpectedInfo.constraintAndPenaltyValues = {
            // Region1 component (limit of region1 = 20.0)
            {.constraintValue = (0.0 + 24.0) + (20.0 + 0.0) - 20.0,
             .penaltyValue =
                 (3.18 * 1.1 * 4 + 1.0 * 1.1 * 8) * kNormPerScopeItem},
            // Region2 component (limit of region2 = 22.0)
            /*
                w.r.t. region2:
                    w.r.t. host3:
                        tenant1-trafficObjects: non-rounded-up contribution to
               host3 (without multpliers) is  = max(0.4, 3) = max(0.4, 3) = 3

                        tenant1-trafficObjects: after rounding up and applying
                        multiplier1 is  = ceil(3)*1.1 = 3.3

                        tenant1-trafficObjects: after rounding up and applying
                        multiplier2 is  = ceil(3.3)*4 = 16

                        tenant2-trafficObjects: non-rounded-up contribution to
               host3 (without multpliers) is  = max(0.115, 2) = 2

                        tenant2-trafficObjects: after rounding up and applying
                        multiplier1 is  = ceil(2)*1.1 = 2*1.1

                        tenant2-trafficObjects: after rounding up and applying
                        multiplier2 is  = ceil(2*1.1)*8 = 24

                w.r.t. host4:
                        tenant1-trafficObjects: non-rounded-up contribution to
               host4 (without multpliers) is  = max(0.6, 3) = max(0.6, 3) = 3

                        tenant1-trafficObjects: after rounding up and applying
                        multiplier1 is  = ceil(3)*1.1 = 3.3

                        tenant1-trafficObjects: after rounding up and applying
                        multiplier2 is  = ceil(3.3)*4 = 16

                        tenant2-trafficObjects: non-rounded-up contribution to
               host4 (without multpliers) is  = max(0, 0) = 0

                w.r.t. host5:
                        tenant1-trafficObjects: non-rounded-up contribution to
               host5 (without multpliers) is  = max(0.5, 3) = max(0.5, 3) = 3

                        tenant1-trafficObjects: after rounding up and applying
                        multiplier1 is  = ceil(3)*1.1 = 3.3

                        tenant1-trafficObjects: after rounding up and applying
                        multiplier2 is  = ceil(3.3)*4 = 16

                        tenant2-trafficObjects: non-rounded-up contribution to
               host5 (without multpliers) is  = max(1.88, 2) = 2

                        tenant2-trafficObjects: after rounding up and applying
                        multiplier1 is  = ceil(2)*1.1 = 2.2

                        tenant2-trafficObjects: after rounding up and applying
                        multiplier2 is  = ceil(2.2)*2 = 6

                        // note multiplier for traffic-objects-2 and host5 is 2
            */
            {.constraintValue = (16 + 24) + (16 + 0.0) + (16 + 6) - 22.0,
             .penaltyValue =
                 ((0.4 * 1.1 * 4) + (0.6 * 1.1 * 4) + (0.5 * 1.1 * 4) +
                  (0.115 * 1.1 * 8) + 0 + (1.88 * 1.1 * 2)) *
                 kNormPerScopeItem},
        };

        // goal value is the sum of max(0, constExpr) + step(constExpr) *
        // additionalPenaltyExpr per constraint. Both broken; constraints
        // contribute 24 + 56 = 80, scaled penalties contribute
        // (22.792 + 11.748) * kNormPerScopeItem.
        initialExpectedInfo.goalValue = (24.0 + 56.0) +
            ((3.18 * 1.1 * 4 + 1.0 * 1.1 * 8) +
             ((0.4 * 1.1 * 4) + (0.6 * 1.1 * 4) + (0.5 * 1.1 * 4) +
              (0.115 * 1.1 * 8) + 0 + (1.88 * 1.1 * 2))) *
                kNormPerScopeItem;
        break;
      }
      case interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM: {
        // 4 constraints (one per group-scope item pair)
        initialExpectedInfo.constraintAndPenaltyValues = {
            // tenant1-region1
            {.constraintValue = (0.0 + 20.0) - 20.0,
             .penaltyValue = 3.18 * 1.1 * 4 * kNormTenant1},
            // tenant2-region1
            {.constraintValue = (24.0 + 0.0) - 20.0,
             .penaltyValue = 1.0 * 1.1 * 8 * kNormTenant2},
            // tenant1-region2
            {.constraintValue = (16 + 16 + 16) - 22.0,
             .penaltyValue = (0.4 + 0.6 + 0.5) * 1.1 * 4 * kNormTenant1},
            // tenant2-region2
            {.constraintValue = (24 + 0 + 6) - 22.0,
             .penaltyValue =
                 ((0.115 * 1.1 * 8) + 0 + (1.88 * 1.1 * 2)) * kNormTenant2},
        };

        initialExpectedInfo.goalValue = (4.0 + 1.0 * 1.1 * 8 * kNormTenant2) +
            (26.0 + (0.4 + 0.6 + 0.5) * 1.1 * 4 * kNormTenant1) +
            (8.0 + ((0.115 * 1.1 * 8) + (1.88 * 1.1 * 2)) * kNormTenant2);
        break;
      }
    }
    VERIFY_CONSTRAINT_COMPONENTS_AND_GOAL_VALUES(
        initialExpectedInfo,
        localSearchComponents,
        localSearchGoalExpr,
        initial);
  }
}

CO_TEST_P(CapacityWithGroupPresenceSpecBuilderTest, WithoutRoundUp) {
  // similar example as above but without roundUp and limits on scopeItems are
  // relative
  co_await addScopeDimension(
      "replicaCount",
      scopeId("region"),
      {{"region1", 5.5}, {"region2", 5.0}},
      1.0);

  interface::CapacityWithGroupPresenceSpec spec;
  spec.dimension() = "replicaCount";
  spec.partition() = "tenantTrafficObjects";
  spec.scope() = "region";
  spec.roundUpGroupUtilOnScopeItem() = false;
  spec.intent() = GetParam();

  auto& scopeItemLimits = *spec.scopeItemToLimit();
  scopeItemLimits.type() = interface::LimitType::RELATIVE;
  scopeItemLimits.globalLimit() = 1;

  auto& groupToPresenceWeight = *spec.groupToPresenceWeight();
  groupToPresenceWeight.globalLimit() = 2;
  groupToPresenceWeight.groupLimits() = {{"tenant1-trafficObjects", 3}};

  const auto universe = buildUniverse();
  auto& builder = expressionBuilder();

  const CapacityWithGroupPresenceSpecBuilder specBuilder(universe, spec, true);
  auto components = co_await specBuilder.constraints(builder);

  assertConstraintViolationBounds(components);

  // local search goal
  const CapacityWithGroupPresenceSpecBuilder localSearchGoalSpecBuilder(
      universe, spec, true);
  auto localSearchGoalExpr =
      co_await localSearchGoalSpecBuilder.goalCoro(builder);

  // verify expected number of components
  switch (GetParam()) {
    case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
      // 2 constraints (one per scope item)
      EXPECT_EQ(2, components.size());
      break;
    }
    case interface::CapacityWithGroupPresenceUsageIntent::
        PER_GROUP_AND_SCOPE_ITEM: {
      // 4 constraints (one per group-scope item pair)
      EXPECT_EQ(4, components.size());
      break;
    }
  }

  // for explanations on the how the values were computed, see test case
  // WithRoundUpAndMaxBound
  {
    auto initial = deltaFromInitial({});
    ExpectedInfo initialExpectedInfo;
    switch (GetParam()) {
      case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
        // 2 constraints (one per scope item)
        // Without roundUp
        // Region1: 3.18 + 2.0 - 5.5 = -0.32 (not broken)
        // Region2: 3.0 + 2.0 - 5.0 = 0.0 (not broken)
        initialExpectedInfo.constraintAndPenaltyValues = {
            // Region1 component (limit of region1 = 5.5)
            {.constraintValue = 3.18 + 2.0 - 5.5,
             .penaltyValue = (3.18 + 1.0) * kNormPerScopeItemNoRoundUp},
            // Region2 component (limit of region2 = 5.0)
            {.constraintValue = 3.0 + 2.0 - 5.0,
             .penaltyValue = (1.5 + 1.995) * kNormPerScopeItemNoRoundUp},
        };

        // goal value = 0 because both constraints are not broken
        initialExpectedInfo.goalValue = 0.0;
        break;
      }
      case interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM: {
        // 4 constraints (one per group-scope item pair)
        initialExpectedInfo.constraintAndPenaltyValues = {
            // tenant1-region1  (not broken)
            {.constraintValue = 3.18 - 5.5,
             .penaltyValue = 3.18 * kNormTenant1NoRoundUp},
            // tenant2-region1  (not broken)
            {.constraintValue = 2.0 - 5.5,
             .penaltyValue = 1.0 * kNormTenant2NoRoundUp},
            // tenant1-region2  (not broken)
            {.constraintValue = 3.0 - 5.0,
             .penaltyValue = 1.5 * kNormTenant1NoRoundUp},
            // tenant2-region2  (not broken)
            {.constraintValue = 2.0 - 5.0,
             .penaltyValue = 1.995 * kNormTenant2NoRoundUp},
        };

        initialExpectedInfo.goalValue = 0.0;
        break;
      }
    }
    VERIFY_CONSTRAINT_COMPONENTS_AND_GOAL_VALUES(
        initialExpectedInfo, components, localSearchGoalExpr, initial);
  }

  {
    auto delta = deltaFromInitial({{"trafficObject5", "host4"}});
    ExpectedInfo deltaExpectedInfo;
    switch (GetParam()) {
      case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
        // 2 constraints (one per scope item)
        deltaExpectedInfo.constraintAndPenaltyValues = {
            // Region1 component: 3.05 + 2.0 - 5.5 = -0.45 (not broken)
            {.constraintValue = 3.05 + 2.0 - 5.5,
             .penaltyValue = (3.05 + 1.0) * kNormPerScopeItemNoRoundUp},
            // Region2 component: 3.0 + 2.0 - 5.0 = 0.0 (not broken)
            {.constraintValue = 3.0 + 2.0 - 5.0,
             .penaltyValue = (1.63 + 1.995) * kNormPerScopeItemNoRoundUp},
        };

        // goal value = 0 because both constraints are not broken
        deltaExpectedInfo.goalValue = 0.0;
        break;
      }
      case interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM: {
        // 4 constraints (one per group-scope item pair)
        deltaExpectedInfo.constraintAndPenaltyValues = {
            // tenant1-region1 (not broken)
            {.constraintValue = 3.05 - 5.5,
             .penaltyValue = 3.05 * kNormTenant1NoRoundUp},
            // tenant2-region1 (not broken)
            {.constraintValue = 2.0 - 5.5,
             .penaltyValue = 1.0 * kNormTenant2NoRoundUp},
            // tenant1-region2 (not broken)
            {.constraintValue = 3.0 - 5.0,
             .penaltyValue = 1.63 * kNormTenant1NoRoundUp},
            // tenant2-region2 (not broken)
            {.constraintValue = 2.0 - 5.0,
             .penaltyValue = 1.995 * kNormTenant2NoRoundUp},
        };

        deltaExpectedInfo.goalValue = 0.0;
        break;
      }
    }
    VERIFY_CONSTRAINT_COMPONENTS_AND_GOAL_VALUES(
        deltaExpectedInfo, components, localSearchGoalExpr, delta);
  }

  {
    // remove tenant1-trafficObjects from region1 and move trafficObject7 to
    // host1
    auto delta = deltaFromInitial(
        {{"trafficObject1", "host4"},
         {"trafficObject5", "host5"},
         {"trafficObject9", "host5"},
         {"trafficObject7", "host1"}});
    ExpectedInfo deltaExpectedInfo;
    switch (GetParam()) {
      case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
        // 2 constraints (one per scope item)
        deltaExpectedInfo.constraintAndPenaltyValues = {
            // Region1 component: 0.0 + 2 - 5.5 (not broken)
            {.constraintValue = 0.0 + 2.0 - 5.5,
             .penaltyValue = (0.0 + 1.88) * kNormPerScopeItemNoRoundUp},
            // Region2 component: 4.68 + 2 - 5.0 (broken)
            {.constraintValue = 4.68 + 2.0 - 5.0,
             .penaltyValue = (4.68 + 0.115) * kNormPerScopeItemNoRoundUp},
        };

        // only region2 broken
        deltaExpectedInfo.goalValue =
            (4.68 + 2.0 - 5.0) + (4.68 + 0.115) * kNormPerScopeItemNoRoundUp;
        break;
      }
      case interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM: {
        // 4 constraints (one per group-scope item pair)
        deltaExpectedInfo.constraintAndPenaltyValues = {
            // tenant1-region1  (not broken)
            {.constraintValue = 0.0 - 5.5,
             .penaltyValue = 0.0 * kNormTenant1NoRoundUp},
            // tenant2-region1  (not broken)
            {.constraintValue = 2.0 - 5.5,
             .penaltyValue = 1.88 * kNormTenant2NoRoundUp},
            // tenant1-region2  (not broken)
            {.constraintValue = 4.68 - 5.0,
             .penaltyValue = 4.68 * kNormTenant1NoRoundUp},
            // tenant2-region2  (not broken)
            {.constraintValue = 2.0 - 5.0,
             .penaltyValue = 0.115 * kNormTenant2NoRoundUp},
        };

        deltaExpectedInfo.goalValue = 0.0;
        break;
      }
    }
    VERIFY_CONSTRAINT_COMPONENTS_AND_GOAL_VALUES(
        deltaExpectedInfo, components, localSearchGoalExpr, delta);
  }
}

CO_TEST_P(
    CapacityWithGroupPresenceSpecBuilderTest,
    WithoutRoundUpAndMultipliersBasic) {
  // similar example as above but without roundUp and limits on scopeItems are
  // relative
  co_await addScopeDimension(
      "replicaCount",
      scopeId("region"),
      {{"region1", 5.5}, {"region2", 5.0}},
      1.0);

  interface::CapacityWithGroupPresenceSpec spec;
  spec.dimension() = "replicaCount";
  spec.partition() = "tenantTrafficObjects";
  spec.scope() = "region";
  spec.roundUpGroupUtilOnScopeItem() = false;
  spec.intent() = GetParam();

  auto& scopeItemLimits = *spec.scopeItemToLimit();
  scopeItemLimits.type() = interface::LimitType::RELATIVE;
  scopeItemLimits.globalLimit() = 1;

  auto& groupToPresenceWeight = *spec.groupToPresenceWeight();
  groupToPresenceWeight.globalLimit() = 2;
  groupToPresenceWeight.groupLimits() = {{"tenant1-trafficObjects", 3}};

  auto& multiplierList = *spec.multiplierList();
  interface::Limit multiplier1;
  multiplier1.type() = interface::LimitType::ABSOLUTE;
  multiplier1.globalLimit() = 1.1;
  multiplier1.scopeItemLimits() = {{"region2", 2.1}};

  interface::Limit multiplier2;
  multiplier2.type() = interface::LimitType::ABSOLUTE;
  multiplier2.groupLimits() = {
      {"tenant1-trafficObjects", 4}, {"tenant2-trafficObjects", 8}};
  multiplier2.scopeItemToGroupLimits() = {
      {"region2", {{"tenant2-trafficObjects", 2}}},
  };

  multiplierList.emplace_back(std::move(multiplier1));
  multiplierList.emplace_back(std::move(multiplier2));

  const auto universe = buildUniverse();
  auto& builder = expressionBuilder();

  const CapacityWithGroupPresenceSpecBuilder specBuilder(universe, spec, true);
  auto components = co_await specBuilder.constraints(builder);

  assertConstraintViolationBounds(components);

  // local search goal
  const CapacityWithGroupPresenceSpecBuilder localSearchGoalSpecBuilder(
      universe, spec, true);
  auto localSearchGoalExpr =
      co_await localSearchGoalSpecBuilder.goalCoro(builder);

  // verify expected number of components
  switch (GetParam()) {
    case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
      // 2 constraints (one per scope item)
      EXPECT_EQ(2, components.size());
      break;
    }
    case interface::CapacityWithGroupPresenceUsageIntent::
        PER_GROUP_AND_SCOPE_ITEM: {
      // 4 constraints (one per group-scope item pair)
      EXPECT_EQ(4, components.size());
      break;
    }
  }

  // for explanations on the how the values were computed, see test case
  // WithoutRoundUp; the only difference is that we have multipliers
  {
    auto initial = deltaFromInitial({});
    ExpectedInfo initialExpectedInfo;
    switch (GetParam()) {
      case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
        // 2 constraints (one per scope item)
        /*
        w.r.t. region1:
            tenant1-trafficObjects: non-rounded-up contribution to region1
            (without multipliers) is = max(1.85+0.13+1.2, 3) = 3.18
            after applying multiplier1 (1.1) = 3.18 * 1.1
            after applying multiplier2 (4) = 3.18 * 1.1 * 4

            tenant2-trafficObjects: non-rounded-up contribution to region1
            (without multipliers) is = max(1, 2) = 2.0
            after applying multiplier1 (1.1) = 2.0 * 1.1
            after applying multiplier2 (8) = 2.0 * 1.1 * 8
        */
        initialExpectedInfo.constraintAndPenaltyValues = {
            // Region1 component (limit of region1 = 5.5)
            {.constraintValue = 3.18 * 1.1 * 4 + 2.0 * 1.1 * 8 - 5.5,
             .penaltyValue =
                 (3.18 * 1.1 * 4 + 1.0 * 1.1 * 8) * kNormPerScopeItemNoRoundUp},
            // Region2 component (limit of region2 = 5.0)
            /*
            w.r.t. region2:
                tenant1-trafficObjects: non-rounded-up contribution to region2
                (without multipliers) is = max(0.4+0.6+0.5, 3) = 3.0
                after applying multiplier1 (2.1) = 3.0 * 2.1 (region2 has
            specific limit) after applying multiplier2 (4) = 3.0 * 2.1 * 4

                tenant2-trafficObjects: non-rounded-up contribution to region2
                (without multipliers) is = max(0.115+1.88, 2) = 2.0
                after applying multiplier1 (2.1) = 2.0 * 2.1 (region2 has
            specific limit) after applying multiplier2 (2) = 2.0 * 2.1 * 2
            (region2 has specific limit for tenant2)
            */
            {.constraintValue = 3.0 * 2.1 * 4 + 2.0 * 2.1 * 2 - 5.0,
             .penaltyValue = (1.5 * 2.1 * 4 + 1.995 * 2.1 * 2) *
                 kNormPerScopeItemNoRoundUp},
        };

        // both broken; goal = constraints + scaled penalties.
        const double region1Constraint = 3.18 * 1.1 * 4 + 2.0 * 1.1 * 8 - 5.5;
        const double region1Penalty =
            (3.18 * 1.1 * 4 + 1.0 * 1.1 * 8) * kNormPerScopeItemNoRoundUp;
        const double region2Constraint = 3.0 * 2.1 * 4 + 2.0 * 2.1 * 2 - 5.0;
        const double region2Penalty =
            (1.5 * 2.1 * 4 + 1.995 * 2.1 * 2) * kNormPerScopeItemNoRoundUp;

        initialExpectedInfo.goalValue = region1Constraint + region1Penalty +
            region2Constraint + region2Penalty;
        break;
      }
      case interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM: {
        // 4 constraints (one per group-scope item pair)
        initialExpectedInfo.constraintAndPenaltyValues = {
            // tenant1-region1
            {.constraintValue = 3.18 * 1.1 * 4 - 5.5,
             .penaltyValue = 3.18 * 1.1 * 4 * kNormTenant1NoRoundUp},
            // tenant2-region1
            {.constraintValue = 2.0 * 1.1 * 8 - 5.5,
             .penaltyValue = 1.0 * 1.1 * 8 * kNormTenant2NoRoundUp},
            // tenant1-region2
            {.constraintValue = 3.0 * 2.1 * 4 - 5.0,
             .penaltyValue = 1.5 * 2.1 * 4 * kNormTenant1NoRoundUp},
            // tenant2-region2
            {.constraintValue = 2.0 * 2.1 * 2 - 5.0,
             .penaltyValue = 1.995 * 2.1 * 2 * kNormTenant2NoRoundUp},
        };

        initialExpectedInfo.goalValue =
            (3.18 * 1.1 * 4 - 5.5 + 3.18 * 1.1 * 4 * kNormTenant1NoRoundUp) +
            (2.0 * 1.1 * 8 - 5.5 + 1.0 * 1.1 * 8 * kNormTenant2NoRoundUp) +
            (3.0 * 2.1 * 4 - 5.0 + 1.5 * 2.1 * 4 * kNormTenant1NoRoundUp) +
            (2.0 * 2.1 * 2 - 5.0 + 1.995 * 2.1 * 2 * kNormTenant2NoRoundUp);
        break;
      }
    }
    VERIFY_CONSTRAINT_COMPONENTS_AND_GOAL_VALUES(
        initialExpectedInfo, components, localSearchGoalExpr, initial);
  }
}

CO_TEST_P(
    CapacityWithGroupPresenceSpecBuilderTest,
    WithRoundUpMaxBoundMultipliersDifferentAggregationPartition) {
  interface::CapacityWithGroupPresenceSpec spec;
  spec.scope() = "region";
  spec.dimension() = "replicaCount";
  spec.partition() = "tenantGroups"; // mainPartition
  spec.aggregationPartition() = "tenantTrafficObjects";
  spec.roundUpGroupUtilOnScopeItem() = true;
  spec.intent() = GetParam();

  auto& scopeItemLimits = *spec.scopeItemToLimit();
  scopeItemLimits.type() = interface::LimitType::ABSOLUTE;
  scopeItemLimits.globalLimit() = 22.0;
  scopeItemLimits.scopeItemToGroupLimits() = {
      {"region1", {{"allTenants-trafficObjects", 20.0}}},
  };

  auto& groupToPresenceWeight = *spec.groupToPresenceWeight();
  groupToPresenceWeight.globalLimit() = 2;
  groupToPresenceWeight.groupLimits() = {{"tenant1-trafficObjects", 3}};

  auto& multiplierList = *spec.multiplierList();
  interface::Limit multiplier1;
  multiplier1.type() = interface::LimitType::ABSOLUTE;
  multiplier1.globalLimit() = 1.1;

  interface::Limit multiplier2;
  multiplier2.type() = interface::LimitType::ABSOLUTE;
  multiplier2.groupLimits() = {
      {"tenant1-trafficObjects", 4}, {"tenant2-trafficObjects", 8}};
  multiplier2.scopeItemToGroupLimits() = {
      {"region2", {{"tenant2-trafficObjects", 2}}},
  };

  multiplierList.emplace_back(std::move(multiplier1));
  multiplierList.emplace_back(std::move(multiplier2));

  const auto universe = buildUniverse();
  auto& builder = expressionBuilder();

  // When intent is PER_SCOPE_ITEM, expect a runtime error because
  // mainPartition != aggregationPartition
  if (GetParam() ==
      interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM) {
    REBALANCER_EXPECT_RUNTIME_ERROR(
        CapacityWithGroupPresenceSpecBuilder(universe, spec, true),
        "main partition and aggregation partition must be the same when CapacityWithGroupPresenceUsageIntent is PER_SCOPE_ITEM, but got mainPartition='tenantGroups', aggregationPartition='tenantTrafficObjects'");
    co_return;
  }

  {
    // local search constraint
    const CapacityWithGroupPresenceSpecBuilder localSearchSpecBuilder(
        universe, spec, true);
    auto localSearchComponents =
        co_await localSearchSpecBuilder.constraints(builder);

    // local search goal
    const CapacityWithGroupPresenceSpecBuilder localSearchGoalSpecBuilder(
        universe, spec, true);
    auto localSearchGoalExpr =
        co_await localSearchGoalSpecBuilder.goalCoro(builder);

    // 2 constraints (one per mainPartition group- main scope item item pair)
    // mainPartition has 1 group: allTenants-trafficObjects
    // scope has 2 items: region1, region2
    EXPECT_EQ(2, localSearchComponents.size());

    auto initial = deltaFromInitial({});
    ExpectedInfo initialExpectedInfo;

    // For mainPartition group "allTenants-trafficObjects" at region1:
    // The utilization is sum of:
    //   tenant1-trafficObjects (aggregationPartition group):
    //     non-rounded-up = max(1.85+0.13+1.2, 3) = 3.18
    //     after multiplier1: ceil(3.18) * 1.1 = 4 * 1.1
    //     after rounding up and multiplier2: ceil(4*1.1) * 4 = 5 * 4 = 20
    //   tenant2-trafficObjects (aggregationPartition group):
    //     non-rounded-up = max(1, 2) = 2
    //     after multiplier1: ceil(2) * 1.1 = 2 * 1.1
    //     after rounding up and multiplier2: ceil(2*1.1) * 8 = 3 * 8 = 24
    //   Total: 20 + 24
    //   Limit: 20
    //   Penalty: 3.18 * 1.1 * 4 + 1.0 * 1.1 * 8  2

    // For mainPartition group "allTenants-trafficObjects" at region2:
    //   tenant1-trafficObjects (aggregationPartition group):
    //     non-rounded-up = max(0.4+0.6+0.5, 3) = 3
    //     after multiplier1: ceil(3) * 1.1 = 3 * 1.1
    //     after rounding up and multiplier2: ceil(3*1.1) * 4 = 4 * 4 = 16
    //   tenant2-trafficObjects (aggregationPartition group):
    //     non-rounded-up = max(0.115+1.88, 2) = 2
    //     after multiplier1: ceil(2) * 1.1 = 2 * 1.1
    //     after rounding up and multiplier2: ceil(2*1.1) * 2 = 3 * 2 = 6
    //     (note: multiplier2 for tenant2 at region2 is 2)
    //   Total: 16 + 6
    //   Limit: 22
    //   Penalty: 1.5 * 1.1 * 4 + 1.995 * 1.1 * 2

    initialExpectedInfo.constraintAndPenaltyValues = {
        // allTenants-region1
        {.constraintValue = (20.0 + 24.0) - 20.0,
         .penaltyValue = (3.18 * 1.1 * 4 + 1.0 * 1.1 * 8) * kNormPerScopeItem},
        // allTenants-region2
        {.constraintValue = 16 + 6 - 22,
         .penaltyValue = (1.5 * 1.1 * 4 + 1.995 * 1.1 * 2) * kNormPerScopeItem},
    };

    // goal value is the sum of max(0, constExpr) + step(constExpr) *
    // additionalPenaltyExpr per constraint. Only region1 is broken; its
    // constraint (=24) and scaled penalty
    // ((3.18 * 1.1 * 4 + 1.0 * 1.1 * 8) * kNormPerScopeItem = 22.792 *
    // kNormPerScopeItem) contribute.
    initialExpectedInfo.goalValue =
        24.0 + (3.18 * 1.1 * 4 + 1.0 * 1.1 * 8) * kNormPerScopeItem;

    VERIFY_CONSTRAINT_COMPONENTS_AND_GOAL_VALUES(
        initialExpectedInfo,
        localSearchComponents,
        localSearchGoalExpr,
        initial);
  }
}

CO_TEST_P(
    CapacityWithGroupPresenceSpecBuilderTest,
    EnsureScopeItemFilterWorks) {
  interface::CapacityWithGroupPresenceSpec spec;
  spec.dimension() = "replicaCount";
  spec.partition() = "tenantTrafficObjects";
  spec.scope() = "region";
  spec.intent() = GetParam();

  spec.scopeItemToLimit()->globalLimit() = 4;
  spec.groupToPresenceWeight()->globalLimit() = 1;
  spec.scopeItemFilter()->itemsBlacklist() = {"region1", "region2"};

  const auto universe = buildUniverse();
  auto& builder = expressionBuilder();

  const CapacityWithGroupPresenceSpecBuilder specBuilder(universe, spec, true);
  auto constraints = co_await specBuilder.constraints(builder);

  // expect everything to be filtered out and hence zero constraint exprs
  EXPECT_EQ(0, constraints.size());
}

CO_TEST_P(CapacityWithGroupPresenceSpecBuilderTest, EnsureGroupFilterWorks) {
  interface::CapacityWithGroupPresenceSpec spec;
  spec.dimension() = "replicaCount";
  spec.partition() = "tenantTrafficObjects";
  spec.scope() = "region";
  spec.intent() = GetParam();
  spec.groupFilter()->itemsWhitelist() = {"tenant1-trafficObjects"};

  const auto universe = buildUniverse();
  auto& builder = expressionBuilder();

  const CapacityWithGroupPresenceSpecBuilder specBuilder(universe, spec, true);
  auto constraints = co_await specBuilder.constraints(builder);

  switch (GetParam()) {
    case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
      // expect 2 constraints (one per scope item)
      EXPECT_EQ(2, constraints.size());
      break;
    }
    case interface::CapacityWithGroupPresenceUsageIntent::
        PER_GROUP_AND_SCOPE_ITEM: {
      // expect 2 constraints (one per group-scope item pair)
      EXPECT_EQ(2, constraints.size());
      break;
    }
  }
}

TEST_P(CapacityWithGroupPresenceSpecBuilderTest, Description) {
  const auto universe = buildUniverse();

  {
    interface::CapacityWithGroupPresenceSpec spec;
    spec.dimension() = "replicaCount";
    spec.partition() = "tenantTrafficObjects";
    spec.scope() = "region";
    spec.roundUpGroupUtilOnScopeItem() = true;
    spec.intent() = GetParam();

    const CapacityWithGroupPresenceSpecBuilder specBuilder(
        universe, spec, true);
    EXPECT_EQ(
        fmt::format(
            "Capacity with group presence {} w.r.t. dimension 'replicaCount', partition 'tenantTrafficObjects' (aggregationPartition 'tenantTrafficObjects'), scope 'region' (aggregationScope 'region'), bound 'MAX', roundUp = true",
            apache::thrift::util::enumNameSafe(GetParam())),
        specBuilder.description());
  }

  {
    // using a different aggregation scope and aggregation partition
    std::string aggregationPartition;
    switch (GetParam()) {
      case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
        aggregationPartition = "tenantGroups";
        break;
      }
      case interface::CapacityWithGroupPresenceUsageIntent::
          PER_GROUP_AND_SCOPE_ITEM: {
        aggregationPartition = "tenantTrafficObjects";
        break;
      }
    };

    interface::CapacityWithGroupPresenceSpec spec;
    spec.dimension() = "replicaCount";
    spec.partition() = "tenantGroups";
    spec.scope() = "region";
    spec.aggregationScope() = "host";
    spec.aggregationPartition() = aggregationPartition;
    spec.roundUpGroupUtilOnScopeItem() = true;
    spec.intent() = GetParam();

    const CapacityWithGroupPresenceSpecBuilder specBuilder(
        universe, spec, true);
    EXPECT_EQ(
        fmt::format(
            "Capacity with group presence {} w.r.t. dimension 'replicaCount', partition 'tenantGroups' (aggregationPartition '{}'), scope 'region' (aggregationScope 'host'), bound 'MAX', roundUp = true",
            apache::thrift::util::enumNameSafe(GetParam()),
            aggregationPartition),
        specBuilder.description());
  }
}

TEST_P(CapacityWithGroupPresenceSpecBuilderTest, SpecInfo) {
  interface::CapacityWithGroupPresenceSpec spec;
  spec.name() = "test";
  spec.dimension() = "replicaCount";
  spec.scope() = "region";
  spec.partition() = "tenantTrafficObjects";
  spec.roundUpGroupUtilOnScopeItem() = true;
  spec.scopeItemToLimit()->type() = interface::LimitType::ABSOLUTE;
  spec.intent() = GetParam();

  const CapacityWithGroupPresenceSpecBuilder specBuilder(
      buildUniverse(), spec, true);

  auto expectedSpecInfo = facebook::rebalancer::SpecParameters{
      .name = "test",
      .scope = "region",
      .partition = "tenantTrafficObjects",
      .dimension = "replicaCount",
      .limitType = "ABSOLUTE"};
  EXPECT_EQ(expectedSpecInfo, specBuilder.getSpecInfo());
}

} // namespace facebook::rebalancer::materializer::tests
