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
#include "algopt/rebalancer/solver/expressions/ObjectPartition.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"

#include <folly/container/irange.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

using InitialAssignment = entities::Map<std::string, std::vector<std::string>>;

class ObjectPartitionLookupTest : public ExpressionTestsBase {
 protected:
  void setDefaultInitialAssignment(int startObjectIndex, int endObjectIndex) {
    std::vector<std::string> objects;
    for (int i = startObjectIndex; i <= endObjectIndex; i++) {
      objects.push_back(fmt::format("object{}", i));
    }
    setInitialAssignment(InitialAssignment{{"container1", objects}});
  }

  entities::ScopeId scope() const {
    return ExpressionTestsBase::scopeId("scope");
  }

  entities::ScopeItemId scopeItem() const {
    return ExpressionTestsBase::scopeItemId(scope(), "scopeItem");
  }

  entities::ScopeId rack() const {
    return ExpressionTestsBase::scopeId("rack");
  }

  entities::ScopeItemId rack(const int i) const {
    return ExpressionTestsBase::scopeItemId(rack(), fmt::format("rack{}", i));
  }

  entities::ScopeId host() const {
    return ExpressionTestsBase::scopeId("host");
  }

  entities::ScopeItemId host(const int i) const {
    return ExpressionTestsBase::scopeItemId(host(), fmt::format("host{}", i));
  }

  entities::GroupId group(const int i) const {
    return groupId(partitionId("partition1"), fmt::format("group{}", i));
  }
};

CO_TEST_F(ObjectPartitionLookupTest, ObjectPartition) {
  setDefaultInitialAssignment(1, 5);

  co_await addPartition(
      "partition1",
      {{"group1", {"object1", "object2", "object3"}},
       {"group2", {"object4"}},
       {"group3", {"object5"}}});

  buildUniverse();
  const auto& universe = getUniverse();

  const ObjectPartition objectPartitionExpr(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      dimensionId("object_count"),
      {});

  const PackerMap<entities::ObjectId, std::vector<entities::GroupId>> groups = {
      {object(1), {group(1)}},
      {object(2), {group(1)}},
      {object(3), {group(1)}},
      {object(4), {group(2)}},
      {object(5), {group(3)}}};

  EXPECT_EQ(groups, objectPartitionExpr.getObjectGroups());
}

CO_TEST_F(ObjectPartitionLookupTest, NegativeLimitsObjectPartitionLookup) {
  setInitialAssignment(
      InitialAssignment{
          {"container1", {"object1"}},
          {"container2", {"object2", "object6", "object7", "object9"}},
          {"container3",
           {"object3", "object4", "object5", "object8", "object10"}}});

  co_await addPartition(
      "partition1",
      {{"group0", {"object1", "object2", "object3", "object9", "object10"}},
       {"group1", {"object4", "object5", "object10"}},
       {"group2", {"object6", "object7", "object8", "object9", "object10"}}});

  co_await addScope("scope", {{"scopeItem", {"container1"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      dimensionId("object_count"),
      {{group(0), 1}, {group(1), 1.5}, {group(2), 3}});

  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{
                  container(1), container(2)}), // containers
          scope(),
          scopeItem(),
          universe,
          assignment,
          /*groupLimitOverrides=*/{{group(0), 3}, {group(1), -10}},
          /*initialDuringObjects=*/{object(9)}));

  // -10 override means we are always going to add 10 to the total
  EXPECT_EQ(10.0, apply(objectPartitionLookup, assignment));

  EXPECT_EQ(10.0, objectPartitionLookup->value);
  EXPECT_EQ(10.0, evaluate(objectPartitionLookup, {}, assignment));

  // overload partition 0
  EXPECT_EQ(
      11.0,
      evaluate(objectPartitionLookup, {{object(3), container(2)}}, assignment));

  // move object with 3 groups over, g0 and g1 exceeded by 1
  EXPECT_EQ(
      13.0,
      evaluate(
          objectPartitionLookup, {{object(10), container(2)}}, assignment));

  // overload partition 0 and partition 2
  EXPECT_EQ(
      12.0,
      evaluate(
          objectPartitionLookup,
          {{object(3), container(2)}, {object(8), container(1)}},
          assignment));

  // overload partition 0 and partition 2, move during away
  EXPECT_EQ(
      12.0,
      evaluate(
          objectPartitionLookup,
          {{object(3), container(2)},
           {object(8), container(1)},
           {object(9), container(3)}},
          assignment));

  // overload partition 0 and change partition 2
  EXPECT_EQ(
      11.0,
      evaluate(
          objectPartitionLookup,
          {{object(3), container(2)}, {object(6), container(3)}},
          assignment));

  // overload partition 1
  EXPECT_EQ(
      12.0,
      evaluate(
          objectPartitionLookup,
          {{object(4), container(1)}, {object(5), container(2)}},
          assignment));

  // check bounds
  EXPECT_EQ(10.0, lower_bound(*objectPartitionLookup));
  // 13 total object/groups along with 10 from the negative override
  EXPECT_EQ(17.0, upper_bound(*objectPartitionLookup));
}

