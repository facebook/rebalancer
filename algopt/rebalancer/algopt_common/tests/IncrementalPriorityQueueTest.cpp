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

#include "algopt/rebalancer/algopt_common/IncrementalPriorityQueue.h"
#include "algopt/rebalancer/algopt_common/IncrementalPriorityQueueV2.h"
#include "algopt/rebalancer/algopt_common/TestUtils.h"

#include <folly/container/irange.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace facebook::algopt::tests {

template <class Queue>
class IncrementalPriorityQueueTest : public ::testing::Test {};

using QueueTypes = ::testing::Types<
    IncrementalPriorityQueue<std::string>,
    IncrementalPriorityQueueV2<std::string>>;
TYPED_TEST_SUITE(IncrementalPriorityQueueTest, QueueTypes);

TYPED_TEST(IncrementalPriorityQueueTest, Basic) {
  TypeParam sorter;
  ASSERT_EQ(0, sorter.size());

  sorter.update({"a", "b"});
  ASSERT_EQ(2, sorter.size());
  EXPECT_FALSE(sorter.is_top_strict());
  EXPECT_EQ("a", sorter.top());

  sorter.remove("a");
  ASSERT_EQ(1, sorter.size());
  EXPECT_TRUE(sorter.is_top_strict());
  EXPECT_EQ("b", sorter.top());

  sorter.remove("b");
  ASSERT_EQ(0, sorter.size());

  sorter.update({"a"});
  ASSERT_EQ(0, sorter.size());

  sorter.update({"b", "c"});
  ASSERT_EQ(1, sorter.size());
  EXPECT_TRUE(sorter.is_top_strict());
  EXPECT_EQ("c", sorter.top());
}

TYPED_TEST(IncrementalPriorityQueueTest, Ties) {
  TypeParam sorter;
  ASSERT_EQ(0, sorter.size());

  sorter.update({"a", "b", "c"});
  ASSERT_EQ(3, sorter.size());
  EXPECT_FALSE(sorter.is_top_strict());
  EXPECT_EQ("a", sorter.top());

  sorter.update({"b", "d", "e"});
  ASSERT_EQ(5, sorter.size());
  EXPECT_TRUE(sorter.is_top_strict());
  EXPECT_EQ("b", sorter.top());

  sorter.remove("b");
  ASSERT_EQ(4, sorter.size());
  EXPECT_FALSE(sorter.is_top_strict());
  EXPECT_EQ("a", sorter.top());

  sorter.update({"b", "c", "e", "f"});
  ASSERT_EQ(5, sorter.size());
  EXPECT_TRUE(sorter.is_top_strict());
  EXPECT_EQ("c", sorter.top());

  sorter.remove("c");
  ASSERT_EQ(4, sorter.size());
  EXPECT_TRUE(sorter.is_top_strict());
  EXPECT_EQ("a", sorter.top());

  sorter.remove("a");
  ASSERT_EQ(3, sorter.size());
  EXPECT_TRUE(sorter.is_top_strict());
  EXPECT_EQ("e", sorter.top());

  sorter.remove("e");
  ASSERT_EQ(2, sorter.size());
  EXPECT_TRUE(sorter.is_top_strict());
  EXPECT_EQ("d", sorter.top());

  sorter.remove("d");
  ASSERT_EQ(1, sorter.size());
  EXPECT_TRUE(sorter.is_top_strict());
  EXPECT_EQ("f", sorter.top());

  sorter.remove("f");
  ASSERT_EQ(0, sorter.size());
}

TYPED_TEST(IncrementalPriorityQueueTest, EmptyException) {
  const TypeParam sorter;
  constexpr auto expectedMessage =
      std::is_same_v<TypeParam, IncrementalPriorityQueueV2<std::string>>
      ? "IncrementalPriorityQueueV2 is expected to be non-empty"
      : "empty";
  REBALANCER_EXPECT_RUNTIME_ERROR(sorter.top(), expectedMessage);
  REBALANCER_EXPECT_RUNTIME_ERROR(sorter.is_top_strict(), expectedMessage);
}

// Listing an item twice in one update() counts twice. In the second update
// "b" and "c" start out tied, and the repeat puts "b" ahead.
TYPED_TEST(IncrementalPriorityQueueTest, DuplicateWithinSingleUpdate) {
  TypeParam sorter;
  sorter.update({"a", "a", "b", "c"});
  ASSERT_EQ(3, sorter.size());
  EXPECT_TRUE(sorter.is_top_strict());
  EXPECT_EQ("a", sorter.top());

  sorter.update({"b", "b", "c"});
  ASSERT_EQ(3, sorter.size());
  EXPECT_TRUE(sorter.is_top_strict());
  EXPECT_EQ("a", sorter.top());

  sorter.remove("a");
  EXPECT_TRUE(sorter.is_top_strict());
  EXPECT_EQ("b", sorter.top());

  sorter.remove("b");
  EXPECT_TRUE(sorter.is_top_strict());
  EXPECT_EQ("c", sorter.top());
}

