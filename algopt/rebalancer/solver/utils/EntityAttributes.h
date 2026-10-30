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

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/Partition.h"
#include "algopt/rebalancer/entities/Scope.h"

#include <folly/executors/CPUThreadPoolExecutor.h>

#include <functional>
#include <memory>
#include <stddef.h>
#pragma once

namespace facebook::rebalancer {

class ChangeSet;

// Maintains a mapping of entities (could be objectId, containerId, groupId etc)
// to a "dynamic" attribute (a real number, map, set etc) that depends on the
// assignment. Recall that assignment is updated by moving objects from
// srcContainer to dstContainer. These attributes are updated when that happens.
class EntityAttributes {
 public:
  virtual ~EntityAttributes() = default;

  // update the entity attributes when the changes have been applied to the
  // assignment
  void updateOnApply(const ChangeSet& changes);

 protected:
  // update attribute when an objectId is moved to dstContainer
  virtual void setOn(
      entities::ObjectId objectId,
      entities::ContainerId dstContainer) = 0;

  // update attribute when an objectId is moved from srcContainer
  virtual void removeFrom(
      entities::ObjectId objectId,
      entities::ContainerId srcContainer) = 0;
};

// This attribute maintains a dynamic map of allowed scope items per group
// along with the count of objects that can be moved without
// violating constraints. The caller must provide a function to evaluate
// how many objects from a group can be moved to a scopeitem.
class AllowedScopeItemsPerGroup : public EntityAttributes {
 public:
  // Evaluates the initial counts in parallel on executor
  explicit AllowedScopeItemsPerGroup(
      const entities::Scope& scope,
      const entities::Partition& partition,
      std::function<size_t(entities::GroupId, entities::ScopeItemId)> evaluate,
      std::shared_ptr<folly::Executor> executor);

  // Returns group -> allowed count for scopeItemId
  const entities::Map<entities::GroupId, size_t>& getGroupsWithNonZeroCount(
      entities::ScopeItemId scopeItemId) const;

 private:
  virtual void setOn(
      entities::ObjectId objectId,
      entities::ContainerId dstContainer) override;

  virtual void removeFrom(
      entities::ObjectId objectId,
      entities::ContainerId srcContainer) override;

  void recomputeAllowedScopeItemsForGroup(entities::GroupId groupId);
  entities::Map<entities::GroupId, size_t> evaluateAllowedGroups(
      entities::ScopeItemId scopeItemId) const;
  const entities::Scope& scope_;
  const entities::Partition& partition_;
  std::function<size_t(entities::GroupId, entities::ScopeItemId)> evaluate_;
  // Only positive counts are stored
  entities::Map<entities::ScopeItemId, entities::Map<entities::GroupId, size_t>>
      scopeItemToGroupToCount_;
};

} // namespace facebook::rebalancer
