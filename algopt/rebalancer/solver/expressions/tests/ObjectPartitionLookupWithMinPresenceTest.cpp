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

#include "algopt/rebalancer/materializer/utils/LimitWrapper.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"

#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class ObjectPartitionLookupWithMinPresenceTest : public ExpressionTestsBase {
 protected:
  folly::coro::Task<std::shared_ptr<const entities::Universe>> setUpUniverse() {
    // Set up initial assignment with objects mapped to containers
    setInitialAssignment(
        folly::F14FastMap<std::string, std::vector<std::string>>{
            {"container1", {"object8"}},
            {"container2", {"object1", "object5", "object9"}},
            {"container3", {"object2", "object6"}},
            {"container4", {"object3"}},
            {"container5", {"object4", "object7"}},
            {"container6", {"object10"}}});

    // container 6 is not part of any region
    co_await addScope(
        "region",
        {{"region1", {"container1", "container2"}},
         {"region2", {"container3", "container4", "container5"}}});

    // dynamic objectDimension
    entities::Map<std::string, double> baseObjectToValue = {
        {"object1", 1.85},
        {"object2", 0.4},
        {"object3", 0.6},
        {"object4", 0.5},
        {"object5", 0.13},
        {"object6", 0.115},
        {"object7", 0.88},
        {"object8", 1.0},
        {"object9", 1.2},
        {"object10", 0.3},
    };

    // only difference between object dimension values for region1 and region2
    // is the value of object7
    auto region2DimensionValues = baseObjectToValue;
    region2DimensionValues["object7"] = 1.88;

    co_await addDynamicObjectDimension(
        "replicaCount",
        region(),
        {{"region1", std::move(baseObjectToValue)},
         {"region2", std::move(region2DimensionValues)}},
        0.0);

    co_await addPartition(
        "partition",
        {{"group1",
          {"object1",
           "object2",
           "object3",
           "object4",
           "object5",
           "object9",
           "object10"}},
         {"group2", {"object6", "object7", "object8"}}});

    co_return buildUniverse();
  }

  static Assignment getInitialAssignment(const entities::Universe& universe) {
    return Assignment(universe.getContainers().getInitialAssignment());
  }

  entities::PartitionId partition() const {
    return ExpressionTestsBase::partitionId("partition");
  }

  entities::DimensionId replicaCountDimId() const {
    return dimensionId("replicaCount");
  }

  entities::ScopeId region() const {
    return ExpressionTestsBase::scopeId("region");
  }

  entities::ScopeItemId region(const int i) const {
    return ExpressionTestsBase::scopeItemId(
        region(), fmt::format("region{}", i));
  }

  entities::GroupId group(const int i) const {
    return ExpressionTestsBase::groupId(partition(), fmt::format("group{}", i));
  }

  static interface::Limit makeLimit(double globalLimit) {
    interface::Limit limit;
    limit.type() = interface::LimitType::ABSOLUTE;
    limit.globalLimit() = globalLimit;
    return limit;
  }

  materializer::LimitWrapper makeLimitWrapper(
      const entities::Universe& universe,
      const interface::Limit& limit) const {
    return materializer::LimitWrapper(universe, limit, region(), partition());
  }

  std::shared_ptr<ObjectPartitionLookupWithMinPresence>
  makeObjectPartitionLookupWithMinPresence(
      const entities::Universe& universe,
      const entities::ScopeItemId aggregationScopeItemId,
      const PackerSet<entities::GroupId>& groupIds,
      const folly::small_vector<materializer::LimitWrapper, 2>& multiplierList =
          {},
      bool makeContinuousPenaltyTerm = false,
      bool roundUpGroupUtilOnScopeItem = false,
      folly::F14FastMap<
          entities::ScopeItemId,
          folly::F14FastSet<entities::GroupId>> scopeItemToAlwaysPresentGroups =
          {}) const {
    auto objectPartition = object_partition(
        partition(),
        replicaCountDimId(),
        /*groupLimits=*/{},
        universe,
        PackerSet<entities::ScopeItemId>({aggregationScopeItemId}),
        groupIds);

    auto groupToPresenceWeight = makeLimit(2.0);
    groupToPresenceWeight.groupLimits() = {{"group1", 3.0}};

    auto groupToExtraAdditivePenalty = makeLimit(0.0);
    groupToExtraAdditivePenalty.groupLimits() = {{"group2", 1.5}};

    const Assignment assignment(
        universe.getContainers().getInitialAssignment());

    auto objectPartitionLookupWithMinPresence =
        std::make_shared<ObjectPartitionLookupWithMinPresence>(
            ObjectPartitionLookupWithMinPresence(
                objectPartition,
                universe.getScope(region()).getContainerIdsPtr(
                    aggregationScopeItemId),
                region(),
                aggregationScopeItemId,
                universe,
                assignment,
                /*groupLimitOverrides=*/{},
                /*initialDuringObjects=*/{},
                /*defaultGroupLimitOverride=*/std::nullopt,
                /*penaltyTransform=*/
                ObjectPartitionLookupPenaltyTransform::IDENTITY,
                /*groupsAllowed=*/0,
                ObjectPartitionLookup<
                    ObjectPartitionLookupWithMinPresencePolicy>::Bound::MAX,
                ObjectPartitionLookupWithMinPresencePolicy::Data(
                    makeLimitWrapper(universe, groupToPresenceWeight),
                    makeLimitWrapper(universe, groupToExtraAdditivePenalty),
                    {{interface::GroupUtilMultiplierTarget::COMMON,
                      multiplierList}},
                    makeContinuousPenaltyTerm,
                    roundUpGroupUtilOnScopeItem,
                    std::move(scopeItemToAlwaysPresentGroups))));

    return objectPartitionLookupWithMinPresence;
  }
};

