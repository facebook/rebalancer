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

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/ObjectValueTypes.h"
#include "algopt/rebalancer/entities/Partition.h"
#include "algopt/rebalancer/solver/tests/IdConverterTestUtils.h"
#include "algopt/rebalancer/solver/utils/ObjectStore.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <folly/container/irange.h>
#include <folly/lang/Bits.h>
#include <gtest/gtest.h>

#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer::packer::tests {
TEST(ObjectStoreTest, WithOutObjectValues) {
  auto factory = ObjectStore::Factory{/*scores=*/nullptr,
                                      /*useDynamicObjectOrdering=*/false};
  auto store = factory.get();
  store.insert(object(1));
  store.insert(object(3));
  store.insert(object(2));
  store.insert(object(5));
  store.insert(object(7));

  std::vector<entities::ObjectId> actualOrder(store.begin(), store.end());
  // order is the same as the order of elements in PackerSet
  PackerSet<entities::ObjectId> expectedOrder(
      {object(1), object(3), object(2), object(5), object(7)});

  EXPECT_EQ(expectedOrder.size(), actualOrder.size());
  auto it = expectedOrder.begin();
  for (const auto i : folly::irange(expectedOrder.size())) {
    EXPECT_EQ(*it, actualOrder[i]);
    ++it;
  }
}

TEST(ObjectStoreTest, WithOutObjectValuesAndUsingDynamicObjectOrdering) {
  auto factory = ObjectStore::Factory{/*scores=*/nullptr,
                                      /*useDynamicObjectOrdering=*/true};
  auto store = factory.get();
  store.insert(object(1));
  store.insert(object(3));
  store.insert(object(2));
  store.insert(object(5));
  store.insert(object(7));

  const std::vector<entities::ObjectId> actualOrder(store.begin(), store.end());
  // order is the same as the insertion order since we are using
  // AccessOrderedFastSet
  const std::vector<entities::ObjectId> expectedOrder(
      {object(1), object(3), object(2), object(5), object(7)});
  EXPECT_EQ(expectedOrder, actualOrder);

  // next, verify that if we stop iteration after two increments, then we
  // restart iteration from the third element
  {
    auto it = store.begin();
    ++it;
    ++it;
    EXPECT_EQ(object(2), *it);
  }

  // verify that the new begin is now the third element
  {
    auto it = store.begin();
    EXPECT_EQ(object(2), *it);
  }

  // now if we do a full iteration, we should start from the third element and
  // circle back to the first two
  const std::vector<entities::ObjectId> actualOrder2(
      store.begin(), store.end());
  const std::vector<entities::ObjectId> expectedOrder2(
      {object(2), object(5), object(7), object(1), object(3)});
  EXPECT_EQ(expectedOrder2, actualOrder2);
}

TEST(ObjectStoreTest, WithObjectValues) {
  const PackerMap<entities::ObjectId, double> scoreMap = {
      {object(1), 100.0},
      {object(2), 10.0},
      {object(4), -1.0},
      {object(5), 200.0},
      {object(6), 200.0}};
  auto scoreMapPtr = std::make_shared<const entities::ObjectIdToDoubleMap>(
      scoreMap, /*defaultValue=*/0.0, /*totalSize=*/8);
  auto objectScores =
      std::make_shared<ObjectScores>(entities::ObjectValues(scoreMapPtr));
  auto factory =
      ObjectStore::Factory(objectScores, /*useDynamicObjectOrdering=*/false);
  auto store = factory.get();
  for (const auto& [objectId, _] : scoreMap) {
    store.insert(objectId);
  }
  // extra element
  store.insert(object(3)); // will take default value of 0.0

  // Note that there is a tie since both object(5) and object(6) have the same
  // value in scoreMap.
  auto expectedFirstElement =
      objectScores->operator()(object(5), object(6)) ? object(5) : object(6);
  auto expectedSecondElement =
      expectedFirstElement == object(5) ? object(6) : object(5);

  std::vector<entities::ObjectId> actualOrder(store.begin(), store.end());
  EXPECT_EQ(expectedFirstElement, actualOrder[0]);
  EXPECT_EQ(expectedSecondElement, actualOrder[1]);

  // sorted order
  const std::vector<entities::ObjectId> expectedOrder(
      {expectedFirstElement,
       expectedSecondElement,
       object(1),
       object(2),
       object(3),
       object(4)});

  EXPECT_EQ(expectedOrder, actualOrder);

  // check that copying works as expected
  auto copiedStore = store;
  const std::vector<entities::ObjectId> copiedActualOrder(
      copiedStore.begin(), copiedStore.end());
  EXPECT_EQ(expectedOrder, copiedActualOrder);

  // modify the original store and check that the copy is not affected
  store.erase(object(1));
  store.erase(object(2));
  const std::vector<entities::ObjectId> modifiedActualOrder(
      store.begin(), store.end());
  const std::vector<entities::ObjectId> modifiedExpectedOrder = {
      expectedFirstElement, expectedSecondElement, object(3), object(4)};
  EXPECT_EQ(modifiedExpectedOrder, modifiedActualOrder);
  EXPECT_EQ(expectedOrder, copiedActualOrder);
}

