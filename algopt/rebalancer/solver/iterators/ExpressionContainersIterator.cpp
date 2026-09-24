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

#include "algopt/rebalancer/solver/iterators/ExpressionContainersIterator.h"

#include <folly/hash/Hash.h>

#include <cassert>

namespace std {
// define std::hash for ContainerWithPriority so it can be used in hash tables
std::size_t hash<facebook::rebalancer::ContainerWithPriority>::operator()(
    const facebook::rebalancer::ContainerWithPriority& containerWithPriority)
    const noexcept {
  return folly::hash::twang_32from64(
      (uint64_t(containerWithPriority.priority) << 32) |
      static_cast<size_t>(containerWithPriority.containerId));
}
} // namespace std

namespace facebook::rebalancer {

// The following optimizations save computation. The relevant code which
// implements them is highlighted below for clarity.
// Optimization 1: do not expand a node v if all the nodes w (w \neq v) in the
// induced subgraph rooted at v affect the same set of containers
// Optimization 2: do not expand a node v if all the containers affected by it
// are fixed containers

ExpressionContainersIterator::ExpressionContainersIterator(
    expression_iterator_ranges ranges,
    bool skipOptimalExpressions,
    uint64_t randomSeed)
    : ranges(std::move(ranges)),
      skipOptimalExpressions_(skipOptimalExpressions),
      randomSeed_(randomSeed) {
  feed_queue();
}

ExpressionContainersIterator& ExpressionContainersIterator::operator++() {
  assert(!queue.empty());
  queue.remove(queue.top());
  feed_queue();
  return *this;
}

bool ExpressionContainersIterator::operator==(
    const ExpressionContainersIterator& other) const {
  // Domain of equality: any pair of valid iterators from the same traversal
  // where at least one points to the end.
  return queue.empty() == other.queue.empty();
}

bool ExpressionContainersIterator::operator!=(
    const ExpressionContainersIterator& other) const {
  return !(*this == other);
}

const entities::ContainerId& ExpressionContainersIterator::operator*() const {
  return queue.top().containerId;
}

bool ExpressionContainersIterator::yields_unique_container() const {
  return !queue.empty() && queue.is_top_strict();
}

void ExpressionContainersIterator::feed_queue() {
  // 1: if current_range_index_ >= ranges.size() we exhausted all expressions in
  // global objective
  // 2: if yields_unique_container, nothing to do, we can return early
  while (current_range_index_ < ranges.size() && !yields_unique_container()) {
    auto& [current, to, objective_pos] = ranges[current_range_index_];

    // 1: if we visited all expressions in the given subtree we can move on to
    // the next objective in the tuple.
    if (current == to) {
      ++current_range_index_;
      continue;
    }

    const auto& [expression, potential] = *current;
    // if current expression is fixed (optimization 2) or has already reached
    // its estimated global lower bound, we can safely ignore it for sorting
    // containers
    if (expression->isFixed() ||
        (skipOptimalExpressions_ &&
         expression->getPrecision().compare(potential, 0.0) == 0)) {
      ++current;
      continue;
    }

    auto uniqueAffectedSetInSubgraph =
        expression->getUniqueAffectedContainersInSubgraphIfExists().getSetPtr();
    auto directlyAffectedSet =
        expression->getDirectlyAffectedContainers().getSetPtr();
    if (uniqueAffectedSetInSubgraph && !directlyAffectedSet) {
      // Optimization 1; node will not be expanded
      // if node does not directly affect containers, avoid unnecessary copying
      addContainers(*uniqueAffectedSetInSubgraph, objective_pos);
    } else if (uniqueAffectedSetInSubgraph && directlyAffectedSet) {
      // Optimization 1; node will not be expanded
      PackerSet<entities::ContainerId> containers =
          *uniqueAffectedSetInSubgraph;
      containers.insert(
          directlyAffectedSet->begin(), directlyAffectedSet->end());
      addContainers(containers, objective_pos);
    } else if (directlyAffectedSet) {
      // if node needs to be expanded and it also directly affects containers
      addContainers(*directlyAffectedSet, objective_pos);
    }

    ++current;
  }
}

void ExpressionContainersIterator::addContainers(
    const PackerSet<entities::ContainerId>& containers,
    [[maybe_unused]] int objective_pos) {
  queue.update(randomizeContainers(containers));
}

ContainerWithPriority ExpressionContainersIterator::randomizeContainer(
    entities::ContainerId containerId) const {
  return ContainerWithPriority{
      .containerId = containerId,
      .priority = folly::hash::twang_32from64(
          (uint64_t(randomSeed_) << 32) | static_cast<size_t>(containerId))};
}

std::vector<ContainerWithPriority>
ExpressionContainersIterator::randomizeContainers(
    const PackerSet<entities::ContainerId>& containerIds) const {
  std::vector<ContainerWithPriority> randomizedContainers;
  randomizedContainers.reserve(containerIds.size());
  for (const entities::ContainerId containerId : containerIds) {
    randomizedContainers.push_back(randomizeContainer(containerId));
  }
  return randomizedContainers;
}

bool ContainerWithPriority::operator<(
    const ContainerWithPriority& other) const {
  if (priority != other.priority) {
    return priority < other.priority;
  }
  return containerId < other.containerId;
}

DescendingExpressionContainersTraversal::
    DescendingExpressionContainersTraversal(
        const GlobalObjective::View& objectiveView,
        bool skipOptimalExpressions,
        const interface::HottestTraversalConfig& traversalConfig,
        uint64_t randomSeed)
    : skipOptimalExpressions_(skipOptimalExpressions), randomSeed_(randomSeed) {
  auto effectiveTraversalConfig = traversalConfig;
  effectiveTraversalConfig.pruneOptimalSubgraphs() =
      skipOptimalExpressions && *traversalConfig.pruneOptimalSubgraphs();
  initTraversal(objectiveView, effectiveTraversalConfig);
}

void DescendingExpressionContainersTraversal::initTraversal(
    const GlobalObjective::View& objectiveView,
    const interface::HottestTraversalConfig& traversalConfig) {
  auto shouldExpand = [](Expression* node) {
    if (node->isFixed()) {
      // Using Optimization 2, i.e., if a node is fixed, we should not expand
      return false;
    }
    // using Optimization 1, i.e., do not expand a node if all nodes in its
    // subgraph affect the same set of containers
    auto& uniqueAffectedSet =
        node->getUniqueAffectedContainersInSubgraphIfExists();

    return !uniqueAffectedSet.exists();
  };

  // special case handling for case of just one objective to avoid extra set
  // operations
  if (objectiveView.size() == 1) {
    const auto& expr = *(objectiveView.begin());
    expression_traversals_.emplace_back(
        expr.get(),
        /*descending=*/true,
        shouldExpand,
        traversalConfig,
        0);
    return;
  }

  int count = 0;
  for (const auto& expr : objectiveView) {
    expression_traversals_.emplace_back(
        expr.get(),
        /*descending=*/true,
        shouldExpand,
        traversalConfig,
        count);
    count++;
  }
}

ExpressionContainersIterator DescendingExpressionContainersTraversal::begin()
    const {
  expression_iterator_ranges ranges;
  for (const auto& [traversal, objective_pos] : expression_traversals_) {
    ranges.emplace_back(traversal.begin(), traversal.end(), objective_pos);
  }
  return ExpressionContainersIterator(
      std::move(ranges), skipOptimalExpressions_, randomSeed_);
}

ExpressionContainersIterator DescendingExpressionContainersTraversal::end()
    const {
  return ExpressionContainersIterator({}, false, randomSeed_);
}
} // namespace facebook::rebalancer