CO_TEST_F(
    ObjectPartitionLookupWithMinPresenceTest,
    UtilWithNoRoundUpOrMultipliers) {
  co_await setUpUniverse();
  const auto& universe = getUniverse();
  auto assignment = getInitialAssignment(universe);

  auto objectPartitionLookup = makeObjectPartitionLookupWithMinPresence(
      universe,
      /*aggregationScopeItemId=*/region(1),
      /*groupIds=*/{group(1), group(2)},
      /*multiplierList=*/{},
      /*makeContinuousPenaltyTerm=*/false,
      /*roundUpGroupUtilOnScopeItem=*/false);

  // skip lp expr evaluation since LP does not yet properly transform the values
  // with ObjectPartitionWithMinPresence
  const LpAssertOptions lpAssertOptions = {
      .exceptionForLpExpr =
          "LP expressions are not yet implemented for ObjectPartitionWithMinPresence"};

  // region1 has 3 objects (object1, object5, object9) from group1 and 1 object
  // (object8) from group 2. group1 has a minimum presence of 3.0 and group2 has
  // a minimum presence of 2.0
  // group1 util = max(1.85 + 0.13 + 1.2, 3.0) = 3.18
  // group2 util = max(1.0, 2.0) = 2.0
  // total util = 3.18 + 2.0 = 5.18
  EXPECT_NEAR(
      5.18, apply(objectPartitionLookup, assignment, lpAssertOptions), 1e-8);
  EXPECT_NEAR(5.18, objectPartitionLookup->value, 1e-8);

  // Verify evaluate() with no changes returns the same value
  EXPECT_NEAR(
      5.18,
      evaluate(objectPartitionLookup, {}, assignment, lpAssertOptions),
      1e-8);

  // Move objects 6 and 7 from group2 into region1
  // region1 now has the same objects from group1, but now has 3 objects
  // (object6, object7, object8) from group2. group1 has a minimum presence
  // of 3.0 and group2 has a minimum presence of 2.0
  // group1 util = max(1.85 + 0.13 + 1.2, 3.0) = 3.18
  // group2 util = max(0.115 + 0.88 + 1.0, 2.0) = 2.0
  // total util = 3.18 + 2.0 = 5.18
  const auto changes1 = ObjectToNewContainer{
      {object(6), container(1)}, {object(7), container(1)}};
  EXPECT_NEAR(
      5.18,
      evaluate(objectPartitionLookup, changes1, assignment, lpAssertOptions),
      1e-8);
  EXPECT_NEAR(
      5.18,
      applyChanges(
          objectPartitionLookup, changes1, assignment, lpAssertOptions),
      1e-8);
  EXPECT_NEAR(5.18, objectPartitionLookup->value, 1e-8);
  assignment.moveTo(object(6), container(1));
  assignment.moveTo(object(7), container(1));

  // Move object1 from group1 out of region1
  // region1 now has 2 objects (object5, object9) from group1 and 3 objects
  // (object6, object7, object8) from group 2. group1 has a minimum presence
  // of 3.0 and group2 has a minimum presence of 2.0
  // group1 util = max(0.13 + 1.2, 3.0) = 3.0
  // group2 util = max(0.115 + 0.88 + 1.0, 2.0) = 2.0
  // total util = 3.0 + 2.0 = 5.0
  const auto changes2 = ObjectToNewContainer{{object(1), container(3)}};
  EXPECT_NEAR(
      5.0,
      evaluate(objectPartitionLookup, changes2, assignment, lpAssertOptions),
      1e-8);
  EXPECT_NEAR(
      5.0,
      applyChanges(
          objectPartitionLookup, changes2, assignment, lpAssertOptions),
      1e-8);
  EXPECT_NEAR(5.0, objectPartitionLookup->value, 1e-8);
}

