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

#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/moves/MoveType.h"
#include "algopt/rebalancer/solver/utils/Problem.h"

namespace facebook::rebalancer {

constexpr folly::StringPiece kGreedyGroupToScopeItemMoveTypeName =
    "GREEDY_GROUP_TO_SCOPE_ITEM";

/*
GreedyGroupToScopeItemMoveType evaluates moving an entire group of objects from
the given partition (as specified in the field 'groupMovesPartition') to one of
its candidate destination scopeItems (resolved from 'destinationsToExplore').
Each object in the group is forced to move into a *unique* container in the
chosen scopeItem, so moves are only explored to scopeItems that have at least as
many containers as the group has objects.
*/
class GreedyGroupToScopeItemMoveType : public MoveType {
 public:
  std::string name() const override;

  GreedyGroupToScopeItemMoveType(
      const interface::LocalSearchSolverSpec& solverConfigs,
      const interface::GreedyGroupToScopeItemMoveTypeSpec& spec,
      const Problem& problem);

  MoveResult findBestMove(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      MoveStatsAggregator& stats,
      const SearchHints& hints,
      double timeLimit) override;

 private:
  MoveResult exploreMovingGroup(
      const MovesEvaluator& evaluator,
      const std::vector<entities::ObjectId>& groupObjectIdsInt,
      const ReferenceList<const std::vector<entities::ContainerId>>&
          destinations,
      MoveStatsAggregator& stats,
      double timeLimit) const;

  const std::string partitionName_;
  const int nSampleSetsToExplore_;
  const interface::DestinationsToExploreOptions destinationsToExplore_;
  // Hard-constraint components used to prune candidate moves before evaluating
  const std::vector<const Expression*> pruningConstraints_;
};
} // namespace facebook::rebalancer
