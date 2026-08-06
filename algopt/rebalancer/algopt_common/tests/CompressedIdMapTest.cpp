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

#include "algopt/rebalancer/algopt_common/CompressedIdMap.h"
#include "algopt/rebalancer/entities/Identifiers.h"

#include <folly/Conv.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <random>
#include <vector>

namespace facebook::algopt::tests {

namespace entities = ::facebook::rebalancer::entities;

namespace {

using CompressedIdObjectMap = CompressedIdMap<entities::ObjectId, double>;

constexpr double kDefaultValue = 0.0;

inline entities::ObjectId id(std::size_t n) {
  return entities::ObjectId(n);
}

entities::Map<entities::ObjectId, double> makeRandomEntries(
    std::size_t totalSize,
    std::size_t nonDefaultCount,
    double defaultValue) {
  std::vector<std::size_t> indices(nonDefaultCount);
  std::mt19937_64 rng;
  const auto range = folly::irange(totalSize);
  std::sample(
      range.begin(), range.end(), indices.begin(), nonDefaultCount, rng);

  entities::Map<entities::ObjectId, double> entries;
  entries.reserve(
      folly::to<typename entities::Map<entities::ObjectId, double>::size_type>(
          nonDefaultCount));
  for (const auto i : folly::irange(nonDefaultCount)) {
    entries.emplace(
        id(indices[i]), defaultValue + 1.0 + static_cast<double>(i));
  }
  return entries;
}

template <typename MapT>
void verifyMatches(
    const MapT& compressedId,
    const entities::Map<entities::ObjectId, double>& reference,
    const std::size_t totalSize,
    const double defaultValue) {
  EXPECT_EQ(compressedId.getDefaultValue(), defaultValue);
  EXPECT_EQ(compressedId.totalSize(), totalSize);
  EXPECT_EQ(compressedId.nonDefaultSize(), reference.size());
  for (const auto i : folly::irange(totalSize)) {
    const auto key = id(i);
    const auto expected = folly::get_default(reference, key, defaultValue);
    EXPECT_EQ(compressedId.getValue(key), expected) << "key=" << i;
  }
}

} // namespace

TEST(CompressedIdMapTest, LayoutSelection) {
  // Default threshold (0.2): density >= 0.2 picks dense.
  struct DefaultThresholdCase {
    std::size_t totalSize;
    std::size_t hint;
    bool expectedDense;
  };
  for (const auto& [totalSize, hint, expectedDense] :
       std::vector<DefaultThresholdCase>{
           {.totalSize = 1000, .hint = 0, .expectedDense = false}, // empty hint
           {.totalSize = 0, .hint = 0, .expectedDense = false}, // zero-size
           {.totalSize = 1000, .hint = 100, .expectedDense = false}, // 10%
           {.totalSize = 1000, .hint = 200, .expectedDense = true}, // 20%
       }) {
    EXPECT_EQ(
        CompressedIdObjectMap(totalSize, kDefaultValue, hint).isDense(),
        expectedDense);
  }

  // Custom threshold of 0 forces dense; threshold > 1 forces sparse.
  using AlwaysDense = CompressedIdMap<entities::ObjectId, double, 0.0>;
  using AlwaysSparse = CompressedIdMap<entities::ObjectId, double, 1.5>;
  // totalSize=0 or expectedNonDefault=0 stays sparse even with AlwaysDense:
  // a zero-sized dense vector is degenerate and a zero-hint dense vector
  // wastes memory.
  EXPECT_FALSE(AlwaysDense(
                   /*totalSize=*/0, kDefaultValue, /*expectedNonDefaultSize=*/0)
                   .isDense());
  EXPECT_FALSE(
      AlwaysDense(
          /*totalSize=*/1000, kDefaultValue, /*expectedNonDefaultSize=*/0)
          .isDense());

  constexpr std::size_t kTotalSize = 100;
  const auto entries =
      makeRandomEntries(kTotalSize, /*nonDefaultCount=*/50, kDefaultValue);
  const AlwaysDense dense(entries, kDefaultValue, kTotalSize);
  const AlwaysSparse sparse(entries, kDefaultValue, kTotalSize);
  EXPECT_TRUE(dense.isDense());
  EXPECT_FALSE(sparse.isDense());
  verifyMatches(dense, entries, kTotalSize, kDefaultValue);
  verifyMatches(sparse, entries, kTotalSize, kDefaultValue);
}

TEST(CompressedIdMapTest, OptimisticHintLandsDenseEvenIfRealizedIsLower) {
  // Caller estimates 50% density (-> dense), but only emplaces 10 entries.
  // Layout was committed at construction, so the map is dense and the slots
  // for un-emplaced keys hold defaultValue.
  constexpr std::size_t kTotalSize = 100;
  constexpr std::size_t kEmplacedCount = 10;
  const auto valueAt = [](std::size_t i) {
    return static_cast<double>(i) + 1.0;
  };

  CompressedIdObjectMap map(
      /*totalSize=*/kTotalSize,
      /*defaultValue=*/kDefaultValue,
      /*expectedNonDefaultSize=*/50);
  for (const auto i : folly::irange(kEmplacedCount)) {
    map.emplace(id(i), valueAt(i));
  }
  EXPECT_TRUE(map.isDense());
  EXPECT_EQ(map.nonDefaultSize(), kEmplacedCount);
  for (const auto i : folly::irange(kTotalSize)) {
    const auto expected = i < kEmplacedCount ? valueAt(i) : kDefaultValue;
    EXPECT_EQ(map.getValue(id(i)), expected) << "key=" << i;
  }
}

TEST(CompressedIdMapTest, EmplaceEdgeCasesInBothLayouts) {
  // Re-emplacing an already-set key is a no-op (first write wins), and
  // emplacing the default value is also a no-op (slot stays default).
  constexpr double kDefault = 3.0;
  constexpr std::size_t kTotalSize = 100;
  for (const std::size_t hint :
       {/*sparse*/ std::size_t{5}, /*dense*/ kTotalSize}) {
    CompressedIdObjectMap map(
        /*totalSize=*/kTotalSize,
        /*defaultValue=*/kDefault,
        /*expectedNonDefaultSize=*/hint);
    map.emplace(id(0), kDefault); // default; skipped.
    map.emplace(id(1), 7.0);
    map.emplace(id(1), 8.0); // already set; ignored.
    map.emplace(id(3), 9.0);

    const entities::Map<entities::ObjectId, double> expected{
        {id(1), 7.0}, {id(3), 9.0}};
    verifyMatches(map, expected, kTotalSize, kDefault);
  }
}

TEST(CompressedIdMapDeathTest, OutOfRangeKeyAborts) {
  CompressedIdObjectMap map(
      /*totalSize=*/10, kDefaultValue, /*expectedNonDefaultSize=*/0);
  EXPECT_DEATH(
      map.emplace(id(99999), 1.0),
      "CompressedIdMap::emplace: key index .*99999.* exceeds totalSize .*10");
  EXPECT_DEATH(
      (void)map.getValue(id(99999)),
      "CompressedIdMap::getValue: key index .*99999.* exceeds totalSize .*10");
  entities::Map<entities::ObjectId, double> entries{{id(99999), 1.0}};
  EXPECT_DEATH(
      (CompressedIdObjectMap(std::move(entries), kDefaultValue, 10)),
      "CompressedIdMap: key index .*99999.* exceeds totalSize .*10");
}

TEST(CompressedIdMapTest, EmplaceVariadicForwardsValueConstruction) {
  CompressedIdMap<entities::ObjectId, std::pair<int, int>> map(
      /*totalSize=*/10,
      /*defaultValue=*/std::pair<int, int>{0, 0},
      /*expectedNonDefaultSize=*/5);
  map.emplace(id(1), 1, 2);
  EXPECT_EQ(map.nonDefaultSize(), 1u);
  EXPECT_EQ(map.getValue(id(1)), (std::pair<int, int>{1, 2}));
}

TEST(CompressedIdMapTest, CopyAndEquality) {
  CompressedIdObjectMap a(
      /*totalSize=*/5, kDefaultValue, /*expectedNonDefaultSize=*/0);
  a.emplace(id(1), 1.5);
  const CompressedIdObjectMap b = a;
  EXPECT_EQ(a, b);
  a.emplace(id(2), 2.5);
  EXPECT_NE(a, b);
}

TEST(CompressedIdMapTest, MoveSemanticsPreserveLayoutAndValues) {
  constexpr std::size_t kTotalSize = 200;
  const auto entries = makeRandomEntries(
      kTotalSize, /*nonDefaultCount=*/50, kDefaultValue); // 25%, dense.
  CompressedIdObjectMap original(entries, kDefaultValue, kTotalSize);
  ASSERT_TRUE(original.isDense());

  const CompressedIdObjectMap moved(std::move(original));
  EXPECT_TRUE(moved.isDense());
  verifyMatches(moved, entries, kTotalSize, kDefaultValue);
}

TEST(CompressedIdMapTest, ForEachNonDefaultVisitsEachEntryOnce) {
  // Empty case: callback is never invoked.
  const CompressedIdObjectMap empty(
      /*totalSize=*/0, kDefaultValue, /*expectedNonDefaultSize=*/0);
  std::size_t emptyCalls = 0;
  empty.forEachNonDefault([&](entities::ObjectId, double) { ++emptyCalls; });
  EXPECT_EQ(emptyCalls, 0u);

  constexpr std::size_t kTotalSize = 200;
  for (const std::size_t kNonDefault : {/*sparse*/ 50u, /*dense*/ 80u}) {
    const auto entries =
        makeRandomEntries(kTotalSize, kNonDefault, kDefaultValue);
    const CompressedIdObjectMap map(entries, kDefaultValue, kTotalSize);

    entities::Map<entities::ObjectId, double> visited;
    map.forEachNonDefault([&](entities::ObjectId key, double value) {
      visited.emplace(key, value);
    });
    EXPECT_EQ(visited, entries);
  }
}

TEST(CompressedIdMapTest, WorksWithRawIntegerKey) {
  // Template instantiates with plain integer keys (not just ObjectId).
  CompressedIdMap<int, int> map(
      /*totalSize=*/5, /*defaultValue=*/-1, /*expectedNonDefaultSize=*/2);
  map.emplace(2, 20);
  EXPECT_EQ(map.getValue(2), 20);
  EXPECT_EQ(map.getValue(0), -1);
}

TEST(CompressedIdMapTest, RangeCtorBuildsAndFiltersDefaults) {
  // Range ctor copies non-default entries from any iterable-of-pairs and
  // skips entries equal to the default.
  const entities::Map<entities::ObjectId, double> entries{
      {id(0), kDefaultValue}, // default; skipped.
      {id(1), 1.5},
      {id(2), kDefaultValue}, // default; skipped.
      {id(3), 3.5}};

  const CompressedIdObjectMap map(entries, kDefaultValue, /*totalSize=*/5);
  EXPECT_EQ(map.nonDefaultSize(), 2u);
  for (const auto& [key, value] : entries) {
    EXPECT_EQ(map.getValue(key), value);
  }
}

TEST(CompressedIdMapTest, RvalueMapCtorSelectsLayoutAfterFilteringDefaults) {
  const entities::Map<entities::ObjectId, double> expected{
      {id(1), 1.5}, {id(3), 3.5}};
  for (const auto& [totalSize, expectedDense] :
       std::vector<std::pair<std::size_t, bool>>{
           {/*sparse after filtering*/ 20, false},
           {/*dense after filtering*/ 5, true}}) {
    entities::Map<entities::ObjectId, double> entries{
        {id(0), kDefaultValue},
        {id(1), 1.5},
        {id(2), kDefaultValue},
        {id(3), 3.5}};
    const CompressedIdObjectMap map(
        std::move(entries), kDefaultValue, totalSize);
    EXPECT_EQ(map.isDense(), expectedDense);
    verifyMatches(map, expected, totalSize, kDefaultValue);
  }
}

} // namespace facebook::algopt::tests
