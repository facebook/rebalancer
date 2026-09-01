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

#include "algopt/rebalancer/solver/utils/ParallelExecution.h"

#include <folly/container/Reserve.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <gtest/gtest.h>

#include <chrono>
#include <mutex>
#include <numeric>
#include <set>
#include <thread>

using namespace facebook::rebalancer;
using namespace folly;

enum class ExecutionMode { SlidingWindow, Batching };

// Shared aggregate functor for collecting results into a vector
template <typename T>
struct VectorAggregate {
  void operator()(std::vector<T>& acc, T&& output) const {
    acc.push_back(std::move(output));
  }
  void operator()(std::vector<T>& acc, std::vector<T>&& other) const {
    folly::grow_capacity_by(acc, other.size());
    acc.insert(
        acc.end(),
        std::make_move_iterator(other.begin()),
        std::make_move_iterator(other.end()));
  }
};

// Helper to run either API with the same parameters
template <
    class Collection,
    class ProcessFn,
    class InitializeFn,
    class AggregateFn>
auto runExecution(
    const ExecutionMode mode,
    folly::CPUThreadPoolExecutor* const executor,
    const Collection& collection,
    const ProcessFn& process,
    const InitializeFn& initialize,
    const AggregateFn& aggregate,
    const int window_size = 2,
    const BatchingExecutionOptions& config = {})
    -> std::invoke_result_t<InitializeFn> {
  switch (mode) {
    case ExecutionMode::SlidingWindow:
      return executeParallelWindow(
          executor, collection, process, initialize, aggregate, window_size);
    case ExecutionMode::Batching:
      return executeParallelBatch(
          executor, collection, process, initialize, aggregate, config);
    default:
      throw std::runtime_error("Unknown ExecutionMode!");
  }
}

class ExecutionTest : public ::testing::TestWithParam<ExecutionMode> {};

INSTANTIATE_TEST_SUITE_P(
    AllModes,
    ExecutionTest,
    ::testing::Values(ExecutionMode::SlidingWindow, ExecutionMode::Batching),
    [](const ::testing::TestParamInfo<ExecutionMode>& info) {
      return info.param == ExecutionMode::SlidingWindow ? "SlidingWindow"
                                                        : "Batching";
    });

TEST_P(ExecutionTest, Basic) {
  CPUThreadPoolExecutor executor(2);
  const std::vector<std::pair<int, int>> inputs = {
      {10, 10}, {3, 4}, {6, 4}, {3, 7}};
  const std::function<int(std::pair<int, int>)> process =
      [](std::pair<int, int> input) { return input.first * input.second; };
  auto initialize = []() { return std::vector<int>{}; };
  auto outputs = runExecution(
      GetParam(),
      &executor,
      inputs,
      process,
      initialize,
      VectorAggregate<int>{});
  std::sort(outputs.begin(), outputs.end());
  const std::vector<int> expected = {12, 21, 24, 100};
  EXPECT_EQ(expected, outputs);
}

TEST_P(ExecutionTest, ProcessException) {
  CPUThreadPoolExecutor executor(2);
  const std::vector<int> inputs = {1, 2, 3, 4, 5};
  const std::function<int(int)> process = [](int /*input*/) -> int {
    throw std::runtime_error("exception processing input");
  };
  auto initialize = []() { return std::vector<int>{}; };
  EXPECT_THROW(
      runExecution(
          GetParam(),
          &executor,
          inputs,
          process,
          initialize,
          VectorAggregate<int>{}),
      std::runtime_error);
}

TEST_P(ExecutionTest, AggregateException) {
  CPUThreadPoolExecutor executor(2);
  const std::vector<int> inputs = {1, 2, 3, 4, 5};
  const std::function<int(int)> process = [](int /*input*/) { return 42; };
  auto initialize = []() { return std::vector<int>{}; };
  struct Aggregate {
    [[noreturn]] void operator()(std::vector<int>& /*acc*/, int&& /*output*/)
        const {
      throw std::runtime_error("exception processing output");
    }
    void operator()(std::vector<int>& /*acc*/, std::vector<int>&& /*other*/)
        const {}
  };
  EXPECT_THROW(
      runExecution(
          GetParam(), &executor, inputs, process, initialize, Aggregate{}),
      std::runtime_error);
}

TEST_P(ExecutionTest, EmptyInput) {
  CPUThreadPoolExecutor executor(2);
  const std::vector<int> inputs = {};
  const std::function<int(int)> process = [](int x) { return x; };
  auto initialize = []() { return std::vector<int>{}; };
  auto outputs = runExecution(
      GetParam(),
      &executor,
      inputs,
      process,
      initialize,
      VectorAggregate<int>{});
  EXPECT_TRUE(outputs.empty());
}