CO_TEST_F(
    ObjectPartitionLookupWithMinPresenceTest,
    UtilWithRoundUpAndMultipliers) {
  co_await setUpUniverse();
  const auto& universe = getUniverse();
  auto assignment = getInitialAssignment(universe);

  folly::small_vector<materializer::LimitWrapper, 2> multiplierList;
  auto multiplier1 = makeLimit(1.1);
  auto multiplier2 = makeLimit(1);
  multiplier2.groupLimits() = {{"group1", 4}, {"group2", 8}};
  multiplierList.emplace_back(makeLimitWrapper(universe, multiplier1));
  multiplierList.emplace_back(makeLimitWrapper(universe, multiplier2));

  auto objectPartitionLookup = makeObjectPartitionLookupWithMinPresence(
      universe,
      /*aggregationScopeItemId=*/region(1),
      /*groupIds=*/{group(1), group(2)},
      /*multiplierList=*/multiplierList,
      /*makeContinuousPenaltyTerm=*/false,
      /*roundUpGroupUtilOnScopeItem=*/true);

  // skip lp expr evaluation since LP does not yet properly transform the values
  // with ObjectPartitionWithMinPresence
  const LpAssertOptions lpAssertOptions = {
      .exceptionForLpExpr =
          "LP expressions are not yet implemented for ObjectPartitionWithMinPresence"};

  // region1 has 3 objects (object1, object5, object9) from group1 and 1 object
  // (object8) from group 2. group1 has a minimum presence of 3.0 and group2 has
  // a minimum presence of 2.0
  // group1 util = max(1.85 + 0.13 + 1.2, 3.0) = 3.18
  //   - after rounding up and applying multiplier1 = ceil(3.18) * 1.1 = 4.4
  //   - after rounding up and applying multiplier2 = ceil(4.4) * 4 = 20
  //   - after rounding up = ceil(20) = 20
  // group2 util = max(1.0, 2.0) = 2.0
  //   - after rounding up and applying multiplier1 = ceil(2.0) * 1.1 = 2.2
  //   - after rounding up and applying multiplier2 = ceil(2.2) * 8 = 24
  //   - after rounding up = ceil(24) = 24
  // total util = 20 + 24 = 44
  EXPECT_NEAR(
      44.0, apply(objectPartitionLookup, assignment, lpAssertOptions), 1e-8);
  EXPECT_NEAR(44.0, objectPartitionLookup->value, 1e-8);

  // Verify evaluate() with no changes returns the same value
  EXPECT_NEAR(
      44.0,
      evaluate(objectPartitionLookup, {}, assignment, lpAssertOptions),
      1e-8);

  // Move objects 6 and 7 from group2 into region1
  // region1 now has the same objects from group1, but now has 3 objects
  // (object6, object7, object8) from group2. group1 has a minimum presence
  // of 3.0 and group2 has a minimum presence of 2.0
  // group1 util = max(1.85 + 0.13 + 1.2, 3.0) = 3.18
  //   - after rounding up and applying multiplier1 = ceil(3.18) * 1.1 = 4.4
  //   - after rounding up and applying multiplier2 = ceil(4.4) * 4 = 20
  //   - after rounding up = ceil(20) = 20
  // group2 util = max(0.115 + 0.88 + 1.0, 2.0) = 2.0
  //   - after rounding up and applying multiplier1 = ceil(2.0) * 1.1 = 2.2
  //   - after rounding up and applying multiplier2 = ceil(2.2) * 8 = 24
  //   - after rounding up = ceil(24) = 24
  // total util = 20 + 24 = 44
  const auto changes1 = ObjectToNewContainer{
      {object(6), container(1)}, {object(7), container(1)}};
  EXPECT_NEAR(
      44.0,
      evaluate(objectPartitionLookup, changes1, assignment, lpAssertOptions),
      1e-8);
  EXPECT_NEAR(
      44.0,
      applyChanges(
          objectPartitionLookup, changes1, assignment, lpAssertOptions),
      1e-8);
  EXPECT_NEAR(44.0, objectPartitionLookup->value, 1e-8);
  assignment.moveTo(object(6), container(1));
  assignment.moveTo(object(7), container(1));

  // Move object1 from group1 out of region1
  // region1 now has 2 objects (object5, object9) from group1 and 3 objects
  // (object6, object7, object8) from group 2. group1 has a minimum presence
  // of 3.0 and group2 has a minimum presence of 2.0
  // group1 util = max(0.13 + 1.2, 3.0) = 3.0
  //   - after rounding up and applying multiplier1 = ceil(3.0) * 1.1 = 3.3
  //   - after rounding up and applying multiplier2 = ceil(3.3) * 4 = 16
  //   - after rounding up = ceil(16) = 16
  // group2 util = max(0.115 + 0.88 + 1.0, 2.0) = 2.0
  //   - after rounding up and applying multiplier1 = ceil(2.0) * 1.1 = 2.2
  //   - after rounding up and applying multiplier2 = ceil(2.2) * 8 = 24
  //   - after rounding up = ceil(24) = 24
  // total util = 16 + 24 = 40
  const auto changes2 = ObjectToNewContainer{{object(1), container(3)}};
  EXPECT_NEAR(
      40.0,
      evaluate(objectPartitionLookup, changes2, assignment, lpAssertOptions),
      1e-8);
  EXPECT_NEAR(
      40.0,
      applyChanges(
          objectPartitionLookup, changes2, assignment, lpAssertOptions),
      1e-8);
  EXPECT_NEAR(40.0, objectPartitionLookup->value, 1e-8);
}