TYPED_TEST(IncrementalPriorityQueueTest, RemoveBeforeAddBansForever) {
  TypeParam sorter;
  sorter.remove("ghost");
  sorter.update({"ghost", "real"});
  ASSERT_EQ(1, sorter.size());
  EXPECT_TRUE(sorter.is_top_strict());
  EXPECT_EQ("real", sorter.top());
}

TYPED_TEST(IncrementalPriorityQueueTest, CopyPreservesState) {
  TypeParam queue;
  queue.update({"a", "b"});
  queue.update({"b", "c"});
  // Removed items are absent from the groups but must remain removed after a
  // copy.
  queue.remove("removed");

  TypeParam copiedQueue = queue;
  copiedQueue.update({"removed"});
  EXPECT_EQ(3, copiedQueue.size());
  EXPECT_EQ("b", copiedQueue.top());

  TypeParam assignedQueue;
  assignedQueue = queue;
  assignedQueue.update({"removed"});
  EXPECT_EQ(3, assignedQueue.size());
  EXPECT_EQ("b", assignedQueue.top());
}

namespace {

using ItemToUpdates = std::map<std::string, std::vector<int>>;

// Same order as IncrementalPriorityQueue: the first differing update number
// decides; if one list is a prefix of the other, the longer one comes first;
// identical lists go to the smaller item.
bool comesFirst(
    const ItemToUpdates::value_type& first,
    const ItemToUpdates::value_type& second) {
  const auto& firstUpdates = first.second;
  const auto& secondUpdates = second.second;
  const auto shared = std::min(firstUpdates.size(), secondUpdates.size());
  for (const auto i : folly::irange(shared)) {
    if (firstUpdates[i] != secondUpdates[i]) {
      return firstUpdates[i] < secondUpdates[i];
    }
  }
  if (firstUpdates.size() != secondUpdates.size()) {
    return firstUpdates.size() > secondUpdates.size();
  }
  return first.first < second.first;
}

// Slow, obvious version of the queue: stores the update numbers per item and
// scans them all to find the top. Short enough to read and trust, so the test
// below can check the real queue against it.
class SimpleQueue {
 public:
  void update(const std::vector<std::string>& items) {
    for (const auto& item : items) {
      if (!removed_.contains(item)) {
        itemToUpdates_[item].push_back(updateCount_);
      }
    }
    ++updateCount_;
  }

  void remove(const std::string& item) {
    removed_.insert(item);
    itemToUpdates_.erase(item);
  }

  size_t size() const {
    return itemToUpdates_.size();
  }

  bool empty() const {
    return itemToUpdates_.empty();
  }

  const std::string& top() const {
    return topItem()->first;
  }

  bool is_top_strict() const {
    const auto top = topItem();
    return std::none_of(
        itemToUpdates_.begin(), itemToUpdates_.end(), [&](const auto& entry) {
          return entry.first != top->first && entry.second == top->second;
        });
  }

 private:
  ItemToUpdates::const_iterator topItem() const {
    return std::min_element(
        itemToUpdates_.begin(), itemToUpdates_.end(), comesFirst);
  }

  int updateCount_ = 0;
  std::set<std::string> removed_;
  ItemToUpdates itemToUpdates_;
};

} // namespace

TYPED_TEST(IncrementalPriorityQueueTest, RandomUpdatesMatchSimpleQueue) {
  constexpr int kNumUpdates = 20;
  constexpr int kNumItems = 2'000;
  constexpr size_t kBatchSize = 200;

  TypeParam queue;
  SimpleQueue simpleQueue;
  std::mt19937 randomEngine(42);
  std::uniform_int_distribution<int> itemPicker(0, kNumItems - 1);

  for (const auto updateIndex : folly::irange(kNumUpdates)) {
    // Batches can repeat an item, so updates with duplicates get covered too.
    std::vector<std::string> batch;
    batch.reserve(kBatchSize);
    for ([[maybe_unused]] const auto _ : folly::irange(kBatchSize)) {
      batch.push_back("item" + std::to_string(itemPicker(randomEngine)));
    }
    queue.update(batch);
    simpleQueue.update(batch);

    if (updateIndex % 5 == 4) {
      const std::string itemToRemove = "item" + std::to_string(updateIndex);
      queue.remove(itemToRemove);
      simpleQueue.remove(itemToRemove);
    }

    ASSERT_EQ(simpleQueue.size(), queue.size());
    EXPECT_EQ(simpleQueue.top(), queue.top());
    EXPECT_EQ(simpleQueue.is_top_strict(), queue.is_top_strict());
  }

  while (!queue.empty()) {
    ASSERT_EQ(simpleQueue.size(), queue.size());
    const std::string top = queue.top();
    EXPECT_EQ(simpleQueue.top(), top);
    queue.remove(top);
    simpleQueue.remove(top);
  }
  EXPECT_TRUE(simpleQueue.empty());
}

} // namespace facebook::algopt::tests
