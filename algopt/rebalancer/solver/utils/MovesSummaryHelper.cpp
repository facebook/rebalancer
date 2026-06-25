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

#include "algopt/rebalancer/solver/utils/MovesSummaryHelper.h"

#include <folly/container/irange.h>

namespace facebook::rebalancer {

/**
 * Creates a summary of moves performed during Local Search.
 *
 * This function transforms internal move results into a structured summary
 * format that can be used for reporting and analysis.
 *
 * @param p The problem instance containing object and container information
 * @param move_result The result of move operations containing the move set and
 * objective deltas
 * @param move_stats Statistics aggregator containing information about move
 * evaluations
 * @param stageId Optional identifier for the rebalancing stage
 * @param cycleId Optional local search cycle number (>= 1 when present)
 * @return interface::MovesSummary A structured summary of all moves, stats,
 * violations & objective deltas
 */
interface::MovesSummary MovesSummaryHelper::makeMovesSummary(
    const Problem& p,
    const MoveResult& move_result,
    const MoveStatsAggregator& move_stats,
    const std::optional<int> stageId,
    std::optional<int> cycleId) {
  interface::MovesSummary summary;

  // Add all the moves performed to the summary
  for (const auto& move : move_result.getMoveSet()) {
    auto single = interface::SingleMove{};
    *single.object() = p.objectName(move.getObject());
    *single.srcContainer() = p.containerName(move.getSourceContainer());
    *single.dstContainer() = p.containerName(move.getDestinationContainer());
    summary.moves()->push_back(std::move(single));
  }

  // Add statistics about move evaluations
  const auto& global_stats = move_stats.getGlobalStats();
  *summary.evalsCount() = global_stats.getTotalMoves();

  // Record counts of invalid moves by constraint type
  for (const auto& [constraint, count] : global_stats.getInvalidMoveReasons()) {
    summary.constraintInvldCnt()[constraint->name] = count;
  }

  // Process objective value changes if available
  if (move_result.hasObjectiveDeltas()) {
    const auto& objDeltas = move_result.getObjectiveDeltas();
    for (const auto pos : folly::irange(objDeltas.size())) {
      for (const auto& delta : objDeltas.at(pos)) {
        // Record any objective change. Do not use precision
        // comparisons. ObjectiveDeltas are in terms of user defined objectives
        // and it is possible that each user defined objective is below the
        // tolerance while their sum, which is based off which a move is taken,
        // is above the tolerance.
        if (delta.oldValue == delta.newValue) {
          continue;
        }

        // Record objective value changes
        auto objChangeSummary = interface::GlobalObjectiveValueChange{};
        objChangeSummary.newValue() = delta.newValue;
        objChangeSummary.change() = delta.oldValue - delta.newValue;
        objChangeSummary.tuplePos() = pos;
        summary.objectives()[delta.objective->name] =
            std::move(objChangeSummary);
      }
    }
  }

  // Set the stage ID if provided
  summary.stageId().from_optional(stageId);

  summary.cycleId().from_optional(cycleId);

  return summary;
}

} // namespace facebook::rebalancer
