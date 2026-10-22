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
    std::function<void(entities::GroupId)> onGroupExhausted,
    std::shared_ptr<folly::Executor> executor)
    : scope_(scope),
      partition_(partition),
      evaluate_(std::move(evaluate)),
      onGroupExhausted_(std::move(onGroupExhausted)) {
  if (!executor) {
    throw std::invalid_argument("AllowedScopeItemsPerGroup needs an executor");
  }
  const auto& groupIds = partition_.getGroupIds();
  folly::coro::blockingWait(
      CoroUtils::runEachFuncAndUpdate(
          groupIds.begin(),
          groupIds.end(),
          [this](const auto& it) { return evaluateAllowedScopeItems(*it); },
          [this](auto&& groupCounts, const auto& it) {
            allowedCounts_[*it] = std::move(groupCounts);
          },
          std::move(executor)));
  for (const auto& [groupId, groupCounts] : allowedCounts_) {
    notifyIfExhausted(groupId, groupCounts);
  }
}

void AllowedScopeItemsPerGroup::recomputeAllowedScopeItemsForGroup(
    entities::GroupId groupId) {
  const auto& groupCounts = allowedCounts_[groupId] =
      evaluateAllowedScopeItems(groupId);
  notifyIfExhausted(groupId, groupCounts);
}

void AllowedScopeItemsPerGroup::notifyIfExhausted(
    entities::GroupId groupId,
    const entities::Map<entities::ScopeItemId, size_t>& groupCounts) const {
  if (groupCounts.empty()) {
    onGroupExhausted_(groupId);
  }
}

entities::Map<entities::ScopeItemId, size_t>
AllowedScopeItemsPerGroup::evaluateAllowedScopeItems(
    entities::GroupId groupId) const {
  entities::Map<entities::ScopeItemId, size_t> groupCounts;
  for (auto scopeItemId : scope_.getScopeItemIds()) {
    const auto count = evaluate_(groupId, scopeItemId);
    if (count > 0) {
      groupCounts[scopeItemId] = count;
    }
  }
  return groupCounts;
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
