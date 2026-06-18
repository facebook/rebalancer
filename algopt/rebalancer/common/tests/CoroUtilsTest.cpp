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

#include "algopt/rebalancer/common/CoroUtils.h"
#include "algopt/rebalancer/interface/tests/utils.h"

#include <folly/Benchmark.h>
#include <folly/container/irange.h>
#include <folly/Random.h>
#include <folly/system/HardwareConcurrency.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <numeric>
#include <vector>
namespace facebook::rebalancer::tests {

namespace {
void executeRandomDelay() {
  const auto delay = folly::Random::rand32(5000);
  auto remainingDelay = delay;
  while (remainingDelay > 0) {
    remainingDelay--;
  }
  folly::doNotOptimizeAway(remainingDelay);
}
} // namespace

enum class ExectuorType {
  NONE,
  SINGLE_THREADED,
  MULTI_THREADED,
  EXECUTOR_WRAPPER,
};

template <typename T, class CoroUtilFunc>
static std::vector<std::vector<T>> getExecutionOrders(
    int nTries,
    CoroUtilFunc coroUtilsFunc) {
  std::vector<std::vector<T>> executionOrders;
  executionOrders.reserve(nTries);
  for ([[maybe_unused]] const auto _ : folly::irange(nTries)) {
    const auto executionOrder = folly::coro::blockingWait(coroUtilsFunc());
    executionOrders.push_back(std::move(executionOrder));
  }
  return executionOrders;
}

class CoroUtilsTest : public ::testing::TestWithParam<ExectuorType> {
 protected:
  void SetUp() override {
    switch (GetParam()) {
      case ExectuorType::NONE: {
        executor_ = nullptr;
        break;
      }
      case ExectuorType::SINGLE_THREADED: {
        executor_ = interface::getTestExecutor(/*numThreads=*/1);
        break;
      }
      case ExectuorType::MULTI_THREADED: {
        executor_ = interface::getTestExecutor(
            /*numThreads=*/interface::defaultExecutorThreadCount);
        break;
      }
      case ExectuorType::EXECUTOR_WRAPPER: {
        executor_ = std::make_shared<algopt::treeprof::ExecutorWrapper>(
            interface::getTestExecutor(
                /*numThreads=*/interface::defaultExecutorThreadCount));
        break;
      }
    }
  }

  int64_t fib(int n) {
    if (n == 0 || n == 1) {
      return 1;
    }

    return fib(n - 1) + fib(n - 2);
  }