CO_TEST_F(ObjectPartitionLookupWithMinPresenceTest, PenaltyWithNoMultipliers) {
  co_await setUpUniverse();
  const auto& universe = getUniverse();
  auto assignment = getInitialAssignment(universe);

  auto objectPartitionLookup = makeObjectPartitionLookupWithMinPresence(
      universe,
      /*aggregationScopeItemId=*/region(1),
      /*groupIds=*/{group(1), group(2)},
      /*multiplierList=*/{},
      /*makeContinuousPenaltyTerm=*/true,
      /*roundUpGroupUtilOnScopeItem=*/false);

  // skip lp expr evaluation since LP does not yet properly transform the values
  // with ObjectPartitionWithMinPresence
  const LpAssertOptions lpAssertOptions = {
      .exceptionForLpExpr =
          "LP expressions are not yet implemented for ObjectPartitionWithMinPresence"};

  // region1 has 3 objects (object1, object5, object9) from group1 and 1 object
  // (object8) from group 2. group1 does not have an extra additive penalty and
  // group 2 has an extra additive penalty of 1.5
  // group1 penalty = 1.85 + 0.13 + 1.2 + 0.0 = 3.18
  // group2 penalty = 1.0 + 1.5 = 2.5
  // total penalty = 3.18 + 2.5 = 5.68
  EXPECT_NEAR(
      5.68, apply(objectPartitionLookup, assignment, lpAssertOptions), 1e-8);
  EXPECT_NEAR(5.68, objectPartitionLookup->value, 1e-8);

  // Verify evaluate() with no changes returns the same value
  EXPECT_NEAR(
      5.68,
      evaluate(objectPartitionLookup, {}, assignment, lpAssertOptions),
      1e-8);

  // Move objects 6 and 7 from group2 into region1
  // region1 now has the same objects from group1, but now has 3 objects
  // (object6, object7, object8) from group2. group1 has an extra additive
  // penalty of 0 and group2 has an extra additive penalty of 1.5
  // group1 penalty = 1.85 + 0.13 + 1.2 + 0.0 = 3.18
  // group2 penalty = 0.115 + 0.88 + 1.0 + 1.5 = 3.495
  // total penalty = 3.18 + 3.495 = 6.675
  const auto changes1 = ObjectToNewContainer{
      {object(6), container(1)}, {object(7), container(1)}};
  EXPECT_NEAR(
      6.675,
      evaluate(objectPartitionLookup, changes1, assignment, lpAssertOptions),
      1e-8);
  EXPECT_NEAR(
      6.675,
      applyChanges(
          objectPartitionLookup, changes1, assignment, lpAssertOptions),
      1e-8);
  EXPECT_NEAR(6.675, objectPartitionLookup->value, 1e-8);
  assignment.moveTo(object(6), container(1));
  assignment.moveTo(object(7), container(1));

  // Move object1 from group1 out of region1
  // region1 now has 2 objects (object5, object9) from group1 and 3 objects
  // (object6, object7, object8) from group 2. group1 has an extra additive
  // penalty of 0 and group2 has an extra additive penalty of 1.5
  // group1 penalty = 0.13 + 1.2 + 0.0 = 1.33
  // group2 penalty = 0.115 + 0.88 + 1.0 + 1.5 = 3.495
  // total penalty = 1.33 + 3.495 = 4.825
  const auto changes2 = ObjectToNewContainer{{object(1), container(3)}};
  EXPECT_NEAR(
      4.825,
      evaluate(objectPartitionLookup, changes2, assignment, lpAssertOptions),
      1e-8);
  EXPECT_NEAR(
      4.825,
      applyChanges(
          objectPartitionLookup, changes2, assignment, lpAssertOptions),
      1e-8);
  EXPECT_NEAR(4.825, objectPartitionLookup->value, 1e-8);
}

CO_TEST_F(ObjectPartitionLookupWithMinPresenceTest, PenaltyWithMultipliers) {
  co_await setUpUniverse();
  const auto& universe = getUniverse();
  auto assignment = getInitialAssignment(universe);

  folly::small_vector<materializer::LimitWrapper, 2> multiplierList;
  auto multiplier1 = makeLimit(1.1);
  auto multiplier2 = makeLimit(1);
  multiplier2.groupLimits() = {{"group1", 4}, {"group2", 8}};
  multiplierList.emplace_back(makeLimitWrapper(universe, multiplier1));
  multiplierList.emplace_back(makeLimitWrapper(universe, multiplier2));

  auto objectPartitionLookup = makeObjectPartitionLookupWithMinPresence(
      universe,
      /*aggregationScopeItemId=*/region(1),
      /*groupIds=*/{group(1), group(2)},
      multiplierList,
      /*makeContinuousPenaltyTerm=*/true,
      /*roundUpGroupUtilOnScopeItem=*/false);

  // skip lp expr evaluation since LP does not yet properly transform the values
  // with ObjectPartitionWithMinPresence
  const LpAssertOptions lpAssertOptions = {
      .exceptionForLpExpr =
          "LP expressions are not yet implemented for ObjectPartitionWithMinPresence"};

  // region1 has 3 objects (object1, object5, object9) from group1 and 1 object
  // (object8) from group 2. group1 has an extra additive penalty of 0 and
  // group 2 has an extra additive penalty of 1.5
  // group1 penalty = 1.85 + 0.13 + 1.2 + 0.0 = 3.18
  //   - after applying multiplier1 = 3.18 * 1.1 = 3.498
  //   - after applying multiplier2 = 3.498 * 4 = 13.992
  // group2 penalty = 1.0 + 1.5 = 2.5
  //   - after applying multiplier1 = 2.5 * 1.1 = 2.75
  //   - after applying multiplier2 = 2.75 * 8 = 22
  // total penalty = 13.992 + 22 = 35.992
  EXPECT_NEAR(
      35.992, apply(objectPartitionLookup, assignment, lpAssertOptions), 1e-8);
  EXPECT_NEAR(35.992, objectPartitionLookup->value, 1e-8);

  // Verify evaluate() with no changes returns the same value
  EXPECT_NEAR(
      35.992,
      evaluate(objectPartitionLookup, {}, assignment, lpAssertOptions),
      1e-8);

  // Move objects 6 and 7 from group2 into region1
  // region1 now has the same objects from group1, but now has 3 objects
  // (object6, object7, object8) from group2. group1 has an extra additive
  // penalty of 0 and group2 has an extra additive penalty of 1.5
  // group1 penalty = 1.85 + 0.13 + 1.2 + 0.0 = 3.18
  //   - after applying multiplier1 = 3.18 * 1.1 = 3.498
  //   - after applying multiplier2 = 3.498 * 4 = 13.992
  // group2 penalty = 0.115 + 0.88 + 1.0 + 1.5 = 3.495
  //   - after applying multiplier1 = 3.495 * 1.1 = 3.8445
  //   - after applying multiplier2 = 3.8445 * 8 = 30.756
  // total penalty = 13.992 + 30.756 = 44.748
  const auto changes1 = ObjectToNewContainer{
      {object(6), container(1)}, {object(7), container(1)}};
  EXPECT_NEAR(
      44.748,
      evaluate(objectPartitionLookup, changes1, assignment, lpAssertOptions),
      1e-8);
  EXPECT_NEAR(
      44.748,
      applyChanges(
          objectPartitionLookup, changes1, assignment, lpAssertOptions),
      1e-8);
  EXPECT_NEAR(44.748, objectPartitionLookup->value, 1e-8);
  assignment.moveTo(object(6), container(1));
  assignment.moveTo(object(7), container(1));

  // Move object1 from group1 out of region1
  // region1 now has 2 objects (object5, object9) from group1 and 3 objects
  // (object6, object7, object8) from group 2. group1 has an extra additive
  // penalty of 0 and group2 has an extra additive penalty of 1.5
  // group1 penalty = 0.13 + 1.2 + 0.0 = 1.33
  //   - after applying multiplier1 = 1.33 * 1.1 = 1.463
  //   - after applying multiplier2 = 1.463 * 4 = 5.852
  // group2 penalty = 0.115 + 0.88 + 1.0 + 1.5 = 3.495
  //   - after applying multiplier1 = 3.495 * 1.1 = 3.8445
  //   - after applying multiplier2 = 3.8445 * 8 = 30.756
  // total penalty = 5.852 + 30.756 = 36.608
  const auto changes2 = ObjectToNewContainer{{object(1), container(3)}};
  EXPECT_NEAR(
      36.608,
      evaluate(objectPartitionLookup, changes2, assignment, lpAssertOptions),
      1e-8);
  EXPECT_NEAR(
      36.608,
      applyChanges(
          objectPartitionLookup, changes2, assignment, lpAssertOptions),
      1e-8);
  EXPECT_NEAR(36.608, objectPartitionLookup->value, 1e-8);
}

