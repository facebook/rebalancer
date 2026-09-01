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

#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/tests/IdConverterTestUtils.h"

#include <folly/container/irange.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <mutex>
#include <random>
#include <set>
#include <thread>

using namespace folly;
using namespace std;

namespace facebook::rebalancer::packer::tests {

TEST(MoveHelperTest, FindBest) {
  CPUThreadPoolExecutor executor(10);
  vector<int> inputs = {-2, -1, 0, 1, 2};
  std::shuffle(inputs.begin(), inputs.end(), mt19937(random_device()()));

  const std::function<MoveResult(int)> evaluate = [](int input) {
    MoveSet moves;
    moves.insert(Move(object(1), container(2), container(3)));
    return MoveResult::makeValid(
        std::move(moves),
        GlobalObjectiveValue({0}),
        GlobalObjectiveValue({0.1 * input}),
        {});
  };
  auto best =
      MoveHelper::findBest(&executor, inputs, evaluate, 10, std::nullopt);
  EXPECT_EQ(5, best.getEvalsCount());
  EXPECT_EQ(-0.2, best.getValue().get(0));
  ASSERT_EQ(1, best.getMoveSet().size());
  EXPECT_EQ(object(1), best.getMoveSet().begin()->getObject());
  EXPECT_EQ(container(2), best.getMoveSet().begin()->getSourceContainer());
  EXPECT_EQ(container(3), best.getMoveSet().begin()->getDestinationContainer());
}

TEST(MoveHelperTest, SampleWithProb) {
  auto rng = folly::Random::DefaultGenerator(1); // NOLINT: use fixed seed
  constexpr auto total = 100'000;

  const auto computeSuccessTrials = [&](int numTrials, int sampleSize) {
    int hits = 0;
    for (const auto _ : folly::irange(numTrials)) {
      hits += MoveHelper::sampleWithProb(sampleSize, total, rng);
    }
    return hits;
  };

  // prob = 1e4 / 1e5 = 0.1
  // expected number of hits = numTrials * prob
  {
    constexpr auto sampleSize = 10'000;
    constexpr auto numTrials = 50'000;
    constexpr auto expectedVal = numTrials * (sampleSize * 1.0 / total);
    const auto hits = computeSuccessTrials(numTrials, sampleSize);
    XLOG(INFO) << fmt::format(
        "Expected value: {}, actual value: {}", expectedVal, hits);
    // Expect error of no more than 10%, in practice it will be more like 1% but
    // take a pessimistic value to ensure that test is not flaky
    EXPECT_NEAR(expectedVal, hits, expectedVal * 0.1);
  }

  // test again with prob = 0.05
  {
    constexpr auto sampleSize = 5'000;
    constexpr auto numTrials = 50'000;
    constexpr auto expectedVal = numTrials * (sampleSize * 1.0 / total);
    const auto hits = computeSuccessTrials(numTrials, sampleSize);
    XLOG(INFO) << fmt::format(
        "Expected value: {}, actual value: {}", expectedVal, hits);
    EXPECT_NEAR(expectedVal, hits, expectedVal * 0.1);
  }
}

TEST(MoveHelperTest, FindBestWithBatchingStrategy) {
  CPUThreadPoolExecutor executor(10);
  const vector<int> inputs = {-2, -1, 0, 1, 2};

  const std::function<MoveResult(int)> evaluate = [](int input) {
    MoveSet moves;
    moves.insert(Move(object(1), container(2), container(3)));
    return MoveResult::makeValid(
        std::move(moves),
        GlobalObjectiveValue({0}),
        GlobalObjectiveValue({0.1 * input}),
        {});
  };

  interface::BatchingExecutionConfig batchingConfig;
  batchingConfig.batchSize() = 2;
  interface::ParallelExecutionConfig execSpec;
  execSpec.set_batching(std::move(batchingConfig));

  auto best = MoveHelper::findBest(&executor, inputs, evaluate, 10, execSpec);
  EXPECT_EQ(5, best.getEvalsCount());
  EXPECT_EQ(-0.2, best.getValue().get(0));
  ASSERT_EQ(1, best.getMoveSet().size());
  EXPECT_EQ(object(1), best.getMoveSet().begin()->getObject());
}

TEST(MoveHelperTest, MaxConcurrencyLimitsParallelEvaluations) {
  CPUThreadPoolExecutor executor(10);
  const vector<int> inputs = {-2, -1, 0, 1, 2};
  std::mutex mutex;
  std::set<std::thread::id> workerThreads;

  const std::function<MoveResult(int)> evaluate = [&](int /*input*/) {
    {
      const std::lock_guard lock(mutex);
      workerThreads.insert(std::this_thread::get_id());
    }
    return MoveResult::makeValid(
        MoveSet(), GlobalObjectiveValue({0}), GlobalObjectiveValue({0}));
  };

  interface::BatchingExecutionConfig batchingConfig;
  batchingConfig.batchSize() = 1;
  batchingConfig.maxConcurrency() = 1;
  interface::ParallelExecutionConfig execSpec;
  execSpec.set_batching(std::move(batchingConfig));

  const auto best =
      MoveHelper::findBest(&executor, inputs, evaluate, 10, execSpec);
  EXPECT_EQ(5, best.getEvalsCount());
  EXPECT_EQ(1, workerThreads.size());
}

TEST(MoveHelperTest, FindBestWithSlidingWindowStrategy) {
  CPUThreadPoolExecutor executor(10);
  const vector<int> inputs = {-2, -1, 0, 1, 2};

  const std::function<MoveResult(int)> evaluate = [](int input) {
    MoveSet moves;
    moves.insert(Move(object(1), container(2), container(3)));
    return MoveResult::makeValid(
        std::move(moves),
        GlobalObjectiveValue({0}),
        GlobalObjectiveValue({0.1 * input}),
        {});
  };

  interface::ParallelExecutionConfig execSpec;
  execSpec.set_slidingWindow(interface::SlidingWindowExecutionConfig{});

  auto best = MoveHelper::findBest(&executor, inputs, evaluate, 10, execSpec);
  EXPECT_EQ(5, best.getEvalsCount());
  EXPECT_EQ(-0.2, best.getValue().get(0));
  ASSERT_EQ(1, best.getMoveSet().size());
  EXPECT_EQ(object(1), best.getMoveSet().begin()->getObject());
}

TEST(MoveHelperTest, FindBestWithNulloptUsesDefault) {
  CPUThreadPoolExecutor executor(10);
  const vector<int> inputs = {-2, -1, 0, 1, 2};

  const std::function<MoveResult(int)> evaluate = [](int input) {
    MoveSet moves;
    moves.insert(Move(object(1), container(2), container(3)));
    return MoveResult::makeValid(
        std::move(moves),
        GlobalObjectiveValue({0}),
        GlobalObjectiveValue({0.1 * input}),
        {});
  };

  auto best =
      MoveHelper::findBest(&executor, inputs, evaluate, 10, std::nullopt);
  EXPECT_EQ(5, best.getEvalsCount());
  EXPECT_EQ(-0.2, best.getValue().get(0));
  ASSERT_EQ(1, best.getMoveSet().size());
  EXPECT_EQ(object(1), best.getMoveSet().begin()->getObject());
}

} // namespace facebook::rebalancer::packer::tests
