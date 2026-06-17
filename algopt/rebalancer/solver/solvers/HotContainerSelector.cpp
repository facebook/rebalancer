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

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/solver/iterators/Deduper.h"
#include "algopt/rebalancer/solver/iterators/ExpressionContainersIterator.h"
#include "algopt/rebalancer/solver/iterators/StlWrapper.h"
#include "algopt/rebalancer/solver/iterators/Transform.h"
#include <algopt/rebalancer/solver/solvers/HotContainerSelector.h>

namespace facebook::rebalancer {

namespace {

/**
 * Retrieves containers with the worst objective values for optimization.
 *
 * This function returns containers that need the most improvement based on
 * either object potential sorting or expression-based traversal.
 *
 * @param objectiveView View of the global objective
 * @param randomSeed Seed for random number generation
 * @param assignment Current assignment of objects to containers
 * @param enableObjectPotentialSorting Whether to use object potential for
 * sorting
 * @return Container of container IDs sorted by optimization priority
 */
AbstractContainer<entities::ContainerId> resetHotContainerTraversal(
    const GlobalObjective::View& objectiveView,
    uint64_t randomSeed,
    const Assignment& assignment,
    bool enableObjectPotentialSorting,
    const interface::HottestTraversalConfig& traversalConfig) {
  if (enableObjectPotentialSorting) {
    if (objectiveView.size() == 0) {
      return makeStlWrapperContainer(std::vector<entities::ContainerId>());
    }
    const std::function<entities::ContainerId(ObjectPotential)> transform =
        [&assignment](
            ObjectPotential objectPotential) -> entities::ContainerId {
      return assignment.getContainer(objectPotential.objectId);
    };
    // TODO: take objects from all objectives in the view
    return makeDeduperContainer(makeTransformContainer(
        (*objectiveView.begin())->getObjectPotentials(true /* descending */),
        transform));
  }
  return makeStlWrapperContainer(DescendingExpressionContainersTraversal(
      objectiveView,
      /*skipOptimalExpressions=*/true,
      traversalConfig,
      randomSeed));
}

/**
 * Finds the worst container that needs optimization.
 *
 * This function iterates through containers and returns the first one that
 * is not in the skip list. If no such container is found in the iterator,
 * it optionally looks for all containers not in the objective.
 *
 * @param problem The optimization problem
 * @param containersIterator Iterator for containers
 * @param containersIteratorEnd End iterator for containers
 * @param skipContainers Set of containers to skip
 * @param exploreMovesFromContainersNotInObjective Whether to consider
 * containers not in objective
 * @return Optional container ID of the worst container, or nullopt if none
 * found
 */
std::optional<entities::ContainerId> findNextHotContainer(
    const Problem& problem,
    AbstractIterator<entities::ContainerId>& containersIterator,
    const AbstractIterator<entities::ContainerId>& containersIteratorEnd,
    const PackerSet<entities::ContainerId>& skipContainers,
    bool exploreMovesFromContainersNotInObjective,
    algopt::Timer& timer) {
  const algopt::TimerScope timerScope(timer);
  while (containersIterator != containersIteratorEnd) {
    const auto container = *containersIterator;
    if (!skipContainers.contains(container)) {
      XLOGF(
          DBG2,
          "Picking Container {} (id: {})",
          problem.containerName(container),
          container);
      return container;
    }
    ++containersIterator;
  }

  if (exploreMovesFromContainersNotInObjective) {
    const auto& allContainers = problem.assignment.getContainers();
    for (auto containerId : allContainers) {
      if (!skipContainers.contains(containerId)) {
        XLOGF(
            DBG2,
            "Picking container {} that is not part of the objective",
            problem.containerName(containerId));
        return containerId;
      }
    }
  }

  return std::nullopt;
}
} // namespace

HotContainerSelector::HotContainerSelector(
    Problem& problem,
    uint64_t randomSeed,
    bool enableObjectPotentialSorting,
    bool exploreMovesFromContainersNotInObjective,
    const GlobalObjective::View& objectiveView,
    const interface::HottestTraversalConfig& traversalConfig)
    : problem_(problem),
      randomSeed_(randomSeed),
      enableObjectPotentialSorting_(enableObjectPotentialSorting),
      exploreMovesFromContainersNotInObjective_(
          exploreMovesFromContainersNotInObjective),
      objectiveView_(objectiveView),
      traversalConfig_{traversalConfig} {}

std::optional<entities::ContainerId> HotContainerSelector::next(
    const PackerSet<entities::ContainerId>& skipContainers) {
  const bool noMoreContainersToExplore =
      exploreMovesFromContainersNotInObjective_
      ? (skipContainers.size() == problem_.containers.size())
      : (containersIterator_ == containersIteratorEnd_);
  if (noMoreContainersToExplore) {
    return std::nullopt;
  }
  return findNextHotContainer(
      problem_,
      *containersIterator_,
      *containersIteratorEnd_,
      skipContainers,
      exploreMovesFromContainersNotInObjective_,
      findTimer_);
}

void HotContainerSelector::reset() {
  const algopt::TimerScope timerScope(findTimer_);
  containersTraversal_ = resetHotContainerTraversal(
      objectiveView_,
      randomSeed_,
      problem_.assignment,
      enableObjectPotentialSorting_,
      traversalConfig_);
  containersIterator_ = containersTraversal_->begin();
  containersIteratorEnd_ = containersTraversal_->end();

  // update the randomSeed so that if two containers are tied, then
  // tie resolution is dynamic
  randomSeed_ = folly::hash::twang_32from64(randomSeed_);
}

double HotContainerSelector::getFindTime() const {
  return findTimer_.getSeconds();
}

} // namespace facebook::rebalancer