CO_TEST_F(ObjectPartitionLookupWithMinPresenceTest, EmptyGroupList) {
  co_await setUpUniverse();
  const auto& universe = getUniverse();
  const auto assignment = getInitialAssignment(universe);

  auto objectPartitionLookup =
      makeObjectPartitionLookupWithMinPresence(universe, region(1), {}, {});

  // skip lp expr evaluation since LP does not yet properly transform the values
  // with ObjectPartitionWithMinPresence
  const LpAssertOptions lpAssertOptions = {
      .exceptionForLpExpr =
          "LP expressions are not yet implemented for ObjectPartitionWithMinPresence"};

  EXPECT_EQ(0.0, apply(objectPartitionLookup, assignment, lpAssertOptions));
  EXPECT_EQ(0.0, lower_bound(*objectPartitionLookup));
  EXPECT_EQ(0.0, upper_bound(*objectPartitionLookup));
}

CO_TEST_F(ObjectPartitionLookupWithMinPresenceTest, GetType) {
  co_await setUpUniverse();
  const auto& universe = getUniverse();

  auto objectPartitionLookup =
      makeObjectPartitionLookupWithMinPresence(universe, region(1), {}, {});
  EXPECT_EQ(
      "ObjectPartitionLookupWithMinPresence", objectPartitionLookup->getType());
}

CO_TEST_F(ObjectPartitionLookupWithMinPresenceTest, HasNoLpIntent) {
  co_await setUpUniverse();
  const auto& universe = getUniverse();

  auto objectPartitionLookup =
      makeObjectPartitionLookupWithMinPresence(universe, region(1), {}, {});
  EXPECT_TRUE(objectPartitionLookup->hasNoLpIntent());
}

