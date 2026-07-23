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

#include "algopt/rebalancer/materializer/utils/Cache.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/if/gen-cpp2/packer_types.h"
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <folly/Function.h>

namespace facebook::rebalancer {

template <class Value>
using ThreadSafeContainer = materializer::SingleEntryCache<Value>;

class Change;

// The groups each object belongs to in one partition, optionally limited to a
// chosen set of groups.
class PartitionInfo {
 public:
  PartitionInfo(
      const entities::Universe& universe,
      entities::PartitionId partitionId,
      std::shared_ptr<const PackerSet<entities::GroupId>> filteredGroupIds =
          nullptr);

  entities::PartitionId getPartitionId() const {
    return partitionId_;
  }

  const entities::Universe& getUniverse() const {
    return universe_;
  }

  bool isRelevant(entities::GroupId groupId) const {
    return !filteredGroupIds_ || filteredGroupIds_->contains(groupId);
  }

  void forEachRelevantGroup(
      folly::FunctionRef<void(entities::GroupId)> fn) const;

  const entities::Map<entities::ObjectId, std::vector<entities::GroupId>>&
  getObjectIdToGroupIds() const {
    return *objectIdToGroupIds_;
  }

 private:
  const entities::Universe& universe_;
  entities::PartitionId partitionId_;
  std::shared_ptr<const PackerSet<entities::GroupId>> filteredGroupIds_;
  std::shared_ptr<
      const entities::Map<entities::ObjectId, std::vector<entities::GroupId>>>
      objectIdToGroupIds_;
};

class ObjectPartition : public Expression {
 public:
  ObjectPartition(
      std::shared_ptr<const PartitionInfo> partitionInfo,
      entities::DimensionId dimensionId,
      PackerMap<entities::GroupId, double> groupLimits,
      std::optional<PackerSet<entities::ScopeItemId>> scopeItemIds =
          std::nullopt,
      PackerMap<entities::GroupId, double> groupCoefficients = {},
      double defaultGroupLimit = 0.0,
      double defaultGroupCoefficient = 1.0);

  std::string innerDigest(size_t maxChildren = 10) const override;

  void updateEquivalenceSets(EquivalenceSets& equivalenceSets) const override;

  ExpressionProperties getProperties() const override;

  double getGroupLimit(entities::GroupId groupId) const;

  double getGroupCoefficient(entities::GroupId groupId) const;

  double getObjectWeight(
      entities::ObjectId objectId,
      std::optional<entities::ScopeItemId> scopeItemId) const;

  const PackerMap<entities::GroupId, std::pair<double, double>>&
  getGroupToTotalPositiveAndNegativeWeights() const;

  // For a groupId, returns a pair, where the first value is sum of weights of
  // objects with a positive weight in the group and the second one is sum of
  // weights of objects with a negative weight
  const std::pair<double, double>& getTotalPositiveAndNegativeWeightsForGroup(
      entities::GroupId groupId) const;

  // Returns a map of groupId to the normalized value of the maximum and minimum
  // possible deviations from the limit. Normalization is based on the value in
  // groupCoefficients_
  const PackerMap<entities::GroupId, std::pair<double, double>>&
  getGroupToMaxAndMinNormalizedDeviationsFromLimit() const;

  // Returns a list of groupId the given objectId belongs to.
  const std::vector<entities::GroupId>& getObjectGroups(
      entities::ObjectId objectId) const;

  // Returns a map of objectId to list of groupId the object belongs to.
  const PackerMap<entities::ObjectId, std::vector<entities::GroupId>>&
  getObjectGroups() const;

  const PackerMap<
      entities::GroupId,
      PackerMap<entities::EquivalenceSetId, int>>&
  getEquivSetGroups(const EquivalenceSets& equiv_sets);

  const PackerMap<entities::GroupId, double>& getGroupNegativeLimits() const;
  const PackerMap<entities::GroupId, double>& getGroupPositiveLimits() const;

  const entities::PartitionId getPartitionId() const;
  const entities::DimensionId getDimensionId() const;

  virtual const std::string_view& getType() const override;

  virtual bool hasNoLpIntent() const override;

  bool shouldComputeBounds() const override;

 protected:
  virtual bool hasValue() const override;

 private:
  virtual Bounds innerLowerAndUpperBounds(
      Context& context,
      const BoundConstraints& bc) const override;

 private:
  std::shared_ptr<const PartitionInfo> partitionInfo_;
  entities::DimensionId dimensionId_;

  // Maps a groupId to its limit. Use param 'defaultGroupLimit' to set a default
  // limit; if nothing is provided, it is set to 0.
  PackerMap<entities::GroupId, double> groupLimits_;
  double defaultGroupLimit_;

  // The list of relevant scope item IDs for dynamic dimensions. In the case
  // where the dimension scope matches that of the ObjectPartitionLookup, this
  // will just be the single scopeItemId from the ObjectPartitionLookup. In the
  // case when the scopes do not match, these will be the dimension scope IDs
  // retrieved from the scope image of the ObjectPartitionLookup and dimension
  // scopes.
  std::optional<PackerSet<entities::ScopeItemId>> scopeItemIds_;

  // Maps a groupId to its limit for the subset of groups whch have a negative
  // limit.
  PackerMap<entities::GroupId, double> groupNegativeLimits_;

  // Maps a groupId to its limit for the subset of groups whch have a positive
  // limit.
  PackerMap<entities::GroupId, double> groupPositiveLimits_;

  // Maps a groupId to its coefficient. Use param 'defaultGroupCoefficient' to
  // set a default limit; if nothing is provided, it is set to 1.
  PackerMap<entities::GroupId, double> groupCoefficients_;
  double defaultGroupCoefficient_;

  // Maps a groupId to a pair, where the first (respectively, second) value is
  // the sum of weights of objects with a positive (resp. negative) weight in
  // that group.
  PackerMap<entities::GroupId, std::pair<double, double>>
      groupToTotalPositiveAndNegativeWeights_;

  // Maps a groupId to a pair, where the first (respectively, second) value is
  // the maximum (respectively, minimum) possible deviation from the limit. The
  // values are normalized based on the values in groupCoefficients_
  PackerMap<entities::GroupId, std::pair<double, double>>
      groupToMaxAndMinNormalizedDeviationsFromLimit_;

  // Equiv set grouping used for efficient LP formulation
  ThreadSafeContainer<PackerMap<
      entities::GroupId /* group */,
      PackerMap<
          entities::EquivalenceSetId /* equiv set */,
          int /* object in equiv set */>>>
      equivSetGroups_;

  const entities::ObjectScalarDimension* dimension_;
};
} // namespace facebook::rebalancer