CO_TEST_F(ObjectPartitionLookupTest, ObjectPartitionLookup) {
  setInitialAssignment(
      InitialAssignment{
          {"container1", {"object1"}},
          {"container2", {"object2", "object6", "object7", "object9"}},
          {"container3",
           {"object3", "object4", "object5", "object8", "object10"}}});

  co_await addPartition(
      "partition1",
      {{"group0", {"object1", "object2", "object3", "object9", "object10"}},
       {"group1", {"object4", "object5", "object10"}},
       {"group2", {"object6", "object7", "object8", "object9", "object10"}}});

  co_await addScope("scope", {{"scopeItem", {"container1"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      dimensionId("object_count"),
      {{group(0), 1}, {group(1), 1.5}, {group(2), 3}});

  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{
                  container(1), container(2)}), // containers
          scope(),
          scopeItem(),
          universe,
          assignment,
          /*groupLimitOverrides=*/{{group(0), 3}},
          /*initialDuringObjects=*/{object(9)}));

  EXPECT_EQ(0.0, apply(objectPartitionLookup, assignment));

  EXPECT_EQ(0.0, objectPartitionLookup->value);
  EXPECT_EQ(0.0, evaluate(objectPartitionLookup, {}, assignment));

  // overload partition 0
  EXPECT_EQ(
      1.0,
      evaluate(objectPartitionLookup, {{object(3), container(2)}}, assignment));

  // move object with 3 groups over, g0 and g1 exceeded by 1
  EXPECT_EQ(
      2.0,
      evaluate(
          objectPartitionLookup, {{object(10), container(2)}}, assignment));

  // overload partition 0 and partition 2
  EXPECT_EQ(
      2.0,
      evaluate(
          objectPartitionLookup,
          {{object(3), container(2)}, {object(8), container(1)}},
          assignment));

  // overload partition 0 and partition 2, move during away
  EXPECT_EQ(
      2.0,
      evaluate(
          objectPartitionLookup,
          {{object(3), container(2)},
           {object(8), container(1)},
           {object(9), container(3)}},
          assignment));

  // overload partition 0 and change partition 2
  EXPECT_EQ(
      1.0,
      evaluate(
          objectPartitionLookup,
          {{object(3), container(2)}, {object(6), container(3)}},
          assignment));

  // overload partition 1
  EXPECT_EQ(
      0.5,
      evaluate(
          objectPartitionLookup,
          {{object(4), container(1)}, {object(5), container(2)}},
          assignment));

  // apply assignment overloading partition 0
  assignment.moveTo(object(3), container(2));
  EXPECT_EQ(1.0, apply(objectPartitionLookup, assignment));
  EXPECT_EQ(1.0, evaluate(objectPartitionLookup, {}, assignment));
  EXPECT_TRUE(descendingChildPotentialsAsExpected(*objectPartitionLookup, {}));

  // moving during object away should do nothing
  EXPECT_EQ(
      1.0,
      evaluate(objectPartitionLookup, {{object(9), container(3)}}, assignment));

  // stop overloading partition 0
  EXPECT_EQ(
      0.0,
      evaluate(objectPartitionLookup, {{object(1), container(3)}}, assignment));

  // alleviate parition 0 but overload partition 1
  EXPECT_EQ(
      0.5,
      evaluate(
          objectPartitionLookup,
          {{object(1), container(3)},
           {object(4), container(1)},
           {object(5), container(2)}},
          assignment));

  // moving during
  assignment.moveTo(object(9), container(3));
  EXPECT_EQ(1.0, apply(objectPartitionLookup, assignment));
  EXPECT_EQ(1.0, evaluate(objectPartitionLookup, {}, assignment));

  // check bounds
  EXPECT_EQ(0.0, lower_bound(*objectPartitionLookup));
  EXPECT_EQ(5.5, upper_bound(*objectPartitionLookup));

  auto changes = ObjectToNewContainer{
      {object(9), container(2)}, {object(3), container(3)}};
  EXPECT_EQ(0.0, applyChanges(objectPartitionLookup, changes, assignment));

  // undo the last 2 apply
  assignment.moveTo(object(3), container(3));
  assignment.moveTo(object(9), container(2));
  EXPECT_EQ(0.0, evaluate(objectPartitionLookup, {}, assignment));
  EXPECT_EQ(
      1.0,
      evaluate(objectPartitionLookup, {{object(3), container(2)}}, assignment));

  EXPECT_EQ(
      2.0,
      evaluate(
          objectPartitionLookup, {{object(10), container(2)}}, assignment));
  EXPECT_TRUE(descendingChildPotentialsAsExpected(*objectPartitionLookup, {}));
}

CO_TEST_F(
    ObjectPartitionLookupTest,
    ObjectPartitionLookupNonOverrideNegativeLimit) {
  setInitialAssignment(
      InitialAssignment{
          {"container1", {}}, {"container2", {}}, {"container3", {"object1"}}});

  co_await addPartition("partition1", {{"group1", {"object1"}}});

  co_await addScope("scope", {{"scopeItem", {"container1"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      dimensionId("object_count"),
      {{group(1), -42}});

  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{
                  container(1), container(2)}), // containers
          scope(),
          scopeItem(),
          universe,
          assignment,
          {}, // limit_overrides
          {} // initial_during_objects
          ));

  EXPECT_EQ(42, apply(objectPartitionLookup, assignment));
}

CO_TEST_F(
    ObjectPartitionLookupTest,
    ObjectPartitionLookupBoundsNegativeObjectWeight) {
  setInitialAssignment(
      InitialAssignment{
          {"container1", {"object1", "object2"}}, {"container2", {}}});

  co_await addPartition("partition1", {{"group1", {"object1", "object2"}}});

  co_await addObjectDimension(
      "object_weight", {{"object1", 50}, {"object2", -10}}, 1.0);

  co_await addScope("scope", {{"scopeItem", {"container1"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  const auto weightDim = dimensionId("object_weight");
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      weightDim,
      {{group(1), -15}});

  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{
                  container(1), container(2)}), // containers
          scope(),
          scopeItem(),
          universe,
          assignment,
          {}, // limit_overrides
          {} // initial_during_objects
          ));

  EXPECT_EQ(5, lower_bound(*objectPartitionLookup));
  EXPECT_EQ(65, upper_bound(*objectPartitionLookup));
}

CO_TEST_F(
    ObjectPartitionLookupTest,
    ObjectPartitionLookupNegativeLimitZeroWeights) {
  setInitialAssignment(
      InitialAssignment{
          {"container1", {"object1", "object2"}}, {"container2", {}}});

  co_await addPartition("partition1", {{"group1", {"object1", "object2"}}});

  co_await addObjectDimension(
      "object_weight", {{"object1", 0.0}, {"object2", 0.0}}, 1.0);

  co_await addScope("scope", {{"scopeItem", {"container1"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  const auto weightDim = dimensionId("object_weight");
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      weightDim,
      {{group(1), -15}});

  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{
                  container(1), container(2)}), // containers
          scope(),
          scopeItem(),
          universe,
          assignment,
          {}, // limit_overrides
          {} // initial_during_objects
          ));

  EXPECT_EQ(15, lower_bound(*objectPartitionLookup));
  EXPECT_EQ(15, upper_bound(*objectPartitionLookup));
}

