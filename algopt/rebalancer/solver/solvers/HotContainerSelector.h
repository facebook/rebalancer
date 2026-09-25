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
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/solver/utils/Util.h"

namespace facebook::rebalancer {

class HotContainerSelector {
 public:
  explicit HotContainerSelector(
      Problem& problem,
      uint64_t randomSeed,
      bool enableObjectPotentialSorting_,
      bool exploreMovesFromContainersNotInObjective_,
      const GlobalObjective::View& objectiveView,
      const interface::HottestTraversalConfig& traversalConfig);

  std::optional<entities::ContainerId> next(
      const PackerSet<entities::ContainerId>& skipContainers = {});
  void reset();
  double getFindTime() const;

 private:
  Problem& problem_;
  uint64_t randomSeed_;
  bool enableObjectPotentialSorting_;
  bool exploreMovesFromContainersNotInObjective_;
  GlobalObjective::View objectiveView_;
  interface::HottestTraversalConfig traversalConfig_;
  algopt::Timer findTimer_;
  std::optional<AbstractIterator<entities::ContainerId>> containersIterator_;
  std::optional<AbstractIterator<entities::ContainerId>> containersIteratorEnd_;
};

} // namespace facebook::rebalancer
