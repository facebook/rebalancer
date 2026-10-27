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
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/ObjectDimension.h"
#include "algopt/rebalancer/entities/ObjectDynamicDimension.h"
#include "algopt/rebalancer/entities/ObjectPartitionRoutingDimension.h"
#include "algopt/rebalancer/entities/Partition.h"
#include "algopt/rebalancer/entities/Partitions.h"

#include <folly/container/F14Map.h>
#include <gtest/gtest.h>

#include <thread>

namespace facebook::rebalancer::entities::tests {

const std::size_t kDefaultTotalObjects = 10;

template <typename T = ObjectId>
std::shared_ptr<const Map<T, double>> makeSharedPtrEntityToValueMap(
    Map<T, double>&& map) {
  return std::make_shared<const Map<T, double>>(std::move(map));
}

inline ObjectId makeObjectId(int id) {
  return ObjectId(id);
}
inline ScopeId makeScopeId(int id) {
  return ScopeId(id);
}
inline ScopeItemId makeScopeItemId(int id) {
  return ScopeItemId(id);
}

inline thrift::ObjectValues makeThriftObjectValues(
    folly::F14FastMap<int, double> objectValues) {
  thrift::ObjectValues values;
  values.objectValues() = std::move(objectValues);
  return values;
}

inline ObjectDimension makeStaticDim(
    const Map<ObjectId, double>& values,
    const double defaultValue,
    const std::size_t totalObjects = kDefaultTotalObjects) {
  return ObjectDimension(
      ObjectIdToDoubleMap(values, defaultValue, totalObjects));
}

inline ObjectDimension makeStaticDim(
    const std::vector<Map<ObjectId, double>>& values,
    const std::vector<double>& defaultValues,
    const std::size_t totalObjects = kDefaultTotalObjects) {
  std::vector<ObjectIdToDoubleMap> maps;
  maps.reserve(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    maps.emplace_back(values[i], defaultValues[i], totalObjects);
  }
  return ObjectDimension(std::move(maps));
}

inline ObjectDimension makeDynamicDim(
    const ScopeId scopeId,
    const Map<ScopeItemId, std::shared_ptr<const Map<ObjectId, double>>>&
        values,
    const double defaultValue,
    const std::size_t totalObjects = kDefaultTotalObjects) {
  Map<ScopeItemId, std::shared_ptr<const ObjectIdToDoubleMap>> maps;
  maps.reserve(values.size());
  for (const auto& [scopeItemId, mapPtr] : values) {
    maps.emplace(
        scopeItemId,
        std::make_shared<const ObjectIdToDoubleMap>(
            *mapPtr, defaultValue, totalObjects));
  }
  return ObjectDimension(scopeId, std::move(maps), defaultValue, totalObjects);
}

TEST(ObjectDimensionTest, Scalar) {
  const auto object1 = makeObjectId(0);
  const auto object2 = makeObjectId(1);
  const auto object3 = makeObjectId(2);

  const auto dimension = makeStaticDim({{object1, 10}, {object2, 20}}, 30);

  EXPECT_EQ(1, dimension.size());

  EXPECT_EQ(10, dimension.at(0).getValue(object1));
  EXPECT_EQ(20, dimension.at(0).getValue(object2));
  EXPECT_EQ(30, dimension.at(0).getValue(object3));

  EXPECT_EQ(30, dimension.at(0).getDefaultValue());

  EXPECT_FALSE(dimension.at(0).isDynamic());
}

TEST(ObjectDimensionTest, Vector) {
  const auto object1 = makeObjectId(0);
  const auto object2 = makeObjectId(1);
  const auto object3 = makeObjectId(2);

  const auto dimension = makeStaticDim(
      {{{object1, 10}, {object2, 20}}, {{object1, 11}, {object2, 21}}},
      {30, 31});

  EXPECT_EQ(2, dimension.size());

  EXPECT_EQ(10, dimension.at(0).getValue(object1));
  EXPECT_EQ(20, dimension.at(0).getValue(object2));
  EXPECT_EQ(30, dimension.at(0).getValue(object3));

  EXPECT_EQ(11, dimension.at(1).getValue(object1));
  EXPECT_EQ(21, dimension.at(1).getValue(object2));
  EXPECT_EQ(31, dimension.at(1).getValue(object3));

  EXPECT_EQ(30, dimension.at(0).getDefaultValue());

  EXPECT_EQ(31, dimension.at(1).getDefaultValue());
}

TEST(ObjectDimensionTest, Dynamic) {
  const auto object1 = makeObjectId(0);
  const auto object2 = makeObjectId(1);
  const auto object3 = makeObjectId(2);

  const auto scope = makeScopeId(10);
  const auto item1 = makeScopeItemId(11);
  const auto item2 = makeScopeItemId(12);
  const auto item3 = makeScopeItemId(13);

  const auto dimension = makeDynamicDim(
      scope,
      {{item1, makeSharedPtrEntityToValueMap({{object1, 10}})},
       {item2, makeSharedPtrEntityToValueMap({{object1, 20}, {object2, 30}})}},
      42);

  EXPECT_EQ(1, dimension.size());

  EXPECT_EQ(10, dimension.at(0).getValue(object1, item1));
  EXPECT_EQ(20, dimension.at(0).getValue(object1, item2));
  EXPECT_EQ(42, dimension.at(0).getValue(object1, item3));

  EXPECT_EQ(42, dimension.at(0).getValue(object2, item1));
  EXPECT_EQ(30, dimension.at(0).getValue(object2, item2));
  EXPECT_EQ(42, dimension.at(0).getValue(object2, item3));

  EXPECT_EQ(42, dimension.at(0).getValue(object3, item1));
  EXPECT_EQ(42, dimension.at(0).getValue(object3, item2));
  EXPECT_EQ(42, dimension.at(0).getValue(object3, item3));

  EXPECT_TRUE(dimension.at(0).isDynamic());
  EXPECT_EQ(scope, dimension.at(0).getScopeId());
}

TEST(ObjectDimensionTest, GetValueSafe) {
  const auto object1 = makeObjectId(0);
  const auto object2 = makeObjectId(1);
  const auto object3 = makeObjectId(2);

  const auto scope = makeScopeId(10);
  const auto item1 = makeScopeItemId(11);
  const auto item2 = makeScopeItemId(12);

  // Create a second scope to test scope mismatch behavior
  const auto scope2 = makeScopeId(20);
  const auto item3 = makeScopeItemId(21);

  // Test ObjectStaticDimension - should ignore scope and return value
  {
    const auto staticDimension =
        makeStaticDim({{object1, 10}, {object2, 20}}, 30);

    // Static dimension ignores scope entirely - always returns value based on
    // objectId
    EXPECT_EQ(
        10,
        staticDimension.at(0).getValueSafe(
            object1, ScopeScopeItemPair{scope, item1}));
    EXPECT_EQ(
        20,
        staticDimension.at(0).getValueSafe(
            object2, ScopeScopeItemPair{scope, item1}));
    EXPECT_EQ(
        30,
        staticDimension.at(0).getValueSafe(
            object3, ScopeScopeItemPair{scope, item1}));

    // Static dimension with nullopt - should return value based on objectId
    EXPECT_EQ(10, staticDimension.at(0).getValueSafe(object1, std::nullopt));
    EXPECT_EQ(20, staticDimension.at(0).getValueSafe(object2, std::nullopt));
    EXPECT_EQ(30, staticDimension.at(0).getValueSafe(object3, std::nullopt));

    // Static dimension with different scope - should still return correct value
    EXPECT_EQ(
        10,
        staticDimension.at(0).getValueSafe(
            object1, ScopeScopeItemPair{scope2, item3}));
  }

  // Test ObjectDynamicDimension - should check scope match
  {
    const auto dynamicDimension = makeDynamicDim(
        scope,
        {{item1, makeSharedPtrEntityToValueMap({{object1, 10}})},
         {item2,
          makeSharedPtrEntityToValueMap({{object1, 20}, {object2, 30}})}},
        42);

    // Matching scope - should return correct values
    EXPECT_EQ(
        10,
        dynamicDimension.at(0).getValueSafe(
            object1, ScopeScopeItemPair{scope, item1}));
    EXPECT_EQ(
        20,
        dynamicDimension.at(0).getValueSafe(
            object1, ScopeScopeItemPair{scope, item2}));
    EXPECT_EQ(
        30,
        dynamicDimension.at(0).getValueSafe(
            object2, ScopeScopeItemPair{scope, item2}));

    // Matching scope, object not in map - should return default
    EXPECT_EQ(
        42,
        dynamicDimension.at(0).getValueSafe(
            object3, ScopeScopeItemPair{scope, item1}));

    // nullopt - should return default value
    EXPECT_EQ(42, dynamicDimension.at(0).getValueSafe(object1, std::nullopt));

    // Non-matching scope - should throw error
    REBALANCER_EXPECT_RUNTIME_ERROR(
        dynamicDimension.at(0).getValueSafe(
            object1, ScopeScopeItemPair{scope2, item3}),
        "scopeItemId is out of scope");
  }

  // Test ObjectPartitionRoutingDimension - should throw error
  {
    const ObjectDimension routingDimension(
        {{GroupId(8), 10}},
        20,
        PartitionId(7),
        RoutingConfigId(4),
        {{GroupId(9), 5}},
        2);

    // getValueSafe should throw for routing dimensions
    REBALANCER_EXPECT_RUNTIME_ERROR(
        routingDimension.at(0).getValueSafe(
            object1, ScopeScopeItemPair{scope, item1}),
        "There is no value associated with an ObjectId in an ObjectPartitionRoutingDimension");
    REBALANCER_EXPECT_RUNTIME_ERROR(
        routingDimension.at(0).getValueSafe(object1, std::nullopt),
        "There is no value associated with an ObjectId in an ObjectPartitionRoutingDimension");
  }
}

TEST(ObjectDimensionTest, StaticToThrift) {
  const auto dimension = makeStaticDim({{ObjectId(8), 10}}, 20);

  auto dimensionThrift = dimension.toThrift();

  EXPECT_EQ(1, dimensionThrift.scalarDimensions()->size());
  EXPECT_EQ(
      thrift::ObjectScalarDimension::Type::objectStaticDimension,
      dimensionThrift.scalarDimensions()->at(0).getType());

  auto& staticDimensionThrift = dimensionThrift.scalarDimensions()
                                    ->at(0)
                                    .objectStaticDimension()
                                    .as_const()
                                    .value();
  EXPECT_EQ(1, staticDimensionThrift.values()->size());
  EXPECT_EQ(10, staticDimensionThrift.values()->at(8));
  EXPECT_EQ(20, *staticDimensionThrift.defaultValue());

  EXPECT_FALSE(*dimensionThrift.isDynamic());
}

TEST(ObjectDimensionTest, StaticFromThrift) {
  thrift::ObjectStaticDimension staticDimensionThrift;
  // Object 9 has the default value; the constructor must preserve lookup
  // behavior for both explicit and default-valued thrift entries.
  staticDimensionThrift.values() = {{8, 10}, {9, 20}};
  staticDimensionThrift.defaultValue() = 20;

  thrift::ObjectScalarDimension scalarDimensionThrift;
  scalarDimensionThrift.objectStaticDimension() = staticDimensionThrift;

  thrift::ObjectDimension dimensionThrift;
  dimensionThrift.scalarDimensions() = {scalarDimensionThrift};
  dimensionThrift.isDynamic() = false;

  auto dimension = ObjectDimension(dimensionThrift, kDefaultTotalObjects);

  EXPECT_EQ(1, dimension.size());

  auto& scalarDimension = dimension.at(0);
  EXPECT_EQ(10, scalarDimension.getValue(ObjectId(8)));
  EXPECT_EQ(20, scalarDimension.getValue(ObjectId(9)));
  EXPECT_EQ(20, scalarDimension.getDefaultValue());
  EXPECT_EQ(20, scalarDimension.getValue(ObjectId(9)));
  EXPECT_FALSE(scalarDimension.isDynamic());

  EXPECT_FALSE(dimension.isDynamic());
}

TEST(ObjectDimensionTest, DynamicToThrift) {
  const auto dimension = makeDynamicDim(
      ScopeId(0),
      {{ScopeItemId(1), makeSharedPtrEntityToValueMap({{ObjectId(2), 10}})}},
      20);

  auto dimensionThrift = dimension.toThrift();

  EXPECT_EQ(1, dimensionThrift.scalarDimensions()->size());
  EXPECT_EQ(
      thrift::ObjectScalarDimension::Type::objectDynamicDimension,
      dimensionThrift.scalarDimensions()->at(0).getType());

  auto& dynamicDimensionThrift = dimensionThrift.scalarDimensions()
                                     ->at(0)
                                     .objectDynamicDimension()
                                     .as_const()
                                     .value();
  EXPECT_EQ(0, *dynamicDimensionThrift.scopeId());
  EXPECT_TRUE(dynamicDimensionThrift.values()->empty());
  const auto& scopedValues = *dynamicDimensionThrift.scopedValues();
  EXPECT_EQ(1, scopedValues.size());
  const auto& itemValues = scopedValues.at(1);
  ASSERT_EQ(thrift::ObjectValues::Type::objectValues, itemValues.getType());
  EXPECT_EQ(1, itemValues.objectValues()->size());
  EXPECT_EQ(10, itemValues.objectValues()->at(2));
  EXPECT_EQ(20, *dynamicDimensionThrift.defaultValue());

  EXPECT_TRUE(*dimensionThrift.isDynamic());
}

TEST(ObjectDimensionTest, GroupBackedDynamicToThriftUsesScopedGroupValues) {
  const auto object1 = makeObjectId(0);
  const auto object2 = makeObjectId(1);
  const auto object3 = makeObjectId(2);
  const auto group1 = GroupId(101);
  const auto group2 = GroupId(102);
  const auto scope = ScopeId(10);
  const auto item1 = ScopeItemId(11);
  const auto item2 = ScopeItemId(12);
  auto partition =
      std::make_shared<const Partition>(Map<GroupId, std::vector<ObjectId>>{
          {group1, {object1, object2}}, {group2, {object3}}});

  const ObjectDimension dimension(
      scope,
      partition,
      {{item1, makeSharedPtrEntityToValueMap<GroupId>({{group1, 10}})},
       {item2, makeSharedPtrEntityToValueMap<GroupId>({{group2, 30}})}},
      42,
      /*totalObjects=*/3,
      /*partitionId=*/PartitionId(7));

  const auto dimensionThrift = dimension.toThrift();
  const auto& dynamicDimensionThrift = dimensionThrift.scalarDimensions()
                                           ->at(0)
                                           .objectDynamicDimension()
                                           .value();

  EXPECT_TRUE(dynamicDimensionThrift.values()->empty());
  const auto& scopedValues = *dynamicDimensionThrift.scopedValues();
  EXPECT_EQ(2, scopedValues.size());

  const auto& item1Values = scopedValues.at(item1.asInt());
  ASSERT_EQ(thrift::ObjectValues::Type::groupValues, item1Values.getType());
  const auto& item1GroupValues = item1Values.groupValues().value();
  EXPECT_EQ(7, *item1GroupValues.partitionId());
  EXPECT_EQ(10, item1GroupValues.values()->at(group1.asInt()));

  const auto& item2Values = scopedValues.at(item2.asInt());
  ASSERT_EQ(thrift::ObjectValues::Type::groupValues, item2Values.getType());
  const auto& item2GroupValues = item2Values.groupValues().value();
  EXPECT_EQ(7, *item2GroupValues.partitionId());
  EXPECT_EQ(30, item2GroupValues.values()->at(group2.asInt()));
  EXPECT_EQ(42, *dynamicDimensionThrift.defaultValue());
}

TEST(ObjectDimensionTest, DynamicFromThrift) {
  thrift::ObjectDynamicDimension dynamicDimensionThrift;
  dynamicDimensionThrift.scopeId() = 0;
  // Object 3 holds the default value; the constructor must drop it from the
  // stored map.
  dynamicDimensionThrift.scopedValues() = {
      {1, makeThriftObjectValues({{2, 10}, {3, 20}})}};
  dynamicDimensionThrift.defaultValue() = 20;

  thrift::ObjectScalarDimension scalarDimensionThrift;
  scalarDimensionThrift.objectDynamicDimension() = dynamicDimensionThrift;

  thrift::ObjectDimension dimensionThrift;
  dimensionThrift.scalarDimensions() = {scalarDimensionThrift};
  dimensionThrift.isDynamic() = true;

  auto dimension = ObjectDimension(dimensionThrift, kDefaultTotalObjects);

  EXPECT_EQ(1, dimension.size());

  auto& scalarDimension = dimension.at(0);
  EXPECT_EQ(ScopeId(0), scalarDimension.getScopeId());
  EXPECT_EQ(10, scalarDimension.getValue(ObjectId(2), ScopeItemId(1)));
  EXPECT_EQ(20, scalarDimension.getValue(ObjectId(3), ScopeItemId(1)));
  EXPECT_EQ(20, scalarDimension.getDefaultValue());
  EXPECT_EQ(20, scalarDimension.getValue(ObjectId(3), ScopeItemId(1)));
  EXPECT_TRUE(scalarDimension.isDynamic());

  EXPECT_TRUE(dimension.isDynamic());
}

TEST(ObjectDimensionTest, GroupBackedDynamicFromThrift) {
  const auto object1 = makeObjectId(0);
  const auto object2 = makeObjectId(1);
  const auto object3 = makeObjectId(2);
  const auto group1 = GroupId(101);
  const auto group2 = GroupId(102);
  const auto partitionId = PartitionId(7);
  const Partitions partitions(
      {{partitionId,
        std::make_shared<Partition>(Map<GroupId, std::vector<ObjectId>>{
            {group1, {object1, object2}}, {group2, {object3}}})}});

  auto makeGroupValues = [&](GroupId groupId, double value) {
    thrift::ObjectGroupValues groupValues;
    groupValues.partitionId() = partitionId.asInt();
    groupValues.values() = {{groupId.asInt(), value}};
    thrift::ObjectValues values;
    values.groupValues() = std::move(groupValues);
    return values;
  };

  thrift::ObjectDynamicDimension dynamicDimensionThrift;
  dynamicDimensionThrift.scopeId() = ScopeId(10).asInt();
  dynamicDimensionThrift.defaultValue() = 42;
  dynamicDimensionThrift.scopedValues() = {
      {ScopeItemId(11).asInt(), makeGroupValues(group1, 10)},
      {ScopeItemId(12).asInt(), makeGroupValues(group2, 30)}};

  thrift::ObjectScalarDimension scalarDimensionThrift;
  scalarDimensionThrift.objectDynamicDimension() = dynamicDimensionThrift;

  thrift::ObjectDimension dimensionThrift;
  dimensionThrift.scalarDimensions() = {scalarDimensionThrift};
  dimensionThrift.isDynamic() = true;

  const ObjectDimension dimension(
      dimensionThrift, /*totalObjects=*/3, &partitions);
  const auto& scalarDimension = dimension.only();
  EXPECT_EQ(nullptr, scalarDimension.values(ScopeItemId(11)).asMapOrNull());
  EXPECT_EQ(10, scalarDimension.getValue(object1, ScopeItemId(11)));
  EXPECT_EQ(10, scalarDimension.getValue(object2, ScopeItemId(11)));
  EXPECT_EQ(42, scalarDimension.getValue(object3, ScopeItemId(11)));
  EXPECT_EQ(30, scalarDimension.getValue(object3, ScopeItemId(12)));
  EXPECT_EQ(62, scalarDimension.values(ScopeItemId(11)).sum());
}

TEST(ObjectDimensionTest, ObjectPartitionRoutingDimensionToThrift) {
  const ObjectDimension dimension(
      {{GroupId(8), 10}},
      20,
      PartitionId(7),
      RoutingConfigId(4),
      {{GroupId(9), 5}},
      2);

  auto dimensionThrift = dimension.toThrift();

  EXPECT_EQ(1, dimensionThrift.scalarDimensions()->size());
  EXPECT_EQ(
      thrift::ObjectScalarDimension::Type::objectPartitionRoutingDimension,
      dimensionThrift.scalarDimensions()->at(0).getType());

  auto& routingDimensionThrift = dimensionThrift.scalarDimensions()
                                     ->at(0)
                                     .objectPartitionRoutingDimension()
                                     .as_const()
                                     .value();
  EXPECT_EQ(1, routingDimensionThrift.groupIdToValue()->size());
  EXPECT_EQ(10, routingDimensionThrift.groupIdToValue()->at(8));
  EXPECT_EQ(20, routingDimensionThrift.defaultValue());
  EXPECT_EQ(7, routingDimensionThrift.partitionId());
  EXPECT_EQ(4, routingDimensionThrift.routingConfigId());
  EXPECT_EQ(1, routingDimensionThrift.groupIdToStaticValue()->size());
  EXPECT_EQ(5, routingDimensionThrift.groupIdToStaticValue()->at(9));
  EXPECT_EQ(2, routingDimensionThrift.defaultStaticValue());

  EXPECT_FALSE(*dimensionThrift.isDynamic());
}

TEST(ObjectDimensionTest, ObjectPartitionRoutingDimensionFromThrift) {
  thrift::ObjectPartitionRoutingDimension routingDimensionThrift;
  // Group 19 matches defaultValue, group 8 matches defaultStaticValue;
  // both must be dropped from the stored maps.
  routingDimensionThrift.groupIdToValue() = {{18, 40}, {19, 20}};
  routingDimensionThrift.defaultValue() = 20;
  routingDimensionThrift.partitionId() = 1;
  routingDimensionThrift.routingConfigId() = 2;
  routingDimensionThrift.groupIdToStaticValue() = {{8, 5}, {9, 12}};
  routingDimensionThrift.defaultStaticValue() = 5;

  thrift::ObjectScalarDimension scalarDimensionThrift;
  scalarDimensionThrift.objectPartitionRoutingDimension() =
      routingDimensionThrift;

  thrift::ObjectDimension dimensionThrift;
  dimensionThrift.scalarDimensions() = {scalarDimensionThrift};
  dimensionThrift.isDynamic() = false;

  auto dimension = ObjectDimension(dimensionThrift, kDefaultTotalObjects);
  EXPECT_EQ(1, dimension.size());
  EXPECT_FALSE(dimension.isDynamic());

  auto& routingDimension =
      dynamic_cast<const ObjectPartitionRoutingDimension&>(dimension.at(0));
  EXPECT_EQ(40, routingDimension.getValue(GroupId(18)));

  EXPECT_EQ(20, routingDimension.getDefaultValue());
  EXPECT_EQ(
      20,
      routingDimension.getValue(
          GroupId(8))); // should return default value for unspecified group id
  EXPECT_EQ(
      20,
      routingDimension.getValue(
          GroupId(19))); // explicit-but-default entry is dropped from the map
                         // and resolves via the default fallback

  EXPECT_EQ(12, routingDimension.getStaticValue(GroupId(9)));

  EXPECT_EQ(
      5,
      routingDimension.getStaticValue(
          GroupId(8))); // should return default value for unspecified group id

  EXPECT_EQ(PartitionId(1), routingDimension.getPartitionId());
  EXPECT_EQ(RoutingConfigId(2), routingDimension.getRoutingConfigId());

  EXPECT_TRUE(routingDimension.isRoutingConfigBased());
  EXPECT_FALSE(routingDimension.isDynamic());

  // Round-trip back to thrift to confirm the default-valued entries (group 19
  // for groupIdToValue, group 8 for groupIdToStaticValue) were dropped.
  const auto routingThrift = routingDimension.toThrift()
                                 .objectPartitionRoutingDimension()
                                 .as_const()
                                 .value();
  EXPECT_EQ(1, routingThrift.groupIdToValue()->size());
  EXPECT_TRUE(routingThrift.groupIdToValue()->contains(18));
  EXPECT_EQ(1, routingThrift.groupIdToStaticValue()->size());
  EXPECT_TRUE(routingThrift.groupIdToStaticValue()->contains(9));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      routingDimension.getValue(ObjectId(9)),
      "There is no value associated with an ObjectId in an ObjectPartitionRoutingDimension");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      routingDimension.getValue(ObjectId(9), ScopeItemId(0)),
      "There is no value associated with an ObjectId in an ObjectPartitionRoutingDimension");
}