CO_TEST_F(ObjectPartitionLookupTest, ObjectPartitionLookupNegativeLimits) {
  setInitialAssignment(
      InitialAssignment{
          {"container1", {"object1", "object2", "object3", "object4"}},
          {"container2", {}}});

  co_await addPartition(
      "partition1",
      {{"group1", {"object1", "object2"}}, {"group2", {"object3", "object4"}}});

  co_await addObjectDimension(
      "object_weight",
      {{"object1", 0.0}, {"object2", 0.0}, {"object3", 10}, {"object4", 20}},
      1.0);

  co_await addScope("scope", {{"scopeItem", {"container1"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  const auto weightDim = dimensionId("object_weight");
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      weightDim,
      {{group(1), -15}, {group(2), -15}});

  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{
                  container(1), container(2)}), // containers
          scope(),
          scopeItem(),
          universe,
          assignment,
          {}, // limit_overrides
          {} // initial_during_objects
          ));

  EXPECT_EQ(30, lower_bound(*objectPartitionLookup));
  EXPECT_EQ(60, upper_bound(*objectPartitionLookup));
}

CO_TEST_F(ObjectPartitionLookupTest, ObjectPartitionLookupWithSquares) {
  setInitialAssignment(
      InitialAssignment{
          {"container0",
           {"object0",
            "object1",
            "object3",
            "object4",
            "object5",
            "object7",
            "object8",
            "object9",
            "object10"}},
          {"container1", {"object2", "object6", "object11"}}});

  co_await addPartition(
      "partition1",
      {{"group0", {"object0", "object1", "object2"}},
       {"group1", {"object3", "object4", "object5", "object6"}},
       {"group2", {"object7", "object8", "object9", "object10", "object11"}}});

  co_await addScope("scope", {{"scopeItem", {"container1"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      dimensionId("object_count"),
      {{group(0), 0}, {group(1), 1}, {group(2), 10}},
      /*scopeItemIds=*/std::nullopt,
      /*groupCoefficients=*/
      {
          {group(0), 1.0 / 3},
          {group(1), 1.0 / 4},
          {group(2), 1.0 / 5},
      });

  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container(0)}), // containers
          scope(),
          scopeItem(),
          universe,
          assignment,
          /*groupLimitOverrides=*/{},
          /*initialDuringObjects=*/{},
          /*defaultGroupLimitOverride=*/2,
          /*penaltyTransform=*/
          ObjectPartitionLookupPenaltyTransform::SQUARE));

  // skip lp expr evaluation since only IDENTITY and STEP penalty transforms are
  // supported
  LpAssertOptions lpAssertOptions = {
      .exceptionForLpExpr =
          "ObjectPartitionLookup: only IDENTITY and STEP penalty transforms are supported in LP"};

  // With normalization, apply() must return (0/3)^2 + (1/4)^2 + (2/5)^2 =
  // 0.2225
  EXPECT_NEAR(
      0.222500,
      apply(objectPartitionLookup, assignment, lpAssertOptions),
      kEps);

  // Move adds (1/3)^2 = 0.1111 to 0.2225
  EXPECT_NEAR(
      0.333611,
      evaluate(
          objectPartitionLookup,
          {{object(2), container(0)}},
          assignment,
          lpAssertOptions),
      kEps);
  EXPECT_NEAR(
      0.410000,
      evaluate(
          objectPartitionLookup,
          {{object(6), container(0)}},
          assignment,
          lpAssertOptions),
      kEps);
  EXPECT_NEAR(
      0.422500,
      evaluate(
          objectPartitionLookup,
          {{object(11), container(0)}},
          assignment,
          lpAssertOptions),
      kEps);
  EXPECT_NEAR(
      0.721111,
      evaluate(
          objectPartitionLookup,
          {{object(2), container(0)},
           {object(6), container(0)},
           {object(11), container(0)}},
          assignment,
          lpAssertOptions),
      kEps);
  EXPECT_NEAR(
      0.222500,
      evaluate(
          objectPartitionLookup,
          {{object(0), container(1)}},
          assignment,
          lpAssertOptions),
      kEps);
  EXPECT_NEAR(
      0.16,
      evaluate(
          objectPartitionLookup,
          {{object(4), container(1)}},
          assignment,
          lpAssertOptions),
      kEps);
  EXPECT_NEAR(
      0.102500,
      evaluate(
          objectPartitionLookup,
          {{object(10), container(1)}},
          assignment,
          lpAssertOptions),
      kEps);

  auto changes = ObjectToNewContainer{
      {object(6), container(0)},
      {object(2), container(0)},
      {object(10), container(1)}};
  EXPECT_NEAR(
      0.401111,
      applyChanges(objectPartitionLookup, changes, assignment, lpAssertOptions),
      kEps);

  EXPECT_EQ(0.0, lower_bound(*objectPartitionLookup));
  EXPECT_NEAR(0.721111, upper_bound(*objectPartitionLookup), kEps);
}

CO_TEST_F(ObjectPartitionLookupTest, ObjectPartitionLookupWithStep) {
  // container0 holds 2 objects of group0 and 1 object of group1. group2's only
  // object starts elsewhere.
  setInitialAssignment(
      InitialAssignment{
          {"container0", {"object0", "object1", "object2"}},
          {"container1", {"object3"}}});

  co_await addPartition(
      "partition1",
      {{"group0", {"object0", "object1"}},
       {"group1", {"object2"}},
       {"group2", {"object3"}}});

  co_await addScope("scope", {{"scopeItem", {"container1"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  // g1 has limit 1 (= its count), so STEP=0 despite being present. g0 and g2
  // default to limit 0, so any object in them yields STEP=1.
  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      dimensionId("object_count"),
      {{group(1), 1}});

  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container(0)}),
          scope(),
          scopeItem(),
          universe,
          assignment,
          /*groupLimitOverrides=*/{},
          /*initialDuringObjects=*/{},
          /*defaultGroupLimitOverride=*/std::nullopt,
          /*penaltyTransform=*/ObjectPartitionLookupPenaltyTransform::STEP));

  // g0 over limit -> 1, g1 at limit -> 0, g2 elsewhere -> 0. Sum = 1.
  EXPECT_EQ(1.0, apply(objectPartitionLookup, assignment));

  // Move g2 into c0: g2 now over limit -> 1. Sum = g0 + g2 = 2.
  EXPECT_EQ(
      2.0,
      evaluate(objectPartitionLookup, {{object(3), container(0)}}, assignment));

  // Move g1 out of c0: g1 now under limit, STEP still 0. Sum = g0 = 1.
  EXPECT_EQ(
      1.0,
      evaluate(objectPartitionLookup, {{object(2), container(1)}}, assignment));

  // All groups can be at-or-under their limits -> lower_bound = 0.
  // Max STEP per group: g0 = 1, g1 = 0 (max weight 1 = limit), g2 = 1 ->
  // upper_bound = 2.
  EXPECT_EQ(0.0, lower_bound(*objectPartitionLookup));
  EXPECT_EQ(2.0, upper_bound(*objectPartitionLookup));
}

