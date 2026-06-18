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

#pragma once

#include "algopt/rebalancer/treeprof/ExecutorWrapper.h"

#include <folly/coro/BlockingWait.h>
#include <folly/coro/Collect.h>
#include <folly/coro/Generator.h>
#include <folly/coro/Task.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/ThreadPoolExecutor.h>

#include <stdexcept>

namespace facebook::rebalancer {

inline std::optional<size_t> getNumThreads(const folly::Executor* e) {
  auto execToCast = e;
  // special handling when using algopt::treeprof::ExecutorWrapper; we need to
  // first cast to ExecutorWrapper and then to ThreadPoolExecutor using the
  // underlying executor
  if (auto wrapper =
          dynamic_cast<const algopt::treeprof::ExecutorWrapper*>(e)) {
    execToCast = wrapper->getUnwrapped();
  }
  const auto threadPoolExecutor =
      dynamic_cast<const folly::ThreadPoolExecutor*>(execToCast);
  return (threadPoolExecutor && threadPoolExecutor->numThreads() > 1)
      ? std::make_optional(threadPoolExecutor->numThreads())
      : std::nullopt;
}

// Returns the number of threads if an existing executor can be used, else
// returns std::nullopt
inline std::optional<size_t> canUseExistingExecutor(
    const folly::Executor* currentCoroExecutor,
    const folly::Executor* givenExecutor) {
  // If no executor is given, we launch tasks on the current coro executor if
  // present. Only executors with more than 1 thread are used to run tasks in
  // parallel to avoid unncessarily creating coroutine tasks and incur
  // associated overhead. Otherwise, tasks are run serially.
  if (givenExecutor) {
    return getNumThreads(givenExecutor);
  } else if (currentCoroExecutor) {
    return getNumThreads(currentCoroExecutor);
  } else {
    return std::nullopt;
  }
}

inline folly::Executor::KeepAlive<folly::Executor> getKeepAliveOfExistingExec(
    folly::Executor* currentCoroExecutor,
    folly::Executor* givenExecutor) {
  if (!givenExecutor && !currentCoroExecutor) [[unlikely]] {
    throw std::runtime_error(
        "Expected at least one of currentCoroExecutor and givenExecutor to be valid");
  }
  return givenExecutor ? folly::getKeepAliveToken(givenExecutor)
                       : folly::getKeepAliveToken(currentCoroExecutor);
}

class CoroUtils {
  using ExecutorPtr = std::shared_ptr<folly::Executor>;

 public:
  /**
   *runEachTaskAndUpdate(...) is used to run several coroutine tasks
   *(possibly) in parallel and apply an updateFunction() after each. It ensures
   *that the updateFunction() is performed only while holding a mutex.
   *'IndexType' argument is typically int/iterator that is used to iterate and
   *generate the coroutine functions.
   */
  template <typename IndexType, class CoroGenerateFunc, class UpdateFunc>
  static inline folly::coro::Task<void> runEachTaskAndUpdate(
      IndexType begin,
      IndexType end,
      const CoroGenerateFunc& coroGenerator,
      const UpdateFunc& updateFunc,
      ExecutorPtr givenExecutor = nullptr) {
    std::mutex updateMutex;
    auto eachCoroTask = [&](auto iter) {
      return waitOnTaskAndUpdateSynchronously(
          coroGenerator, iter, updateMutex, updateFunc);
    };
    co_await runEachCoroFuncOnExec(
        begin, end, eachCoroTask, std::move(givenExecutor));
  }

  /**
   *runEachFuncAndUpdate(...) is used to run several functions (possibly) in
   *parallel and apply an updateFunction() based on the result of each
   *function. 'IndexType' argument is typically int/iterator that is used to
   *iterate and generate the coroutine functions.

   *If a suitable executor does NOT exist, the functions are run serially
   *without incurring the overhead of creating coroutine tasks.

   *If a suitable executor exists, then appropriate coroutine tasks are created
   *that ensure the that the updateFunction() is performed only while holding a
   *mutex.
   */
  template <typename IndexType, class GenerateFunc, class UpdateFunc>
  static inline folly::coro::Task<void> runEachFuncAndUpdate(
      IndexType begin,
      IndexType end,
      const GenerateFunc& funcGenerator,
      const UpdateFunc& updateFunc,
      ExecutorPtr givenExecutor = nullptr) {
    const auto currentCoroExecutor = co_await folly::coro::co_current_executor;
    if (!canUseExistingExecutor(currentCoroExecutor, givenExecutor.get())) {
      for (auto iter = begin; iter != end; ++iter) {
        auto result = funcGenerator(iter);
        updateFunc(std::move(result), iter);
      }
      co_return;
    }

    // We first need to convert each func (from the given funcGenerator) to a
    // coroTask and then  use runEachTaskAndUpdate(...)
    auto funcToCoroGenerator =
        [&](auto iter) -> folly::coro::Task<decltype(funcGenerator(iter))> {
      co_return funcGenerator(iter);
    };
    co_await runEachTaskAndUpdate(
        begin, end, funcToCoroGenerator, updateFunc, std::move(givenExecutor));
  }

