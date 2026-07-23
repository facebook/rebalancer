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
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"

#include <fmt/format.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {
namespace {
std::shared_ptr<const PackerSet<entities::GroupId>> makeFilteredGroupIds(
    PackerSet<entities::GroupId> groupIds) {
  return std::make_shared<const PackerSet<entities::GroupId>>(
      std::move(groupIds));
}
} // namespace

class ObjectPartitionTest : public ExpressionTestsBase {
 protected:
  inline const static std::string kPartitionName = "partition1";

  entities::GroupId group(int index) {
    return groupId(partitionId(kPartitionName), fmt::format("group{}", index));
  }
};

CO_TEST_F(ObjectPartitionTest, ThrowsWhenGroupLimitsContainFilteredOutGroup) {
  setInitialAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"container1", {"object1", "object2", "object3"}}});

  co_await addPartition(
      kPartitionName,
      {{"group1", {"object1", "object2"}}, {"group2", {"object3"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  const auto partition1Id = partitionId(kPartitionName);
  const auto objectCountDimId = dimensionId("object_count");

  const auto filteredGroupIds = makeFilteredGroupIds({group(1)});

  // Try to create ObjectPartition with groupLimits containing group(2) which
  // is not in filteredGroupIds
  REBALANCER_EXPECT_RUNTIME_ERROR(
      ObjectPartition(
          std::make_shared<const PartitionInfo>(
              universe, partition1Id, filteredGroupIds),
          objectCountDimId,
          {{group(1), 1.0}, {group(2), 2.0}}), // group(2) not in filter
      "groupLimits contains group that is not in filteredGroupIds");
}

CO_TEST_F(
    ObjectPartitionTest,
    ThrowsWhenGroupCoefficientsContainFilteredOutGroup) {
  setInitialAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"container1", {"object1", "object2", "object3"}}});

  co_await addPartition(
      kPartitionName,
      {{"group1", {"object1", "object2"}}, {"group2", {"object3"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  const auto partition1Id = partitionId(kPartitionName);
  const auto objectCountDimId = dimensionId("object_count");

  const auto filteredGroupIds = makeFilteredGroupIds({group(1)});

  // Try to create ObjectPartition with groupCoefficients containing group(2)
  // which is not in filteredGroupIds
  REBALANCER_EXPECT_RUNTIME_ERROR(
      ObjectPartition(
          std::make_shared<const PartitionInfo>(
              universe, partition1Id, filteredGroupIds),
          objectCountDimId,
          {},
          std::nullopt,
          {{group(1), 1.0}, {group(2), 2.0}}, // group(2) not in filter
          1.0,
          1.0),
      "groupCoefficients contains group that is not in filteredGroupIds");
}

CO_TEST_F(ObjectPartitionTest, FilteredGroupIdsGetGroupLimitAndCoefficient) {
  setInitialAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"container1", {"object1", "object2", "object3", "object4"}}});

  co_await addPartition(
      kPartitionName,
      {{"group1", {"object1", "object2"}},
       {"group2", {"object3"}},
       {"group3", {"object4"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  const auto partition1Id = partitionId(kPartitionName);
  const auto objectCountDimId = dimensionId("object_count");

  const auto filteredGroupIds = makeFilteredGroupIds({group(1), group(2)});

  const ObjectPartition objectPartition(
      std::make_shared<const PartitionInfo>(
          universe, partition1Id, filteredGroupIds),
      objectCountDimId,
      {{group(1), 10.0}, {group(2), 20.0}},
      std::nullopt,
      {{group(1), 1.5}, {group(2), 2.5}});

  // Should work for filtered groups
  EXPECT_EQ(10.0, objectPartition.getGroupLimit(group(1)));
  EXPECT_EQ(20.0, objectPartition.getGroupLimit(group(2)));

  EXPECT_EQ(1.5, objectPartition.getGroupCoefficient(group(1)));
  EXPECT_EQ(2.5, objectPartition.getGroupCoefficient(group(2)));

  // Should throw for non-filtered group
  REBALANCER_EXPECT_RUNTIME_ERROR(
      objectPartition.getGroupLimit(group(3)),
      fmt::format(
          "Cannot get limit for group {} which is not in filteredGroupIds",
          group(3)));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      objectPartition.getGroupCoefficient(group(3)),
      fmt::format(
          "Cannot get coefficient for group {} which is not in filteredGroupIds",
          group(3)));
}

CO_TEST_F(ObjectPartitionTest, FilteredGroupIdsObjectGroups) {
  setInitialAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"container1",
           {"object1", "object2", "object3", "object4", "object5"}}});

  co_await addPartition(
      kPartitionName,
      {{"group1", {"object1", "object2", "object4"}},
       {"group2", {"object2", "object3", "object4"}},
       {"group3", {"object4", "object5"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  const auto partition1Id = partitionId(kPartitionName);
  const auto objectCountDimId = dimensionId("object_count");

  const auto filteredGroupIds = makeFilteredGroupIds({group(1), group(2)});

  const ObjectPartition objectPartition(
      std::make_shared<const PartitionInfo>(
          universe, partition1Id, filteredGroupIds),
      objectCountDimId,
      {{group(1), 10.0}, {group(2), 20.0}});

  const PackerMap<entities::ObjectId, std::vector<entities::GroupId>>
      expectedGroups = {
          {object(1), {group(1)}},
          {object(2), {group(1), group(2)}},
          {object(3), {group(2)}},
          {object(4), {group(1), group(2)}}};

  EXPECT_EQ(expectedGroups, objectPartition.getObjectGroups());

  // Object 5 should not be in the map since it only belongs to filtered-out
  // group 3
  EXPECT_TRUE(objectPartition.getObjectGroups(object(5)).empty());
}

CO_TEST_F(ObjectPartitionTest, EmptyFilteredGroupIds) {
  setInitialAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"container1", {"object1", "object2", "object3"}}});

  co_await addPartition(
      kPartitionName,
      {{"group1", {"object1", "object2"}}, {"group2", {"object3"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  const auto partition1Id = partitionId(kPartitionName);
  const auto objectCountDimId = dimensionId("object_count");

  const auto filteredGroupIds = makeFilteredGroupIds({});

  const ObjectPartition objectPartition(
      std::make_shared<const PartitionInfo>(
          universe, partition1Id, filteredGroupIds),
      objectCountDimId,
      {});

  // All objects should be filtered out
  EXPECT_TRUE(objectPartition.getObjectGroups().empty());
}

CO_TEST_F(ObjectPartitionTest, FilteredGroupIdsWithDefaultLimits) {
  setInitialAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"container1", {"object1", "object2", "object3"}}});

  co_await addPartition(
      kPartitionName,
      {{"group1", {"object1"}},
       {"group2", {"object2"}},
       {"group3", {"object3"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  const auto partition1Id = partitionId(kPartitionName);
  const auto objectCountDimId = dimensionId("object_count");

  const auto filteredGroupIds = makeFilteredGroupIds({group(1), group(2)});

  const ObjectPartition objectPartition(
      std::make_shared<const PartitionInfo>(
          universe, partition1Id, filteredGroupIds),
      objectCountDimId,
      {{group(1), 5.0}}, // Only group 1 has explicit limit
      std::nullopt,
      {},
      /*defaultGroupLimit=*/10.0);

  // Group 1 has explicit limit
  EXPECT_EQ(5.0, objectPartition.getGroupLimit(group(1)));

  // Group 2 in filter should get default limit
  EXPECT_EQ(10.0, objectPartition.getGroupLimit(group(2)));

  // Group 3 not in filter should throw
  REBALANCER_EXPECT_RUNTIME_ERROR(
      objectPartition.getGroupLimit(group(3)),
      fmt::format(
          "Cannot get limit for group {} which is not in filteredGroupIds",
          group(3)));
}

} // namespace facebook::rebalancer::packer::tests