CO_TEST_F(
    ObjectPartitionLookupTest,
    ObjectPartitionLookupWithStepAndGroupsAllowed) {
  // 5 groups with one object each. g0..g3 have a limit of 0; g4 has a limit
  // of 1. We count how many groups are over their limit in container0;
  // container1 just holds objects that are currently moved "out" of container0.
  //
  // groupsAllowed_=3 lets us ignore up to 3 of those over-limit groups, so
  // the count we return is max(0, over_limit_count - 3).
  setInitialAssignment(
      InitialAssignment{
          {"container0", {"object0", "object1", "object2", "object3"}},
          {"container1", {"object4"}}});

  co_await addPartition(
      "partition1",
      {{"group0", {"object0"}},
       {"group1", {"object1"}},
       {"group2", {"object2"}},
       {"group3", {"object3"}},
       {"group4", {"object4"}}});

  co_await addScope("scope", {{"scopeItem", {"container1"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      dimensionId("object_count"),
      {{group(4), 1}});

  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container(0)}),
          scope(),
          scopeItem(),
          universe,
          assignment,
          /*groupLimitOverrides=*/{},
          /*initialDuringObjects=*/{},
          /*defaultGroupLimitOverride=*/std::nullopt,
          /*penaltyTransform=*/ObjectPartitionLookupPenaltyTransform::STEP,
          /*groupsAllowed=*/3));

  // Allowing some groups to go over is only supported when minimizing.
  const LpAssertOptions lpOptions{
      .exceptionOnlyForLpExprMax =
          "ObjectPartitionLookup: groupsAllowed_ > 0 when minimizing=false is not supported in LP"};

  // 4 groups (g0..g3) are over their limit; ignore 3 -> count = 1.
  EXPECT_EQ(1.0, apply(objectPartitionLookup, assignment, lpOptions));

  // Move g0's object out: only 3 groups still over (g1..g3); ignore 3 -> 0.
  EXPECT_EQ(
      0.0,
      evaluate(
          objectPartitionLookup,
          {{object(0), container(1)}},
          assignment,
          lpOptions));

  // Move g0 and g1 out: only 2 groups over (g2, g3). We can ignore at most
  // 3, but there are only 2 to ignore -> count = 0.
  EXPECT_EQ(
      0.0,
      evaluate(
          objectPartitionLookup,
          {{object(0), container(1)}, {object(1), container(1)}},
          assignment,
          lpOptions));

  // Move g4's object into container0. g4 is still within its limit of 1, so
  // it isn't an over-limit group. The 4 original over-limit groups are
  // unchanged -> ignore 3 -> count = 1.
  EXPECT_EQ(
      1.0,
      evaluate(
          objectPartitionLookup,
          {{object(4), container(0)}},
          assignment,
          lpOptions));
}

CO_TEST_F(ObjectPartitionLookupTest, ObjectPartitionLookupGroupsLimit) {
  std::vector<std::string> allObjects;
  for (const auto i : folly::irange(16)) {
    allObjects.push_back(fmt::format("object{}", i));
  }
  setInitialAssignment(InitialAssignment{{"container1", allObjects}});

  co_await addPartition(
      "partition1",
      {{"group0",
        {"object0",
         "object1",
         "object2",
         "object3",
         "object4",
         "object5",
         "object6",
         "object7"}},
       {"group1",
        {"object8",
         "object9",
         "object10",
         "object11",
         "object12",
         "object13",
         "object14",
         "object15"}}});

  co_await addScope("scope", {{"scopeItem", {"container1"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  auto assignment1 =
      Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      dimensionId("object_count"),
      {{group(0), 3}, {group(1), 3}});

  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container(1)}), // containers
          scope(),
          scopeItem(),
          universe,
          assignment1,
          /*groupLimitOverrides=*/{},
          /*initialDuringObjects=*/{},
          /*defaultGroupLimitOverride=*/std::nullopt,
          /*penaltyTransform=*/
          ObjectPartitionLookupPenaltyTransform::IDENTITY,
          /*groupsAllowed=*/1));

  LpAssertOptions lpAssertOptions{
      .exceptionOnlyForLpExprMax =
          "ObjectPartitionLookup: groupsAllowed_ > 0 when minimizing=false is not supported in LP"};

  EXPECT_EQ(5.0, apply(objectPartitionLookup, assignment1, lpAssertOptions));
  EXPECT_EQ(5.0, objectPartitionLookup->value);

  auto assignment2 = Assignment(
      {{container(1),
        {object(5),
         object(6),
         object(7),
         object(8),
         object(9),
         object(10),
         object(11),
         object(12),
         object(13),
         object(14),
         object(15)}}});

  EXPECT_EQ(0.0, apply(objectPartitionLookup, assignment2, lpAssertOptions));
  EXPECT_EQ(0.0, objectPartitionLookup->value);
}

CO_TEST_F(ObjectPartitionLookupTest, ObjectPartitionLookup2) {
  setInitialAssignment(
      InitialAssignment{
          {"container0", {"object0", "object1"}},
          {"container1", {"object2", "object3"}}});

  co_await addPartition(
      "partition1", {{"group0", {"object0", "object1", "object2", "object3"}}});

  co_await addScope("scope", {{"scopeItem", {"container1"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      dimensionId("object_count"),
      {} /*limits*/,
      /*scopeItemIds=*/std::nullopt,
      {{group(0), 0.25}} /*coefficients*/);

  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container(0)}), // containers
          scope(),
          scopeItem(),
          universe,
          assignment,
          /*groupLimitOverrides=*/{},
          /*initialDuringObjects=*/{},
          /*defaultGroupLimitOverride=*/std::nullopt,
          /*penaltyTransform=*/
          ObjectPartitionLookupPenaltyTransform::SQUARE,
          /*groupsAllowed=*/0));

  // skip lp expr evaluation since only IDENTITY and STEP penalty transforms are
  // supported
  LpAssertOptions lpAssertOptions = {
      .exceptionForLpExpr =
          "ObjectPartitionLookup: only IDENTITY and STEP penalty transforms are supported in LP"};

  EXPECT_EQ(0.25, apply(objectPartitionLookup, assignment, lpAssertOptions));

  {
    auto changes = ObjectToNewContainer{{object(0), container(1)}};
    EXPECT_EQ(
        0.0625,
        applyChanges(
            objectPartitionLookup, changes, assignment, lpAssertOptions));
  }

  {
    // update assignment
    assignment.moveTo(object(0), container(1));
    auto changes = ObjectToNewContainer{{object(0), container(0)}};
    EXPECT_EQ(
        0.25,
        evaluate(objectPartitionLookup, changes, assignment, lpAssertOptions));
  }
}