TEST_P(ExecutionTest, LargeInput) {
  CPUThreadPoolExecutor executor(4);
  std::vector<int> inputs(1000);
  std::iota(inputs.begin(), inputs.end(), 1); // 1, 2, ..., 1000
  const std::function<int(int)> process = [](int x) { return x; };
  auto initialize = []() { return std::vector<int>{}; };
  auto outputs = runExecution(
      GetParam(),
      &executor,
      inputs,
      process,
      initialize,
      VectorAggregate<int>{});
  EXPECT_EQ(500500, std::accumulate(outputs.begin(), outputs.end(), 0));
}

// QueueFull test is specific to executeParallelWindow API - uses specific
// queue config
TEST(ExecuteParallelWindowTest, QueueFull) {
  CPUThreadPoolExecutor executor(
      1,
      std::make_unique<LifoSemMPMCQueue<CPUThreadPoolExecutor::CPUTask>>(2),
      std::make_shared<NamedThreadFactory>("CPUThreadPool"));
  const std::vector<int> inputs = {1, 2, 3, 4, 5, 6, 7, 8};
  // The process function must block the executor thread long enough that the
  // queue fills before any task completes. Submissions happen on the main
  // thread in microseconds; a 1-second sleep gives a ~1,000,000x margin.
  const std::function<int(int)> process = [](int /*input*/) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return 42;
  };
  auto initialize = []() { return 0; };
  auto aggregate = [](int& /*acc*/, int&& /*output*/) {};
  EXPECT_THROW(
      executeParallelWindow(
          &executor, inputs, process, initialize, aggregate, 4),
      std::exception);
}

TEST(ExecuteParallelBatchTest, CustomBatchSize) {
  CPUThreadPoolExecutor executor(4);
  std::vector<int> inputs(500);
  std::iota(inputs.begin(), inputs.end(), 1);

  const std::function<int(int)> process = [](int x) { return x * 2; };
  auto initialize = []() { return std::vector<int>{}; };

  // Test with small batch size
  const BatchingExecutionOptions smallBatch{.batchSize = 16};
  auto result1 = executeParallelBatch(
      &executor,
      inputs,
      process,
      initialize,
      VectorAggregate<int>{},
      smallBatch);
  EXPECT_EQ(result1.size(), 500);

  // Test with large batch size
  const BatchingExecutionOptions largeBatch{.batchSize = 512};
  auto result2 = executeParallelBatch(
      &executor,
      inputs,
      process,
      initialize,
      VectorAggregate<int>{},
      largeBatch);
  EXPECT_EQ(result2.size(), 500);

  // Both should produce same sum
  constexpr int expectedSum = 250500; // 2*(1+2+...+500) = 2*125250 = 250500
  EXPECT_EQ(std::accumulate(result1.begin(), result1.end(), 0), expectedSum);
  EXPECT_EQ(std::accumulate(result2.begin(), result2.end(), 0), expectedSum);
}

TEST(ExecuteParallelBatchTest, DefaultConfig) {
  CPUThreadPoolExecutor executor(2);
  const std::vector<int> inputs = {1, 2, 3, 4, 5};
  const std::function<int(int)> process = [](int x) { return x; };
  const auto initialize = []() { return 0; };
  const auto aggregate = [](int& acc, int&& val) { acc += val; };

  auto result =
      executeParallelBatch(&executor, inputs, process, initialize, aggregate);
  EXPECT_EQ(result, 15);
}

TEST(ExecuteParallelBatchTest, MaxConcurrencyLimitsConsumers) {
  CPUThreadPoolExecutor executor(4);
  std::vector<int> inputs(100);
  std::iota(inputs.begin(), inputs.end(), 1);
  std::mutex mutex;
  std::set<std::thread::id> workerThreads;
  const std::function<int(int)> process = [&](int input) {
    const std::lock_guard lock(mutex);
    workerThreads.insert(std::this_thread::get_id());
    return input;
  };
  const auto initialize = []() { return 0; };
  const auto aggregate = [](int& acc, int&& val) { acc += val; };
  const BatchingExecutionOptions config{.batchSize = 1, .maxConcurrency = 1};

  const auto result = executeParallelBatch(
      &executor, inputs, process, initialize, aggregate, config);

  EXPECT_EQ(result, 5050);
  EXPECT_EQ(workerThreads.size(), 1);
}

TEST(ExecuteParallelBatchTest, RejectsZeroMaxConcurrency) {
  CPUThreadPoolExecutor executor(2);
  const std::vector<int> inputs = {1};
  const std::function<int(int)> process = [](int input) { return input; };
  const auto initialize = []() { return 0; };
  const auto aggregate = [](int& acc, int&& val) { acc += val; };
  const BatchingExecutionOptions config{.maxConcurrency = 0};

  EXPECT_THROW(
      executeParallelBatch(
          &executor, inputs, process, initialize, aggregate, config),
      std::invalid_argument);
}
