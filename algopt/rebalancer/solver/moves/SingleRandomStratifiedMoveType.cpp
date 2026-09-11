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

#include "algopt/rebalancer/solver/moves/SingleRandomStratifiedMoveType.h"

#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/moves/MoveTypeUtils.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"
#include "algopt/rebalancer/solver/utils/Util.h"

namespace facebook::rebalancer {
std::string SingleRandomStratifiedMoveType::name() const {
  return kSingleRandomStratifiedMoveTypeName.str();
}

SingleRandomStratifiedMoveType::SingleRandomStratifiedMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::SingleRandomStratifiedMoveTypeSpec& spec)
    : MoveType(solverConfigs), spec_(spec) {}

SingleRandomStratifiedMoveType::SingleRandomStratifiedMoveType(
    const interface::LocalSearchSolverSpec& configs)
    : MoveType(configs),
      spec_(configs.singleRandomStratifiedMoveTypeSpec().to_optional()) {}

MoveResult SingleRandomStratifiedMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /*hints*/,
    double timeLimit) {
  const algopt::Timer timer(true);
  auto& problem = evaluator.getProblem();
  const auto& precision = problem.getUniverse().getPrecision();

  const ObjectDeduper dedupedObjs(
      &problem.getEquivalenceSets(), evaluator.getDynamicObjects(hotContainer));

  const auto minObjectsToExplore =
      spec_ ? *spec_->minObjectsToExplore() : minHotObjects();

  int objectCount = 0;
  auto bestResult = MoveResult::makeEmpty();
  for (auto hotObject : dedupedObjs) {
    if (problem.fixed_objects.contains(hotObject)) {
      continue;
    }

    if (timer.getSeconds() >= timeLimit) {
      stats.incrNumTimeouts(bestResult);
      return bestResult;
    }

    auto result = MoveHelper::findBestMove(
        evaluator,
        hotContainer,
        hotObject,
        getSampledDestinationsSet(hotObject, hotContainer, problem),
        timeLimit - timer.getSeconds(),
        stats,
        getParallelExecutionConfig());
    bestResult.aggregate(std::move(result));

    // return early only if we have already explored at least
    // minObjectsToExplore and found an improvement
    if (++objectCount >= minObjectsToExplore &&
        bestResult.isBetter(precision)) {
      return bestResult;
    }
  }
  return bestResult;
}

PackerSet<entities::ContainerId>
SingleRandomStratifiedMoveType::getSampledDestinationsSet(
    entities::ObjectId hotObject,
    entities::ContainerId hotContainer,
    Problem& problem) {
  // TODO:: remove the notion of similarContainers and instead make
  // singleRandomSpec and DestinationsToExplore non-optional
  // If spec is given, then take the sampleSize and destinationsToExplore from
  // the spec, else use the global samplingSize and simlarContainers specified
  // in localSearchSolverSpec
  if (!spec_) {
    return problem.getSimilarContainers().stratifiedSample(
        stratifiedSampleSize(),
        getAcceptingContainersCheckFunc(problem, hotContainer));
  }

  auto sampleSize = getSampleSize(
      *spec_->stratifiedSampleSize(),
      problem.getUniverse().getEntityName(hotObject));

  auto constrainedDestinations = getDestinationsToExplore(
      *spec_->destinationsToExplore(), hotContainer, hotObject, problem);

  return getEqualSizeRandomSamplesFromEachSetIn(
      constrainedDestinations,
      sampleSize,
      rng_,
      std::make_optional(hotContainer) /*containerToExcludeFromSample*/);
}

} // namespace facebook::rebalancer