TEST(ObjectDimensionTest, HasNegativeOrZeroValues) {
  const auto object1 = makeObjectId(0);
  const auto object2 = makeObjectId(1);
  const auto object3 = makeObjectId(2);

  {
    // scalar dimension
    const auto dimension1 =
        makeStaticDim({{object1, 10}, {object2, -0.01}, {object3, 0}}, 30);
    EXPECT_TRUE(dimension1.hasNegativeValues());
    EXPECT_TRUE(dimension1.hasZeroValuedObjects());

    const auto dimension2 =
        makeStaticDim({{object1, 10}, {object2, 0.01}}, -0.01);
    EXPECT_TRUE(dimension2.hasNegativeValues());
    EXPECT_FALSE(dimension2.hasZeroValuedObjects());

    const auto dimension3 = makeStaticDim({{object1, 10}, {object2, 0.01}}, 0);
    EXPECT_FALSE(dimension3.hasNegativeValues());
    EXPECT_TRUE(dimension3.hasZeroValuedObjects());

    const auto dimension4 = makeStaticDim({{object1, 10}, {object2, 0.01}}, 30);
    EXPECT_FALSE(dimension4.hasNegativeValues());
    EXPECT_FALSE(dimension4.hasZeroValuedObjects());

    // NOTE: although all objects have zero values, hasZeroValuedObjects()
    // return true because it is conservative and does not current have access
    // to the number of objects in the problem
    const auto dimension5 =
        makeStaticDim({{object1, 10}, {object2, 0.01}, {object3, 10}}, 0);
    EXPECT_TRUE(dimension5.hasZeroValuedObjects());

    const auto item1 = makeScopeItemId(11);
    EXPECT_TRUE(dimension1.hasZeroValuedObjects(item1));
    EXPECT_TRUE(dimension3.hasZeroValuedObjects(item1));
  }
  {
    // vector dimension
    const auto dimension1 = makeStaticDim(
        {{{object1, 10}, {object2, 20}}, {{object1, 11}, {object2, -1}}},
        {30, 31});
    EXPECT_TRUE(dimension1.hasNegativeValues());
    EXPECT_FALSE(dimension1.hasZeroValuedObjects());

    const auto dimension2 = makeStaticDim(
        {{{object1, 10}, {object2, 20}}, {{object1, 11}, {object2, 0}}},
        {30, -31});
    EXPECT_TRUE(dimension2.hasNegativeValues());
    EXPECT_TRUE(dimension2.hasZeroValuedObjects());

    // Test all positive values
    const auto dimension3 = makeStaticDim(
        {{{object1, 10}, {object2, 20}}, {{object1, 11}, {object2, 1}}},
        {30, 31});
    EXPECT_FALSE(dimension3.hasNegativeValues());
    EXPECT_FALSE(dimension3.hasZeroValuedObjects());
  }
  {
    // dynamic dimension
    const auto scope = makeScopeId(30);
    const auto item1 = makeScopeItemId(31);
    const auto item2 = makeScopeItemId(32);
    const auto dimension1 = makeDynamicDim(
        scope,
        {{item1, makeSharedPtrEntityToValueMap({{object1, 10}})},
         {item2,
          makeSharedPtrEntityToValueMap(
              {{object1, 20}, {object2, 30}, {object3, -3}})}},
        42);
    EXPECT_TRUE(dimension1.hasNegativeValues());
    EXPECT_FALSE(dimension1.hasZeroValuedObjects());

    const auto dimension2 = makeDynamicDim(
        scope,
        {{item1, makeSharedPtrEntityToValueMap({{object1, 10}})},
         {item2,
          makeSharedPtrEntityToValueMap(
              {{object1, 20}, {object2, 30}, {object3, 0}})}},
        0);
    EXPECT_FALSE(dimension2.hasNegativeValues());
    EXPECT_TRUE(dimension2.hasZeroValuedObjects());

    // dynamic dimensions, check per scope item
    EXPECT_TRUE(dimension2.hasZeroValuedObjects(item1)); // default value is 0
    EXPECT_TRUE(dimension2.hasZeroValuedObjects(item2)); // object3 has value 0

    // Test all positive values
    const auto dimension3 = makeDynamicDim(
        scope,
        {{item1, makeSharedPtrEntityToValueMap({{object1, 10}})},
         {item2,
          makeSharedPtrEntityToValueMap(
              {{object1, 20}, {object2, 30}, {object3, 3}})}},
        42);
    EXPECT_FALSE(dimension3.hasNegativeValues());
    EXPECT_FALSE(dimension3.hasZeroValuedObjects(item1));
    EXPECT_FALSE(dimension3.hasZeroValuedObjects(item2));

    // NOTE: although all objects have zero values, hasZeroValuedObjects()
    // return true because it is conservative and does not current have access
    // to the number of objects in the problem
    const auto dimension4 = makeDynamicDim(
        scope,
        {{item2,
          makeSharedPtrEntityToValueMap(
              {{object1, 20}, {object2, 30}, {object3, 3}})}},
        0);
    EXPECT_TRUE(dimension4.hasZeroValuedObjects(item2));
  }
  {
    // scalar from thrift
    thrift::ObjectStaticDimension staticDimensionThrift;
    staticDimensionThrift.values() = {{8, 10}};
    staticDimensionThrift.defaultValue() = -20;

    thrift::ObjectScalarDimension scalarDimensionThrift;
    scalarDimensionThrift.objectStaticDimension() = staticDimensionThrift;

    thrift::ObjectDimension dimensionThrift;
    dimensionThrift.scalarDimensions() = {scalarDimensionThrift};

    EXPECT_TRUE(ObjectDimension(dimensionThrift, kDefaultTotalObjects)
                    .hasNegativeValues());
    EXPECT_FALSE(ObjectDimension(dimensionThrift, kDefaultTotalObjects)
                     .hasZeroValuedObjects());

    thrift::ObjectStaticDimension staticDimensionThrift2;
    staticDimensionThrift2.values() = {{8, 0}};
    staticDimensionThrift2.defaultValue() = 10;
    scalarDimensionThrift.objectStaticDimension() = staticDimensionThrift2;
    dimensionThrift.scalarDimensions() = {scalarDimensionThrift};

    EXPECT_FALSE(ObjectDimension(dimensionThrift, kDefaultTotalObjects)
                     .hasNegativeValues());
    EXPECT_TRUE(ObjectDimension(dimensionThrift, kDefaultTotalObjects)
                    .hasZeroValuedObjects());

    // Test all positive values
    thrift::ObjectStaticDimension staticDimensionThrift3;
    staticDimensionThrift3.values() = {{8, 10}};
    staticDimensionThrift3.defaultValue() = 20;
    scalarDimensionThrift.objectStaticDimension() = staticDimensionThrift3;
    dimensionThrift.scalarDimensions() = {scalarDimensionThrift};

    EXPECT_FALSE(ObjectDimension(dimensionThrift, kDefaultTotalObjects)
                     .hasNegativeValues());
    EXPECT_FALSE(ObjectDimension(dimensionThrift, kDefaultTotalObjects)
                     .hasZeroValuedObjects());
  }

  {
    // dynamic from thrift
    thrift::ObjectDynamicDimension dynamicDimensionThrift;
    dynamicDimensionThrift.scopeId() = 0;
    dynamicDimensionThrift.scopedValues() = {
        {1, makeThriftObjectValues({{2, -10}})}};
    dynamicDimensionThrift.defaultValue() = 20;

    thrift::ObjectScalarDimension scalarDimensionThrift;
    scalarDimensionThrift.objectDynamicDimension() = dynamicDimensionThrift;

    thrift::ObjectDimension dimensionThrift;
    dimensionThrift.scalarDimensions() = {scalarDimensionThrift};
    dimensionThrift.isDynamic() = true;

    EXPECT_TRUE(ObjectDimension(dimensionThrift, kDefaultTotalObjects)
                    .hasNegativeValues());

    thrift::ObjectDynamicDimension dynamicDimensionThrift2;
    dynamicDimensionThrift2.scopeId() = 0;
    dynamicDimensionThrift2.scopedValues() = {
        {1, makeThriftObjectValues({{2, 0}})}};
    dynamicDimensionThrift2.defaultValue() = 10;
    scalarDimensionThrift.objectDynamicDimension() = dynamicDimensionThrift2;
    dimensionThrift.scalarDimensions() = {scalarDimensionThrift};
    // For dynamic dimension from thrift, we need to check with specific scope
    // items
    EXPECT_FALSE(ObjectDimension(dimensionThrift, kDefaultTotalObjects)
                     .hasNegativeValues());
    EXPECT_TRUE(ObjectDimension(dimensionThrift, kDefaultTotalObjects)
                    .hasZeroValuedObjects(ScopeItemId(1)));

    // Test all positive values
    thrift::ObjectDynamicDimension dynamicDimensionThrift3;
    dynamicDimensionThrift3.scopeId() = 0;
    dynamicDimensionThrift3.scopedValues() = {
        {1, makeThriftObjectValues({{2, 10}})}};
    dynamicDimensionThrift3.defaultValue() = 20;
    scalarDimensionThrift.objectDynamicDimension() = dynamicDimensionThrift3;
    dimensionThrift.scalarDimensions() = {scalarDimensionThrift};

    EXPECT_FALSE(ObjectDimension(dimensionThrift, kDefaultTotalObjects)
                     .hasNegativeValues());
    EXPECT_FALSE(ObjectDimension(dimensionThrift, kDefaultTotalObjects)
                     .hasZeroValuedObjects(ScopeItemId(1)));
  }
  {
    // ObjectPartitionRoutingDimension from thrift
    thrift::ObjectPartitionRoutingDimension routingDimensionThrift;
    routingDimensionThrift.groupIdToValue() = {{18, 0}};
    routingDimensionThrift.defaultValue() = 20;
    routingDimensionThrift.partitionId() = 1;
    routingDimensionThrift.routingConfigId() = 2;

    thrift::ObjectScalarDimension scalarDimensionThrift;
    scalarDimensionThrift.objectPartitionRoutingDimension() =
        routingDimensionThrift;

    thrift::ObjectDimension dimensionThrift;
    dimensionThrift.scalarDimensions() = {scalarDimensionThrift};
    dimensionThrift.isDynamic() = false;

    REBALANCER_EXPECT_RUNTIME_ERROR(
        ObjectDimension(dimensionThrift, kDefaultTotalObjects)
            .hasZeroValuedObjects(),
        "Unexpected call to hasZeroValuedObjects() w.r.t. ObjectPartitionRoutingDimension");
  }
}