CO_TEST_F(ObjectPartitionLookupTest, ObjectPartitionLookupCustomDimension) {
  setInitialAssignment(
      InitialAssignment{
          {"container0", {"object0", "object1", "object2"}},
          {"container1", {"object3"}}});

  co_await addPartition(
      "partition1", {{"group0", {"object0", "object1", "object2", "object3"}}});

  co_await addObjectDimension(
      "object_weight", {{"object1", 1.2}, {"object2", 1.4}}, 1.1);

  co_await addScope("scope", {{"scopeItem", {"container1"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  const auto weightDim = dimensionId("object_weight");
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto partitionExpr = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      weightDim,
      {} /*limits*/);

  auto lookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          partitionExpr,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container(0)}),
          scope(),
          scopeItem(),
          universe,
          assignment));

  // Test apply.
  EXPECT_NEAR(3.7, apply(lookup, assignment), 1e-8);

  // Test evaluate.
  {
    // Move object 1 from container 0 to container 1.
    auto changes = ObjectToNewContainer{{object(1), container(1)}};
    EXPECT_NEAR(2.5, evaluate(lookup, changes, assignment), 1e-8);
  }

  // Test bounds.
  EXPECT_EQ(0, lower_bound(*lookup));
  EXPECT_NEAR(4.8, upper_bound(*lookup), 1e-9);
}

CO_TEST_F(ObjectPartitionLookupTest, ObjectPartitionLookupMinBound) {
  setInitialAssignment(
      InitialAssignment{
          {"container0", {"object0", "object1", "object2"}},
          {"container1", {"object3"}}});

  co_await addPartition(
      "partition1",
      {{"group0", {"object0", "object1"}},
       {"group1", {"object2"}},
       {"group2", {"object3"}}});

  co_await addScope("scope", {{"scopeItem", {"container1"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      dimensionId("object_count"),
      {{group(0), 2.0}, {group(1), 0.0}, {group(2), 0.0}} /*limits*/,
      /*scopeItemIds=*/std::nullopt,
      /*groupCoefficients=*/{},
      /*defaultGroupLimit=*/1.0);

  auto lookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container(0)}),
          scope(),
          scopeItem(),
          universe,
          assignment,
          /*groupLimitOverrides=*/{},
          /*initialDuringObjects=*/{},
          /*defaultGroupLimitOverride=*/std::nullopt,
          /*penaltyTransform=*/
          ObjectPartitionLookupPenaltyTransform::IDENTITY,
          /*groupsAllowed=*/0,
          ObjectPartitionLookupDefault::Bound::MIN));

  // initially violations w.r.t. group1
  EXPECT_NEAR(0, apply(lookup, assignment), 1e-8);

  // Move object 1 from container 0 to container 1.
  auto changes1 = ObjectToNewContainer{{object(1), container(1)}};
  EXPECT_NEAR(1, evaluate(lookup, changes1, assignment), 1e-8);

  // Move all objects  from container 0 to container 1. This makes the
  // value 2 (violation w.r.t. group0). Also, test partialApply.
  auto changes2 = ObjectToNewContainer{
      {object(2), container(1)},
      {object(1), container(1)},
      {object(0), container(1)}};
  EXPECT_NEAR(2, evaluate(lookup, changes2, assignment), 1e-8);
  EXPECT_NEAR(2, applyChanges(lookup, changes2, assignment), 1e-8);
}

CO_TEST_F(ObjectPartitionLookupTest, DynamicDimensionWithDifferentScope) {
  // Test case where the dynamic dimension's scope differs from the lookup's
  // scopeId. This tests innerFullApply, innerPartialApply, evaluate, and
  // getBounds when scopeMatchesDimensionScope_ is false.

  setInitialAssignment(
      InitialAssignment{
          {"container1", {"object1", "object3"}},
          {"container2", {"object2", "object4"}},
          {"container3", {}},
          {"container4", {"object5"}}});

  co_await addPartition(
      "partition1",
      {{"group1", {"object1", "object2"}},
       {"group2", {"object3", "object4", "object5"}}});

  // Create lookup scope
  co_await addScope(
      "rack",
      {{"rack0", {"container1", "container2", "container4"}},
       {"rack1", {"container3"}}});

  // Create dimension scope
  co_await addScope(
      "host",
      {{"host0", {"container1"}},
       {"host1", {"container2"}},
       {"host2", {"container3"}}});

  // Create dynamic dimension with different weights per scope item
  co_await addDynamicObjectDimension(
      "dynamicWeight",
      host(),
      {{"host0", {{"object1", 1.0}, {"object3", 2.0}}},
       {"host1", {{"object2", 3.0}, {"object4", 4.0}}}},
      2.0);

  buildUniverse();
  const auto& universe = getUniverse();
  const auto weightDim = dimensionId("dynamicWeight");
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      weightDim,
      {{group(1), 2.0}, {group(2), 3.0}},
      PackerSet<entities::ScopeItemId>{host(0), host(1)},
      {} /*groupCoefficients*/,
      0.0 /*defaultGroupLimit*/,
      1.0 /*defaultGroupCoefficient*/);

  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{
                  container(1), container(2), container(4)}),
          rack(),
          rack(0),
          universe,
          assignment,
          /*groupLimitOverrides=*/{},
          /*initialDuringObjects=*/{}));

  // Test innerFullApply
  // Initial state:
  // - container(1) has object(1) with weight 1.0 and object(3) with weight 2.0
  // - container(2) has object(2) with weight 3.0 and object(4) with weight 4.0
  // - container(4) has object(5) with default weight 2.0
  // Group1: 1.0 + 3.0 = 4.0, limit=2.0, penalty = 2.0
  // Group2: 2.0 + 4.0 + 2.0 = 8.0, limit=3.0, penalty = 5.0
  // Total = 7.0
  EXPECT_EQ(7.0, apply(objectPartitionLookup, assignment));

  // Test evaluate - move object(2) from container(2) to container(1)
  // object(2) in container(1) would have weight based on host0
  // object(2) doesn't have entry in host0, so default = 2.0
  // Group1: 1.0 + 2.0 = 3.0, limit=2.0, penalty = 1.0
  // Group2: 2.0 + 4.0 + 2.0 = 8.0, limit=3.0, penalty = 5.0
  // Total = 6.0
  EXPECT_EQ(
      6.0,
      evaluate(objectPartitionLookup, {{object(2), container(1)}}, assignment));
  EXPECT_EQ(
      6.0,
      applyChanges(
          objectPartitionLookup, {{object(2), container(1)}}, assignment));

  // Test innerPartialApply
  assignment.moveTo(object(2), container(1));
  EXPECT_EQ(6.0, apply(objectPartitionLookup, assignment));

  // Test getBounds
  // Lower bound: minimum penalty (when all groups are at their minimum)
  EXPECT_EQ(0.0, lower_bound(*objectPartitionLookup));
  // Upper bound: maximum weights object(1)=2.0, object(2)=3.0, object(3)=2.0,
  // object(4)=4.0, object(5)=2.0 (Max for each object considering all possible
  // scope items in the dimension) Group1 max: 2.0+3.0 = 5.0, limit=2.0,
  // penalty=3.0 Group2 max: 2.0+4.0+2.0 = 8.0, limit=3.0, penalty=5.0 Total
  // = 8.0 Note: ObjectPartition computes bounds based on all possible weights
  // across all scope items in the dimension.
  EXPECT_EQ(8.0, upper_bound(*objectPartitionLookup));
}

