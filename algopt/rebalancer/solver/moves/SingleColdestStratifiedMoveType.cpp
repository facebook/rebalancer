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

#include "algopt/rebalancer/solver/moves/SingleColdestStratifiedMoveType.h"

#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/moves/MoveTypeUtils.h"
#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"

#include <vector>

namespace facebook::rebalancer {

std::string SingleColdestStratifiedMoveType::name() const {
  return kSingleColdestStratifiedMoveTypeName.str();
}

MoveResult SingleColdestStratifiedMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /*hints*/,
    double timeLimit) {
  const algopt::Timer timer(true);
  auto bestResult = MoveResult::makeEmpty();

  auto& problem = evaluator.getProblem();
  const auto& precision = problem.getUniverse().getPrecision();
  auto& similarContainers = problem.getSimilarContainers();

  const ObjectDeduper dedupedObjects(
      &problem.getEquivalenceSets(), evaluator.getDynamicObjects(hotContainer));

  int objectCount = 0;

  for (auto hotObject : dedupedObjects) {
    if (timer.getSeconds() >= timeLimit) {
      stats.incrNumTimeouts(bestResult);
      return bestResult;
    }
    if (problem.fixed_objects.contains(hotObject)) {
      continue;
    }

    auto sampleSize = stratifiedSampleSize();
    auto sampleSet = similarContainers.stratifiedOrderedSample(
        sampleSize,
        getAcceptingContainersCheckFunc(problem, hotContainer),
        includeEqualSizeRandomSample());

    auto result = MoveHelper::findBestMove(
        evaluator,
        hotContainer,
        hotObject,
        sampleSet,
        timeLimit - timer.getSeconds(),
        stats,
        parallelExecutionSelector(),
        name());

    bestResult.aggregate(std::move(result));

    // return early only if we have already explored at least min_hotObjects
    // and found an improvement
    if (++objectCount >= minHotObjects() && bestResult.isBetter(precision)) {
      return bestResult;
    }
  }

  return bestResult;
}

} // namespace facebook::rebalancer
