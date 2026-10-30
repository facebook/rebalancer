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

#include "algopt/rebalancer/common/CoroUtils.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Partition.h"
#include "algopt/rebalancer/entities/Scope.h"
#include "algopt/rebalancer/solver/utils/Change.h"
#include "algopt/rebalancer/solver/utils/ChangeSet.h"

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
    std::function<size_t(entities::GroupId, entities::ScopeItemId)> evaluate,
    std::shared_ptr<folly::Executor> executor)
    : scope_(scope), partition_(partition), evaluate_(std::move(evaluate)) {
  if (!executor) {
    throw std::invalid_argument("AllowedScopeItemsPerGroup needs an executor");
  }
  const auto& scopeItemIds = scope_.getScopeItemIds();
  folly::coro::blockingWait(
      CoroUtils::runEachFuncAndUpdate(
          scopeItemIds.begin(),
          scopeItemIds.end(),
          [this](const auto& it) { return evaluateAllowedGroups(*it); },
          [this](auto&& groupToCount, const auto& it) {
            scopeItemToGroupToCount_[*it] = std::move(groupToCount);
          },
          std::move(executor)));
}

void AllowedScopeItemsPerGroup::recomputeAllowedScopeItemsForGroup(
    entities::GroupId groupId) {
  for (const auto scopeItemId : scope_.getScopeItemIds()) {
    auto& groupToCount = scopeItemToGroupToCount_[scopeItemId];
    if (const auto count = evaluate_(groupId, scopeItemId); count > 0) {
      groupToCount[groupId] = count;
    } else {
      groupToCount.erase(groupId);
    }
  }
}

entities::Map<entities::GroupId, size_t>
AllowedScopeItemsPerGroup::evaluateAllowedGroups(
    entities::ScopeItemId scopeItemId) const {
  entities::Map<entities::GroupId, size_t> groupToCount;
  for (const auto groupId : partition_.getGroupIds()) {
    if (const auto count = evaluate_(groupId, scopeItemId); count > 0) {
      groupToCount[groupId] = count;
    }
  }
  return groupToCount;
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

const entities::Map<entities::GroupId, size_t>&
AllowedScopeItemsPerGroup::getGroupsWithNonZeroCount(
    entities::ScopeItemId scopeItemId) const {
  static const entities::Map<entities::GroupId, size_t> kEmptyGroupToCount;
  return folly::get_ref_default(
      scopeItemToGroupToCount_, scopeItemId, kEmptyGroupToCount);
}

} // namespace facebook::rebalancer
