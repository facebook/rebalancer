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

#include "algopt/rebalancer/algopt_common/AssociativeMap.h"
#include "algopt/rebalancer/algopt_common/ValueSortedMap.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookupWithMinPresence.h"
#include "algopt/rebalancer/solver/if/gen-cpp2/packer_types.h"
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"
#include "algopt/rebalancer/solver/utils/Util.h"

namespace facebook::rebalancer {

class Expression;
class Change;
class ObjectPartition;

// Transform applied to each group's penalty before the penalties are summed.
// IDENTITY leaves the penalty unchanged; SQUARE returns penalty^2; STEP returns
// 1 if the penalty is strictly positive and 0 otherwise.
enum class ObjectPartitionLookupPenaltyTransform {
  IDENTITY,
  SQUARE,
  STEP,
};

// Default policy for ObjectPartitionLookup
struct ObjectPartitionLookupDefaultPolicy {
  static constexpr std::string_view typeName = "ObjectPartitionLookup";
};

template <typename Policy = ObjectPartitionLookupDefaultPolicy>
class ObjectPartitionLookup : public Expression {
 public:
  enum Bound {
    MAX,
    MIN,
  };

 public:
  ObjectPartitionLookup(
      std::shared_ptr<Expression> objectPartition,
      std::shared_ptr<const PackerSet<entities::ContainerId>>
          lookupContainersPtr,
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId,
      const entities::Universe& universe,
      const Assignment& initialAssignment,
      PackerMap<entities::GroupId, double> groupLimitOverrides = {},
      PackerSet<entities::ObjectId> initialDuringObjects = {},
      std::optional<double> defaultGroupLimitOverride = std::nullopt,
      ObjectPartitionLookupPenaltyTransform penaltyTransform =
          ObjectPartitionLookupPenaltyTransform::IDENTITY,
      int groupsAllowed = 0,
      Bound bound = Bound::MAX,
      std::conditional_t<
          std::is_same_v<Policy, ObjectPartitionLookupWithMinPresencePolicy>,
          typename ObjectPartitionLookupWithMinPresencePolicy::Data,
          std::monostate> data = {});

  void updateEquivalenceSets(EquivalenceSets& equivSets) const override;

  virtual std::optional<AffectedByChange> isAffectedByChange(
      const AffectedByChangeDecisionData& data) const override;

  AbstractContainer<ObjectPotential> getObjectPotentials(
      bool descending) const override;

  // Children (ObjectVector/ObjectPartition) have no values themselves, so have
  // no point in being sorted.
  std::vector<std::pair<Expression*, double>> get_sorted_children(
      bool) const override;

  std::shared_ptr<Expression> get_do_not_make_worse_copy(
      const Assignment& initialAssignment) const;

  ExpressionProperties getProperties() const override;

  using Expression::references;
  bool references(entities::ContainerId container) const override {
    return lookupContainersPtr_->contains(container);
  }

  std::optional<entities::ScopeItemId> getDimensionScopeItemId(
      entities::ContainerId container) const;

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

  const entities::PartitionId getPartitionId() const;

  const entities::DimensionId getDimensionId() const;

  const entities::ScopeItemId getScopeItemId() const;

  const entities::ScopeId getScopeId() const;

  const PackerMap<
      entities::GroupId,
      algopt::SumMap<entities::ObjectId, double>>&
  getGroupObjectWeights() const;

 protected:
  // Helper to get extra data for MinPresence policy
  // Only available when Policy is ObjectPartitionLookupWithMinPresencePolicy
  template <typename P = Policy>
  std::enable_if_t<
      std::is_same_v<P, ObjectPartitionLookupWithMinPresencePolicy>,
      const typename ObjectPartitionLookupWithMinPresencePolicy::Data&>
  getData() const {
    return data_;
  }

 private:
  class GroupPenalties {
   public:
    struct Compare {
      bool operator()(
          const std::pair<entities::GroupId, double>& lhs,
          const std::pair<entities::GroupId, double>& rhs) const;
    };

    using SortedMap =
        facebook::algopt::ValueSortedMap<entities::GroupId, double, Compare>;
    using const_iterator = typename SortedMap::const_iterator;

    explicit GroupPenalties(bool iterable);
    void clear();
    void assign(entities::GroupId group_id, double penalty);
    double get(entities::GroupId group_id) const {
      return sum_map.getValueIfExists(group_id).value_or(0.0);
    }
    double sum_all() const;
    double sum_top(int n) const;
    const_iterator begin() const;
    const_iterator end() const;

   private:
    algopt::SumMap<entities::GroupId, double> sum_map;
    folly::Optional<SortedMap> sorted_map;
  };

