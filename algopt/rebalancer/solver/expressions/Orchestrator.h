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
#include "algopt/rebalancer/solver/utils/PriorityInfo.h"

#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/executors/ThreadPoolExecutor.h>

#include <vector>

namespace facebook::rebalancer {

class Orchestrator {
 public:
  Orchestrator();

  void init(
      std::vector<Expression*> roots,
      AffectedByChangeDecisionData data,
      const PackerSet<entities::ContainerId>& fixedContainers = {});

  void apply(Context& context, const Assignment& assignment) const;

  double evaluate(const Expression* root, Context& context) const;

  void updateEquivalenceSets(
      EquivalenceSets& equivalenceSets,
      entities::EntityIdType numObjects) const;

  void buildLp(
      const std::vector<ExprPtr>& objectiveNodes,
      const PackerMap<ExprPtr, std::string>& constraintNodesToNames,
      Problem& problem,
      LpContext& context,
      const interface::OptimalSolverSpec& configs) const;

  // find leaf nodes affected by dynamic containers
  // add the changed children nodes to context.changedChildren
  // used by lp
  folly::F14FastMap<Expression*, folly::F14FastSet<Expression*>>
  getDynamicChildren(
      const PackerSet<entities::ContainerId>& dynamicContainers) const;

  void initBoundsBottomUp(std::shared_ptr<folly::ThreadPoolExecutor> executor);

  // returns the set of nodes in the order traversed by orchestrator
  const std::vector<Expression*>& getNodesInPostorder() const;

  const folly::F14VectorMap<Expression*, std::vector<Expression*>>&
  getNodeToParents() const;

  inline size_t getNumFixedNodes() const {
    return nFixedNodes_;
  }

  inline size_t getNumNodes() const {
    return postOrder_.size();
  }

 private:
  // initialize nodesPriority_
  void initOrder(
      std::vector<int>& nodeRangeByRoots,
      const PackerSet<entities::ContainerId>& fixedContainers);
  // set parents for each node
  void initParents();
  // set leafStore_, containerToLeaves_, objectToLeaves_,
  // containerObjectToLeaves_, and toCheckLeaves_
  void initIndex(
      const std::vector<int>& nodeRangeByRoots,
      const AffectedByChangeDecisionData& data);

  void initChangeInfo(
      Expression* node,
      int rootId,
      const AffectedByChangeDecisionData& data);

  // traverse trees in post order
  void traversePostOrder(
      Expression* node,
      folly::F14VectorSet<Expression*>& visited,
      const PackerSet<entities::ContainerId>& fixedContainers,
      size_t currRootIdx);

  // find leaf nodes affected by changes
  // push changed leaf nodes to context.readyNodes
  void computeChangedLeaves(Context& context) const;

  // find all changed leaf nodes
  // push them to context.readyNodes
  void computeAllChangedLeaves(Context& context) const;

  // find changed leaf nodes belonging to a root
  // push them to context.readyNodes
  void computeChangedLeavesByRoot(Context& context, int root) const;

  // propagating changes to parents, push parent to context.readyNodes
  void notifyChange(Context& context, Expression* changedNode) const;

  void buildLpBottomUp(
      const std::vector<ExprPtr>& objectiveNodes,
      const PackerMap<ExprPtr, std::string>& constraintNodesToNames,
      Problem& problem,
      LpContext& context,
      const interface::OptimalSolverSpec& configs) const;

  inline const PriorityInfo& priority(const Expression* expr) const {
    return nodeToPriority_.at(expr);
  }

 private:
  using LeafId = uint32_t;

  // Per-leaf data (expression, filter) stored in flat arrays,
  // each indexed by a contiguous LeafId.
  class LeafStore {
   public:
    LeafId add(Expression* node, ChangeFilterFn filter);

    Expression* at(LeafId id) const {
      return leaves_[id];
    }
    const ChangeFilterFn& filter(LeafId id) const {
      return filters_[id];
    }

   private:
    std::vector<Expression*> leaves_;
    std::vector<ChangeFilterFn> filters_;
  };
  LeafStore leafStore_;

  // maps containerId to leaves, per root
  std::vector<PackerMap<entities::ContainerId, std::vector<LeafId>>>
      containerToLeaves_;

  // maps objectId to leaves, per root
  std::vector<PackerMap<entities::ObjectId, std::vector<LeafId>>>
      objectToLeaves_;

  // maps (containerId, objectId) pairs to leaves
  std::vector<PackerMap<
      entities::ContainerId,
      PackerMap<entities::ObjectId, std::vector<LeafId>>>>
      containerObjectToLeaves_;

  // leaves that need checking regardless of changes
  std::vector<std::vector<LeafId>> toCheckLeaves_;

  folly::F14VectorMap<Expression*, std::vector<Expression*>> nodeToParents_;

  folly::F14FastMap<const Expression*, PriorityInfo> nodeToPriority_;

  std::vector<Expression*> roots_;

  std::vector<Expression*> postOrder_;

  size_t nFixedNodes_ = 0;
};
} // namespace facebook::rebalancer