TEST(ObjectStoreTest, WithObjectValuesAndUsingDynamicObjectOrdering) {
  const PackerMap<entities::ObjectId, double> scoreMap = {
      {object(1), 100.0},
      {object(2), 10.0},
      {object(4), -1.0},
      {object(5), 200.0},
      {object(6), 200.0}};
  auto scoreMapPtr = std::make_shared<const entities::ObjectIdToDoubleMap>(
      scoreMap, /*defaultValue=*/0.0, /*totalSize=*/8);
  auto objectScores =
      std::make_shared<ObjectScores>(entities::ObjectValues(scoreMapPtr));
  auto factory =
      ObjectStore::Factory(objectScores, /*useDynamicObjectOrdering=*/true);
  auto store = factory.get();
  for (const auto& [objectId, _] : scoreMap) {
    store.insert(objectId);
  }
  // extra element
  store.insert(object(3)); // will take default value of 0.0

  // Note that there is a tie since both object(5) and object(6) have the same
  // value in scoreMap.
  auto expectedFirstElement =
      objectScores->operator()(object(5), object(6)) ? object(5) : object(6);
  auto expectedSecondElement =
      expectedFirstElement == object(5) ? object(6) : object(5);

  std::vector<entities::ObjectId> actualOrder(store.begin(), store.end());
  EXPECT_EQ(expectedFirstElement, actualOrder[0]);
  EXPECT_EQ(expectedSecondElement, actualOrder[1]);

  // sorted order
  const std::vector<entities::ObjectId> expectedOrder(
      {expectedFirstElement,
       expectedSecondElement,
       object(1),
       object(2),
       object(3),
       object(4)});

  EXPECT_EQ(expectedOrder, actualOrder);

  // next, verify that if we stop iteration after two increments, then we
  // restart iteration from the third element
  {
    auto it = store.begin();
    ++it;
    ++it;
    EXPECT_EQ(object(1), *it);
  }

  // verify that the new begin has been updated correctly is now the third
  // element
  {
    auto it = store.begin();
    EXPECT_EQ(object(1), *it);
  }

  // now if we do a full iteration, we should start from the third element and
  // circle back to the first two
  const std::vector<entities::ObjectId> actualOrder2(
      store.begin(), store.end());
  const std::vector<entities::ObjectId> expectedOrder2(
      {object(1),
       object(2),
       object(3),
       object(4),
       expectedFirstElement,
       expectedSecondElement});
  EXPECT_EQ(expectedOrder2, actualOrder2);

  // next, verify that if we insert an element, then the order is reset and we
  // back to sorted order
  store.insert(object(7));
  EXPECT_EQ(expectedFirstElement, *store.begin()); // begin was reset
  EXPECT_EQ(7, store.size());

  store.erase(object(7));

  // check that copying works as expected
  auto copiedStore = store;
  const std::vector<entities::ObjectId> copiedActualOrder(
      copiedStore.begin(), copiedStore.end());
  EXPECT_EQ(expectedOrder, copiedActualOrder);

  // modify the original store and check that the copy is not affected
  store.erase(object(1));
  store.erase(object(2));
  const std::vector<entities::ObjectId> modifiedActualOrder(
      store.begin(), store.end());
  const std::vector<entities::ObjectId> modifiedExpectedOrder = {
      expectedFirstElement, expectedSecondElement, object(3), object(4)};
  EXPECT_EQ(modifiedExpectedOrder, modifiedActualOrder);
  EXPECT_EQ(expectedOrder, copiedActualOrder);
}

TEST(ObjectStoreTest, WithGroupBackedObjectValues) {
  const auto group1 = entities::GroupId(1);
  const auto group2 = entities::GroupId(2);
  auto partition = std::make_shared<const entities::Partition>(
      entities::Map<entities::GroupId, std::vector<entities::ObjectId>>{
          {group1, {object(1), object(2)}}, {group2, {object(3)}}});
  auto groupValues =
      std::make_shared<const entities::Map<entities::GroupId, double>>(
          entities::Map<entities::GroupId, double>{
              {group1, 10.0}, {group2, 100.0}});
  auto objectScores = std::make_shared<ObjectScores>(entities::ObjectValues(
      std::move(groupValues),
      std::move(partition),
      /*defaultValue=*/0.0,
      /*totalObjects=*/8,
      /*partitionId=*/entities::PartitionId(0)));
  auto store =
      ObjectStore::Factory(objectScores, /*useDynamicObjectOrdering=*/false)
          .get();

  store.insert(object(1));
  store.insert(object(2));
  store.insert(object(3));
  store.insert(object(4)); // default score

  const auto expectedSecond =
      objectScores->operator()(object(1), object(2)) ? object(1) : object(2);
  const auto expectedThird =
      expectedSecond == object(1) ? object(2) : object(1);
  const std::vector<entities::ObjectId> expectedOrder(
      {object(3), expectedSecond, expectedThird, object(4)});
  const std::vector<entities::ObjectId> actualOrder(store.begin(), store.end());
  EXPECT_EQ(expectedOrder, actualOrder);
}

} // namespace facebook::rebalancer::packer::tests
