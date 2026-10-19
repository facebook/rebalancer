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

#include "algopt/rebalancer/interface/tests/UniverseProblemBuilderTestBase.h"

#include <folly/container/F14Set.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::interface::tests {

class UniverseProblemBuilderAsyncTest : public UniverseProblemBuilderTestBase {
};

INSTANTIATE_TEST_CASE_P(
    NumThreads,
    UniverseProblemBuilderAsyncTest,
    ::testing::Values(0, 1, 4, 16, 45));

TEST_P(UniverseProblemBuilderAsyncTest, ManyDynamicDimensionsScopesPartitions) {
  constexpr size_t kNumObjects = 10'000;
  constexpr size_t kNumContainers = 500;
  constexpr size_t kNumDynamicDimensions = 100;
  constexpr size_t kNumScopes = 25;
  constexpr size_t kNumPartitions = 50;
  constexpr size_t kNumScopeItemsPerScope = kNumContainers / 100;
  constexpr size_t kNumGroupsPerPartition = kNumObjects / 100;

  auto builder = getBuilder();

  const auto assignment =
      generateInitialAssignment(kNumObjects, kNumContainers);
  builder.setAssignment(assignment);

  const auto scopeItemToContainers =
      generateScopeItemToContainers(kNumScopeItemsPerScope, kNumContainers);
  const auto groupToObjects =
      generateGroupToObjects(kNumGroupsPerPartition, kNumObjects);

  folly::F14FastSet<size_t> addedScopes;
  folly::F14FastSet<size_t> addedPartitions;

  for (const auto i : folly::irange(kNumDynamicDimensions)) {
    const auto scopeIndex = i % kNumScopes;
    const auto scopeName = fmt::format("scope_{}", scopeIndex);
    if (addedScopes.insert(scopeIndex).second) {
      builder.addScope(scopeName, scopeItemToContainers);
    }

    const auto partitionIndex = i % kNumPartitions;
    const auto partitionName = fmt::format("partition_{}", partitionIndex);
    if (addedPartitions.insert(partitionIndex).second) {
      builder.addPartition(partitionName, groupToObjects);
    }

    const auto baseValue = static_cast<double>(i);

    {
      const auto dimensionName = fmt::format("dynamic_dim_{}", i);
      const auto scopeItemToObjectToValue = generateScopeItemToObjectToValue(
          kNumScopeItemsPerScope, kNumObjects, baseValue);
      builder.addDynamicObjectDimension(
          dimensionName, scopeName, scopeItemToObjectToValue, baseValue);
    }

    {
      const auto dimensionName =
          fmt::format("dynamic_dim_with_partition_{}", i);
      const auto scopeItemToGroupToValue = generateScopeItemToGroupToValue(
          kNumScopeItemsPerScope, kNumGroupsPerPartition, baseValue);
      builder.addDynamicObjectDimension(
          dimensionName,
          scopeName,
          partitionName,
          scopeItemToGroupToValue,
          baseValue);
    }
  }

  const auto universe = builder.build();

  for (const auto i : folly::irange(kNumDynamicDimensions)) {
    const auto dimensionName = fmt::format("dynamic_dim_{}", i);
    const auto dimensionId = universe->getDimensionId(dimensionName);
    EXPECT_EQ(dimensionName, universe->getEntityName(dimensionId));

    const auto dimNameWithPartition =
        fmt::format("dynamic_dim_with_partition_{}", i);
    const auto dimIdWithPartition =
        universe->getDimensionId(dimNameWithPartition);
    EXPECT_EQ(
        dimNameWithPartition, universe->getEntityName(dimIdWithPartition));
  }

  const auto& objects = universe->getObjects();

  constexpr size_t kDimIndex1 = 11;
  const auto dimId1 =
      universe->getDimensionId(fmt::format("dynamic_dim_{}", kDimIndex1));
  const auto& dim1 = objects.getDimension(dimId1).only();
  EXPECT_DOUBLE_EQ(static_cast<double>(kDimIndex1), dim1.getDefaultValue());

  constexpr size_t kDimIndex2 = 78;
  const auto dimId2 = universe->getDimensionId(
      fmt::format("dynamic_dim_with_partition_{}", kDimIndex2));
  const auto& dim2 = objects.getDimension(dimId2).only();
  EXPECT_DOUBLE_EQ(static_cast<double>(kDimIndex2), dim2.getDefaultValue());

  EXPECT_EQ(kNumObjects, universe->getNumObjects());
  EXPECT_EQ(kNumContainers, universe->getContainers().getContainerIds().size());
  // + 1 because of trivialScope
  EXPECT_EQ(kNumScopes + 1, universe->getScopeIds().size());
  EXPECT_EQ(kNumPartitions, universe->getPartitionIds().size());
  // 2*kNumDynamicDimensions + 1 because there is one objectCount dimension + in
  // the loop above, we add both versions of dynamic dimensions
  EXPECT_EQ(2 * kNumDynamicDimensions + 1, objects.getDimensionIds().size());
}

