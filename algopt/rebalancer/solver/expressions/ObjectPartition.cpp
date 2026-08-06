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

#include "algopt/rebalancer/solver/expressions/ObjectPartition.h"

#include "algopt/rebalancer/solver/expressions/PropertiesHelper.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"

#include <folly/container/Enumerate.h>

#include <sstream>
#include <stdexcept>

namespace {
const std::vector<facebook::rebalancer::entities::GroupId> kEmptyGroups = {};
constexpr std::string_view type = "ObjectPartition";
} // namespace

namespace facebook::rebalancer {

using entities::ContainerId;
using entities::DimensionId;
using entities::EquivalenceSetId;
using entities::GroupId;
using entities::ObjectId;
using entities::PartitionId;
using entities::ScopeItemId;

PartitionInfo::PartitionInfo(
    const entities::Universe& universe,
    PartitionId partitionId,
    std::shared_ptr<const PackerSet<GroupId>> filteredGroupIds)
    : universe_(universe),
      partitionId_(partitionId),
      filteredGroupIds_(std::move(filteredGroupIds)) {
  const auto& rawObjectIdToGroupIds =
      universe_.getPartition(partitionId_).getObjectIdToGroupIdsPtr();
  // No filter: share the partition's map instead of copying it.
  if (!filteredGroupIds_) {
    objectIdToGroupIds_ = rawObjectIdToGroupIds;
    return;
  }
  entities::Map<ObjectId, std::vector<GroupId>> filtered;
  for (const auto& [objectId, groupIds] : *rawObjectIdToGroupIds) {
    std::vector<GroupId> kept;
    std::ranges::copy_if(
        groupIds, std::back_inserter(kept), [this](const auto& groupId) {
          return filteredGroupIds_->contains(groupId);
        });
    if (!kept.empty()) {
      filtered.emplace(objectId, std::move(kept));
    }
  }
  objectIdToGroupIds_ =
      std::make_shared<const entities::Map<ObjectId, std::vector<GroupId>>>(
          std::move(filtered));
}

void PartitionInfo::forEachRelevantGroup(
    folly::FunctionRef<void(GroupId)> fn) const {
  if (filteredGroupIds_) {
    for (const auto groupId : *filteredGroupIds_) {
      fn(groupId);
    }
  } else {
    for (const auto groupId :
         universe_.getPartition(partitionId_).getGroupIds()) {
      fn(groupId);
    }
  }
}

ObjectPartition::ObjectPartition(
    std::shared_ptr<const PartitionInfo> partitionInfo,
    DimensionId dimensionId,
    PackerMap<GroupId, double> groupLimits,
    std::optional<PackerSet<ScopeItemId>> scopeItemIds,
    PackerMap<GroupId, double> groupCoefficients,
    double defaultGroupLimit,
    double defaultGroupCoefficient)
    : Expression(partitionInfo->getUniverse()),
      partitionInfo_(std::move(partitionInfo)),
      dimensionId_(dimensionId),
      groupLimits_(std::move(groupLimits)),
      defaultGroupLimit_(defaultGroupLimit),
      scopeItemIds_(std::move(scopeItemIds)),
      groupCoefficients_(std::move(groupCoefficients)),
      defaultGroupCoefficient_(defaultGroupCoefficient) {
  auto& objectDimension = universe_->getObjects().getDimension(dimensionId_);
  if (objectDimension.size() != 1 ||
      objectDimension.at(0).isRoutingConfigBased()) {
    throw std::runtime_error(
        "non-scalar dimensions or ObjectPartitionRoutingDimensions are not currently supported with objectPartition");
  }
  if (objectDimension.only().isDynamic() != scopeItemIds_.has_value()) {
    throw std::runtime_error(
        "Dynamic dimension must have scopeItemIds set and vice versa");
  }

  dimension_ = &objectDimension.at(0);

  const auto checkGroupsRelevant = [&](const auto& groupToValue,
                                       std::string_view name) {
    for (const auto& [groupId, _] : groupToValue) {
      if (!partitionInfo_->isRelevant(groupId)) {
        throw std::runtime_error(
            fmt::format(
                "{} contains group that is not in filteredGroupIds", name));
      }
    }
  };
  checkGroupsRelevant(groupLimits_, "groupLimits");
  checkGroupsRelevant(groupCoefficients_, "groupCoefficients");

  // Compute group weights.
  for (auto& [objectId, groupIds] : partitionInfo_->getObjectIdToGroupIds()) {
    double maxObjectWeight = std::numeric_limits<double>::lowest();
    double minObjectWeight = std::numeric_limits<double>::max();
    if (scopeItemIds_.has_value() && !scopeItemIds_.value().empty()) {
      // When we have scopeItemIds_, we need to check the value of the object in
      // all of them to determine its max and min values
      for (auto& scopeItemId : scopeItemIds_.value()) {
        const double objectWeight = getObjectWeight(objectId, scopeItemId);
        maxObjectWeight = std::max(maxObjectWeight, objectWeight);
        minObjectWeight = std::min(minObjectWeight, objectWeight);
      }
    } else {
      const double objectWeight = getObjectWeight(objectId, std::nullopt);
      maxObjectWeight = objectWeight;
      minObjectWeight = objectWeight;
    }
    for (auto groupId : groupIds) {
      auto& [totalPositiveWeight, totalNegativeWeight] =
          groupToTotalPositiveAndNegativeWeights_[groupId];
      totalNegativeWeight += minObjectWeight < 0 ? minObjectWeight : 0;
      totalPositiveWeight += maxObjectWeight > 0 ? maxObjectWeight : 0;
    }
  }

  // make sure that the coefficients are non-negative
  if (defaultGroupCoefficient < 0) {
    throw std::runtime_error("Coefficient of each group must be non-negative.");
  }
  for (auto& [_, coeff] : groupCoefficients_) {
    if (coeff < 0) {
      throw std::runtime_error(
          "Coefficient of each group must be non-negative.");
    }
  }

  // Compute group negative limits.
  for (const auto& [groupId, limit] : groupLimits_) {
    if (limit < 0) {
      groupNegativeLimits_.emplace(groupId, limit);
    } else if (limit > 0) {
      groupPositiveLimits_.emplace(groupId, limit);
    }
  }

  for (const auto& [groupId, totalPositiveAndNegativeWeights] :
       groupToTotalPositiveAndNegativeWeights_) {
    auto& [totalPositiveWeight, totalNegativeWeight] =
        totalPositiveAndNegativeWeights;

    auto groupLimit = getGroupLimit(groupId);
    auto groupCoefficient = getGroupCoefficient(groupId);
    const double maxNormalizedDeviationFromLimit =
        (totalPositiveWeight - groupLimit) * groupCoefficient;
    const double minNormalizedDeviationFromLimit =
        (totalNegativeWeight - groupLimit) * groupCoefficient;

    groupToMaxAndMinNormalizedDeviationsFromLimit_.emplace(
        groupId,
        std::make_pair(
            maxNormalizedDeviationFromLimit, minNormalizedDeviationFromLimit));
  }
}

const std::string_view& ObjectPartition::getType() const {
  return type;
}

void ObjectPartition::updateEquivalenceSets(
    EquivalenceSets& equivalenceSets) const {
  partitionInfo_->forEachRelevantGroup([&](const entities::GroupId groupId) {
    equivalenceSets.mappingMerge(partitionInfo_->getPartitionId(), groupId);
  });

  if (dimension_->isDynamic()) {
    for (const auto scopeItemId : *scopeItemIds_) {
      equivalenceSets.mappingMerge(dimensionId_, scopeItemId);
    }
  } else {
    equivalenceSets.mappingMerge(dimensionId_);
  }
}

Bounds ObjectPartition::innerLowerAndUpperBounds(
    Context& /* unused */,
    const BoundConstraints& /* unused */) const {
  throw std::runtime_error("Unsupported function");
}

bool ObjectPartition::shouldComputeBounds() const {
  return false;
}

std::string ObjectPartition::innerDigest(size_t maxChildren) const {
  std::stringstream ss;
  ss << "[obj: groups]";
  const auto& objectIdToGroupIds = partitionInfo_->getObjectIdToGroupIds();
  for (const auto [idx, entry] : folly::enumerate(objectIdToGroupIds)) {
    const auto& [objectId, groupIds] = entry;
    if (idx >= maxChildren) {
      ss << "... " << objectIdToGroupIds.size() - maxChildren << " more";
      break;
    }
    ss << " ";
    ss << fmt::format("{}: {{", universe_->getEntityName(objectId));
    PackerMap<GroupId, int> countPerGroup;
    for (const auto groupId : groupIds) {
      countPerGroup[groupId]++;
    }

    for (const auto& [idx2, entry2] : folly::enumerate(countPerGroup)) {
      const auto& [groupId, groupCount] = entry2;
      if (idx2 > 0) {
        ss << ", ";
      }
      ss << groupId << ":" << groupCount;
    }
    ss << "}";
  }
  return ss.str();
}

ExpressionProperties ObjectPartition::getProperties() const {
  ExpressionProperties properties;
  if (dimension_->isDynamic() && scopeItemIds_.has_value() &&
      scopeItemIds_->size() > 1) {
    // TODO: properly handle the case where there are multiple scope items
    properties.properties()->emplace(
        "object weights",
        PropertiesHelper::makeStringValue(
            "not available when dimension scope does not match util scope"));
  } else {
    const auto& storage = dimension_->isDynamic()
        ? dimension_->values(*scopeItemIds_.value().begin())
        : dimension_->values();
    properties.properties()->emplace(
        "object_weights",
        PropertiesHelper::makeObjectIdDoubleMapValue(storage));
  }
  // TODO: add group_limits once groupId to groupName translation is
  // supported
  return properties;
}

bool ObjectPartition::references(ObjectId object) const {
  return partitionInfo_->getObjectIdToGroupIds().contains(object);
}

double ObjectPartition::getGroupLimit(GroupId groupId) const {
  if (!partitionInfo_->isRelevant(groupId)) {
    throw std::runtime_error(
        fmt::format(
            "Cannot get limit for group {} which is not in filteredGroupIds",
            groupId));
  }
  return folly::get_default(groupLimits_, groupId, defaultGroupLimit_);
}

double ObjectPartition::getGroupCoefficient(GroupId groupId) const {
  if (!partitionInfo_->isRelevant(groupId)) {
    throw std::runtime_error(
        fmt::format(
            "Cannot get coefficient for group {} which is not in filteredGroupIds",
            groupId));
  }
  return folly::get_default(
      groupCoefficients_, groupId, defaultGroupCoefficient_);
}

double ObjectPartition::getObjectWeight(
    ObjectId objectId,
    std::optional<ScopeItemId> scopeItemId) const {
  if (scopeItemId.has_value() && scopeItemIds_.has_value() &&
      !scopeItemIds_->contains(scopeItemId.value())) {
    throw std::runtime_error(
        "The provided scope item is not in the list of acceptable scope items");
  }
  return dimension_->getValue(objectId, scopeItemId);
}

const PackerMap<entities::GroupId, std::pair<double, double>>&
ObjectPartition::getGroupToTotalPositiveAndNegativeWeights() const {
  return groupToTotalPositiveAndNegativeWeights_;
}

const std::pair<double, double>&
ObjectPartition::getTotalPositiveAndNegativeWeightsForGroup(
    GroupId groupId) const {
  return groupToTotalPositiveAndNegativeWeights_.at(groupId);
}

const PackerMap<GroupId, std::pair<double, double>>&
ObjectPartition::getGroupToMaxAndMinNormalizedDeviationsFromLimit() const {
  return groupToMaxAndMinNormalizedDeviationsFromLimit_;
}

const std::vector<GroupId>& ObjectPartition::getObjectGroups(
    ObjectId objectId) const {
  return folly::get_ref_default(
      partitionInfo_->getObjectIdToGroupIds(), objectId, kEmptyGroups);
}

const PackerMap<ObjectId, std::vector<GroupId>>&
ObjectPartition::getObjectGroups() const {
  return partitionInfo_->getObjectIdToGroupIds();
}

// Sometimes we can more efficiently calculate when operating on
// equivalence sets summary of the object partition data instead of
// the full data. On the first call this builds the map which
// should only be called after the equivalence sets are finished
// completed (DO NOT CALL FROM CONSTRUCTOR), then returns the cached
// result every subsequent call.
const PackerMap<GroupId, PackerMap<EquivalenceSetId, int>>&
ObjectPartition::getEquivSetGroups(const EquivalenceSets& equivalenceSets) {
  return equivSetGroups_.getSavedOrCompute([&] {
    PackerMap<GroupId, PackerMap<EquivalenceSetId, int>> equivSetGroups;
    PackerMap<GroupId, int> membershipCounts;
    for (const auto& [object, groups] :
         partitionInfo_->getObjectIdToGroupIds()) {
      membershipCounts.clear();
      for (const auto& group : groups) {
        membershipCounts[group]++;
      }

      for (const auto& [group, count] : membershipCounts) {
        auto& equivSetGroupsForGroup = equivSetGroups[group];
        auto equiv_set = equivalenceSets.at(object);
        if (auto existingCount =
                folly::get_ptr(equivSetGroupsForGroup, equiv_set)) {
          if (count != *existingCount) {
            throw std::runtime_error(
                fmt::format(
                    "Two objects of the same equivalence set have different "
                    "membership counts to a group, one has {} while another has {}",
                    count,
                    *existingCount));
          }
        } else {
          equivSetGroupsForGroup.emplace(equiv_set, count);
        }
      }
    }
    return equivSetGroups;
  });
}

const PackerMap<GroupId, double>& ObjectPartition::getGroupNegativeLimits()
    const {
  return groupNegativeLimits_;
}

const PackerMap<GroupId, double>& ObjectPartition::getGroupPositiveLimits()
    const {
  return groupPositiveLimits_;
}

const entities::PartitionId ObjectPartition::getPartitionId() const {
  return partitionInfo_->getPartitionId();
}

const entities::DimensionId ObjectPartition::getDimensionId() const {
  return dimensionId_;
}

bool ObjectPartition::hasNoLpIntent() const {
  return true;
}

bool ObjectPartition::hasValue() const {
  return false;
}

} // namespace facebook::rebalancer