  /**
   *runEachTask(...) is used to run several functions (possibly) in parallel.
   *'IndexType' is typically int/iterator that is used to iterate and
   *generate the coroutine functions.
   */
  template <typename IndexType, class CoroGenerateFunc>
  static inline folly::coro::Task<void> runEachTask(
      IndexType begin,
      IndexType end,
      const CoroGenerateFunc& coroGenerator,
      ExecutorPtr givenExecutor = nullptr,
      std::optional<size_t> maxConcurrency = std::nullopt) {
    static_assert(
        std::is_same_v<
            std::invoke_result_t<decltype(coroGenerator), IndexType>,
            folly::coro::Task<void>>,
        "Expect coroFunc to take an 'IndexType' as argument and return folly::coro::Task<void>");

    co_await runEachCoroFuncOnExec(
        begin, end, coroGenerator, std::move(givenExecutor), maxConcurrency);
  }

  /**
   *runEachTaskBatched(...) is like runEachTask(...) but splits the integer
   *index range [begin, end) into one task per executor thread (each task
   *walks its slice serially) instead of one task per index. Use when the
   *range can dwarf the thread count and per-index work is small enough that
   *coroutine-creation overhead matters. Falls back to a serial inline loop
   *when no multi-thread executor is available.
   */
  template <typename IndexType, class CoroGenerateFunc>
  static inline folly::coro::Task<void> runEachTaskBatched(
      IndexType begin,
      IndexType end,
      const CoroGenerateFunc& coroGenerator,
      ExecutorPtr givenExecutor = nullptr) {
    static_assert(
        std::is_same_v<
            std::invoke_result_t<decltype(coroGenerator), IndexType>,
            folly::coro::Task<void>>,
        "Expect coroFunc to take an 'IndexType' as argument and return folly::coro::Task<void>");

    const size_t totalItems = static_cast<size_t>(end - begin);
    if (totalItems == 0) {
      co_return;
    }

    // Fallback to serial execution if no suitable executor is present
    const auto currentCoroExecutor = co_await folly::coro::co_current_executor;
    const auto numThreads =
        canUseExistingExecutor(currentCoroExecutor, givenExecutor.get());
    if (!numThreads) {
      for (IndexType i = begin; i < end; ++i) {
        co_await coroGenerator(i);
      }
      co_return;
    }

    const size_t numBatches = std::min(*numThreads, totalItems);
    const size_t minItemsPerBatch = totalItems / numBatches;
    const size_t nBatchesWithExtra = totalItems % numBatches;

    co_await runEachTask<size_t>(
        0,
        numBatches,
        [&](size_t batchIdx) -> folly::coro::Task<void> {
          const size_t itemsInThisBatch =
              minItemsPerBatch + (batchIdx < nBatchesWithExtra ? 1 : 0);
          const size_t startIdx = batchIdx * minItemsPerBatch +
              std::min(batchIdx, nBatchesWithExtra);
          const size_t endIdx = startIdx + itemsInThisBatch;
          for (size_t i = startIdx; i < endIdx; ++i) {
            co_await coroGenerator(static_cast<IndexType>(begin + i));
          }
        },
        std::move(givenExecutor));
  }

  /**
   *runEachFunc(...) is used to run several functions (possibly) in parallel.
   *'IndexType' is typically int/iterator that is used to iterate and generate
   *the coroutine functions.

   *If a suitable executor does NOT exist, the functions are run serially
   *without incurring the overhead of creating coroutine tasks.

   *If a suitable executor exists, then appropriate coroutine tasks are created
   *and executed on the executor.
   */
  template <typename IndexType, class GenerateFunc>
  static inline folly::coro::Task<void> runEachFunc(
      IndexType begin,
      IndexType end,
      const GenerateFunc& funcGenerator,
      ExecutorPtr givenExecutor = nullptr) {
    static_assert(
        std::is_same_v<
            std::invoke_result_t<decltype(funcGenerator), IndexType>,
            void>,
        "Expect coroFunc to take an 'IndexType' as argument and return void");

    const auto currentCoroExecutor = co_await folly::coro::co_current_executor;
    if (!canUseExistingExecutor(currentCoroExecutor, givenExecutor.get())) {
      for (auto iter = begin; iter != end; ++iter) {
        funcGenerator(iter);
      }
      co_return;
    }

    auto coroGenerator = [&](auto iter) -> folly::coro::Task<void> {
      funcGenerator(iter);
      co_return;
    };
    co_await runEachCoroFuncOnExec(begin, end, coroGenerator, givenExecutor);
  }