TEST_P(
    UniverseProblemBuilderAsyncTest,
    DynamicDimensionDefaultsToObjectBackedStorage) {
  auto builder = getBuilder();
  builder.setAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"container_0", {"object_0", "object_2"}},
          {"container_1", {"object_1"}}});
  builder.addScope(
      "scope",
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"scopeItem_0", {"container_0"}}, {"scopeItem_1", {"container_1"}}});
  builder.addPartition(
      "partition",
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"group_0", {"object_0", "object_1"}}, {"group_1", {"object_2"}}});
  builder.addDynamicObjectDimension(
      "expanded_dim",
      "scope",
      "partition",
      folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>{
          {"scopeItem_0", {{"group_0", 7}}}, {"scopeItem_1", {{"group_1", 4}}}},
      1);

  const auto universe = builder.build();
  const auto dimId = universe->getDimensionId("expanded_dim");
  const auto& dimension = universe->getObjects().getDimension(dimId).only();
  EXPECT_TRUE(dimension.isDynamic());

  const auto scopeId = universe->getScopeId("scope");
  const auto si0 = universe->getScopeItemId(scopeId, "scopeItem_0");
  const auto si1 = universe->getScopeItemId(scopeId, "scopeItem_1");

  const auto& storage0 = dimension.values(si0);
  const auto objectValues = storage0.asMapOrNull();
  ASSERT_NE(nullptr, objectValues);

  EXPECT_EQ(7, dimension.getValue(universe->getObjectId("object_0"), si0));
  EXPECT_EQ(7, dimension.getValue(universe->getObjectId("object_1"), si0));
  EXPECT_EQ(1, dimension.getValue(universe->getObjectId("object_2"), si0));
  EXPECT_EQ(4, dimension.getValue(universe->getObjectId("object_2"), si1));
}

TEST_P(UniverseProblemBuilderAsyncTest, GroupBasedStaticDimension) {
  auto builder = getBuilder();
  builder.setAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"container_0", {"object_0", "object_2"}},
          {"container_1", {"object_1"}}});
  builder.addPartition(
      "partition",
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"group_0", {"object_0", "object_1"}}, {"group_1", {"object_2"}}});
  builder.addObjectDimension(
      "compact_dim",
      "partition",
      folly::F14FastMap<std::string, double>{{"group_0", 7}},
      /*defaultValue=*/1);

  const auto universe = builder.build();
  const auto& dimension =
      universe->getObjects()
          .getDimension(universe->getDimensionId("compact_dim"))
          .only();
  EXPECT_EQ(7, dimension.getValue(universe->getObjectId("object_0")));
  EXPECT_EQ(7, dimension.getValue(universe->getObjectId("object_1")));
  EXPECT_EQ(1, dimension.getValue(universe->getObjectId("object_2")));
}

TEST_P(UniverseProblemBuilderAsyncTest, GroupBackedDynamicDimensionStorage) {
  auto builder = getBuilder();
  builder.setGroupBackedDynamicDimensions(/*enable=*/true);
  builder.setAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"container_0", {"object_0", "object_2"}},
          {"container_1", {"object_1"}}});
  builder.addScope(
      "scope",
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"scopeItem_0", {"container_0"}}, {"scopeItem_1", {"container_1"}}});
  builder.addPartition(
      "partition",
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"group_0", {"object_0", "object_1"}}, {"group_1", {"object_2"}}});
  builder.addDynamicObjectDimension(
      "compact_dim",
      "scope",
      "partition",
      folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>{
          {"scopeItem_0", {{"group_0", 7}}}, {"scopeItem_1", {{"group_1", 4}}}},
      1);

  const auto universe = builder.build();
  const auto dimId = universe->getDimensionId("compact_dim");
  const auto& dimension = universe->getObjects().getDimension(dimId).only();
  EXPECT_TRUE(dimension.isDynamic());

  const auto scopeId = universe->getScopeId("scope");
  const auto si0 = universe->getScopeItemId(scopeId, "scopeItem_0");
  const auto si1 = universe->getScopeItemId(scopeId, "scopeItem_1");

  const auto& storage0 = dimension.values(si0);
  // The builder kept the compact form: group-backed storage has no single
  // object map to hand out (object-backed storage would return one).
  EXPECT_EQ(nullptr, storage0.asMapOrNull());
  const auto partitionId = universe->getPartitionId("partition");
  const auto group0Id = universe->getGroupId(partitionId, "group_0");
  int partitionGroupCount = 0;
  bool sawObjectMap = false;
  storage0.visit(
      [&](const entities::ObjectIdToDoubleMap&) { sawObjectMap = true; },
      [&](entities::PartitionId observedPartitionId,
          const entities::GroupIdToDoubleMap& groupValues) {
        EXPECT_EQ(partitionId, observedPartitionId);
        for (const auto& groupEntry : groupValues) {
          EXPECT_EQ(group0Id, groupEntry.first);
          ++partitionGroupCount;
        }
      });
  EXPECT_FALSE(sawObjectMap);
  EXPECT_EQ(1, partitionGroupCount);

  EXPECT_EQ(7, dimension.getValue(universe->getObjectId("object_0"), si0));
  EXPECT_EQ(7, dimension.getValue(universe->getObjectId("object_1"), si0));
  EXPECT_EQ(1, dimension.getValue(universe->getObjectId("object_2"), si0));
  EXPECT_EQ(4, dimension.getValue(universe->getObjectId("object_2"), si1));

  EXPECT_EQ(15, dimension.values(si0).sum());

  auto universeThrift = universe->toThrift();
  const auto& dynamicDimensionThrift = universeThrift.objects()
                                           ->dimensions()
                                           ->at(dimId.asInt())
                                           .scalarDimensions()
                                           ->at(0)
                                           .get_objectDynamicDimension();
  EXPECT_TRUE(dynamicDimensionThrift.values()->empty());
  const auto& scopedValues = *dynamicDimensionThrift.scopedValues();
  ASSERT_TRUE(scopedValues.contains(si0.asInt()));
  const auto& thriftValues = scopedValues.at(si0.asInt());
  ASSERT_EQ(
      entities::thrift::ObjectValues::Type::groupValues,
      thriftValues.getType());
  EXPECT_EQ(partitionId.asInt(), *thriftValues.get_groupValues().partitionId());

  const entities::Universe roundTrippedUniverse(universeThrift);
  const auto& roundTrippedDimension =
      roundTrippedUniverse.getObjects().getDimension(dimId).only();
  const auto& roundTrippedStorage = roundTrippedDimension.values(si0);
  EXPECT_EQ(nullptr, roundTrippedStorage.asMapOrNull());
  bool roundTripSawObjectMap = false;
  int roundTripGroupCount = 0;
  roundTrippedStorage.visit(
      [&](const entities::ObjectIdToDoubleMap&) {
        roundTripSawObjectMap = true;
      },
      [&](entities::PartitionId observedPartitionId,
          const entities::GroupIdToDoubleMap& groupValues) {
        EXPECT_EQ(partitionId, observedPartitionId);
        for (const auto& groupEntry : groupValues) {
          EXPECT_EQ(group0Id, groupEntry.first);
          ++roundTripGroupCount;
        }
      });
  EXPECT_FALSE(roundTripSawObjectMap);
  EXPECT_EQ(1, roundTripGroupCount);
  EXPECT_EQ(
      7,
      roundTrippedDimension.getValue(
          roundTrippedUniverse.getObjectId("object_0"), si0));
  EXPECT_EQ(
      1,
      roundTrippedDimension.getValue(
          roundTrippedUniverse.getObjectId("object_2"), si0));
}

