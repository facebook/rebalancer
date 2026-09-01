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

#include "algopt/rebalancer/solver/iterators/Timeout.h"
#include "algopt/rebalancer/solver/utils/ParallelExecution.h"

#include <functional>
#include <stdexcept>

namespace facebook::rebalancer {

template <class Input, class InputCollection>
inline MoveResult MoveHelper::findBest(
    folly::ThreadPoolExecutor* const executor,
    const InputCollection& inputs,
    const std::function<MoveResult(Input)>& evaluate,
    const double timeout,
    const std::optional<interface::ParallelExecutionConfig>& execSpec) {
  return execute(
      executor,
      inputs,
      evaluate,
      []() noexcept { return MoveResult::makeEmpty(); },
      [](MoveResult& acc, MoveResult&& result) noexcept {
        acc.aggregate(std::move(result));
      },
      timeout,
      execSpec);
}

template <
    class Input,
    class InputCollection,
    class InitializeFn,
    class AggregateFn>
inline auto MoveHelper::execute(
    folly::ThreadPoolExecutor* const executor,
    const InputCollection& inputs,
    const std::function<MoveResult(Input)>& process,
    const InitializeFn& initialize,
    const AggregateFn& aggregate,
    const double timeout,
    const std::optional<interface::ParallelExecutionConfig>& execSpec)
    -> std::invoke_result_t<InitializeFn> {
  Timeout<InputCollection> timeoutInputs(inputs, timeout);
  timeoutInputs.start_timer();

  const auto executeBatch =
      [&](const interface::BatchingExecutionConfig& batchingConfig) {
        BatchingExecutionOptions options;
        if (*batchingConfig.batchSize() != 0) {
          options.batchSize = static_cast<size_t>(*batchingConfig.batchSize());
        }
        if (*batchingConfig.maxConcurrency() > 0) {
          options.maxConcurrency =
              static_cast<size_t>(*batchingConfig.maxConcurrency());
        }
        return executeParallelBatch(
            executor, timeoutInputs, process, initialize, aggregate, options);
      };
  const auto executeSlidingWindow = [&]() {
    const int windowSize = static_cast<int>(10 + executor->numThreads());
    return executeParallelWindow(
        executor, timeoutInputs, process, initialize, aggregate, windowSize);
  };

  if (execSpec) {
    switch (execSpec->getType()) {
      case interface::ParallelExecutionConfig::Type::batching: {
        return executeBatch(execSpec->get_batching());
      }
      case interface::ParallelExecutionConfig::Type::slidingWindow:
        return executeSlidingWindow();
      case interface::ParallelExecutionConfig::Type::__EMPTY__:
        throw std::invalid_argument(
            "Parallel execution config cannot be empty");
    }
  }
  return executeSlidingWindow();
}

template <typename RNG>
inline bool
MoveHelper::sampleWithProb(const int sampleSize, const int setSize, RNG& rng) {
  if (sampleSize > setSize) {
    return true;
  }
  const auto prob = static_cast<double>(sampleSize) / setSize;
  std::bernoulli_distribution dist(prob);
  return dist(rng);
}

} // namespace facebook::rebalancer