CO_TEST_F(
    ObjectPartitionLookupWithMinPresenceTest,
    UtilWithDifferentMultiplierTargets) {
  // This test verifies that groupUtilMultipliers with different targets
  // (UTILIZATION, PRESENCE_WEIGHT, COMMON) work correctly.
  // - UTILIZATION multipliers apply only to actual utilization
  // - PRESENCE_WEIGHT multipliers apply only to presence weight
  // - COMMON multipliers apply to both
  co_await setUpUniverse();
  const auto& universe = getUniverse();
  auto assignment = getInitialAssignment(universe);

  const entities::ScopeItemId aggregationScopeItemId = region(1);
  const PackerSet<entities::GroupId> groupIds = {group(1), group(2)};

  auto objectPartition = object_partition(
      partition(),
      replicaCountDimId(),
      /*groupLimits=*/{},
      universe,
      PackerSet<entities::ScopeItemId>({aggregationScopeItemId}),
      groupIds);

  // Set up presence weight: group1 = 3.0, group2 = 2.0 (default)
  auto groupToPresenceWeight = makeLimit(2.0);
  groupToPresenceWeight.groupLimits() = {{"group1", 3.0}};

  auto groupToExtraAdditivePenalty = makeLimit(0.0);

  // Set up multipliers with different targets:
  // - UTILIZATION multiplier: 2.0 (applies only to actual util)
  // - PRESENCE_WEIGHT multiplier: 1.5 (applies only to presence weight)
  // - COMMON multiplier: 1.1 (applies to both)
  folly::F14FastMap<
      interface::GroupUtilMultiplierTarget,
      folly::small_vector<materializer::LimitWrapper, 2>>
      groupUtilMultiplierMap;

  auto utilMultiplier = makeLimit(2.0);
  groupUtilMultiplierMap[interface::GroupUtilMultiplierTarget::UTILIZATION]
      .emplace_back(makeLimitWrapper(universe, utilMultiplier));

  auto presenceMultiplier = makeLimit(1.5);
  groupUtilMultiplierMap[interface::GroupUtilMultiplierTarget::PRESENCE_WEIGHT]
      .emplace_back(makeLimitWrapper(universe, presenceMultiplier));

  auto commonMultiplier = makeLimit(1.1);
  groupUtilMultiplierMap[interface::GroupUtilMultiplierTarget::COMMON]
      .emplace_back(makeLimitWrapper(universe, commonMultiplier));

  auto objectPartitionLookup =
      std::make_shared<ObjectPartitionLookupWithMinPresence>(
          ObjectPartitionLookupWithMinPresence(
              objectPartition,
              universe.getScope(region()).getContainerIdsPtr(
                  aggregationScopeItemId),
              region(),
              aggregationScopeItemId,
              universe,
              assignment,
              /*groupLimitOverrides=*/{},
              /*initialDuringObjects=*/{},
              /*defaultGroupLimitOverride=*/std::nullopt,
              /*penaltyTransform=*/
              ObjectPartitionLookupPenaltyTransform::IDENTITY,
              /*groupsAllowed=*/0,
              ObjectPartitionLookup<
                  ObjectPartitionLookupWithMinPresencePolicy>::Bound::MAX,
              ObjectPartitionLookupWithMinPresencePolicy::Data(
                  makeLimitWrapper(universe, groupToPresenceWeight),
                  makeLimitWrapper(universe, groupToExtraAdditivePenalty),
                  std::move(groupUtilMultiplierMap),
                  /*makeContinuousPenaltyTerm=*/false,
                  /*roundUpGroupUtilOnScopeItem=*/false)));

  // skip lp expr evaluation since LP does not yet properly transform the values
  // with ObjectPartitionWithMinPresence
  const LpAssertOptions lpAssertOptions = {
      .exceptionForLpExpr =
          "LP expressions are not yet implemented for ObjectPartitionWithMinPresence"};

  // region1 has 3 objects (object1, object5, object9) from group1 and 1 object
  // (object8) from group 2.
  //
  // group1:
  //   actual util = 1.85 + 0.13 + 1.2 = 3.18
  //   after UTILIZATION multiplier (2.0) = 3.18 * 2.0 = 6.36
  //   after COMMON multiplier (1.1) = 6.36 * 1.1 = 6.996
  //
  //   presence weight = 3.0
  //   after PRESENCE_WEIGHT multiplier (1.5) = 3.0 * 1.5 = 4.5
  //   after COMMON multiplier (1.1) = 4.5 * 1.1 = 4.95
  //
  //   group1 contribution = max(6.996, 4.95) = 6.996
  //
  // group2:
  //   actual util = 1.0
  //   after UTILIZATION multiplier (2.0) = 1.0 * 2.0 = 2.0
  //   after COMMON multiplier (1.1) = 2.0 * 1.1 = 2.2
  //
  //   presence weight = 2.0
  //   after PRESENCE_WEIGHT multiplier (1.5) = 2.0 * 1.5 = 3.0
  //   after COMMON multiplier (1.1) = 3.0 * 1.1 = 3.3
  //
  //   group2 contribution = max(2.2, 3.3) = 3.3
  //
  // total util = 6.996 + 3.3 = 10.296
  EXPECT_NEAR(
      10.296, apply(objectPartitionLookup, assignment, lpAssertOptions), 1e-8);
  EXPECT_NEAR(10.296, objectPartitionLookup->value, 1e-8);

  // Verify evaluate() with no changes returns the same value
  EXPECT_NEAR(
      10.296,
      evaluate(objectPartitionLookup, {}, assignment, lpAssertOptions),
      1e-8);
}