TEST(ObjectDimensionTest, GetExtremumValue) {
  // These tests assumes there exist at least one positive value.
  // If there is no positive value, the minimum will be 0.
  // This is because the default value is set to 0 in ObjectScalarDimension

  const auto object1 = makeObjectId(0);
  const auto object2 = makeObjectId(1);
  const auto object3 = makeObjectId(2);

  {
    // scalar dimension
    const auto dimension1 = makeStaticDim({{object1, -0.5}, {object2, 4}}, 3);
    EXPECT_EQ(3, dimension1.getMinimumPositiveValue());
    EXPECT_EQ(4, dimension1.getMaximumValue());

    const auto dimension2 = makeStaticDim({{object1, 0.5}, {object2, 4}}, -3);
    EXPECT_EQ(0.5, dimension2.getMinimumPositiveValue());
    EXPECT_EQ(4, dimension2.getMaximumValue());

    // all negative values, expect answer to be 0
    const auto dimension3 = makeStaticDim({{object1, -0.5}, {object2, -4}}, -3);
    EXPECT_EQ(std::nullopt, dimension3.getMinimumPositiveValue());
    EXPECT_EQ(-0.5, dimension3.getMaximumValue());
  }

  {
    // vector dimension
    const auto dimension1 = makeStaticDim(
        {{{object1, 10}, {object2, 20}}, {{object1, 11}, {object2, -1}}},
        {30, 31});
    const auto dimension2 = makeStaticDim(
        {{{object1, 10}, {object2, 20}}, {{object1, 11}, {object2, 1}}},
        {30, -31});
    const auto dimension3 = makeStaticDim(
        {{{object1, 10}, {object2, 20}}, {{object1, 11}, {object2, 12}}},
        {-1, 0.1});
    const auto dimension4 = makeStaticDim(
        {{{object1, -10}, {object2, -20}}, {{object1, -11}, {object2, -12}}},
        {-1, -0.1});

    EXPECT_EQ(10, dimension1.getMinimumPositiveValue());
    EXPECT_EQ(1, dimension2.getMinimumPositiveValue());
    EXPECT_EQ(0.1, dimension3.getMinimumPositiveValue());
    EXPECT_EQ(std::nullopt, dimension4.getMinimumPositiveValue());

    EXPECT_EQ(31, dimension1.getMaximumValue());
    EXPECT_EQ(30, dimension2.getMaximumValue());
    EXPECT_EQ(20, dimension3.getMaximumValue());
    EXPECT_EQ(-0.1, dimension4.getMaximumValue());
  }

  {
    // dynamic dimension
    const auto scope = makeScopeId(40);
    const auto item1 = makeScopeItemId(41);
    const auto item2 = makeScopeItemId(42);
    const auto dimension1 = makeDynamicDim(
        scope,
        {{item1, makeSharedPtrEntityToValueMap({{object1, 10}})},
         {item2,
          makeSharedPtrEntityToValueMap(
              {{object1, 20}, {object2, 30}, {object3, -3}})}},
        42);
    const auto dimension2 = makeDynamicDim(
        scope,
        {{item1, makeSharedPtrEntityToValueMap({{object1, 10}})},
         {item2,
          makeSharedPtrEntityToValueMap(
              {{object1, 20}, {object2, 30}, {object3, 3}})}},
        -3);
    const auto dimension3 = makeDynamicDim(
        scope,
        {{item1, makeSharedPtrEntityToValueMap({{object1, 10}})},
         {item2,
          makeSharedPtrEntityToValueMap(
              {{object1, 20}, {object2, 30}, {object3, 3}})}},
        1);
    const auto dimension4 = makeDynamicDim(
        scope,
        {{item1, makeSharedPtrEntityToValueMap({{object1, -10}})},
         {item2,
          makeSharedPtrEntityToValueMap(
              {{object1, -20}, {object2, -30}, {object3, -3}})}},
        -1);

    EXPECT_EQ(10, dimension1.getMinimumPositiveValue());
    EXPECT_EQ(3, dimension2.getMinimumPositiveValue());
    EXPECT_EQ(1, dimension3.getMinimumPositiveValue());
    EXPECT_EQ(std::nullopt, dimension4.getMinimumPositiveValue());

    EXPECT_EQ(42, dimension1.getMaximumValue());
    EXPECT_EQ(30, dimension2.getMaximumValue());
    EXPECT_EQ(30, dimension3.getMaximumValue());
    EXPECT_EQ(-1, dimension4.getMaximumValue());
  }
  {
    // scalar from thrift
    constexpr std::size_t kThriftTotalObjects = 21;
    thrift::ObjectStaticDimension staticDimensionThrift;
    staticDimensionThrift.values() = {{8, 10}, {20, 5}};
    staticDimensionThrift.defaultValue() = -20;

    thrift::ObjectScalarDimension scalarDimensionThrift;
    scalarDimensionThrift.objectStaticDimension() = staticDimensionThrift;

    thrift::ObjectDimension dimensionThrift;
    dimensionThrift.scalarDimensions() = {scalarDimensionThrift};

    EXPECT_EQ(
        5,
        ObjectDimension(dimensionThrift, kThriftTotalObjects)
            .getMinimumPositiveValue());
    EXPECT_EQ(
        10,
        ObjectDimension(dimensionThrift, kThriftTotalObjects)
            .getMaximumValue());

    thrift::ObjectStaticDimension staticDimensionThrift2;
    staticDimensionThrift2.values() = {{8, 10}, {20, 5}};
    staticDimensionThrift2.defaultValue() = 1;

    thrift::ObjectScalarDimension scalarDimensionThrift2;
    scalarDimensionThrift2.objectStaticDimension() = staticDimensionThrift2;

    thrift::ObjectDimension dimensionThrift2;
    dimensionThrift2.scalarDimensions() = {scalarDimensionThrift2};

    EXPECT_EQ(
        1,
        ObjectDimension(dimensionThrift2, kThriftTotalObjects)
            .getMinimumPositiveValue());
    EXPECT_EQ(
        10,
        ObjectDimension(dimensionThrift2, kThriftTotalObjects)
            .getMaximumValue());

    thrift::ObjectStaticDimension staticDimensionThrift3;
    staticDimensionThrift3.values() = {{8, -10}, {20, -5}};
    staticDimensionThrift3.defaultValue() = -1;

    thrift::ObjectScalarDimension scalarDimensionThrift3;
    scalarDimensionThrift3.objectStaticDimension() = staticDimensionThrift3;

    thrift::ObjectDimension dimensionThrift3;
    dimensionThrift3.scalarDimensions() = {scalarDimensionThrift3};

    EXPECT_EQ(
        std::nullopt,
        ObjectDimension(dimensionThrift3, kThriftTotalObjects)
            .getMinimumPositiveValue());
    EXPECT_EQ(
        -1,
        ObjectDimension(dimensionThrift3, kThriftTotalObjects)
            .getMaximumValue());
  }

  {
    // dynamic from thrift
    thrift::ObjectDynamicDimension dynamicDimensionThrift;
    dynamicDimensionThrift.scopeId() = 0;
    dynamicDimensionThrift.scopedValues() = {
        {1, makeThriftObjectValues({{2, -10}})}};
    dynamicDimensionThrift.defaultValue() = 20;

    thrift::ObjectScalarDimension scalarDimensionThrift;
    scalarDimensionThrift.objectDynamicDimension() = dynamicDimensionThrift;

    thrift::ObjectDimension dimensionThrift;
    dimensionThrift.scalarDimensions() = {scalarDimensionThrift};
    dimensionThrift.isDynamic() = true;

    EXPECT_EQ(
        20,
        ObjectDimension(dimensionThrift, kDefaultTotalObjects)
            .getMinimumPositiveValue());
    EXPECT_EQ(
        20,
        ObjectDimension(dimensionThrift, kDefaultTotalObjects)
            .getMaximumValue());

    thrift::ObjectDynamicDimension dynamicDimensionThrift2;
    dynamicDimensionThrift2.scopeId() = 0;
    dynamicDimensionThrift2.scopedValues() = {
        {1, makeThriftObjectValues({{2, -10}})}};
    dynamicDimensionThrift2.defaultValue() = -1;

    thrift::ObjectScalarDimension scalarDimensionThrift2;
    scalarDimensionThrift2.objectDynamicDimension() = dynamicDimensionThrift2;

    thrift::ObjectDimension dimensionThrift2;
    dimensionThrift2.scalarDimensions() = {scalarDimensionThrift2};
    dimensionThrift2.isDynamic() = true;

    EXPECT_EQ(
        std::nullopt,
        ObjectDimension(dimensionThrift2, kDefaultTotalObjects)
            .getMinimumPositiveValue());
    EXPECT_EQ(
        -1,
        ObjectDimension(dimensionThrift2, kDefaultTotalObjects)
            .getMaximumValue());
  }
  {
    thrift::ObjectPartitionRoutingDimension routingDimensionThrift;
    routingDimensionThrift.groupIdToValue() = {{18, 40}, {10, 10}};
    routingDimensionThrift.defaultValue() = 20;
    routingDimensionThrift.partitionId() = 1;
    routingDimensionThrift.routingConfigId() = 2;

    thrift::ObjectScalarDimension scalarDimensionThrift;
    scalarDimensionThrift.objectPartitionRoutingDimension() =
        routingDimensionThrift;

    thrift::ObjectDimension dimensionThrift;
    dimensionThrift.scalarDimensions() = {scalarDimensionThrift};
    dimensionThrift.isDynamic() = false;

    auto dimension = ObjectDimension(dimensionThrift, kDefaultTotalObjects);
    EXPECT_EQ(10, dimension.getMinimumPositiveValue());

    thrift::ObjectPartitionRoutingDimension routingDimensionThrift2;
    routingDimensionThrift2.groupIdToValue() = {{18, -1}, {10, -1}};
    routingDimensionThrift2.defaultValue() = -1;
    routingDimensionThrift2.partitionId() = 1;
    routingDimensionThrift2.routingConfigId() = 2;

    thrift::ObjectScalarDimension scalarDimensionThrift2;
    scalarDimensionThrift2.objectPartitionRoutingDimension() =
        routingDimensionThrift2;

    thrift::ObjectDimension dimensionThrift2;
    dimensionThrift2.scalarDimensions() = {scalarDimensionThrift2};
    dimensionThrift2.isDynamic() = false;

    auto dimension2 = ObjectDimension(dimensionThrift2, kDefaultTotalObjects);
    EXPECT_EQ(std::nullopt, dimension2.getMinimumPositiveValue());
    EXPECT_EQ(40, dimension.getMaximumValue());
    EXPECT_EQ(-1, dimension2.getMaximumValue());
  }
}

