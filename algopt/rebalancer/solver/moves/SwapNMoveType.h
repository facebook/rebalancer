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

#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/moves/MoveType.h"

namespace facebook::rebalancer {

constexpr folly::StringPiece kSwapNMoveTypeName = "SWAP_N";

class SwapNMoveType : public MoveType {
 public:
  SwapNMoveType(
      const interface::LocalSearchSolverSpec& solverConfigs,
      const interface::SwapNMoveTypeSpec& spec,
      const Problem& problem);

  std::string name() const override;

  MoveResult findBestMove(
      const MovesEvaluator& evaluator,
      entities::ContainerId hot_container,
      MoveStatsAggregator& stats,
      const SearchHints& hints,
      double timeLimit) override;

  MoveResult findBestMove(
      const MovesEvaluator& evaluator,
      const std::vector<entities::ObjectId>& objectIdsToMove,
      const std::vector<std::vector<entities::ContainerId>>&
          destinationScopeIds,
      MoveStatsAggregator& stats,
      double timeLimit);

 private:
  static PackerSet<int> pickRandom(int n, int k);
  interface::SwapNMoveTypeSpec spec_;
  // Source objects resolved from spec names at construction
  const PackerSet<entities::ObjectId> sourceObjectIds_;
};

} // namespace facebook::rebalancer