TEST_P(UniverseProblemBuilderAsyncTest, ManyObjectDimensions) {
  constexpr size_t kNumObjects = 10'000;
  constexpr size_t kNumContainers = 500;
  constexpr size_t kNumDimensions = 100;

  auto builder = getBuilder();

  const auto assignment =
      generateInitialAssignment(kNumObjects, kNumContainers);

  builder.setAssignment(assignment);

  for (const auto i : folly::irange(kNumDimensions)) {
    const auto baseValue = static_cast<double>(i);
    builder.addObjectDimension(
        /*dimensionName=*/fmt::format("obj_dim_{}", i),
        generateObjectToValue(kNumObjects, /*baseValue=*/i),
        /*defaultValue=*/baseValue);

    builder.addObjectDimension(
        /*dimensionName=*/fmt::format("obj_dim_vector_{}", i),
        generateObjectToValues(kNumObjects, /*baseValue=*/i),
        /*defaultValue=*/baseValue);
  }

  const auto universe = builder.build();

  for (const auto i : folly::irange(kNumDimensions)) {
    const auto dimensionName = fmt::format("obj_dim_{}", i);
    const auto dimensionId = universe->getDimensionId(dimensionName);
    EXPECT_EQ(dimensionName, universe->getEntityName(dimensionId));

    const auto dimNameVector = fmt::format("obj_dim_vector_{}", i);
    const auto dimIdVector = universe->getDimensionId(dimNameVector);
    EXPECT_EQ(dimNameVector, universe->getEntityName(dimIdVector));
  }

  const auto& objects = universe->getObjects();

  constexpr size_t kDimIndex1 = 11;
  const auto dimId1 =
      universe->getDimensionId(fmt::format("obj_dim_{}", kDimIndex1));
  const auto& dim1 = objects.getDimension(dimId1).only();
  EXPECT_DOUBLE_EQ(static_cast<double>(kDimIndex1), dim1.getDefaultValue());

  constexpr size_t kDimIndex2 = 78;
  const auto dimId2 =
      universe->getDimensionId(fmt::format("obj_dim_vector_{}", kDimIndex2));
  const auto& dim2 = objects.getDimension(dimId2);
  EXPECT_DOUBLE_EQ(
      static_cast<double>(kDimIndex2), dim2.at(0).getDefaultValue());

  EXPECT_EQ(kNumObjects, universe->getNumObjects());
  EXPECT_EQ(kNumContainers, universe->getContainers().getContainerIds().size());
  // 2*kNumDimensions + 1 because there is one objectCount dimension + in
  // the loop above, we add both scalar and vector versions of object dimensions
  EXPECT_EQ(2 * kNumDimensions + 1, objects.getDimensionIds().size());
}
} // namespace facebook::rebalancer::interface::tests