TEST(ObjectDimensionTest, IsRoutingConfigBased) {
  const auto object1 = makeObjectId(0);
  const auto object2 = makeObjectId(1);

  {
    // Test ObjectDimension with routing config based scalar dimension
    const ObjectDimension routingDimension(
        {{GroupId(8), 10}},
        20,
        PartitionId(7),
        RoutingConfigId(4),
        {{GroupId(9), 5}},
        2);

    EXPECT_TRUE(routingDimension.isRoutingConfigBased());
  }

  {
    // Test ObjectDimension with non-routing config based scalar dimensions
    const auto staticDimension =
        makeStaticDim({{object1, 10}, {object2, 20}}, 30);
    EXPECT_FALSE(staticDimension.isRoutingConfigBased());

    const auto scope = makeScopeId(50);
    const auto item1 = makeScopeItemId(51);
    const auto dynamicDimension = makeDynamicDim(
        scope, {{item1, makeSharedPtrEntityToValueMap({{object1, 10}})}}, 42);
    EXPECT_FALSE(dynamicDimension.isRoutingConfigBased());
  }

  {
    // Test ObjectDimension with multiple scalar dimensions where at least one
    // is routing config based Create a thrift-based dimension with both static
    // and routing dimensions
    thrift::ObjectStaticDimension staticDimensionThrift;
    staticDimensionThrift.values() = {{8, 10}};
    staticDimensionThrift.defaultValue() = 20;

    thrift::ObjectScalarDimension staticScalarDimensionThrift;
    staticScalarDimensionThrift.objectStaticDimension() = staticDimensionThrift;

    thrift::ObjectPartitionRoutingDimension routingDimensionThrift;
    routingDimensionThrift.groupIdToValue() = {{18, 40}};
    routingDimensionThrift.defaultValue() = 20;
    routingDimensionThrift.partitionId() = 1;
    routingDimensionThrift.routingConfigId() = 2;

    thrift::ObjectScalarDimension routingScalarDimensionThrift;
    routingScalarDimensionThrift.objectPartitionRoutingDimension() =
        routingDimensionThrift;

    thrift::ObjectDimension dimensionThrift;
    dimensionThrift.scalarDimensions() = {
        staticScalarDimensionThrift, routingScalarDimensionThrift};
    dimensionThrift.isDynamic() = false;

    auto mixedDimension =
        ObjectDimension(dimensionThrift, kDefaultTotalObjects);
    EXPECT_TRUE(mixedDimension.isRoutingConfigBased());
  }
}