CO_TEST_F(
    ObjectPartitionLookupWithMinPresenceTest,
    UtilWithDifferentMultiplierTargetsAndRoundUp) {
  // Same as above but with roundUpGroupUtilOnScopeItem = true
  co_await setUpUniverse();
  const auto& universe = getUniverse();
  auto assignment = getInitialAssignment(universe);

  const entities::ScopeItemId aggregationScopeItemId = region(1);
  const PackerSet<entities::GroupId> groupIds = {group(1), group(2)};

  auto objectPartition = object_partition(
      partition(),
      replicaCountDimId(),
      /*groupLimits=*/{},
      universe,
      PackerSet<entities::ScopeItemId>({aggregationScopeItemId}),
      groupIds);

  auto groupToPresenceWeight = makeLimit(2.0);
  groupToPresenceWeight.groupLimits() = {{"group1", 3.0}};

  auto groupToExtraAdditivePenalty = makeLimit(0.0);

  folly::F14FastMap<
      interface::GroupUtilMultiplierTarget,
      folly::small_vector<materializer::LimitWrapper, 2>>
      groupUtilMultiplierMap;

  auto utilMultiplier = makeLimit(2.0);
  groupUtilMultiplierMap[interface::GroupUtilMultiplierTarget::UTILIZATION]
      .emplace_back(makeLimitWrapper(universe, utilMultiplier));

  auto presenceMultiplier = makeLimit(1.5);
  groupUtilMultiplierMap[interface::GroupUtilMultiplierTarget::PRESENCE_WEIGHT]
      .emplace_back(makeLimitWrapper(universe, presenceMultiplier));

  auto commonMultiplier = makeLimit(1.1);
  groupUtilMultiplierMap[interface::GroupUtilMultiplierTarget::COMMON]
      .emplace_back(makeLimitWrapper(universe, commonMultiplier));

  auto objectPartitionLookup =
      std::make_shared<ObjectPartitionLookupWithMinPresence>(
          ObjectPartitionLookupWithMinPresence(
              objectPartition,
              universe.getScope(region()).getContainerIdsPtr(
                  aggregationScopeItemId),
              region(),
              aggregationScopeItemId,
              universe,
              assignment,
              /*groupLimitOverrides=*/{},
              /*initialDuringObjects=*/{},
              /*defaultGroupLimitOverride=*/std::nullopt,
              /*penaltyTransform=*/
              ObjectPartitionLookupPenaltyTransform::IDENTITY,
              /*groupsAllowed=*/0,
              ObjectPartitionLookup<
                  ObjectPartitionLookupWithMinPresencePolicy>::Bound::MAX,
              ObjectPartitionLookupWithMinPresencePolicy::Data(
                  makeLimitWrapper(universe, groupToPresenceWeight),
                  makeLimitWrapper(universe, groupToExtraAdditivePenalty),
                  std::move(groupUtilMultiplierMap),
                  /*makeContinuousPenaltyTerm=*/false,
                  /*roundUpGroupUtilOnScopeItem=*/true)));

  const LpAssertOptions lpAssertOptions = {
      .exceptionForLpExpr =
          "LP expressions are not yet implemented for ObjectPartitionWithMinPresence"};

  // region1 has 3 objects (object1, object5, object9) from group1 and 1 object
  // (object8) from group 2.
  //
  // group1:
  //   actual util = 1.85 + 0.13 + 1.2 = 3.18
  //   after ceil = ceil(3.18) = 4
  //   after UTILIZATION multiplier (2.0) = 4 * 2.0 = 8
  //   after ceil = ceil(8) = 8
  //   after COMMON multiplier (1.1) = 8 * 1.1 = 8.8
  //   after ceil = ceil(8.8) = 9
  //
  //   presence weight = 3.0
  //   after ceil = ceil(3.0) = 3
  //   after PRESENCE_WEIGHT multiplier (1.5) = 3 * 1.5 = 4.5
  //   after ceil = ceil(4.5) = 5
  //   after COMMON multiplier (1.1) = 5 * 1.1 = 5.5
  //   after ceil = ceil(5.5) = 6
  //
  //   group1 contribution = max(9, 6) = 9
  //
  // group2:
  //   actual util = 1.0
  //   after ceil = ceil(1.0) = 1
  //   after UTILIZATION multiplier (2.0) = 1 * 2.0 = 2
  //   after ceil = ceil(2) = 2
  //   after COMMON multiplier (1.1) = 2 * 1.1 = 2.2
  //   after ceil = ceil(2.2) = 3
  //
  //   presence weight = 2.0
  //   after ceil = ceil(2.0) = 2
  //   after PRESENCE_WEIGHT multiplier (1.5) = 2 * 1.5 = 3
  //   after ceil = ceil(3) = 3
  //   after COMMON multiplier (1.1) = 3 * 1.1 = 3.3
  //   after ceil = ceil(3.3) = 4
  //
  //   group2 contribution = max(3, 4) = 4
  //
  // total util = 9 + 4 = 13
  EXPECT_NEAR(
      13.0, apply(objectPartitionLookup, assignment, lpAssertOptions), 1e-8);
  EXPECT_NEAR(13.0, objectPartitionLookup->value, 1e-8);
}

// scopeItemToAlwaysPresentGroups: a (group, scope item) listed in
// scopeItemToAlwaysPresentGroups has its presence floor honored even when the
// group's actual utilization in the scope item drops to 0 -- as if a frozen
// object kept step(util) == 1. Groups not listed keep the default step(util)
// gating.
CO_TEST_F(
    ObjectPartitionLookupWithMinPresenceTest,
    AlwaysPresentHonorsFloorAtZeroUtil) {
  co_await setUpUniverse();
  const auto& universe = getUniverse();
  auto assignment = getInitialAssignment(universe);

  // scopeItemToAlwaysPresentGroups only for group2 in region1. The floor value
  // comes from groupToPresenceWeight (2.0 for group2).
  auto withAlwaysPresent = makeObjectPartitionLookupWithMinPresence(
      universe,
      /*aggregationScopeItemId=*/region(1),
      /*groupIds=*/{group(1), group(2)},
      /*multiplierList=*/{},
      /*makeContinuousPenaltyTerm=*/false,
      /*roundUpGroupUtilOnScopeItem=*/false,
      /*scopeItemToAlwaysPresentGroups=*/{{region(1), {group(2)}}});
  auto withoutAlwaysPresent = makeObjectPartitionLookupWithMinPresence(
      universe,
      /*aggregationScopeItemId=*/region(1),
      /*groupIds=*/{group(1), group(2)},
      /*multiplierList=*/{},
      /*makeContinuousPenaltyTerm=*/false,
      /*roundUpGroupUtilOnScopeItem=*/false,
      /*scopeItemToAlwaysPresentGroups=*/{});

  const LpAssertOptions lpAssertOptions = {
      .exceptionForLpExpr =
          "LP expressions are not yet implemented for ObjectPartitionWithMinPresence"};

  // At the initial assignment region1 holds object8 (group2), so group2 util >
  // 0 and the floor applies in BOTH cases (step(util) == 1 regardless of
  // scopeItemToAlwaysPresentGroups). group1 util = max(3.18, 3.0) = 3.18;
  // group2 = max(1.0, 2.0) = 2.0; total = 5.18.
  EXPECT_NEAR(
      5.18, apply(withAlwaysPresent, assignment, lpAssertOptions), 1e-8);
  EXPECT_NEAR(
      5.18, apply(withoutAlwaysPresent, assignment, lpAssertOptions), 1e-8);

  // Move object8 (group2's only object in region1) out to region2 -> group2 has
  // zero actual util in region1.
  const auto changes = ObjectToNewContainer{{object(8), container(3)}};

  // Without scopeItemToAlwaysPresentGroups: step(0) == 0 -> group2 floor not
  // applied -> group2 = 0. total = group1 (3.18) + group2 (0) = 3.18.
  EXPECT_NEAR(
      3.18,
      evaluate(withoutAlwaysPresent, changes, assignment, lpAssertOptions),
      1e-8);

  // With scopeItemToAlwaysPresentGroups: floor honored even at zero util ->
  // group2 = max(2.0, 0) = 2.0. total = group1 (3.18) + group2 (2.0) = 5.18.
  EXPECT_NEAR(
      5.18,
      evaluate(withAlwaysPresent, changes, assignment, lpAssertOptions),
      1e-8);
}

