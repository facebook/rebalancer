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

#include "algopt/rebalancer/solver/utils/ParallelExecutionSelector.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>

namespace facebook::rebalancer::tests {

namespace {

using Strategy = ParallelExecutionSelector::Strategy;
using Observation = ParallelExecutionSelector::Observation;

constexpr std::array<Strategy, 4> kAbba = {
    Strategy::SlidingWindow,
    Strategy::Batching,
    Strategy::Batching,
    Strategy::SlidingWindow,
};

interface::ParallelExecutionConfig autoConfig() {
  interface::AutoExecutionConfig autoExecution;
  autoExecution.batching()->batchSize() = 64;
  autoExecution.batching()->maxConcurrency() = 7;
  interface::ParallelExecutionConfig config;
  config.set_autoExecution(std::move(autoExecution));
  return config;
}

// Feeds each strategy a fixed observation until AUTO locks, checking that the
// strategies alternate in ABBA order.
void explore(
    ParallelExecutionSelector& selector,
    const Observation& slidingWindow,
    const Observation& batching) {
  for (size_t sample = 0; selector.isExploring(); ++sample) {
    ASSERT_LT(sample, 100) << "AUTO never locked";
    ASSERT_EQ(selector.strategy(), kAbba.at(sample % kAbba.size()));
    selector.record(
        selector.strategy() == Strategy::Batching ? batching : slidingWindow,
        "test");
  }
}

} // namespace

TEST(ParallelExecutionSelectorTest, DefaultsToSlidingWindow) {
  const ParallelExecutionSelector selector(std::nullopt);

  EXPECT_EQ(selector.strategy(), Strategy::SlidingWindow);
  EXPECT_FALSE(selector.isExploring());
}

TEST(ParallelExecutionSelectorTest, PreservesExplicitBatchingOptions) {
  interface::BatchingExecutionConfig batchingConfig;
  batchingConfig.batchSize() = 64;
  batchingConfig.maxConcurrency() = 7;
  interface::ParallelExecutionConfig config;
  config.set_batching(std::move(batchingConfig));
  const ParallelExecutionSelector selector(config);

  EXPECT_EQ(selector.strategy(), Strategy::Batching);
  EXPECT_EQ(selector.batchingOptions().batchSize, 64);
  EXPECT_EQ(selector.batchingOptions().maxConcurrency, 7);
  EXPECT_FALSE(selector.isExploring());
}

TEST(ParallelExecutionSelectorTest, AutoLocksBatchingWhenItIsFaster) {
  ParallelExecutionSelector selector(autoConfig());

  explore(
      selector,
      {.evaluations = 1'000, .durationSecs = 1.0},
      {.evaluations = 2'000, .durationSecs = 1.0});

  EXPECT_EQ(selector.strategy(), Strategy::Batching);
  EXPECT_EQ(selector.batchingOptions().batchSize, 64);
  EXPECT_EQ(selector.batchingOptions().maxConcurrency, 7);
}

TEST(ParallelExecutionSelectorTest, AutoLocksSlidingWindowWhenItIsFaster) {
  ParallelExecutionSelector selector(autoConfig());

  explore(
      selector,
      {.evaluations = 2'000, .durationSecs = 1.0},
      {.evaluations = 1'000, .durationSecs = 1.0});

  EXPECT_EQ(selector.strategy(), Strategy::SlidingWindow);
}

TEST(ParallelExecutionSelectorTest, AutoKeepsSlidingWindowOnATie) {
  ParallelExecutionSelector selector(autoConfig());

  explore(
      selector,
      {.evaluations = 1'000, .durationSecs = 1.0},
      {.evaluations = 1'000, .durationSecs = 1.0});

  EXPECT_EQ(selector.strategy(), Strategy::SlidingWindow);
}

TEST(ParallelExecutionSelectorTest, AutoIgnoresObservationsAfterLocking) {
  ParallelExecutionSelector selector(autoConfig());
  explore(
      selector,
      {.evaluations = 1'000, .durationSecs = 1.0},
      {.evaluations = 2'000, .durationSecs = 1.0});

  selector.record({.evaluations = 1'000'000, .durationSecs = 1.0}, "test");

  EXPECT_EQ(selector.strategy(), Strategy::Batching);
  EXPECT_FALSE(selector.isExploring());
}

TEST(ParallelExecutionSelectorTest, AutoWeighsSamplesByDuration) {
  ParallelExecutionSelector selector(autoConfig());
  // Sliding window alternates a small fast call (2'000/s) with a large slow one
  // (500/s); batching always runs at 700/s. Averaging per-call rates would pick
  // sliding window, but total evaluations over total time picks batching.
  const std::array<Observation, 2> slidingWindowSamples = {
      Observation{.evaluations = 100, .durationSecs = 0.05},
      Observation{.evaluations = 5'000, .durationSecs = 10.0},
  };
  const Observation batchingSample{.evaluations = 700, .durationSecs = 1.0};

  size_t slidingWindowCalls = 0;
  for (size_t sample = 0; selector.isExploring(); ++sample) {
    ASSERT_LT(sample, 100) << "AUTO never locked";
    selector.record(
        selector.strategy() == Strategy::Batching
            ? batchingSample
            : slidingWindowSamples.at(
                  slidingWindowCalls++ % slidingWindowSamples.size()),
        "test");
  }

  EXPECT_EQ(selector.strategy(), Strategy::Batching);
}

TEST(ParallelExecutionSelectorTest, AutoIgnoresEmptySamples) {
  ParallelExecutionSelector selector(autoConfig());

  selector.record({.evaluations = 0, .durationSecs = 1.0}, "test");
  selector.record({.evaluations = 1'000, .durationSecs = 0}, "test");

  EXPECT_EQ(selector.strategy(), Strategy::SlidingWindow);
  EXPECT_TRUE(selector.isExploring());
  selector.record({.evaluations = 1'000, .durationSecs = 1.0}, "test");
  EXPECT_EQ(selector.strategy(), Strategy::Batching);
}

TEST(ParallelExecutionSelectorTest, AutoEmptySamplesNeverStarveAStrategy) {
  ParallelExecutionSelector selector(autoConfig());

  // An empty call before every batching call must not move the schedule, or
  // batching would lose its slots and AUTO would lock without measuring it.
  for (size_t sample = 0; selector.isExploring(); ++sample) {
    ASSERT_LT(sample, 100) << "AUTO never locked";
    const auto expected = kAbba.at(sample % kAbba.size());
    if (expected == Strategy::Batching) {
      selector.record({.evaluations = 0, .durationSecs = 1.0}, "test");
    }
    ASSERT_EQ(selector.strategy(), expected);
    selector.record(
        expected == Strategy::Batching
            ? Observation{.evaluations = 2'000, .durationSecs = 1.0}
            : Observation{.evaluations = 1'000, .durationSecs = 1.0},
        "test");
  }

  EXPECT_EQ(selector.strategy(), Strategy::Batching);
}

TEST(ParallelExecutionSelectorTest, RejectsEmptyConfig) {
  EXPECT_THROW(
      ParallelExecutionSelector{interface::ParallelExecutionConfig{}},
      std::invalid_argument);
}

} // namespace facebook::rebalancer::tests
