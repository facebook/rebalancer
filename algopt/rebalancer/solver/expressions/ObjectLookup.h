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

#include "algopt/rebalancer/algopt_common/AssociativeHybridMap.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/if/gen-cpp2/packer_types.h"
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <set>

namespace facebook::rebalancer {

class Expression;
class Change;
class ObjectVector;

class ObjectLookup : public Expression {
 public:
  explicit ObjectLookup(
      std::shared_ptr<Expression>,
      std::shared_ptr<const PackerSet<entities::ContainerId>> containersPtr,
      const entities::Universe& universe,
      const Assignment& initialAssignment);

  void updateEquivalenceSets(EquivalenceSets& equivSets) const override;

  virtual std::optional<AffectedByChange> isAffectedByChange(
      const AffectedByChangeDecisionData& data) const override;

  // Child (ObjectVector) has no value by itself, so have
  // no point in being sorted.
  virtual std::vector<std::pair<Expression*, double>> get_sorted_children(
      bool) const override;

  AbstractContainer<ObjectPotential> getObjectPotentials(
      bool descending) const override;

  ExpressionProperties getProperties() const override;

  using Expression::references;
  bool references(entities::ContainerId container) const override {
    return containersPtr_->contains(container);
  }

  std::shared_ptr<const PackerSet<entities::ContainerId>> containersPtr_;

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

  bool inner_is_integer(Context& context) override;

 protected:
  ObjectVector* object_vector;

 private:
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

  double applyAssignment(const Assignment& assignment);

  bool shouldComputeDescendingChildPotentials() const override;

  std::string innerDigest(size_t maxChildren = 10) const override;
  void set_directly_affected_containers();
  double getObjectValue(entities::ObjectId objectId) const;
  int getCurrObjectsInContainersSize(const Assignment& assignment) const;

  std::vector<std::pair<entities::ObjectId, double>>
  getObjectValuesFromObjectVectorAndUpdateObjectPotentials(
      const Assignment& assignment);

  std::vector<std::pair<entities::ObjectId, double>>
  getObjectValuesFromAssignmentAndUpdateObjectPotentials(
      const Assignment& assignment);

  PackerMap<entities::ObjectId, int> getNetChangePerObject(
      const ChangeSet& changes);

  algopt::SumHybridMap<entities::ObjectId, double> values;
  std::set<ObjectPotential> objectPotentials_;
};
} // namespace facebook::rebalancer