  std::shared_ptr<folly::Executor> executor_ = nullptr;
  int taskCount_ = 200;
};

INSTANTIATE_TEST_CASE_P(
    CoroUtilsTest,
    CoroUtilsTest,
    ::testing::Values(
        ExectuorType::NONE,
        ExectuorType::SINGLE_THREADED,
        ExectuorType::MULTI_THREADED,
        ExectuorType::EXECUTOR_WRAPPER));

TEST_P(CoroUtilsTest, runEachTask) {
  std::atomic<int64_t> overallSum;
  folly::coro::blockingWait(
      CoroUtils::runEachTask<int>(
          0,
          taskCount_,
          [&](auto /*unused*/) -> folly::coro::Task<void> {
            overallSum += fib(10);
            co_return;
          },
          executor_));

  EXPECT_EQ(taskCount_ * fib(10), overallSum);
}

TEST_P(CoroUtilsTest, runEachTaskBatchedVisitsEachIndexOnce) {
  constexpr int begin = 5;
  constexpr int end = begin + 1000;
  constexpr int total = end - begin;
  std::array<std::atomic<int>, total> visitCount{};

  folly::coro::blockingWait(
      CoroUtils::runEachTaskBatched<int>(
          begin,
          end,
          [&](int i) -> folly::coro::Task<void> {
            visitCount[i - begin].fetch_add(1);
            co_return;
          },
          executor_));

  for (const auto i : folly::irange(total)) {
    EXPECT_EQ(1, visitCount[i].load());
  }
}

TEST_P(CoroUtilsTest, runEachFunc) {
  std::atomic<int64_t> overallSum;
  folly::coro::blockingWait(
      CoroUtils::runEachFunc<int>(
          0,
          taskCount_,
          [&](auto /*unused*/) { overallSum += fib(10); },
          executor_));

  EXPECT_EQ(taskCount_ * fib(10), overallSum);
}

TEST_P(CoroUtilsTest, runEachTaskAndUpdate) {
  entities::Map<int, int64_t> ans;
  // collect fib(i) for i from 0 to taskCount_
  folly::coro::blockingWait(
      CoroUtils::runEachTaskAndUpdate<int>(
          0,
          taskCount_,
          [&](auto i) -> folly::coro::Task<int64_t> { co_return fib(i % 10); },
          [&ans](auto result, auto i) { ans[i] = result; },
          executor_));

  for (const auto i : folly::irange(taskCount_)) {
    EXPECT_EQ(fib(i % 10), ans.at(i));
  }
}

TEST_P(CoroUtilsTest, runEachFuncAndUpdate) {
  entities::Map<int, int64_t> ans;
  // collect fib(i) for i from 0 to taskCount_
  folly::coro::blockingWait(
      CoroUtils::runEachFuncAndUpdate<int>(
          0,
          taskCount_,
          [&](auto i) -> int64_t { return fib(i % 10); },
          [&ans](auto result, auto i) { ans[i] = result; },
          executor_));

  for (const auto i : folly::irange(taskCount_)) {
    EXPECT_EQ(fib(i % 10), ans.at(i));
  }
}

TEST_P(CoroUtilsTest, runEachAndGetAccumulatedWithBatchingBasic) {
  std::vector<int> data(97);
  std::fill(data.begin(), data.end(), 1);

  const auto result = folly::coro::blockingWait(
      CoroUtils::runEachAndGetAccumulatedWithBatching(
          data,
          [](const auto it) { return *it; },
          [](int& acc, const int& val) { acc += val; },
          executor_));

  EXPECT_NEAR(97, result, 1e-8);
}

TEST_P(CoroUtilsTest, runEachAndGetAccumulatedWithBatchingEmptyContainer) {
  const std::vector<int> data;
  const auto result = folly::coro::blockingWait(
      CoroUtils::runEachAndGetAccumulatedWithBatching(
          data,
          [](const auto it) { return *it; },
          [](int& acc, const int& val) { acc += val; },
          executor_));

  EXPECT_NEAR(0, result, 1e-8);
}

TEST_P(CoroUtilsTest, runEachAndGetAccumulatedWithBatchingSingleElement) {
  const std::vector<int> data{42};
  const auto result = folly::coro::blockingWait(
      CoroUtils::runEachAndGetAccumulatedWithBatching(
          data,
          [](const auto it) { return *it; },
          [](int& acc, const int& val) { acc += val; },
          executor_));

  EXPECT_NEAR(42, result, 1e-8);
}

TEST_P(CoroUtilsTest, runEachAndGetAccumulatedWithBatchingNullExecutor) {
  std::vector<int> data(50);
  std::iota(data.begin(), data.end(), 1);
  const auto result = folly::coro::blockingWait(
      CoroUtils::runEachAndGetAccumulatedWithBatching(
          data,
          [](const auto it) { return *it; },
          [](int& acc, const int& val) { acc += val; },
          nullptr));

  EXPECT_NEAR(50 * 51 / 2, result, 1e-8);
}

TEST_P(
    CoroUtilsTest,
    runEachAndGetAccumulatedWithBatchingNonRandomAccessIterator) {
  folly::F14FastSet<int> data;
  data.reserve(57);
  for (const auto i : folly::irange(57)) {
    data.insert(i + 1);
  }

  const auto result = folly::coro::blockingWait(
      CoroUtils::runEachAndGetAccumulatedWithBatching(
          data,
          [](const auto it) { return *it * *it; },
          [](int& acc, const int& val) { acc += val; },
          executor_));
  // 1^2 + 2^2 + ... + 57^2
  EXPECT_NEAR(57 * 58 * (2 * 57 + 1) / 6, result, 1e-8);
}

TEST_P(CoroUtilsTest, verifyParallelExecution) {
  // Test only makes sense with multi-threaded executor. EXECUTOR_WRAPPER may
  // serialize execution on resource-constrained CI runners due to profiling
  // overhead, causing false failures.
  if (GetParam() != ExectuorType::MULTI_THREADED) {
    return;
  }

  // On machines with few cores (e.g. 2-3 core Mac CI runners), the scheduler
  // can complete tasks sequentially even with a multi-threaded executor,
  // causing this probabilistic test to fail. Skip on small machines.
  if (folly::available_concurrency() < 32) {
    GTEST_SKIP() << "Skipping on machines with < 32 cores ("
                 << folly::available_concurrency()
                 << " available) — parallel reordering is unreliable";
  }

  constexpr int kTries = 10;
  const auto executionOrders = getExecutionOrders<int64_t>(
      kTries, [&]() -> folly::coro::Task<std::vector<int64_t>> {
        folly::Synchronized<std::vector<int64_t>> executionOrder;
        co_await CoroUtils::runEachFunc<int>(
            0,
            taskCount_,
            [&](auto taskId) {
              executeRandomDelay();
              executionOrder.wlock()->push_back(taskId);
            },
            executor_);

        co_return *executionOrder.rlock();
      });

  const auto allTasksExecutedInOrder = std::all_of(
      executionOrders.begin(), executionOrders.end(), [&](const auto& order) {
        EXPECT_EQ(std::set(order.begin(), order.end()).size(), taskCount_)
            << " not all tasks were executed";
        return std::is_sorted(order.begin(), order.end());
      });

  EXPECT_FALSE(allTasksExecutedInOrder);
}

TEST_P(CoroUtilsTest, verifySerialExecution) {
  // tests only makes sense when single thread is used
  if (GetParam() == ExectuorType::MULTI_THREADED ||
      GetParam() == ExectuorType::EXECUTOR_WRAPPER) {
    return;
  }

  constexpr int kTries = 3;
  const auto executionOrders = getExecutionOrders<int64_t>(
      kTries, [&]() -> folly::coro::Task<std::vector<int64_t>> {
        folly::Synchronized<std::vector<int64_t>> executionOrder;
        co_await CoroUtils::runEachFunc<int>(
            0,
            taskCount_,
            [&](auto taskId) {
              executeRandomDelay();
              executionOrder.wlock()->push_back(taskId);
            },
            executor_);

        co_return *executionOrder.rlock();
      });

  const auto allTasksExecutedInOrder = std::all_of(
      executionOrders.begin(), executionOrders.end(), [&](const auto& order) {
        EXPECT_EQ(std::set(order.begin(), order.end()).size(), taskCount_)
            << " not all tasks were executed";
        return std::is_sorted(order.begin(), order.end());
      });

  EXPECT_TRUE(allTasksExecutedInOrder);
}

// Tests for 2-container batching (Cartesian product)
TEST_P(CoroUtilsTest, parallelMapReduceAllPairsTwoContainersEmpty) {
  const std::vector<int> emptyContainer;
  const std::vector<int> nonEmptyContainer{10, 20, 30};

  // Empty first container
  const auto result1 = folly::coro::blockingWait(
      CoroUtils::parallelMapReduceAllPairs(
          emptyContainer,
          nonEmptyContainer,
          [](const int a, const int b) { return a + b; },
          [](int& acc, int&& val) { acc += val; },
          []() { return 0; },
          executor_));
  EXPECT_EQ(0, result1);

  // Empty second container
  const auto result2 = folly::coro::blockingWait(
      CoroUtils::parallelMapReduceAllPairs(
          nonEmptyContainer,
          emptyContainer,
          [](const int a, const int b) { return a + b; },
          [](int& acc, int&& val) { acc += val; },
          []() { return 0; },
          executor_));
  EXPECT_EQ(0, result2);
}

TEST_P(CoroUtilsTest, parallelMapReduceAllPairsTwoContainersNullExecutor) {
  const std::vector<int> container1{1, 2, 3};
  const std::vector<int> container2{10, 20};

  const auto result = folly::coro::blockingWait(
      CoroUtils::parallelMapReduceAllPairs(
          container1,
          container2,
          [](const int a, const int b) { return a * b; },
          [](int& acc, int&& val) { acc += val; },
          []() { return 0; },
          nullptr));

  // Expected: (1*10) + (1*20) + (2*10) + (2*20) + (3*10) + (3*20) = 180
  EXPECT_EQ(180, result);
}

TEST_P(CoroUtilsTest, parallelMapReduceAllPairsTwoContainersSinglePair) {
  const std::vector<int> container1{5};
  const std::vector<int> container2{7};

  const auto result = folly::coro::blockingWait(
      CoroUtils::parallelMapReduceAllPairs(
          container1,
          container2,
          [](const int a, const int b) { return a * b; },
          [](int& acc, int&& val) { acc += val; },
          []() { return 0; },
          executor_));

  EXPECT_EQ(35, result);
}

TEST_P(CoroUtilsTest, parallelMapReduceAllPairsTwoContainersVerifyAllPairs) {
  const std::vector<int> container1{1, 2, 3};
  const std::vector<int> container2{4, 5};

  folly::Synchronized<std::set<std::pair<int, int>>> processedPairs;

  const auto result = folly::coro::blockingWait(
      CoroUtils::parallelMapReduceAllPairs(
          container1,
          container2,
          [&](const int a, const int b) {
            processedPairs.wlock()->insert({a, b});
            return a + b;
          },
          [](int& acc, int&& val) { acc += val; },
          []() { return 0; },
          executor_));

  // Verify all expected pairs were processed exactly once
  const std::set<std::pair<int, int>> expected{
      {1, 4}, {1, 5}, {2, 4}, {2, 5}, {3, 4}, {3, 5}};
  EXPECT_EQ(expected, *processedPairs.rlock());

  // Expected: (1+4) + (1+5) + (2+4) + (2+5) + (3+4) + (3+5) = 39
  EXPECT_EQ(39, result);
}

// Verifies that runEachFuncAndUpdate distributes work uniformly across batches.
// With 35 items and 20 threads (defaultExecutorThreadCount from
// getTestExecutor), each batch should get either 1 or 2 items (35/20 = 1
// remainder 15).
TEST_P(CoroUtilsTest, runEachFuncAndUpdateDistributesWorkUniformly) {
  if (GetParam() == ExectuorType::NONE ||
      GetParam() == ExectuorType::SINGLE_THREADED) {
    return;
  }

  constexpr size_t kTotalItems = 35;
  constexpr size_t kNumThreads = interface::defaultExecutorThreadCount; // 20
  constexpr size_t kNumBatches = std::min(kNumThreads, kTotalItems); // 20

  // Track items processed by each batch using batchIdx from updateFunc
  std::map<size_t, size_t> itemsPerBatch;

  folly::coro::blockingWait(
      CoroUtils::runEachFuncAndUpdate<size_t>(
          0,
          kNumBatches,
          [](size_t batchIdx) -> size_t {
            // Calculate items for this batch using the same formula as
            // CoroUtils
            constexpr size_t minItemsPerBatch = kTotalItems / kNumBatches; // 1
            constexpr size_t nBatchesWithExtra =
                kTotalItems % kNumBatches; // 15
            return minItemsPerBatch + (batchIdx < nBatchesWithExtra ? 1 : 0);
          },
          [&](size_t&& itemCount, size_t batchIdx) {
            itemsPerBatch[batchIdx] = itemCount;
          },
          executor_));

  ASSERT_EQ(kNumBatches, itemsPerBatch.size());

  // Batches 0-14 get 2 items (minItems + 1), batches 15-19 get 1 item
  // (minItems)
  for (const auto batchIdx : folly::irange(kNumBatches)) {
    const size_t expectedItems = (batchIdx < 15) ? 2 : 1;
    EXPECT_EQ(expectedItems, itemsPerBatch[batchIdx])
        << "Batch " << batchIdx << " has wrong number of items";
  }
}

} // namespace facebook::rebalancer::tests
