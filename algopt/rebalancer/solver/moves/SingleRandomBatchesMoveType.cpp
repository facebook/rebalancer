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

#include "algopt/rebalancer/solver/moves/SingleRandomBatchesMoveType.h"

#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"

#include <folly/Random.h>

#include <random>

using namespace std;

namespace facebook::rebalancer {

SingleRandomBatchesMoveType::SingleRandomBatchesMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::SingleRandomBatchesMoveTypeSpec& spec)
    : MoveType(solverConfigs), spec_(spec) {}

std::string SingleRandomBatchesMoveType::name() const {
  return kSingleRandomBatchesMoveTypeName.str();
}

MoveResult SingleRandomBatchesMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /*hints*/,
    double timeLimit) {
  auto bestResult = MoveResult::makeEmpty();

  const algopt::Timer timer(true);
  const auto& precision = evaluator.getProblem().getUniverse().getPrecision();

  const ObjectDeduper dedupedObjs(
      &evaluator.getProblem().getEquivalenceSets(),
      evaluator.getDynamicObjects(hotContainer));

  auto batchSize = *spec_.randomContainerBatchSize();
  for (auto hotObject : dedupedObjs) {
    if (timer.getSeconds() >= timeLimit) {
      stats.incrNumTimeouts(bestResult);
      return bestResult;
    }
    if (evaluator.getProblem().fixed_objects.contains(hotObject)) {
      continue;
    }
    vector<entities::ContainerId> containers(
        evaluator.getProblem().containers.begin(),
        evaluator.getProblem().containers.end());
    std::shuffle(
        containers.begin(), containers.end(), folly::ThreadLocalPRNG());
    for (size_t i = 0; i < containers.size(); i += batchSize) {
      PackerSet<entities::ContainerId> batch;
      for (size_t j = i; j < containers.size() && j < i + batchSize; ++j) {
        batch.insert(containers[j]);
      }
      auto result = MoveHelper::findBestMove(
          evaluator,
          hotContainer,
          hotObject,
          batch,
          timeLimit - timer.getSeconds(),
          stats,
          parallelExecutionSelector(),
          name());
      bestResult.aggregate(std::move(result));
      // return after the first result that improves the objective
      if (bestResult.isBetter(precision)) {
        return bestResult;
      }
    }
  }
  return bestResult;
}

} // namespace facebook::rebalancer