TEST(ObjectDimensionTest, StaticObjectValuesReference) {
  const auto object1 = makeObjectId(0);
  const auto object2 = makeObjectId(1);

  const auto dimension = makeStaticDim({{object1, 10}, {object2, 20}}, 5);
  const auto& scalarDim = dimension.at(0);

  const auto& vals1 = scalarDim.values();
  const auto& vals2 = scalarDim.values();
  EXPECT_EQ(&vals1, &vals2);
  EXPECT_EQ(10, scalarDim.getValue(object1));
  EXPECT_EQ(20, scalarDim.getValue(object2));
  EXPECT_NE(nullptr, vals1.asMapOrNull());
}

TEST(ObjectDimensionTest, ObjectValuesForEachNonDefault) {
  const auto object1 = makeObjectId(0);
  const auto object2 = makeObjectId(1);

  auto map = std::make_shared<const ObjectIdToDoubleMap>(
      Map<ObjectId, double>{{object1, 10}, {object2, 20}},
      /*defaultValue=*/5.0,
      kDefaultTotalObjects);

  const ObjectValues storage(map);

  EXPECT_EQ(2, storage.nonDefaultCount());

  Map<ObjectId, double> collected;
  storage.forEachNonDefault(
      [&](ObjectId id, double val) { collected.emplace(id, val); });
  EXPECT_EQ(2, collected.size());
  EXPECT_EQ(10, collected.at(object1));
  EXPECT_EQ(20, collected.at(object2));
}

