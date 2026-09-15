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

#include "algopt/rebalancer/algopt_common/DynamicBitSet.h"

#include <folly/container/irange.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace facebook::algopt::tests {

TEST(DynamicBitSetTest, ZeroSizedIsEmpty) {
  const DynamicBitSet bs(/*numBits=*/0);
  EXPECT_EQ(bs.size(), 0u);
  EXPECT_TRUE(bs.empty());
  EXPECT_EQ(bs.numBlocks(), 0u);
}

TEST(DynamicBitSetTest, ZeroInitiallyAllUnset) {
  const DynamicBitSet bs(200);
  EXPECT_EQ(bs.size(), 200u);
  EXPECT_FALSE(bs.empty());
  for (const auto i : folly::irange(200u)) {
    EXPECT_FALSE(bs.isSet(i)) << "i=" << i;
  }
}

TEST(DynamicBitSetTest, SetMakesBitTestable) {
  DynamicBitSet bs(256);
  bs.set(0);
  bs.set(63);
  bs.set(64); // first bit of second block
  bs.set(255);
  EXPECT_TRUE(bs.isSet(0));
  EXPECT_TRUE(bs.isSet(63));
  EXPECT_TRUE(bs.isSet(64));
  EXPECT_TRUE(bs.isSet(255));
  EXPECT_FALSE(bs.isSet(1));
  EXPECT_FALSE(bs.isSet(62));
  EXPECT_FALSE(bs.isSet(65));
  EXPECT_FALSE(bs.isSet(254));
}

TEST(DynamicBitSetTest, SetReturnsTrueOnFirstSetFalseOnRepeat) {
  DynamicBitSet bs(64);
  EXPECT_TRUE(bs.set(7));
  EXPECT_FALSE(bs.set(7));
  EXPECT_FALSE(bs.set(7));
  EXPECT_TRUE(bs.set(8));
}

TEST(DynamicBitSetTest, SetIsIdempotent) {
  DynamicBitSet bs(64);
  bs.set(7);
  bs.set(7);
  bs.set(7);
  EXPECT_TRUE(bs.isSet(7));
  std::size_t count = 0;
  bs.forEachSetBit([&](std::size_t) { ++count; });
  EXPECT_EQ(count, 1u);
}

TEST(DynamicBitSetTest, NumBlocksMatchesCeilDivision) {
  EXPECT_EQ(DynamicBitSet(0).numBlocks(), 0u);
  EXPECT_EQ(DynamicBitSet(1).numBlocks(), 1u);
  EXPECT_EQ(DynamicBitSet(63).numBlocks(), 1u);
  EXPECT_EQ(DynamicBitSet(64).numBlocks(), 1u);
  EXPECT_EQ(DynamicBitSet(65).numBlocks(), 2u);
  EXPECT_EQ(DynamicBitSet(128).numBlocks(), 2u);
  EXPECT_EQ(DynamicBitSet(129).numBlocks(), 3u);
}

TEST(DynamicBitSetTest, ForEachSetBitVisitsNothingWhenAllUnset) {
  const DynamicBitSet bs(500);
  std::size_t count = 0;
  bs.forEachSetBit([&](std::size_t) { ++count; });
  EXPECT_EQ(count, 0u);
}

TEST(DynamicBitSetTest, ForEachSetBitVisitsAllSetBitsInAscendingOrder) {
  DynamicBitSet bs(200);
  const std::vector<std::size_t> setBits{1, 5, 63, 64, 65, 128, 199};
  for (const auto i : setBits) {
    bs.set(i);
  }

  std::vector<std::size_t> visited;
  bs.forEachSetBit([&](std::size_t i) { visited.push_back(i); });
  EXPECT_EQ(visited, setBits);
}

TEST(DynamicBitSetTest, BlocksLayoutMatchesSetIndices) {
  DynamicBitSet bs(192);
  bs.set(0);
  bs.set(1);
  bs.set(63);
  bs.set(64);
  bs.set(127);
  bs.set(128);

  ASSERT_EQ(bs.numBlocks(), 3u);
  const auto blocks = bs.dataPtr();
  EXPECT_EQ(blocks[0], 0b11ull | (std::uint64_t{1} << 63));
  EXPECT_EQ(blocks[1], 0b1ull | (std::uint64_t{1} << 63));
  EXPECT_EQ(blocks[2], 0b1ull);
}

TEST(DynamicBitSetTest, MoveTransfersState) {
  DynamicBitSet bs(128);
  bs.set(5);
  bs.set(70);

  const DynamicBitSet moved(std::move(bs));
  EXPECT_EQ(moved.size(), 128u);
  EXPECT_TRUE(moved.isSet(5));
  EXPECT_TRUE(moved.isSet(70));
}

TEST(DynamicBitSetTest, MergeFromIsUnion) {
  DynamicBitSet a(200);
  a.set(1);
  a.set(64);
  a.set(199);

  DynamicBitSet b(200);
  b.set(64); // overlaps a
  b.set(65); // same block as 64, new bit
  b.set(150); // new block

  a.mergeFrom(b);

  for (const auto i : {1u, 64u, 65u, 150u, 199u}) {
    EXPECT_TRUE(a.isSet(i)) << "i=" << i;
  }
  EXPECT_FALSE(a.isSet(0));
  EXPECT_FALSE(a.isSet(63));
  EXPECT_FALSE(b.isSet(1));
  EXPECT_TRUE(b.isSet(64));
  EXPECT_TRUE(b.isSet(65));
  EXPECT_TRUE(b.isSet(150));
  EXPECT_FALSE(b.isSet(199));
}

TEST(DynamicBitSetTest, MergeFromEmptyIsNoOp) {
  DynamicBitSet a(64);
  a.set(3);
  const DynamicBitSet empty(64);

  a.mergeFrom(empty);

  EXPECT_TRUE(a.isSet(3));
}

TEST(DynamicBitSetTest, AtomicSetFromManyThreadsSetsEveryBit) {
  constexpr std::size_t kNumBits = 4096;
  constexpr std::size_t kNumThreads = 8;

  DynamicBitSet bitSet(kNumBits);

  // Interleave the strides so threads contend on the same 64-bit blocks.
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (const auto t : folly::irange(kNumThreads)) {
    threads.emplace_back([&bitSet, t] {
      for (std::size_t i = t; i < kNumBits; i += kNumThreads) {
        bitSet.atomicSet(i);
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  for (const auto i : folly::irange(kNumBits)) {
    ASSERT_TRUE(bitSet.isSet(i)) << "i=" << i;
  }
}

} // namespace facebook::algopt::tests