CO_TEST_F(
    ObjectPartitionLookupTest,
    DynamicDimensionWithDifferentScopeAndLimitOverrides) {
  // Test that limit overrides work correctly with different dimension scope

  setInitialAssignment(
      InitialAssignment{
          {"container1", {"object1"}},
          {"container2", {"object2"}},
          {"container3", {"object3"}}});

  co_await addPartition(
      "partition1", {{"group1", {"object1", "object2", "object3"}}});

  // Create lookup scope
  co_await addScope("rack", {{"rack0", {"container1"}}});

  // Create dimension scope
  co_await addScope(
      "host",
      {{"host0", {"container1"}},
       {"host1", {"container2"}},
       {"host2", {"container3"}}});

  // Create dynamic dimension with different weights per scope item
  co_await addDynamicObjectDimension(
      "dynamicWeight",
      host(),
      {{"host0", {{"object1", 10.0}}},
       {"host1", {{"object2", 20.0}}},
       {"host2", {{"object3", 30.0}}}},
      5.0);

  buildUniverse();
  const auto& universe = getUniverse();
  const auto weightDim = dimensionId("dynamicWeight");
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      weightDim,
      {{group(1), 50.0}},
      PackerSet<entities::ScopeItemId>{host(0), host(1)});

  // Test with limit override
  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container(1), container(2)}),
          rack(),
          rack(0),
          universe,
          assignment,
          /*groupLimitOverrides=*/{{group(1), 15.0}}, // reduce limit to 15.0
          /*initialDuringObjects=*/{}));

  // container(1) has object(1) with weight 10.0
  // container(2) has object(2) with weight 20.0
  // Group1: 10.0 + 20.0 = 30.0, limit=15.0, penalty = 15.0
  EXPECT_EQ(15.0, apply(objectPartitionLookup, assignment));

  // Move object(3) to container(1): object(3) will have default weight 5.0
  // Group1: 10.0 + 20.0 + 5.0 = 35.0, limit=15.0, penalty = 20.0
  EXPECT_EQ(
      20.0,
      evaluate(objectPartitionLookup, {{object(3), container(1)}}, assignment));
  EXPECT_EQ(
      20.0,
      applyChanges(
          objectPartitionLookup, {{object(3), container(1)}}, assignment));

  // Test getBounds with overrides
  EXPECT_EQ(0.0, lower_bound(*objectPartitionLookup));
  // Upper bound: Max weights for objects in lookup containers
  // object(1) in container(1): 10.0
  // object(2) in container(2): 20.0
  // object(3) in container(1) or container(2): 5.0
  // Group1: 10.0 + 20.0 + 5.0 = 35.0, limit=15.0, penalty = 20.0
  EXPECT_EQ(20.0, upper_bound(*objectPartitionLookup));
}

