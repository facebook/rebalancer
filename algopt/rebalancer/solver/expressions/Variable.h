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
#include "algopt/rebalancer/solver/if/gen-cpp2/packer_types.h"
#include "algopt/rebalancer/solver/utils/Util.h"

namespace facebook::rebalancer {

class Change;
class LinearSum;

class Variable : public Expression {
 public:
  Variable(
      entities::ObjectId obj,
      entities::ContainerId con,
      const entities::Universe& universe,
      const Assignment& initialAssignment);

  void updateEquivalenceSets(EquivalenceSets& equivSets) const override;

  std::string innerDigest(size_t maxChildren = 10) const override;

  virtual std::optional<std::pair<entities::ObjectId, entities::ContainerId>>
  getVar() const override;

  virtual bool inner_is_integer(Context& context) override;

  void set_directly_affected_containers();

  AbstractContainer<ObjectPotential> getObjectPotentials(
      bool descending) const override;

  ExpressionProperties getProperties() const override;

  using Expression::references;
  bool references(entities::ObjectId objectId) const override {
    return object == objectId;
  }
  bool references(entities::ContainerId containerId) const override {
    return container == containerId;
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

  virtual bool hasNoLpIntent() const override;

  virtual std::optional<AffectedByChange> isAffectedByChange(
      const AffectedByChangeDecisionData& data) const override;

  entities::ObjectId object;

  entities::ContainerId container;

 private:
  double applyAssignment(const Assignment& assignment) const;

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
};

} // namespace facebook::rebalancer