  virtual double innerFullApply(
      const TopToBottomEvaluator& evaluator,
      const Assignment& assignment) override;

  double computeFromAssignment(const Assignment& assignment);

  virtual double innerPartialApply(
      const BottomToTopEvaluator& evaluator,
      const Assignment& assignment,
      const ChangeSet& changes) override;

  bool shouldComputeDescendingChildPotentials() const override;

  std::string innerDigest(size_t maxChildren = 10) const override;

  void set_directly_affected_containers();

  virtual Bounds innerLowerAndUpperBounds(
      Context& /*context*/,
      const BoundConstraints& bc) const override;

  Bounds getBounds(const BoundConstraints& bc) const;

  Bounds getBounds() const;

  std::pair<double, double> getMaxAndMinNormalizedDeviationsFromLimit(
      entities::GroupId groupId) const;

  double getGroupLimit(entities::GroupId group) const;

  double getGroupPenalty(double weight, entities::GroupId groupId) const;

  double computePenalty(double deviationFromLimit) const;

  // Immutable properties
  ObjectPartition* objectPartition_;
  std::shared_ptr<const PackerSet<entities::ContainerId>> lookupContainersPtr_;
  PackerMap<entities::GroupId, double> groupLimitOverrides_;
  std::optional<double> defaultGroupLimitOverride_;
  PackerSet<entities::ObjectId> initialDuringObjects_;
  ObjectPartitionLookupPenaltyTransform penaltyTransform_;
  int groupsAllowed_;
  Bound bound_;
  entities::ScopeId scopeId_;
  entities::ScopeItemId scopeItemId_;
  const entities::ObjectScalarDimension& dimension_;
  bool scopeMatchesDimensionScope_;
  const entities::Scope& dimensionScope_;

  // Mutable state:

  // Maps a groupId to a map of objectId to how many times the object is present
  // in the group. An object may be present in a group multiple times, because
  // ObjectPartition may encode an object appearing multiple times in the same
  // group.
  PackerMap<entities::GroupId, PackerMap<entities::ObjectId, int>>
      groupObjectCounts_;

  // Maps a groupId to a map of objectId to weight contributed by the object in
  // the group. If an object appears multiple times in the group, the weight is
  // the total contribution of all repetitions.
  PackerMap<entities::GroupId, algopt::SumMap<entities::ObjectId, double>>
      groupObjectWeights_;

  // Map of groupId to the penalty contributed by the group.
  GroupPenalties groupPenalties_;

  // Set of objectId currently contributing to this expression's formula.
  PackerSet<entities::ObjectId> contributingObjectIds_;

  // Maps a groupId to a pair, where the first (respectively, second) value is
  // the sum of weights of initialDuringObjects with a positive (resp. negative)
  // weight in that group
  PackerMap<entities::GroupId, std::pair<double, double>>
      groupToDuringObjectsTotalPositiveAndNegativeWeights_;

  // Maps an objectId to its dimensionScopeItemId for the current assignment.
  // This is updated in full/partial apply and used when the assignment cannot
  // be accessed directly. It is only needed when the ObjectPartition's scope
  // (dimension scope) is different from the one specified in the constructor.
  PackerMap<entities::ObjectId, entities::ScopeItemId>
      objectToAssignmentDimensionScopeItem_;

  // Policy-specific data (only used by certain policies like MinPresence)
  std::conditional_t<
      std::is_same_v<Policy, ObjectPartitionLookupWithMinPresencePolicy>,
      typename ObjectPartitionLookupWithMinPresencePolicy::Data,
      std::monostate>
      data_;
};

// Type alias for the default non-templated ObjectPartitionLookup
using ObjectPartitionLookupDefault =
    ObjectPartitionLookup<ObjectPartitionLookupDefaultPolicy>;

using ObjectPartitionLookupWithMinPresence =
    ObjectPartitionLookup<ObjectPartitionLookupWithMinPresencePolicy>;

// Forward declarations of template specializations for
// ObjectPartitionLookupWithMinPresencePolicy
template <>
double ObjectPartitionLookup<ObjectPartitionLookupWithMinPresencePolicy>::
    getGroupPenalty(double weight, entities::GroupId groupId) const;

template <>
Bounds ObjectPartitionLookup<
    ObjectPartitionLookupWithMinPresencePolicy>::getBounds() const;

template <>
algopt::lp::Expression
ObjectPartitionLookup<ObjectPartitionLookupWithMinPresencePolicy>::lp(
    const LpEvaluator& evaluator,
    bool minimizing,
    const interface::OptimalSolverSpec& configs);

} // namespace facebook::rebalancer
