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

#include "algopt/rebalancer/solver/utils/SearchHints.h"

#include "algopt/rebalancer/solver/moves/MoveSet.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/solver/utils/SimilarContainers.h"

#include <memory>
#include <utility>

namespace facebook::rebalancer {

SearchHints::SearchHints(SearchHintsConfig config)
    : config_(std::move(config)) {}

void SearchHints::reset(Problem& problem, const MovesEvaluator& movesEvaluator)
    const {
  if (config_.enable_coldest_stratified_containers) {
    auto& similarContainers = problem.getSimilarContainers();
    auto containerPotentials = movesEvaluator.computeContainerPotentials();
    similarContainers.setOrderedGroups(containerPotentials);
  }
}

void SearchHints::update(
    Problem& problem,
    const MovesEvaluator& movesEvaluator,
    const MoveResult& appliedMoveResult) const {
  if (config_.enable_coldest_stratified_containers) {
    auto& similarContainers = problem.getSimilarContainers();

    // for now we reset and recompute all the ContainerPotentials when more than
    // one move is applied simultaneously
    if (appliedMoveResult.getMoveSet().size() != 1) {
      return reset(problem, movesEvaluator);
    }

    auto changesInContainerPotentials =
        movesEvaluator.updateContainerPotentialsAfterMove(appliedMoveResult);
    similarContainers.updateOrderedGroups(changesInContainerPotentials);
  }
}
} // namespace facebook::rebalancer