TEST(ObjectDimensionTest, GroupBackedDynamic) {
  const auto object1 = makeObjectId(0);
  const auto object2 = makeObjectId(1);
  const auto object3 = makeObjectId(2);
  const auto group1 = GroupId(101);
  const auto group2 = GroupId(102);

  const auto scope = makeScopeId(10);
  const auto item1 = makeScopeItemId(11);
  const auto item2 = makeScopeItemId(12);
  auto partition =
      std::make_shared<const Partition>(Map<GroupId, std::vector<ObjectId>>{
          {group1, {object1, object2}}, {group2, {object3}}});

  const ObjectDimension dimension(
      scope,
      partition,
      {{item1, makeSharedPtrEntityToValueMap<GroupId>({{group1, 10}})},
       {item2, makeSharedPtrEntityToValueMap<GroupId>({{group2, 30}})}},
      42,
      /*totalObjects=*/3,
      /*partitionId=*/PartitionId(0));

  const auto& scalarDimension = dimension.at(0);
  EXPECT_TRUE(scalarDimension.isDynamic());
  EXPECT_EQ(scope, scalarDimension.getScopeId());

  EXPECT_EQ(10, scalarDimension.getValue(object1, item1));
  EXPECT_EQ(10, scalarDimension.getValue(object2, item1));
  EXPECT_EQ(42, scalarDimension.getValue(object3, item1));
  EXPECT_EQ(42, scalarDimension.getValue(object1, item2));
  EXPECT_EQ(42, scalarDimension.getValue(object2, item2));
  EXPECT_EQ(30, scalarDimension.getValue(object3, item2));

  EXPECT_EQ(62, scalarDimension.values(item1).sum());
  EXPECT_EQ(114, scalarDimension.values(item2).sum());

  const std::vector<ObjectId> subset{object1, object3};
  EXPECT_EQ(
      52,
      scalarDimension.values(item1).sum(
          folly::Range<const ObjectId*>(subset.data(), subset.size())));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      scalarDimension.values(),
      "values of an object dynamic dimension depend on the scope item");

  const auto& storage = scalarDimension.values(item1);
  EXPECT_EQ(&storage, &scalarDimension.values(item1));
  EXPECT_EQ(2, storage.nonDefaultCount());
  // Group-backed storage has no single object map to hand out.
  EXPECT_EQ(nullptr, storage.asMapOrNull());
}

