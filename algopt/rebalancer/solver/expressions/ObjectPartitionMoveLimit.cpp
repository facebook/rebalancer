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

#include "algopt/rebalancer/solver/expressions/ObjectPartitionMoveLimit.h"

#include "algopt/rebalancer/solver/expressions/LpEvaluator.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"

#include <folly/container/Enumerate.h>

#include <algorithm>
#include <sstream>
#include <vector>

namespace {
constexpr std::string_view type = "ObjectPartitionMoveLimit";
}

namespace facebook::rebalancer {

bool ObjectPartitionMoveLimit::Compare::operator()(
    const std::pair<entities::GroupId, double>& lhs,
    const std::pair<entities::GroupId, double>& rhs) const {
  if (lhs.second == rhs.second) {
    return lhs.first < rhs.first;
  }
  return lhs.second > rhs.second;
}

ObjectPartitionMoveLimit::ObjectPartitionMoveLimit(
    const entities::Universe& universe,
    Assignment originalAssignment,
    entities::PartitionId partitionId,
    entities::DimensionId dimensionId,
    /* group_idx -> limit */
    PackerMap<entities::GroupId, double> groupLimits,
    entities::Set<entities::ContainerId> sourceContainerIdsNotAffectingLimit,
    entities::Set<entities::ContainerId>
        destinationContainerIdsNotAffectingLimit)
    : Expression(universe, /*initialValue=*/0.0),
      originalAssignment_(std::move(originalAssignment)),
      partitionId_(partitionId),
      groupLimits_(std::move(groupLimits)),
      sourceContainerIdsNotAffectingLimit_(
          std::move(sourceContainerIdsNotAffectingLimit)),
      destinationContainerIdsNotAffectingLimit_(
          std::move(destinationContainerIdsNotAffectingLimit)),
      containerScopeId_(
          universe_->getScopeId(universe_->getContainerTypeName())) {
  set_directly_affected_containers();
  dimension_ = &universe_->getObjects().getDimension(dimensionId).only();
  partition_ = &universe_->getPartition(partitionId_);
}

const std::string_view& ObjectPartitionMoveLimit::getType() const {
  return type;
}

void ObjectPartitionMoveLimit::updateEquivalenceSets(
    EquivalenceSets& equivalenceSets) const {
  // object belongs to different groups belongs to different set
  equivalenceSets.mappingMerge(partitionId_);
  // two objects that belong to same initial container and have the same weights
  // for all containers are equivalent.
  // Because we are comparing a list of weights across all objects to determine
  // equivalency, it will be expensive when the container size is large, thus
  // eliminating the performance gains from equivalent sets. We need to revisit
  // this logic for large container sizes.
  const auto& objectToGroups = partition_->getObjectIdToGroupIds();
  auto isDynamic = dimension_->isDynamic();
  PackerMap<
      entities::ObjectId,
      std::pair<entities::ContainerId, std::vector<double>>>
      mergeMap;
  for (const auto& [object, _] : objectToGroups) {
    auto sourceContainer = originalAssignment_.getContainer(object);
    std::vector<double> weights;
    if (!isDynamic) {
      weights = {dimension_->getValue(object)};
    } else {
      const auto containersIds = universe_->getContainers().getContainerIds();
      weights.reserve(containersIds.size());
      for (const auto& container : containersIds) {
        weights.push_back(getObjectMoveCost(object, container));
      }
    }

    mergeMap.emplace(object, std::make_pair(sourceContainer, weights));
  }

  equivalenceSets.mappingMerge(mergeMap);
}

void ObjectPartitionMoveLimit::set_directly_affected_containers() {
  auto localDirectlyAffectedContainersPtr =
      std::make_shared<PackerSet<entities::ContainerId>>();
  for (auto container : originalAssignment_.getContainers()) {
    const bool irrelevantContainer =
        sourceContainerIdsNotAffectingLimit_.contains(container) &&
        destinationContainerIdsNotAffectingLimit_.contains(container);
    if (!irrelevantContainer) {
      localDirectlyAffectedContainersPtr->insert(container);
    }
  }
  directlyAffectedContainers.set(localDirectlyAffectedContainersPtr);
}

bool ObjectPartitionMoveLimit::doesMoveAffectLimit(
    entities::ObjectId objectId,
    std::optional<entities::ContainerId> destinationContainerId) const {
  auto initialSourceContainerId = originalAssignment_.getContainer(objectId);
  const auto containerIds = universe_->getContainers().getContainerIds();
  // if no specific destination is provided, then we are checking if there is
  // some move that affects the limit. There is a move that affects the limit if
  // the source container is not in sourceContainerIdsNotAffectingLimit_ and
  // there is at least one destination container that is not in the
  // destinationsNotAffectsLimit
  if (!destinationContainerId.has_value()) {
    return !sourceContainerIdsNotAffectingLimit_.contains(
               initialSourceContainerId) &&
        destinationContainerIdsNotAffectingLimit_.size() != containerIds.size();
  }

  return (initialSourceContainerId != *destinationContainerId) &&
      (!sourceContainerIdsNotAffectingLimit_.contains(
          initialSourceContainerId)) &&
      (!destinationContainerIdsNotAffectingLimit_.contains(
          *destinationContainerId));
}

double ObjectPartitionMoveLimit::moveCost(
    entities::GroupId group,
    double groupDeviation) const {
  return std::max(0.0, groupDeviation - groupLimits_.at(group));
}

double ObjectPartitionMoveLimit::innerFullApply(
    const TopToBottomEvaluator& /* evaluator */,
    const Assignment& assignment) {
  groupDeviations_.clear();
  groupMoveCosts_.clear();
  auto& objectToGroups = partition_->getObjectIdToGroupIds();
  for (auto& [object, destinationContainer] :
       assignment.getObjectToContainerMap()) {
    const bool relevantMove = objectToGroups.contains(object) &&
        doesMoveAffectLimit(object, destinationContainer);
    if (!relevantMove) {
      continue;
    }

    for (auto group : objectToGroups.at(object)) {
      groupDeviations_[group].update(
          object, getObjectMoveCost(object, destinationContainer));
    }
  }
  for (auto& [group, groupDeviation] : groupDeviations_) {
    groupMoveCosts_.assign(group, moveCost(group, groupDeviation.query()));
  }
  value = groupMoveCosts_.size() == 0 ? 0 : groupMoveCosts_.begin()->second;
  return value;
}

double ObjectPartitionMoveLimit::innerPartialApply(
    const BottomToTopEvaluator& /* evaluator */,
    const Assignment& /* assignment */,
    const ChangeSet& changes) {
  PackerSet<entities::GroupId> groupsWithUpdatedDeviations;
  auto& objectToGroups = partition_->getObjectIdToGroupIds();
  for (auto& change : changes) {
    if (change.getValue() != 1) {
      continue;
    }

    auto object = change.getObject();
    if (!objectToGroups.contains(object)) {
      continue;
    }

    for (auto group : objectToGroups.at(object)) {
      auto newObjectContribution =
          getObjectMoveCost(object, change.getContainer());
      groupDeviations_[group].update(object, newObjectContribution);
      groupsWithUpdatedDeviations.emplace(group);
    }
  }

  for (auto group : groupsWithUpdatedDeviations) {
    groupMoveCosts_.assign(
        group, moveCost(group, groupDeviations_[group].query()));
  }

  value = groupMoveCosts_.size() == 0 ? 0 : groupMoveCosts_.begin()->second;
  return value;
}

double ObjectPartitionMoveLimit::evaluate(
    const BottomToTopEvaluator& /* evaluator */,
    const ChangeSet& changes) const {
  PackerMap<entities::GroupId, PackerMap<entities::ObjectId, double>>
      groupToNewObjectContributions;
  auto& objectToGroups = partition_->getObjectIdToGroupIds();
  for (auto& change : changes) {
    if (change.getValue() != 1) {
      continue;
    }

    auto object = change.getObject();
    if (!objectToGroups.contains(object)) {
      continue;
    }

    for (auto group : objectToGroups.at(object)) {
      auto newObjectContribution =
          getObjectMoveCost(object, change.getContainer());
      groupToNewObjectContributions[group][object] = newObjectContribution;
    }
  }

  double maxNewMoveCost = 0;
  for (auto& [group, objectToNewContribution] : groupToNewObjectContributions) {
    auto currDeviationPtr = folly::get_ptr(groupDeviations_, group);
    double totalChangeInDeviation = 0.0;
    for (auto [object, newContribution] : objectToNewContribution) {
      double currContribution = 0.0;
      if (currDeviationPtr) {
        auto valueOpt = currDeviationPtr->getValueIfExists(object);
        if (valueOpt.has_value()) {
          currContribution = valueOpt.value();
        }
      }

      totalChangeInDeviation += (newContribution - currContribution);
    }

    const double currGroupDeviation =
        (currDeviationPtr) ? currDeviationPtr->query() : 0.0;
    maxNewMoveCost = std::max(
        maxNewMoveCost,
        moveCost(group, currGroupDeviation + totalChangeInDeviation));
  }

  // if every group had an update or if the maxNewMoveCost is greater than the
  // existing value, then maxMoveCost is the same as the maxNewMoveCost
  if (maxNewMoveCost >= value ||
      groupToNewObjectContributions.size() == groupLimits_.size()) {
    return maxNewMoveCost;
  }

  double maxMoveCost = maxNewMoveCost;
  for (auto& [groupId, moveCost] : groupMoveCosts_) {
    if (groupToNewObjectContributions.contains(groupId)) {
      // the moveCost for groups that  have new objectContributions is not
      // correct in 'groupMoveCosts_' and these updated costs have been
      // considered while computing maxNewMoveCost
      continue;
    }

    maxMoveCost = std::max(maxMoveCost, moveCost);
  }

  return maxMoveCost;
}

Bounds ObjectPartitionMoveLimit::innerLowerAndUpperBounds(
    Context& /* unused */,
    const BoundConstraints& /* unused */) const {
  Bounds bounds{
      // objects in every group are always assigned to their original assignment
      .lower_bound = 0,
      // objects in every group are not assigned to their original assignment
      // anymore
      .upper_bound = 0};

  const auto& partition = universe_->getPartition(partitionId_);
  for (auto groupId : partition.getGroupIds()) {
    double groupMoveCost = 0;

    // Getting the move cost for a group means summing up the weight of all
    // objects inside it. For static dimensions, every object has a single
    // weight. For dynamic dimensions, we need to find the maximum weight for
    // the object to any container.
    for (auto objectId : partition.getObjectIds(groupId)) {
      if (!dimension_->isDynamic()) {
        groupMoveCost += doesMoveAffectLimit(objectId, std::nullopt)
            ? dimension_->getValue(objectId)
            : 0.0;
      } else {
        // Get the maximum cost for moving this object to all containers;
        double maxObjectMoveCost = 0;
        for (auto destinationContainerId :
             universe_->getContainers().getContainerIds()) {
          maxObjectMoveCost = std::max(
              maxObjectMoveCost,
              getObjectMoveCost(objectId, destinationContainerId));
        }

        groupMoveCost += maxObjectMoveCost;
      }
    }

    bounds.upper_bound = std::max(bounds.upper_bound, groupMoveCost);
  }

  return bounds;
}

std::vector<std::pair<Expression*, double>>
ObjectPartitionMoveLimit::get_sorted_children(bool) const {
  return {};
}

bool ObjectPartitionMoveLimit::shouldComputeDescendingChildPotentials() const {
  return false;
}

std::optional<AffectedByChange> ObjectPartitionMoveLimit::isAffectedByChange(
    const AffectedByChangeDecisionData& /*data*/) const {
  if (directlyAffectedContainers.getNonNullSet().size() <
      originalAssignment_.getContainers().size()) {
    return AffectedByChange(directlyAffectedContainers.getSetPtr());
  }

  return AffectedByChange(true /*affectedByAllChanges*/);
}

std::string ObjectPartitionMoveLimit::innerDigest(size_t maxChildren) const {
  std::stringstream ss;
  ss << "move_limits:";
  // Sort by group name for deterministic output across hash-map orderings and
  // across platforms (raw GroupId values depend on insertion order through
  // unordered maps and are not portable).
  std::vector<std::pair<std::string, double>> sortedEntries;
  sortedEntries.reserve(groupLimits_.size());
  for (const auto& [groupId, limit] : groupLimits_) {
    sortedEntries.emplace_back(universe_->getEntityName(groupId), limit);
  }
  std::sort(sortedEntries.begin(), sortedEntries.end());
  for (const auto [idx, entry] : folly::enumerate(sortedEntries)) {
    const auto& [groupName, limit] = entry;
    if (idx >= maxChildren) {
      ss << " ... " << sortedEntries.size() - maxChildren << " more";
      break;
    }
    ss << (idx > 0 ? ", " : " ") << fmt::format("{}={}", groupName, limit);
  }
  return ss.str();
}

void ObjectPartitionMoveLimit::updateExprForLp() {
  lpProvider_ = const_expr(0, getUniverse());

  /* For static dimensions = (summation of weight * (1-variable)) - limit <= 0
   */
  /* For dynamic dimensions = (summation of for every c weight(o, c) * variable)
   * - limit <= 0 */
  const auto& partition = universe_->getPartition(partitionId_);
  for (auto groupId : partition.getGroupIds()) {
    auto expr = const_expr(0, getUniverse());
    for (auto objectId : partition.getObjectIds(groupId)) {
      auto sourceContainerId = originalAssignment_.getContainer(objectId);
      if (!dimension_->isDynamic() &&
          !sourceContainerIdsNotAffectingLimit_.contains(sourceContainerId) &&
          destinationContainerIdsNotAffectingLimit_.empty()) {
        expr += dimension_->getValue(objectId) *
            (1 -
             variable(
                 objectId,
                 sourceContainerId,
                 getUniverse(),
                 originalAssignment_));
      } else {
        for (auto destinationContainerId :
             universe_->getContainers().getContainerIds()) {
          expr +=
              (getObjectMoveCost(objectId, destinationContainerId) *
               variable(
                   objectId,
                   destinationContainerId,
                   getUniverse(),
                   originalAssignment_));
        }
      }
    }

    expr -= groupLimits_.at(groupId);
    inplace_max(lpProvider_, expr);
  }
}

void ObjectPartitionMoveLimit::lpIntent(
    const LpEvaluator& evaluator,
    bool minimizing) {
  if (!lpProvider_) {
    updateExprForLp();
  }
  return evaluator.computeLpIntent(lpProvider_, minimizing);
}

algopt::lp::Expression ObjectPartitionMoveLimit::lp(
    const LpEvaluator& evaluator,
    bool minimizing,
    const interface::OptimalSolverSpec& configs) {
  if (!lpProvider_) {
    updateExprForLp();
  }
  return evaluator.lp(lpProvider_.get(), minimizing, configs);
}

double ObjectPartitionMoveLimit::getObjectMoveCost(
    entities::ObjectId objectId,
    entities::ContainerId destinationContainerId) const {
  if (!doesMoveAffectLimit(objectId, destinationContainerId)) {
    return 0;
  }

  if (!dimension_->isDynamic()) {
    return dimension_->getValue(objectId);
  }

  // If the dimension is dynamic, then the object has different  values in
  // source and destination containers. For now, we say that the cost of the
  // move is max(dimensionValue in source, dimensionValue in destination). It's
  // possible that sometimes this may not be the case, in which case this
  // needsto be made configurable. to be made configurable.
  return std::max(
      getObjectDimensionValueInContainer(
          objectId, originalAssignment_.getContainer(objectId)),
      getObjectDimensionValueInContainer(objectId, destinationContainerId));
}

double ObjectPartitionMoveLimit::getObjectDimensionValueInContainer(
    entities::ObjectId objectId,
    entities::ContainerId containerId) const {
  auto containerScopeItemId = universe_->getScopeItemId(
      containerScopeId_, universe_->getEntityName(containerId));
  return dimension_->getValue(objectId, containerScopeItemId);
}

} // namespace facebook::rebalancer