CO_TEST_F(
    ObjectPartitionLookupTest,
    DynamicDimensionWithDifferentScopeMinBound) {
  // Test MIN bound with different dimension scope

  setInitialAssignment(
      InitialAssignment{
          {"container1", {"object1"}},
          {"container2", {"object2"}},
          {"container3", {"object3"}}});

  co_await addPartition(
      "partition1",
      {{"group1", {"object1", "object2"}}, {"group2", {"object3"}}});

  // Create lookup scope
  co_await addScope("rack", {{"rack0", {"container1", "container2"}}});

  // Create dimension scope
  co_await addScope(
      "host",
      {{"host0", {"container1"}},
       {"host1", {"container2"}},
       {"host2", {"container3"}}});

  // Create dynamic dimension with different weights per scope item
  co_await addDynamicObjectDimension(
      "dynamicWeight",
      host(),
      {{"host0", {{"object1", 2.0}}},
       {"host1", {{"object2", 3.0}}},
       {"host2", {{"object3", 1.0}}}},
      1.0);

  buildUniverse();
  const auto& universe = getUniverse();
  const auto weightDim = dimensionId("dynamicWeight");
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      weightDim,
      {{group(1), 10.0}, {group(2), 5.0}},
      PackerSet<entities::ScopeItemId>{host(0), host(1)});

  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container(1), container(2)}),
          rack(),
          rack(0),
          universe,
          assignment,
          /*groupLimitOverrides=*/{},
          /*initialDuringObjects=*/{},
          /*defaultGroupLimitOverride=*/std::nullopt,
          /*penaltyTransform=*/
          ObjectPartitionLookupPenaltyTransform::IDENTITY,
          /*groupsAllowed=*/0,
          ObjectPartitionLookupDefault::Bound::MIN));

  // container(1) has object(1) with weight 2.0
  // container(2) has object(2) with weight 3.0
  // MIN bound: penalty for being below limit
  // Group1: 2.0 + 3.0 = 5.0, limit=10.0, penalty = 10.0 - 5.0 = 5.0
  // Group2: 0.0, limit=5.0, penalty = 5.0
  // Total = 10.0
  EXPECT_EQ(10.0, apply(objectPartitionLookup, assignment));

  // Move object(3) to container(1): weight will be default 1.0
  // Group1: 2.0 + 3.0, limit=10.0, penalty = 5.0
  // Group2: 1.0, limit=5.0, penalty = 4.0
  // Total = 9.0
  EXPECT_EQ(
      9.0,
      evaluate(objectPartitionLookup, {{object(3), container(1)}}, assignment));
  EXPECT_EQ(
      9.0,
      applyChanges(
          objectPartitionLookup, {{object(3), container(1)}}, assignment));

  // Test partialApply
  assignment.moveTo(object(3), container(1));
  EXPECT_EQ(9.0, apply(objectPartitionLookup, assignment));

  // Test bounds
  // Lower bound (MIN): minimum penalty when groups are filled to capacity
  // Group1: max weight 2.0+3.0=5.0, limit=10.0, penalty = max(0, 10-5) = 5.0
  // Group2: max weight 1.0 (default), limit=5.0, penalty = max(0, 5-1) = 4.0
  // Total = 9.0
  EXPECT_EQ(9.0, lower_bound(*objectPartitionLookup));
  // Upper bound: when all groups are empty
  // Group1: 0.0, limit=10.0, penalty = 10.0
  // Group2: 0.0, limit=5.0, penalty = 5.0
  // Total = 15.0
  EXPECT_EQ(15.0, upper_bound(*objectPartitionLookup));
}

CO_TEST_F(
    ObjectPartitionLookupTest,
    DynamicDimensionWithDifferentScopeThrowsWithInitialDuringObjects) {
  // Test that constructor throws when using different dimension scope with
  // initialDuringObjects (per the restriction in lines 69-74 of
  // ObjectPartitionLookup.cpp)

  setInitialAssignment(
      InitialAssignment{
          {"container1", {"object1"}}, {"container2", {"object2"}}});

  co_await addPartition("partition1", {{"group1", {"object1", "object2"}}});

  // Create lookup scope
  co_await addScope("rack", {{"rack0", {"container1"}}});

  // Create dimension scope (different from lookup scope)
  co_await addScope("host", {{"host0", {"container1"}}});

  // Create dynamic dimension
  co_await addDynamicObjectDimension(
      "dynamicWeight", host(), {{"host0", {{"object1", 1.0}}}}, 1.0);

  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  const auto weightDim = dimensionId("dynamicWeight");

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      weightDim,
      {{group(1), 2.0}},
      PackerSet<entities::ScopeItemId>{host(0)});

  // Should throw when trying to create with initialDuringObjects
  REBALANCER_EXPECT_RUNTIME_ERROR(
      std::make_shared<ObjectPartitionLookupDefault>(
          ObjectPartitionLookupDefault(
              objectPartition,
              std::make_shared<PackerSet<entities::ContainerId>>(
                  PackerSet<entities::ContainerId>{container(1)}),
              rack(),
              rack(0),
              universe,
              assignment,
              /*groupLimitOverrides=*/{},
              /*initialDuringObjects=*/{object(1)})),
      "ObjectPartitionLookup: initialDuringObjects_ is expected to be empty when dynamic dimension's scope differs from the main scope");
}

CO_TEST_F(
    ObjectPartitionLookupTest,
    DynamicDimensionWithDifferentScopeGetObjectPotentials) {
  // Test that getObjectPotentials works correctly with different dimension
  // scope. This covers lines 600-607 and 219-221 in ObjectPartitionLookup.cpp

  setInitialAssignment(
      InitialAssignment{
          {"container1", {"object1", "object3"}},
          {"container2", {"object2", "object4"}}});

  co_await addPartition(
      "partition1",
      {{"group1", {"object1", "object2"}}, {"group2", {"object3", "object4"}}});

  // Create lookup scope
  co_await addScope("rack", {{"rack0", {"container1", "container2"}}});

  // Create dimension scope (different from lookup scope)
  co_await addScope(
      "host", {{"host0", {"container1"}}, {"host1", {"container2"}}});

  // Create dynamic dimension with different weights per scope item
  co_await addDynamicObjectDimension(
      "dynamicWeight",
      host(),
      {{"host0", {{"object1", 2.0}, {"object3", 6.0}}},
       {"host1", {{"object2", 4.0}, {"object4", 5.0}}}},
      1.0);

  buildUniverse();
  const auto& universe = getUniverse();
  const auto weightDim = dimensionId("dynamicWeight");
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      weightDim,
      {{group(1), 3.0}, {group(2), 4.0}},
      PackerSet<entities::ScopeItemId>{host(0), host(1)});

  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container(1), container(2)}),
          rack(),
          rack(0),
          universe,
          assignment,
          /*groupLimitOverrides=*/{},
          /*initialDuringObjects=*/{}));

  // Apply to initialize
  EXPECT_EQ(10.0, apply(objectPartitionLookup, assignment));

  // Call getObjectPotentials directly
  auto potentials = objectPartitionLookup->getObjectPotentials(true);

  // Verify the retrieved potentials for the contributing objects
  // group(1) has total weight 6.0 (2.0+4.0), limit 3.0, penalty 3.0
  // group(2) has total weight 8.0 (6.0+5.0), limit 4.0, penalty 7.0
  // Potential = reduction in penalty if object is removed
  // object(1) in group(1), weight 2.0: removing gives penalty 1.0
  //   potential = 3.0-1.0 = 2.0
  // object(2) in group(1), weight 4.0: removing gives penalty 0.0
  //   potential = 3.0-0.0 = 3.0
  // object(3) in group(2), weight 6.0: removing gives penalty 1.0
  //   potential = 7.0-1.0 = 6.0
  // object(4) in group(2), weight 5.0: removing gives penalty 2.0
  //   potential = 7.0-2.0 = 5.0
  // Sorted by descending potential:
  //   object(4)=4.0, object(3)=3.0, object(2)=3.0, object(1)=2.0
  EXPECT_EQ(
      std::vector<ObjectPotential>(potentials.begin(), potentials.end()),
      std::vector<ObjectPotential>(
          {{object(3), 6.0},
           {object(4), 5.0},
           {object(2), 3.0},
           {object(1), 2.0}}));
}

