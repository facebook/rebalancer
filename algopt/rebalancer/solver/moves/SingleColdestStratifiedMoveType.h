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

#include "algopt/rebalancer/solver/moves/MoveType.h"

namespace facebook::rebalancer {

class Problem;

constexpr folly::StringPiece kSingleColdestStratifiedMoveTypeName =
    "SINGLE_COLDEST_STRATIFIED";

// SingleColdestStratifiedMoveType evaluates single moves to a subset of
// destination containers, the coldest drawn in equal numbers from each
// similarity class.
// A container is deemed "coldest" if its containerPotential is the least, where
// containerPotential is defined using the contribution of the container to the
// goal values and the number of objects it has.

// NOTE: This moveType will work best when the goal expr is such that
// removing a container (along with the objects it has) does not result in
// making the goal value(s) worse. (An example where the goal value becomes
// worse when removing a container is when using the BalanceSpec with the
// "linear" formula definition.)
class SingleColdestStratifiedMoveType : public MoveType {
 public:
  std::string name() const override;

  explicit SingleColdestStratifiedMoveType(
      const interface::LocalSearchSolverSpec& configs)
      : MoveType(configs) {}
  MoveResult findBestMove(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      MoveStatsAggregator& stats,
      const SearchHints& hints,
      double timeLimit) override;
};
} // namespace facebook::rebalancer
