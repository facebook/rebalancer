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
#include "algopt/rebalancer/solver/expressions/LpEvaluator.h"
#include "algopt/rebalancer/solver/if/gen-cpp2/packer_types.h"
#include "algopt/rebalancer/solver/moves/MoveSet.h"
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"
#include "algopt/rebalancer/solver/utils/Util.h"

namespace facebook::rebalancer {

class BipartiteSwaps : public Expression {
 public:
  explicit BipartiteSwaps(
      PackerMap<entities::ObjectId, entities::ContainerId> initial_assignment,
      PackerSet<entities::ContainerId> left_subset,
      PackerSet<entities::ContainerId> right_subset,
      const entities::Universe& universe);

  ExpressionProperties getProperties() const override;

  using Expression::references;
  bool references(entities::ContainerId container) const override {
    return left_subset_.contains(container) ||
        right_subset_.contains(container);
  }

  using Expression::evaluate;
  virtual double evaluate(
      const BottomToTopEvaluator& evaluator,
      const ChangeSet& changes) const override;

  using Expression::lp;
  virtual algopt::lp::Expression lp(
      const LpEvaluator& evaluator,
      bool minimizing,
      const interface::OptimalSolverSpec& configs) override;

  virtual const std::string_view& getType() const override;

  virtual std::optional<AffectedByChange> isAffectedByChange(
      const AffectedByChangeDecisionData& data) const override;

  void updateEquivalenceSets(EquivalenceSets&) const override;

 private:
  using Transition = std::pair<entities::ContainerId, entities::ContainerId>;
  // Stores map of <src, dest> container to total number of moving objects
  // from src to dest for every container pair in moves
  using CounterMap = PackerMap<Transition, int>;

  virtual double innerFullApply(
      const TopToBottomEvaluator& evaluator,
      const Assignment& assignment) override;

  virtual double innerPartialApply(
      const BottomToTopEvaluator& evaluator,
      const Assignment& assignment,
      const ChangeSet& changes) override;

  virtual Bounds innerLowerAndUpperBounds(
      Context& context,
      const BoundConstraints& bc) const override;

  void set_directly_affected_containers();

  ExprPtr getExprForLp(const LpEvaluator& evaluator) const;

  // get minimal set of moves that produce the given assignment when applied
  // to the initial assignment
  MoveSet get_moves(const Assignment& assignment) const;

  // compute counters delta for a given set of moves
  CounterMap get_delta(const MoveSet& moves) const;

  // evaluate the expression given a counters delta
  double evaluate_delta(const CounterMap& counts_delta) const;
  bool is_same_side_move(const Transition& transition) const;

  void lpIntent(const LpEvaluator& evaluator, bool minimizing) override;

 private:
  PackerMap<entities::ObjectId, entities::ContainerId> initial_assignment;
  PackerSet<entities::ContainerId> left_subset_;
  PackerSet<entities::ContainerId> right_subset_;
  // counts_base[t] is the count of unique objects that moved from container
  // t.first to container t.second with respect to the initial assignment
  CounterMap counts_base;
  ExprPtr lpExpr_ = nullptr;
};
} // namespace facebook::rebalancer