TEST(ObjectDimensionTest, GroupBackedExpandsGroupsToObjectValues) {
  const auto object1 = makeObjectId(0);
  const auto object2 = makeObjectId(1);
  const auto object3 = makeObjectId(2);
  const auto object4 = makeObjectId(3);
  const auto ungroupedObject = makeObjectId(4);
  const auto group1 = GroupId(101);
  const auto group2 = GroupId(102);
  const auto group3 = GroupId(103);
  const auto emptyGroup = GroupId(104);
  constexpr double kDefaultValue = 5.0;

  auto partition =
      std::make_shared<const Partition>(Map<GroupId, std::vector<ObjectId>>{
          {group1, {object1, object2}},
          {group2, {object3}},
          {group3, {object4}},
          {emptyGroup, {}}});

  // group1 and group2 share a value; group3 differs; emptyGroup has no objects.
  const ObjectValues storage(
      makeSharedPtrEntityToValueMap<GroupId>(
          {{group1, 7.0}, {group2, 7.0}, {group3, 9.0}, {emptyGroup, 11.0}}),
      partition,
      kDefaultValue,
      /*totalObjects=*/5,
      /*partitionId=*/PartitionId(0));

  // forEachNonDefault expands every stored group to its objects, each carrying
  // the group's value.
  Map<ObjectId, double> nonDefaultValues;
  storage.forEachNonDefault([&](ObjectId objectId, double value) {
    nonDefaultValues.emplace(objectId, value);
  });
  EXPECT_EQ(
      (Map<ObjectId, double>{
          {object1, 7.0}, {object2, 7.0}, {object3, 7.0}, {object4, 9.0}}),
      nonDefaultValues);

  // Group-backed storage dispatches to the group branch of visit, never the
  // object-map branch, and reports its owning partition id.
  bool sawObjectMap = false;
  std::vector<GroupId> visitedGroups;
  storage.visit(
      [&](const ObjectIdToDoubleMap&) { sawObjectMap = true; },
      [&](PartitionId observedPartitionId,
          const GroupIdToDoubleMap& groupValues) {
        EXPECT_EQ(PartitionId(0), observedPartitionId);
        for (const auto& groupEntry : groupValues) {
          visitedGroups.push_back(groupEntry.first);
        }
      });
  EXPECT_FALSE(sawObjectMap);
  EXPECT_EQ(4, visitedGroups.size());

  EXPECT_EQ(7.0, storage.getObjectValue(object1));
  EXPECT_EQ(7.0, storage.getObjectValue(object3));
  EXPECT_EQ(9.0, storage.getObjectValue(object4));
  EXPECT_EQ(kDefaultValue, storage.getObjectValue(ungroupedObject));
}