  /**
   * Processes Cartesian product of two containers in batches. Uses as many
   * batches as the number of threads in the executor. Within each batch a local
   * aggregation is done using the given function and then results are
   * accumulated with proper synchronization.
   *
   * Example with 10 objects × 20 containers (200 pairs) and 4 threads:
   * - Creates 4 batches of 50 pairs each
   * - Batches are processed in parallel, each batch accumulates its 50 pairs
   * internally
   * - Final accumulation combines 4 batch results (synchronized)
   *
   * @param container1 First container to iterate over
   * @param container2 Second container to iterate over
   * @param workFunc Function that processes each pair: (Item1, Item2) -> T
   * @param accumulateFunc Accumulation function: (T&, T&&) -> void
   * @param initResultFunc Function to create initial/empty result: () -> T
   * @param givenExecutor Thread pool executor (nullptr = serial execution)
   * @return Accumulated result of type T
   */
  template <
      typename Container1Type,
      typename Container2Type,
      typename WorkFunc,
      typename AccumulateFunc,
      typename InitResultFunc,
      typename ResultType = std::invoke_result_t<InitResultFunc>>
  static inline folly::coro::Task<ResultType> parallelMapReduceAllPairs(
      const Container1Type& container1,
      const Container2Type& container2,
      const WorkFunc& workFunc,
      const AccumulateFunc& accumulateFunc,
      const InitResultFunc& initResultFunc,
      ExecutorPtr givenExecutor = nullptr) {
    // Convert to vectors for size and random access
    using ValueType1 = std::decay_t<decltype(*container1.begin())>;
    using ValueType2 = std::decay_t<decltype(*container2.begin())>;

    const std::vector<ValueType1> vec1(container1.begin(), container1.end());
    const std::vector<ValueType2> vec2(container2.begin(), container2.end());

    const size_t totalPairs = vec1.size() * vec2.size();
    if (totalPairs == 0) {
      co_return initResultFunc();
    }

    // Fallback to serial execution if no suitable executor is present
    const auto currentCoroExecutor = co_await folly::coro::co_current_executor;
    const auto existsNumThreads =
        canUseExistingExecutor(currentCoroExecutor, givenExecutor.get());
    if (!existsNumThreads) {
      ResultType result = initResultFunc();
      for (const auto& item1 : vec1) {
        for (const auto& item2 : vec2) {
          auto temp = workFunc(item1, item2);
          accumulateFunc(result, std::move(temp));
        }
      }
      co_return result;
    }

    // Batch processing with parallel execution
    const size_t numBatches = std::min(*existsNumThreads, totalPairs);
    const size_t minPairsPerBatch = totalPairs / numBatches;
    const size_t nBatchesWithExtra = totalPairs % numBatches;

    ResultType finalResult = initResultFunc();
    co_await runEachFuncAndUpdate<size_t>(
        0,
        numBatches,
        [&](size_t batchIdx) -> ResultType {
          const size_t pairsInThisBatch =
              minPairsPerBatch + (batchIdx < nBatchesWithExtra ? 1 : 0);
          const size_t startPairIdx = batchIdx * minPairsPerBatch +
              std::min(batchIdx, nBatchesWithExtra);
          const size_t endPairIdx = startPairIdx + pairsInThisBatch;

          ResultType batchResult = initResultFunc();
          for (size_t pairIdx = startPairIdx; pairIdx < endPairIdx; ++pairIdx) {
            const size_t idx1 = pairIdx / vec2.size();
            const size_t idx2 = pairIdx % vec2.size();
            auto temp = workFunc(vec1[idx1], vec2[idx2]);
            accumulateFunc(batchResult, std::move(temp));
          }
          return batchResult;
        },
        [&](ResultType&& batchResult, size_t /*batchIdx*/) {
          accumulateFunc(finalResult, std::forward<ResultType>(batchResult));
        },
        std::move(givenExecutor));

    co_return finalResult;
  }