// scopeItemToAlwaysPresentGroups is per (group, scope item): a group NOT listed
// keeps step() gating and drops to 0 at zero util, while a listed group keeps
// its floor.
CO_TEST_F(
    ObjectPartitionLookupWithMinPresenceTest,
    AlwaysPresentIsSelectivePerGroup) {
  co_await setUpUniverse();
  const auto& universe = getUniverse();
  auto assignment = getInitialAssignment(universe);

  // scopeItemToAlwaysPresentGroups only for group2 (group1 is intentionally not
  // listed).
  auto objectPartitionLookup = makeObjectPartitionLookupWithMinPresence(
      universe,
      /*aggregationScopeItemId=*/region(1),
      /*groupIds=*/{group(1), group(2)},
      /*multiplierList=*/{},
      /*makeContinuousPenaltyTerm=*/false,
      /*roundUpGroupUtilOnScopeItem=*/false,
      /*scopeItemToAlwaysPresentGroups=*/{{region(1), {group(2)}}});

  const LpAssertOptions lpAssertOptions = {
      .exceptionForLpExpr =
          "LP expressions are not yet implemented for ObjectPartitionWithMinPresence"};

  // Route every region1 object of BOTH groups out to region2:
  //  - group1 objects in region1: object1, object5, object9 (all in container2)
  //  - group2 object in region1: object8 (in container1)
  const auto changes = ObjectToNewContainer{
      {object(1), container(3)},
      {object(5), container(3)},
      {object(9), container(3)},
      {object(8), container(3)}};

  // group1 is NOT always-present -> step(0) == 0 -> floor not applied -> 0.
  // group2 IS always-present -> floor honored -> max(2.0, 0) = 2.0.
  // total = 0 + 2.0 = 2.0.
  EXPECT_NEAR(
      2.0,
      evaluate(objectPartitionLookup, changes, assignment, lpAssertOptions),
      1e-8);
}

// scopeItemToAlwaysPresentGroups with roundUp + multipliers: the forced floor
// flows through the same rounding/multiplier pipeline as a normally-present
// group.
CO_TEST_F(
    ObjectPartitionLookupWithMinPresenceTest,
    AlwaysPresentWithRoundUpAndMultiplier) {
  co_await setUpUniverse();
  const auto& universe = getUniverse();
  auto assignment = getInitialAssignment(universe);

  folly::small_vector<materializer::LimitWrapper, 2> multiplierList;
  auto multiplier = makeLimit(1);
  multiplier.groupLimits() = {{"group1", 4}, {"group2", 8}};
  multiplierList.emplace_back(makeLimitWrapper(universe, multiplier));

  auto objectPartitionLookup = makeObjectPartitionLookupWithMinPresence(
      universe,
      /*aggregationScopeItemId=*/region(1),
      /*groupIds=*/{group(1), group(2)},
      /*multiplierList=*/multiplierList,
      /*makeContinuousPenaltyTerm=*/false,
      /*roundUpGroupUtilOnScopeItem=*/true,
      /*scopeItemToAlwaysPresentGroups=*/{{region(1), {group(2)}}});

  const LpAssertOptions lpAssertOptions = {
      .exceptionForLpExpr =
          "LP expressions are not yet implemented for ObjectPartitionWithMinPresence"};

  // Move group2's only region1 object (object8) out -> group2 util in region1 =
  // 0.
  const auto changes = ObjectToNewContainer{{object(8), container(3)}};

  // group1 util = max(1.85 + 0.13 + 1.2, 3.0) = 3.18
  //   - ceil(3.18) = 4, * multiplier(group1=4) = 16, ceil(16) = 16
  // group2 forced floor = presence weight 2.0 (applied despite zero util)
  //   - ceil(2.0) = 2, * multiplier(group2=8) = 16, ceil(16) = 16
  // total util = 16 + 16 = 32
  EXPECT_NEAR(
      32.0,
      evaluate(objectPartitionLookup, changes, assignment, lpAssertOptions),
      1e-8);
}

} // namespace facebook::rebalancer::packer::tests
