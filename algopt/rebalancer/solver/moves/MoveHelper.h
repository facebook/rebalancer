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

#include "algopt/rebalancer/solver/moves/MoveResult.h"
#include "algopt/rebalancer/solver/utils/ParallelExecutionSelector.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <folly/executors/ThreadPoolExecutor.h>
#include <folly/Random.h>

#include <string_view>

namespace facebook::rebalancer {

class MoveStatsAggregator;
class MovesEvaluator;
class Problem;

class MoveHelper {
 public:
  static MoveResult findBestMove(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      entities::ObjectId hotObject,
      const PackerSet<entities::ContainerId>& coldContainers,
      double timeout,
      MoveStatsAggregator& stats,
      ParallelExecutionSelector& executionSelector,
      std::string_view moveTypeName);

  template <class Input, class InputCollection>
  static MoveResult findBest(
      folly::ThreadPoolExecutor* executor,
      const InputCollection& inputs,
      const std::function<MoveResult(Input)>& evaluate,
      double timeout,
      ParallelExecutionSelector& executionSelector,
      std::string_view moveTypeName);

  // Returns true with probability sampleSize / setSize.
  template <typename RNG = folly::Random::DefaultGenerator>
  static inline bool sampleWithProb(int sampleSize, int setSize, RNG& rng);

 private:
  template <
      class Input,
      class InputCollection,
      class InitializeFn,
      class AggregateFn>
  static auto execute(
      folly::ThreadPoolExecutor* executor,
      const InputCollection& inputs,
      const std::function<MoveResult(Input)>& process,
      const InitializeFn& initialize,
      const AggregateFn& aggregate,
      double timeout,
      const ParallelExecutionSelector& executionSelector)
      -> std::invoke_result_t<InitializeFn>;
};
} // namespace facebook::rebalancer

#include "algopt/rebalancer/solver/moves/MoveHelper-inl.h"