  /**
   * Processes container items in batches. Uses as many batches as the number of
   * threads in the executor. Within each batch a local aggregation is done
   * using the given function and then results are accumulated with proper
   * synchronization.
   *
   * Example with 600 entries and 100 threads:
   * - Creates 100 batches of 6 entries each
   * - Batches are processed in parallel, each batch accumulates its 6 entries
   * internally
   * - Final accumulation combines 100 batch results (synchronized)
   *
   * @param container Container to iterate over
   * @param workFunc Function that processes each element: (const_iterator) -> T
   * @param accumulateFunc Accumulation function: (T&, const T&) -> void
   * @param givenExecutor Thread pool executor (nullptr = serial execution)
   * @return Accumulated result of type T
   */
  template <
      typename ContainerType,
      typename WorkFunc,
      typename AccumulateFunc,
      typename ResultType = std::
          invoke_result_t<WorkFunc, typename ContainerType::const_iterator>>
  static inline folly::coro::Task<ResultType>
  runEachAndGetAccumulatedWithBatching(
      const ContainerType& container,
      const WorkFunc& workFunc,
      const AccumulateFunc& accumulateFunc,
      ExecutorPtr givenExecutor = nullptr) {
    using IterType = typename ContainerType::const_iterator;
    using IterCategory =
        typename std::iterator_traits<IterType>::iterator_category;
    constexpr bool isRandomAccess =
        std::is_same_v<IterCategory, std::random_access_iterator_tag>;

    const size_t totalItems = container.size();
    if (totalItems == 0) {
      co_return ResultType{};
    }

    // Fallback to serial execution if no suitable executor is present
    const auto currentCoroExecutor = co_await folly::coro::co_current_executor;
    const auto existsNumThreads =
        canUseExistingExecutor(currentCoroExecutor, givenExecutor.get());
    if (!existsNumThreads) {
      ResultType result{};
      const auto end = container.end();
      for (auto it = container.begin(); it != end; ++it) {
        accumulateFunc(result, workFunc(it));
      }
      co_return result;
    }

    const auto numBatches = std::min(*existsNumThreads, totalItems);
    const size_t minItemsPerBatch = totalItems / numBatches;
    const size_t nBatchesWithExtra = totalItems % numBatches;

    // Execute batches in parallel with synchronized accumulation
    ResultType finalResult{};
    co_await runEachFuncAndUpdate<size_t>(
        0,
        numBatches,
        [&, minItemsPerBatch, nBatchesWithExtra](
            size_t batchIdx) -> ResultType {
          // all batches have at least minItemsPerBatch; ones with index in [0,
          // nBatchesWithExtra) have 1 extra item
          const size_t itemsInThisBatch =
              minItemsPerBatch + (batchIdx < nBatchesWithExtra ? 1 : 0);
          const size_t startIdx = batchIdx * minItemsPerBatch +
              std::min(batchIdx, nBatchesWithExtra);
          const size_t endIdx = startIdx + itemsInThisBatch;
          const auto& containerBegin = container.begin();

          IterType it;
          if constexpr (isRandomAccess) {
            it = containerBegin + startIdx;
          } else {
            it = containerBegin;
            std::advance(it, startIdx);
          }

          // Process batch items and accumulate locally
          ResultType batchResult{};
          for (size_t i = startIdx; i < endIdx; ++i) {
            accumulateFunc(batchResult, workFunc(it));
            ++it;
          }
          return batchResult;
        },
        [&](const ResultType& batchResult, size_t /*batchIdx*/) {
          accumulateFunc(finalResult, batchResult);
        },
        std::move(givenExecutor));

    co_return finalResult;
  }

 private:
  template <class GenerateFunc, typename IndexType, class UpdateFunc>
  static inline folly::coro::Task<void> waitOnTaskAndUpdateSynchronously(
      const GenerateFunc& generate,
      IndexType iter,
      std::mutex& updateMutex,
      const UpdateFunc& updateFunc) {
    auto result = co_await generate(iter);

    {
      const std::scoped_lock<std::mutex> lockUpdates(updateMutex);
      updateFunc(std::move(result), iter);
    }

    co_return;
  }

  template <typename IndexType, class CoroGenerateFunc>
  static inline folly::coro::Task<void> runEachCoroFuncOnExec(
      IndexType begin,
      IndexType end,
      const CoroGenerateFunc& coroGenerator,
      ExecutorPtr givenExecutor = nullptr,
      std::optional<size_t> maxConcurrency = std::nullopt) {
    /* If an executor is NOT provided or if the executor on which the task was
     * launched is NOT of type folly::Executor, we run all the tasks serially */
    const auto currentCoroExecutor = co_await folly::coro::co_current_executor;
    const auto numThreads =
        canUseExistingExecutor(currentCoroExecutor, givenExecutor.get());
    if (!numThreads.has_value() ||
        (maxConcurrency && maxConcurrency.value() <= 1)) {
      for (auto iter = begin; iter != end; ++iter) {
        co_await coroGenerator(iter);
      }
      co_return;
    }

    const auto windowSize = maxConcurrency.value_or(*numThreads);
    const auto keepAliveExec =
        getKeepAliveOfExistingExec(currentCoroExecutor, givenExecutor.get());
    co_await folly::coro::collectAllWindowed(
        [&]() -> folly::coro::Generator<folly::coro::TaskWithExecutor<void>&&> {
          for (auto iter = begin; iter != end; ++iter) {
            co_yield co_withExecutor(keepAliveExec, coroGenerator(iter));
          }
        }(),
        windowSize);
  }
};

} // namespace facebook::rebalancer
