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

#include "algopt/rebalancer/solver/moves/AsyncDestinationsMoveType.h"

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/solver/iterators/Filter.h"
#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"

#include <fmt/core.h>

namespace facebook::rebalancer {

MoveResult AsyncDestinationsMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /*hints*/,
    double timeLimit) {
  const algopt::Timer timer(true);

  auto coldContainers = Filter(
      evaluator.getProblem().containers,
      [&evaluator, hotContainer](auto container) {
        return container != hotContainer &&
            !evaluator.getProblem().not_accepting_containers.contains(
                container);
      });

  const std::function<MoveResult(entities::ContainerId)> evaluate =
      [this, &evaluator, &stats, hotContainer](
          entities::ContainerId coldContainer) {
        return exploreFromAllDestinations(
            evaluator, hotContainer, coldContainer, stats);
      };

  const auto& problem = evaluator.getProblem();
  const auto& precision = problem.getUniverse().getPrecision();

  const auto bestResult = MoveHelper::findBest(
      problem.configs.threadPool.get(),
      coldContainers,
      evaluate,
      timeLimit,
      parallelExecutionSelector(),
      name());

  XLOG_EVERY_N(INFO, 100) << name() << " move evaluations per second "
                          << bestResult.getEvalsCount() / timer.getSeconds()
                          << " total " << bestResult.getEvalsCount() << " in "
                          << timer.getSeconds() << "s, "
                          << (bestResult.isBetter(precision)
                                  ? fmt::format(
                                        "found improvement {} from {}",
                                        bestResult.getValue().toString(),
                                        bestResult.getOldValue().toString())
                                  : "no improvement");
  return bestResult;
}
} // namespace facebook::rebalancer
