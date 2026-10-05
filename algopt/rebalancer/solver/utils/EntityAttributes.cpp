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

#include "algopt/rebalancer/solver/utils/EntityAttributes.h"

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Partition.h"
#include "algopt/rebalancer/entities/Scope.h"
#include "algopt/rebalancer/entities/Set.h"
#include "algopt/rebalancer/solver/utils/Change.h"
#include "algopt/rebalancer/solver/utils/ChangeSet.h"

#include "multifeed/hash/HeterogeneousAccess.h"
#include "multifeed/hash/QuickHashMap.h"

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <fmt/format.h>
#include <folly/container/MapUtil.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace facebook::rebalancer {
namespace {

entities::GroupId getOnlyGroupId(
    const entities::Partition& partition,
    entities::ObjectId objectId) {
  auto& groupIds = partition.getObjectIdToGroupIds().at(objectId);
  if (groupIds.size() != 1) {
    // since the partition is disjoint, there should be only one group id
    throw std::runtime_error(
        fmt::format(
            "Expected exactly one group id for object {}, but got {}",
            objectId,
            groupIds.size()));
  }
  return *groupIds.begin();
}

entities::Set<entities::ScopeItemId> emptyScopeItemSet = {};

} // namespace

void EntityAttributes::updateOnApply(const ChangeSet& changes) {
  for (auto& change : changes) {
    if (change.getValue() == 1) {
      setOn(change.getObject(), change.getContainer());
    } else if (change.getValue() == -1) {
      removeFrom(change.getObject(), change.getContainer());
    } else {
      throw std::runtime_error(
          fmt::format("Invalid change {}", change.toString()));
    }
  }
}

AllowedScopeItemsPerGroup::AllowedScopeItemsPerGroup(
    const entities::Scope& scope,
    const entities::Partition& partition,
    std::function<size_t(entities::GroupId, entities::ScopeItemId)> evaluate)
    : scope_(scope), partition_(partition), evaluate_(std::move(evaluate)) {
  for (auto groupId : partition_.getGroupIds()) {
    recomputeAllowedScopeItemsForGroup(groupId);
  }
}

void AllowedScopeItemsPerGroup::recomputeAllowedScopeItemsForGroup(
    entities::GroupId groupId) {
  // Clear existing counts for this group
  auto& groupCounts = allowedCounts_[groupId];
  groupCounts.clear();

  for (auto scopeItemId : scope_.getScopeItemIds()) {
    const auto count = evaluate_(groupId, scopeItemId);
    if (count > 0) {
      groupCounts[scopeItemId] = count;
    }
  }
}

void AllowedScopeItemsPerGroup::setOn(
    entities::ObjectId objectId,
    entities::ContainerId /*dstContainer*/) {
  auto groupId = getOnlyGroupId(partition_, objectId);
  recomputeAllowedScopeItemsForGroup(groupId);
}

void AllowedScopeItemsPerGroup::removeFrom(
    entities::ObjectId objectId,
    entities::ContainerId /*srcContainer*/) {
  auto groupId = getOnlyGroupId(partition_, objectId);
  recomputeAllowedScopeItemsForGroup(groupId);
}

std::optional<size_t> AllowedScopeItemsPerGroup::getAllowedCount(
    entities::GroupId groupId,
    entities::ScopeItemId scopeItemId) const {
  auto allowedScopeItemsForGroupPtr = folly::get_ptr(allowedCounts_, groupId);
  if (!allowedScopeItemsForGroupPtr || allowedScopeItemsForGroupPtr->empty()) {
    // Group not found - server has no allowed scope items or
    // Group found but has no allowed scope items (exhausted)
    return std::nullopt;
  }
  return folly::get_default(*allowedScopeItemsForGroupPtr, scopeItemId, 0);
}

} // namespace facebook::rebalancer
