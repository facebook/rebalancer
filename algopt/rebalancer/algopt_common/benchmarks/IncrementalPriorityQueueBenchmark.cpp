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

#include <folly/Benchmark.h>
#include <folly/container/irange.h>
#include <folly/init/Init.h>
#include <gtest/gtest.h>

#include <numeric>
#include <vector>

namespace facebook::algopt::benchmarks {

namespace {

std::vector<int> makeItems(const int numItems) {
  std::vector<int> items(numItems);
  std::iota(items.begin(), items.end(), 0);
  return items;
}

// Feed overlapping batches until the first item is no longer tied. Remove it,
// then feed more batches if the next item is tied.
template <typename Queue>
void runFeedUntilTopStandsAloneThenPop(
    const int numRounds,
    const int numItems) {
  constexpr int kBatchSize = 5;
  constexpr int kStep = 3;

  Queue queue;
  int nextRound = 0;
  const auto feed = [&]() {
    while (nextRound < numRounds && (queue.empty() || !queue.is_top_strict())) {
      std::vector<int> batch;
      batch.reserve(kBatchSize);
      for (const auto j : folly::irange(kBatchSize)) {
        batch.push_back((nextRound * kStep + j) % numItems);
      }
      queue.update(batch);
      ++nextRound;
    }
  };

  feed();
  while (!queue.empty()) {
    queue.remove(queue.top());
    feed();
  }
  EXPECT_EQ(numRounds, nextRound);
}

// Add all items together so they are tied, then remove them one at a time.
template <typename Queue>
void runDrainManyTiedItems(const int numItems) {
  Queue queue;
  queue.update(makeItems(numItems));
  while (!queue.empty()) {
    queue.remove(queue.top());
  }
}

// Add all items together so they are tied, then remove only the first item.
template <typename Queue>
void runPopOneFromManyTiedItems(const int numItems) {
  Queue queue;
  queue.update(makeItems(numItems));
  queue.remove(queue.top());
  EXPECT_EQ(numItems - 1, queue.size());
}

// Give the same items the same priority in every update.
template <typename Queue>
void runRepeatSameItems(const int numUpdates) {
  constexpr int kNumItems = 10;
  Queue queue;
  const auto items = makeItems(kNumItems);
  for ([[maybe_unused]] const auto _ : folly::irange(numUpdates)) {
    queue.update(items);
  }
  EXPECT_EQ(kNumItems, queue.size());
  EXPECT_FALSE(queue.is_top_strict());
  EXPECT_EQ(0, queue.top());
}

// Give every item the same long history, then time only the tie checks.
template <typename Queue>
FOLLY_NOINLINE bool checkTopIsStrict(const Queue& queue) {
  return queue.is_top_strict();
}

template <typename Queue>
void runCheckTopAfterManyUpdates() {
  constexpr int kNumItems = 300;
  constexpr int kNumUpdates = 1'000;
  constexpr int kNumChecks = 20'000;

  folly::BenchmarkSuspender suspender;
  Queue queue;
  const auto items = makeItems(kNumItems);
  for ([[maybe_unused]] const auto _ : folly::irange(kNumUpdates)) {
    queue.update(items);
  }
  suspender.dismiss();

  int tiedCount = 0;
  for ([[maybe_unused]] const auto _ : folly::irange(kNumChecks)) {
    const auto topIsStrict = checkTopIsStrict(queue);
    folly::doNotOptimizeAway(topIsStrict);
    if (!topIsStrict) {
      ++tiedCount;
    }
  }
  suspender.rehire();
  // Every item has the same list, so the top never stands alone.
  EXPECT_EQ(kNumChecks, tiedCount);
}

} // namespace

// Models the rebalancer loop: feed updates until one item can be chosen, then
// remove it and continue.
BENCHMARK(FeedUntilTopStandsAloneThenPop) {
  runFeedUntilTopStandsAloneThenPop<IncrementalPriorityQueue<int>>(
      /*numRounds=*/20'000, /*numItems=*/50'000);
}

// Measures the cost of building and completely draining one large tie.
BENCHMARK(DrainManyTiedItems) {
  runDrainManyTiedItems<IncrementalPriorityQueue<int>>(/*numItems=*/30'000);
}

// Measures the cost of building a large tie and removing its first item.
BENCHMARK(PopOneFromManyTiedItems) {
  runPopOneFromManyTiedItems<IncrementalPriorityQueue<int>>(
      /*numItems=*/30'000);
}

// Builds a shorter history by repeatedly updating the same items.
BENCHMARK(RepeatSameItems2kTimes) {
  runRepeatSameItems<IncrementalPriorityQueue<int>>(/*numUpdates=*/2'000);
}

// Builds a much longer history. Together, these two benchmarks show whether
// updates become slower as history grows.
BENCHMARK(RepeatSameItems20kTimes) {
  runRepeatSameItems<IncrementalPriorityQueue<int>>(/*numUpdates=*/20'000);
}

// Measures the cost of checking a tie after every item has a long history.
BENCHMARK(CheckTopAfterManyUpdates) {
  runCheckTopAfterManyUpdates<IncrementalPriorityQueue<int>>();
}

} // namespace facebook::algopt::benchmarks

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
