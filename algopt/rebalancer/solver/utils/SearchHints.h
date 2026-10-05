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

#include "algopt/rebalancer/solver/moves/MoveResult.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <folly/Optional.h>

#include <vector>

namespace facebook::rebalancer {

struct SearchHintsConfig {
  bool enable_coldest_stratified_containers = false;
  int stratified_sample_size = 0;
};

// SearchHints computes and stores information commonly needed during search
// by certain move types. Multiple move types, and multiple invocations of the
// same move type can then reuse this information, which only needs to be
// recomputed when the state changes (i.e. after an apply).
class SearchHints {
 public:
  explicit SearchHints(SearchHintsConfig config);

  void reset(Problem& problem, const MovesEvaluator& movesEvaluator) const;

  void update(
      Problem& problem,
      const MovesEvaluator& movesEvaluator,
      const MoveResult& appliedMoveResult) const;

 private:
  void computeContainerContributionsToGoal(
      Problem& problem,
      const MovesEvaluator& movesEvaluator);

  void updateContainerContributionsToGoal(
      Problem& problem,
      const MovesEvaluator& movesEvaluator,
      const MoveResult& appliedMoveResult);

  SearchHintsConfig config_;
};

} // namespace facebook::rebalancer
