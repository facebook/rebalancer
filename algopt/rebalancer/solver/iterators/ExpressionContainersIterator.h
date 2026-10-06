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

#include "algopt/rebalancer/algopt_common/IncrementalPriorityQueue.h"
#include "algopt/rebalancer/algopt_common/IncrementalPriorityQueueV2.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/iterators/ExpressionIterator.h"
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"

#include <iterator>
#include <vector>

namespace facebook::rebalancer {

struct ExpressionIteratorRange {
  explicit ExpressionIteratorRange(
      PreOrderExpressionIterator current,
      PreOrderExpressionIterator to,
      int objective_pos)
      : current(std::move(current)),
        to(std::move(to)),
        objective_pos(objective_pos) {}

  PreOrderExpressionIterator current;
  PreOrderExpressionIterator to;
  int objective_pos;
};

struct ExpressionTraversalWithCount {
  explicit ExpressionTraversalWithCount(
      Expression* expression,
      bool descending,
      std::function<bool(Expression*)> shouldExpand,
      interface::HottestTraversalConfig traversalConfig,
      int objective_pos)
      : traversal(
            expression,
            descending,
            std::move(shouldExpand),
            std::move(traversalConfig)),
        objective_pos(objective_pos) {}

  PreOrderExpressionTraversal traversal;
  int objective_pos;
};

using expression_iterator_ranges =
    folly::small_vector<ExpressionIteratorRange, expected_tuple_size>;

// in case of a priority tie between containers, we want to randomize the
// container yielded by IncrementalPriorityQueue::top(), so we create this
// struct to encapsulate the container ID and an arbitrary priority which we
// give a random value to force a random order of containers in the queue
// when there is a tie
struct ContainerWithPriority {
  entities::ContainerId containerId;
  uint32_t priority;

  bool operator==(const ContainerWithPriority& other) const {
    return containerId == other.containerId && priority == other.priority;
  }
  bool operator<(const ContainerWithPriority& other) const;
};

} // namespace facebook::rebalancer

namespace std {
template <>
struct hash<facebook::rebalancer::ContainerWithPriority> {
  std::size_t operator()(const facebook::rebalancer::ContainerWithPriority&
                             containerWithPriority) const noexcept;
};
} // namespace std

namespace facebook::rebalancer {

using ContainerPriorityQueueV1 =
    facebook::algopt::IncrementalPriorityQueue<ContainerWithPriority>;
using ContainerPriorityQueueV2 =
    facebook::algopt::IncrementalPriorityQueueV2<ContainerWithPriority>;

// Iterates efficiently over containers directly affected by expressions in the
// order given by an ExpressionIterator, without repetitions.
template <class Queue = ContainerPriorityQueueV1>
class ExpressionContainersIterator
    : public std::iterator<std::input_iterator_tag, entities::ContainerId> {
 public:
  explicit ExpressionContainersIterator(
      expression_iterator_ranges ranges,
      bool skipOptimalExpressions,
      uint64_t randomSeed);
  ExpressionContainersIterator& operator++();
  bool operator==(const ExpressionContainersIterator& other) const;
  bool operator!=(const ExpressionContainersIterator& other) const;
  const entities::ContainerId& operator*() const;

 private:
  // queue is not empty and its top is strict, so no need to call feed_queue
  bool yields_unique_container() const;
  void feed_queue();
  void addContainers(
      const PackerSet<entities::ContainerId>& containers,
      int objective_pos);
  ContainerWithPriority randomizeContainer(
      entities::ContainerId containerId) const;
  std::vector<ContainerWithPriority> randomizeContainers(
      const PackerSet<entities::ContainerId>& containerIds) const;

 private:
  expression_iterator_ranges ranges;
  Queue queue;
  size_t current_range_index_ = 0;
  bool skipOptimalExpressions_;
  PackerMap<entities::ContainerId, int> container_to_pos;
  uint64_t randomSeed_;
};

template <class Queue = ContainerPriorityQueueV1>
class DescendingExpressionContainersTraversal {
 public:
  using value_type = entities::ContainerId;
  using const_iterator = ExpressionContainersIterator<Queue>;

  explicit DescendingExpressionContainersTraversal(
      const GlobalObjective::View& objectiveView,
      bool skipOptimalExpressions = false,
      const interface::HottestTraversalConfig& traversalConfig =
          getDefaultTraversalConfig(),
      uint64_t randomSeed = 0);

  const_iterator begin() const;
  const_iterator end() const;

 private:
  static const interface::HottestTraversalConfig& getDefaultTraversalConfig() {
    static const interface::HottestTraversalConfig config{};
    return config;
  }

  void initTraversal(
      const GlobalObjective::View& objectiveView,
      const interface::HottestTraversalConfig& traversalConfig);

 private:
  folly::small_vector<ExpressionTraversalWithCount, expected_tuple_size>
      expression_traversals_;
  bool skipOptimalExpressions_;
  uint64_t randomSeed_;
};

} // namespace facebook::rebalancer
