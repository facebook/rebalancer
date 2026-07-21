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

#include "algopt/rebalancer/solver/moves/SingleChainMoveType.h"

namespace facebook::rebalancer {

class Problem;

constexpr folly::StringPiece kSingleChainFastMoveTypeName = "SINGLE_CHAIN_FAST";

// For each possible pair of objects {o1, o2} (where o2 is an object in the hot
// container), perform 2 moves concurrently:
// 1. Move o1 to the source of o2.
// 2. Move o2 to each possible destination.
// It can be thought of as a chain of length 2, where one object moves to a
// container, and another object from that container moves to a different one.
// As soon as one of these moves improves the objective, we stop exploring.
// Unlike SINGLE_CHAIN, SINGLE_CHAIN_FAST doesn't explore moving every possible
// object from the hot container. It picks an arbitrary object from the hot
// container and evaluates all possible destinations, stopping as soon as an
// improvement is found; this means that we may end up not visiting all o2
// possible objects, nor all o1 objects for each o2. If none of such moves
// improve the objective, then the process is repeated with a different object
// from the hot container, until an improvement is found, or all objects are
// exhausted.
class SingleChainFastMoveType : public SingleChainMoveType {
 public:
  std::string name() const override;

  explicit SingleChainFastMoveType(
      const interface::LocalSearchSolverSpec& solverConfigs,
      const interface::SingleChainFastMoveTypeSpec& spec);

  MoveResult findBestMove(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      MoveStatsAggregator& stats,
      const SearchHints& hints,
      double timeLimit) override;
};

} // namespace facebook::rebalancer