CO_TEST_F(ObjectPartitionLookupTest, GetterMethods) {
  // Test simple getter methods: getPartitionId(), getScopeId(),
  // getScopeItemId(), getGroupObjectWeights(), and get_sorted_children()

  setInitialAssignment(
      InitialAssignment{
          {"container1", {"object1", "object3"}}, {"container2", {"object2"}}});

  co_await addPartition(
      "partition1",
      {{"group1", {"object1", "object2"}}, {"group2", {"object3"}}});

  co_await addScope("scope", {{"scopeItem", {"container1", "container2"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = ObjectPartition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      dimensionId("object_count"),
      {{group(1), 2.0}, {group(2), 1.0}});

  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          std::make_shared<ObjectPartition>(objectPartition),
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container(1), container(2)}),
          scope(),
          scopeItem(),
          universe,
          assignment));

  // Apply to initialize the lookup
  apply(objectPartitionLookup, assignment);

  // Test getPartitionId()
  EXPECT_EQ(partitionId("partition1"), objectPartitionLookup->getPartitionId());

  // Test getScopeId()
  EXPECT_EQ(scope(), objectPartitionLookup->getScopeId());

  // Test getScopeItemId()
  EXPECT_EQ(scopeItem(), objectPartitionLookup->getScopeItemId());

  // Test getGroupObjectWeights() - should return a non-empty map after apply
  const auto& groupObjectWeights =
      objectPartitionLookup->getGroupObjectWeights();
  EXPECT_FALSE(groupObjectWeights.empty());
  EXPECT_TRUE(groupObjectWeights.count(group(1)) > 0);
  EXPECT_TRUE(groupObjectWeights.count(group(2)) > 0);

  // Test get_sorted_children() - should return empty vector
  auto sortedChildren = objectPartitionLookup->get_sorted_children(true);
  EXPECT_TRUE(sortedChildren.empty());
}

CO_TEST_F(ObjectPartitionLookupTest, FilteredGroupIdsObjectCount) {
  setInitialAssignment(
      InitialAssignment{
          {"container1", {"object1", "object2", "object4", "object6"}},
          {"container2", {"object3", "object5", "object7"}},
          {"container3", {}}});

  co_await addPartition(
      "partition1",
      {{"group1", {"object1", "object2", "object3"}},
       {"group2", {"object4", "object5"}},
       {"group3", {"object6", "object7"}}});

  co_await addScope("scope", {{"scopeItem", {"container1", "container2"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  const auto filteredGroupIds =
      std::make_shared<const PackerSet<entities::GroupId>>(
          PackerSet<entities::GroupId>{group(1), group(2)});

  const auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1"), filteredGroupIds),
      dimensionId("object_count"),
      {{group(1), 2.0}, {group(2), 2.0}});

  auto objectPartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container(1), container(2)}),
          scope(),
          scopeItem(),
          universe,
          assignment));

  // Initial:
  // group(1) has 2 objects in container(1), 1 in container(2) = 3 total,
  // limit 2 = 1 over
  // group(2) has 1 object in container(1), 1 in container(2) = 2 total,
  // limit 2 = 0 over
  // group(3) is filtered out so doesn't contribute
  EXPECT_EQ(1.0, apply(objectPartitionLookup, assignment));

  // Move object(1) from g1 away, should reduce violation
  EXPECT_EQ(
      0.0,
      evaluate(objectPartitionLookup, {{object(1), container(3)}}, assignment));

  // Move object(4) from container(1) to container(2) - no change in violation
  // since both containers are in scope
  EXPECT_EQ(
      1.0,
      evaluate(objectPartitionLookup, {{object(4), container(2)}}, assignment));

  // Moving object(6) from filtered-out group(3) should have no effect
  EXPECT_EQ(
      1.0,
      evaluate(objectPartitionLookup, {{object(6), container(2)}}, assignment));
}

CO_TEST_F(ObjectPartitionLookupTest, ObjectPartitionLookupInitialValue) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1"}}, {"container1", {"object2"}}});
  co_await addPartition(
      "partition1",
      {{"group0", {"object0", "object1"}}, {"group1", {"object2"}}});
  co_await addScope("scope", {{"scopeItem", {"container0"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  auto objPart = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      dimensionId("object_count"),
      {{group(0), 1}});

  auto lookup = object_partition_lookup(
      objPart,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(0)}),
      scope(),
      scopeItem(),
      assignment);
  EXPECT_EQ(1.0, lookup->getInitialValue());
}

CO_TEST_F(
    ObjectPartitionLookupTest,
    OrderOfChangeProcessingMattersInPartialApply) {
  setInitialAssignment(
      InitialAssignment{
          {"container1", {"object1"}}, {"container2", {"object2"}}});

  co_await addPartition("partition1", {{"group0", {"object1", "object2"}}});

  co_await addScope("rack", {{"rack0", {"container1", "container2"}}});
  co_await addScope(
      "host", {{"host0", {"container1"}}, {"host1", {"container2"}}});

  co_await addDynamicObjectDimension(
      "dynamicWeight",
      host(),
      {{"host0", {{"object1", 1.0}, {"object2", 10.0}}},
       {"host1", {{"object1", 100.0}, {"object2", 1000.0}}}},
      0.0);

  buildUniverse();
  const auto& universe = getUniverse();
  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      dimensionId("dynamicWeight"),
      {{group(0), 0.0}},
      PackerSet<entities::ScopeItemId>{host(0), host(1)});

  auto opl = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container(1), container(2)}),
          rack(),
          rack(0),
          universe,
          assignment,
          /*groupLimitOverrides=*/{},
          /*initialDuringObjects=*/{}));

  apply(opl, assignment);

  const ObjectToNewContainer swap = {
      {object(1), container(2)}, {object(2), container(1)}};

  const double predicted = evaluate(opl, swap, assignment);
  const double partial = applyChanges(opl, swap, assignment);
  const double full = apply(opl, getModifiedAssignment(assignment, swap));
  EXPECT_EQ(full, predicted);
  EXPECT_EQ(full, partial);
}

} // namespace facebook::rebalancer::packer::tests
