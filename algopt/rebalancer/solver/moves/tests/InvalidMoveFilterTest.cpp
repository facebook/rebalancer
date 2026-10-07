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

#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"

#include <gtest/gtest.h>

#include <thread>
#include <utility>
#include <vector>

namespace facebook::rebalancer::tests {

using entities::ContainerId;
using entities::EntityIdType;
using entities::ObjectId;

TEST(InvalidMoveFilterTest, EmptyFilterSkipsNothing) {
  const InvalidMoveFilter filter(/*numObjects=*/100, /*numContainers=*/50);
  EXPECT_TRUE(filter.empty());
  EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(0), ContainerId(0)));
  EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(99), ContainerId(49)));
}

TEST(InvalidMoveFilterTest, EmptyZeroContainerFilterSkipsWithoutIndexing) {
  const InvalidMoveFilter filter(/*numObjects=*/1, /*numContainers=*/0);

  EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(0), ContainerId(0)));
  EXPECT_FALSE(filter.anyMarkedInvalid({ObjectId(0)}, ContainerId(0)));
}

TEST(InvalidMoveFilterTest, MarkInvalid) {
  InvalidMoveFilter filter(/*numObjects=*/10, /*numContainers=*/5);

  filter.markInvalid({ObjectId(3)}, ContainerId(2));

  EXPECT_FALSE(filter.empty());
  EXPECT_TRUE(filter.isMarkedInvalid(ObjectId(3), ContainerId(2)));
  EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(3), ContainerId(0)));
  EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(0), ContainerId(2)));
}

TEST(InvalidMoveFilterTest, MultiplePairs) {
  InvalidMoveFilter filter(/*numObjects=*/10, /*numContainers=*/5);

  filter.markInvalid({ObjectId(0)}, ContainerId(0));
  filter.markInvalid({ObjectId(9)}, ContainerId(4));
  filter.markInvalid({ObjectId(5)}, ContainerId(2));

  EXPECT_TRUE(filter.isMarkedInvalid(ObjectId(0), ContainerId(0)));
  EXPECT_TRUE(filter.isMarkedInvalid(ObjectId(9), ContainerId(4)));
  EXPECT_TRUE(filter.isMarkedInvalid(ObjectId(5), ContainerId(2)));
  EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(0), ContainerId(4)));
  EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(9), ContainerId(0)));
}

TEST(InvalidMoveFilterTest, MarkInvalidObjects) {
  InvalidMoveFilter filter(/*numObjects=*/130, /*numContainers=*/3);

  filter.markInvalid(
      {ObjectId(0), ObjectId(5), ObjectId(64), ObjectId(127), ObjectId(129)},
      ContainerId(2));

  for (const auto objectId : {0, 5, 64, 127, 129}) {
    EXPECT_TRUE(filter.isMarkedInvalid(ObjectId(objectId), ContainerId(2)));
  }
  EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(63), ContainerId(2)));
  EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(64), ContainerId(1)));
}

TEST(InvalidMoveFilterTest, MarkNoObjectsIsNoOp) {
  InvalidMoveFilter filter(/*numObjects=*/10, /*numContainers=*/1);

  filter.markInvalid({}, ContainerId(0));

  EXPECT_TRUE(filter.empty());
}

TEST(InvalidMoveFilterTest, AnyMarkedInvalidChecksOneDestinationRow) {
  InvalidMoveFilter filter(/*numObjects=*/5, /*numContainers=*/3);
  filter.markInvalid({ObjectId(2)}, ContainerId(1));

  EXPECT_TRUE(filter.anyMarkedInvalid(
      {ObjectId(0), ObjectId(2), ObjectId(4)}, ContainerId(1)));
  EXPECT_FALSE(
      filter.anyMarkedInvalid({ObjectId(0), ObjectId(4)}, ContainerId(1)));
  EXPECT_FALSE(filter.anyMarkedInvalid({ObjectId(2)}, ContainerId(2)));
}

TEST(InvalidMoveFilterTest, OneObjectInvalidForAllContainers) {
  InvalidMoveFilter filter(/*numObjects=*/5, /*numContainers=*/10);

  for (const auto c : folly::irange(10)) {
    filter.markInvalid({ObjectId(2)}, ContainerId(c));
  }

  for (const auto c : folly::irange(10)) {
    EXPECT_TRUE(filter.isMarkedInvalid(ObjectId(2), ContainerId(c)));
    EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(0), ContainerId(c)));
  }
}

