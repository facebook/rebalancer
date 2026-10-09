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

#include "algopt/rebalancer/solver/moves/SingleFastMoveType.h"

#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"

namespace facebook::rebalancer {

SingleFastMoveType::SingleFastMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::SingleFastMoveTypeSpec& spec)
    : MoveType(solverConfigs), spec_(spec) {}

std::string SingleFastMoveType::name() const {
  return kSingleFastMoveTypeName.str();
}

MoveResult SingleFastMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /*hints*/,
    double timeLimit) {
  auto& problem = evaluator.getProblem();
  const algopt::Timer timer(true);
  const auto& precision = problem.getUniverse().getPrecision();

  const ObjectDeduper dedupedObjs(
      &problem.getEquivalenceSets(), evaluator.getDynamicObjects(hotContainer));

  int objectsEvaluated = 0;
  auto bestMove = MoveResult::makeEmpty();
  for (auto hotObject : dedupedObjs) {
    if (problem.fixed_objects.contains(hotObject)) {
      continue;
    }

    if (timer.getSeconds() >= timeLimit) {
      stats.incrNumTimeouts(bestMove);
      return bestMove;
    }

    MoveResult result = MoveResult::makeEmpty();

    const auto remainingTime = timeLimit - timer.getSeconds();
    if (spec_.destinationsToExplore().has_value()) {
      result = MoveHelper::findBestMove(
          evaluator,
          hotContainer,
          hotObject,
          getDestinations(
              hotObject,
              hotContainer,
              problem,
              spec_.destinationsToExplore().value()),
          remainingTime,
          stats,
          parallelExecutionSelector(),
          name());
    } else {
      result = MoveHelper::findBestMove(
          evaluator,
          hotContainer,
          hotObject,
          problem.containers,
          remainingTime,
          stats,
          parallelExecutionSelector(),
          name());
    }
    bestMove.aggregate(std::move(result));

    // return the best move after evaluating min_hot_objects objects out of
    // the hot container if it improves the objective, or keep evaluating
    // objects until an improvement is found
    if (++objectsEvaluated >= *spec_.minHotObjects() &&
        bestMove.isBetter(precision)) {
      return bestMove;
    }
  }
  return bestMove;
}

PackerSet<entities::ContainerId> SingleFastMoveType::getDestinations(
    entities::ObjectId hotObject,
    entities::ContainerId hotContainer,
    Problem& problem,
    interface::DestinationsToExploreOptions& destinationsToExplore) {
  const auto constrainedDestinations = getDestinationsToExplore(
      destinationsToExplore, hotContainer, hotObject, problem);

  PackerSet<entities::ContainerId> allowedDestinations;
  for (const auto& containers : constrainedDestinations) {
    allowedDestinations.insert(
        containers.get().begin(), containers.get().end());
  }

  return allowedDestinations;
}

} // namespace facebook::rebalancer