TEST(ObjectDimensionTest, GroupBackedDynamicTracksZeroValuesPerScopeItem) {
  const auto object1 = makeObjectId(0);
  const auto object2 = makeObjectId(1);
  const auto object3 = makeObjectId(2);
  const auto group1 = GroupId(101);
  const auto group2 = GroupId(102);

  const auto scope = makeScopeId(10);
  const auto itemWithZero = makeScopeItemId(11);
  const auto itemWithoutZero = makeScopeItemId(12);
  auto partition =
      std::make_shared<const Partition>(Map<GroupId, std::vector<ObjectId>>{
          {group1, {object1, object2}}, {group2, {object3}}});

  const ObjectDimension dimension(
      scope,
      partition,
      {{itemWithZero, makeSharedPtrEntityToValueMap<GroupId>({{group1, 0.0}})},
       {itemWithoutZero,
        makeSharedPtrEntityToValueMap<GroupId>({{group2, 30.0}})}},
      /*defaultValue=*/42.0,
      /*totalObjects=*/3,
      /*partitionId=*/PartitionId(0));

  const auto& scalarDimension = dimension.at(0);
  EXPECT_TRUE(scalarDimension.hasZeroValuedObjects());
  EXPECT_TRUE(scalarDimension.hasZeroValuedObjects(itemWithZero));
  EXPECT_FALSE(scalarDimension.hasZeroValuedObjects(itemWithoutZero));
}

TEST(ObjectDimensionTest, GroupBackedDynamicSliceGroup) {
  const auto object1 = makeObjectId(0);
  const auto object2 = makeObjectId(1);
  const auto object3 = makeObjectId(2);
  const auto group1 = GroupId(101);
  const auto group2 = GroupId(102);

  const auto scope = makeScopeId(10);
  const auto item1 = makeScopeItemId(11);
  auto partition =
      std::make_shared<const Partition>(Map<GroupId, std::vector<ObjectId>>{
          {group1, {object1, object2}}, {group2, {object3}}});

  const ObjectDimension dimension(
      scope,
      partition,
      {{item1,
        makeSharedPtrEntityToValueMap<GroupId>({{group1, 10}, {group2, 30}})}},
      42,
      /*totalObjects=*/3,
      /*partitionId=*/PartitionId(0));

  const auto& scalarDimension = dimension.at(0);
  const auto& storage = scalarDimension.values(item1);
  const auto groupStorage = storage.sliceGroup(*partition, group1);
  EXPECT_EQ(nullptr, groupStorage.asMapOrNull());
  EXPECT_EQ(20, groupStorage.sum());
  EXPECT_EQ(10, groupStorage.getObjectValue(object1));
  EXPECT_EQ(10, groupStorage.getObjectValue(object2));
  EXPECT_EQ(0, groupStorage.getObjectValue(object3));

  const Partition crossPartition(
      Map<GroupId, std::vector<ObjectId>>{{GroupId(201), {object1, object3}}});
  const auto crossPartitionStorage =
      storage.sliceGroup(crossPartition, GroupId(201));
  EXPECT_NE(nullptr, crossPartitionStorage.asMapOrNull());
  EXPECT_EQ(40, crossPartitionStorage.sum());
  EXPECT_EQ(10, crossPartitionStorage.getObjectValue(object1));
  EXPECT_EQ(0, crossPartitionStorage.getObjectValue(object2));
  EXPECT_EQ(30, crossPartitionStorage.getObjectValue(object3));
}

TEST(ObjectDimensionTest, StaticDimensionSliceGroup) {
  const auto object1 = makeObjectId(0);
  const auto object2 = makeObjectId(1);
  const auto group1 = GroupId(101);
  const Partition partition(
      Map<GroupId, std::vector<ObjectId>>{{group1, {object1}}});
  const auto dimension = makeStaticDim({{object1, 10}}, 5);
  const auto& scalarDim = dimension.at(0);
  const auto groupStorage = scalarDim.values().sliceGroup(partition, group1);
  EXPECT_NE(nullptr, groupStorage.asMapOrNull());
  EXPECT_EQ(10, groupStorage.sum());
  EXPECT_EQ(10, groupStorage.getObjectValue(object1));
  EXPECT_EQ(0, groupStorage.getObjectValue(object2));
}

TEST(ObjectDimensionTest, DynamicHasZeroValuedObjectsConcurrent) {
  // Two threads call hasZeroValuedObjects() concurrently on the same
  // ObjectDynamicDimension, each for its own scope item. Without per-scope-item
  // values pre-computed in the constructor, the two calls race on the same
  // shared cache and TSAN catches the race.
  const auto scope = makeScopeId(0);
  const auto scopeItemA = makeScopeItemId(1);
  const auto scopeItemB = makeScopeItemId(2);

  Map<ScopeItemId, std::shared_ptr<const entities::ObjectIdToDoubleMap>> values;
  values.emplace(
      scopeItemA,
      std::make_shared<const ObjectIdToDoubleMap>(
          Map<ObjectId, double>{{makeObjectId(0), 0.0}},
          /*defaultValue=*/1.0,
          kDefaultTotalObjects));
  values.emplace(
      scopeItemB,
      std::make_shared<const ObjectIdToDoubleMap>(
          Map<ObjectId, double>{{makeObjectId(0), 5.0}},
          /*defaultValue=*/1.0,
          kDefaultTotalObjects));

  const ObjectDynamicDimension dimension(
      scope, std::move(values), /*defaultValue=*/1.0, kDefaultTotalObjects);

  std::thread threadA(
      [&] { EXPECT_TRUE(dimension.hasZeroValuedObjects(scopeItemA)); });
  std::thread threadB(
      [&] { EXPECT_FALSE(dimension.hasZeroValuedObjects(scopeItemB)); });
  threadA.join();
  threadB.join();
}
} // namespace facebook::rebalancer::entities::tests