TEST(InvalidMoveFilterTest, AllObjectsInvalidForOneContainer) {
  InvalidMoveFilter filter(/*numObjects=*/10, /*numContainers=*/3);

  std::vector<ObjectId> objectIds;
  objectIds.reserve(10);
  for (const auto objectId : folly::irange(10)) {
    objectIds.emplace_back(objectId);
  }
  filter.markInvalid(objectIds, ContainerId(1));

  for (const auto o : folly::irange(10)) {
    EXPECT_TRUE(filter.isMarkedInvalid(ObjectId(o), ContainerId(1)));
    EXPECT_FALSE(filter.isMarkedInvalid(ObjectId(o), ContainerId(0)));
  }
}

TEST(InvalidMoveFilterTest, DuplicateMarksAreIdempotent) {
  InvalidMoveFilter filter(/*numObjects=*/5, /*numContainers=*/5);

  filter.markInvalid({ObjectId(1), ObjectId(1)}, ContainerId(3));
  filter.markInvalid({ObjectId(1)}, ContainerId(3));

  EXPECT_TRUE(filter.isMarkedInvalid(ObjectId(1), ContainerId(3)));
}

TEST(InvalidMoveFilterTest, MergeFromUnionsBothFilters) {
  InvalidMoveFilter a(/*numObjects=*/5, /*numContainers=*/5);
  a.markInvalid({ObjectId(1)}, ContainerId(2));
  a.markInvalid({ObjectId(3)}, ContainerId(4)); // shared with b

  InvalidMoveFilter b(/*numObjects=*/5, /*numContainers=*/5);
  b.markInvalid({ObjectId(0)}, ContainerId(0));
  b.markInvalid({ObjectId(3)}, ContainerId(4)); // shared with a

  a.mergeFrom(b);

  // a's own pairs are kept.
  EXPECT_TRUE(a.isMarkedInvalid(ObjectId(1), ContainerId(2)));
  // b's pairs are added.
  EXPECT_TRUE(a.isMarkedInvalid(ObjectId(0), ContainerId(0)));
  // the shared pair stays invalid.
  EXPECT_TRUE(a.isMarkedInvalid(ObjectId(3), ContainerId(4)));
  // an unmarked pair stays valid.
  EXPECT_FALSE(a.isMarkedInvalid(ObjectId(2), ContainerId(2)));
}

TEST(InvalidMoveFilterTest, MergeFromEmptyIsNoOp) {
  InvalidMoveFilter a(/*numObjects=*/3, /*numContainers=*/3);
  a.markInvalid({ObjectId(1)}, ContainerId(1));

  const InvalidMoveFilter empty(/*numObjects=*/3, /*numContainers=*/3);
  a.mergeFrom(empty);

  EXPECT_TRUE(a.isMarkedInvalid(ObjectId(1), ContainerId(1)));
  EXPECT_FALSE(a.isMarkedInvalid(ObjectId(0), ContainerId(0)));
}

TEST(InvalidMoveFilterTest, MergeFromIntoEmptyFilterPopulatesIt) {
  InvalidMoveFilter populated(/*numObjects=*/5, /*numContainers=*/5);
  populated.markInvalid({ObjectId(2)}, ContainerId(1));

  InvalidMoveFilter dst(/*numObjects=*/5, /*numContainers=*/5);
  EXPECT_TRUE(dst.empty());

  dst.mergeFrom(populated);

  EXPECT_FALSE(dst.empty());
  EXPECT_TRUE(dst.isMarkedInvalid(ObjectId(2), ContainerId(1)));
}

TEST(InvalidMoveFilterTest, ConcurrentMarkingMarksEveryPair) {
  constexpr EntityIdType kNumObjects = 500;
  constexpr EntityIdType kNumContainers = 40;
  constexpr EntityIdType kNumThreads = 8;

  // Each thread owns a stride of objects but marks across every container, so
  // threads collide on the same rows and on the same 64-bit blocks.
  const auto objectsFor = [](EntityIdType thread) {
    std::vector<ObjectId> objects;
    for (EntityIdType o = thread; o < kNumObjects; o += kNumThreads) {
      objects.emplace_back(o);
    }
    return objects;
  };

  InvalidMoveFilter actual(kNumObjects, kNumContainers);
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (const auto t : folly::irange(kNumThreads)) {
    threads.emplace_back([&actual, objects = objectsFor(t)] {
      for (const auto c : folly::irange(kNumContainers)) {
        actual.markInvalid(objects, ContainerId(c));
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_FALSE(actual.empty());
  for (EntityIdType o = 0; o < kNumObjects; ++o) {
    for (EntityIdType c = 0; c < kNumContainers; ++c) {
      ASSERT_TRUE(actual.isMarkedInvalid(ObjectId(o), ContainerId(c)))
          << "object=" << o << " container=" << c;
    }
  }
}

TEST(InvalidMoveFilterTest, ConcurrentFirstTouchOfSameRowLosesNoBits) {
  // Every thread races to be the one that allocates the single container's row.
  constexpr EntityIdType kNumObjects = 2000;
  constexpr EntityIdType kNumThreads = 16;

  InvalidMoveFilter filter(kNumObjects, /*numContainers=*/1);
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (const auto t : folly::irange(kNumThreads)) {
    threads.emplace_back([&filter, t] {
      std::vector<ObjectId> objects;
      for (EntityIdType o = t; o < kNumObjects; o += kNumThreads) {
        objects.emplace_back(o);
      }
      filter.markInvalid(objects, ContainerId(0));
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  for (EntityIdType o = 0; o < kNumObjects; ++o) {
    ASSERT_TRUE(filter.isMarkedInvalid(ObjectId(o), ContainerId(0)))
        << "object=" << o;
  }
}

TEST(InvalidMoveFilterTest, ConcurrentBitSetsLoseNoBits) {
  constexpr EntityIdType kNumObjects = 2000;
  constexpr EntityIdType kNumThreads = 16;

  std::vector<algopt::DynamicBitSet> sources;
  sources.reserve(kNumThreads);
  for (const auto t : folly::irange(kNumThreads)) {
    auto& source = sources.emplace_back(kNumObjects);
    for (EntityIdType o = t; o < kNumObjects; o += kNumThreads) {
      source.set(o);
    }
  }

  InvalidMoveFilter filter(kNumObjects, /*numContainers=*/1);
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (const auto t : folly::irange(kNumThreads)) {
    threads.emplace_back([&filter, source = std::move(sources.at(t))]() {
      filter.markInvalid(source, ContainerId(0));
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  for (const auto o : folly::irange(kNumObjects)) {
    ASSERT_TRUE(filter.isMarkedInvalid(ObjectId(o), ContainerId(0)))
        << "object=" << o;
  }
}

TEST(InvalidMoveFilterTest, CopyingPreservesRowsWithoutSharingThem) {
  InvalidMoveFilter original(/*numObjects=*/8, /*numContainers=*/4);
  original.markInvalid({ObjectId(1)}, ContainerId(2));

  InvalidMoveFilter copy = original;
  copy.markInvalid({ObjectId(3)}, ContainerId(2));
  copy.markInvalid({ObjectId(4)}, ContainerId(0));

  EXPECT_TRUE(copy.isMarkedInvalid(ObjectId(1), ContainerId(2)));
  EXPECT_TRUE(copy.isMarkedInvalid(ObjectId(3), ContainerId(2)));
  EXPECT_TRUE(copy.isMarkedInvalid(ObjectId(4), ContainerId(0)));
  EXPECT_FALSE(original.isMarkedInvalid(ObjectId(3), ContainerId(2)));
  EXPECT_FALSE(original.isMarkedInvalid(ObjectId(4), ContainerId(0)));
}

TEST(InvalidMoveFilterTest, MovingPreservesRows) {
  InvalidMoveFilter original(/*numObjects=*/8, /*numContainers=*/4);
  original.markInvalid({ObjectId(1)}, ContainerId(2));

  const InvalidMoveFilter moved = std::move(original);

  EXPECT_FALSE(moved.empty());
  EXPECT_TRUE(moved.isMarkedInvalid(ObjectId(1), ContainerId(2)));
}

} // namespace facebook::rebalancer::tests
