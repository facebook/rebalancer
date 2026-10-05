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

#include <functional>
#include <memory>
#include <optional>
#include <stddef.h>
#include <utility>
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
  explicit AllowedScopeItemsPerGroup(
      const entities::Scope& scope,
      const entities::Partition& partition,
      std::function<size_t(entities::GroupId, entities::ScopeItemId)> evaluate);

  // Returns the count of objects that can be moved from groupId to scopeItemId
  // Returns nullopt if no object of groupId can be moved to scopeItemId
  // Returns 0 if groupId has allowed scope items but not for this scopeItemId
  std::optional<size_t> getAllowedCount(
      entities::GroupId groupId,
      entities::ScopeItemId scopeItemId) const;

 private:
  virtual void setOn(
      entities::ObjectId objectId,
      entities::ContainerId dstContainer) override;

  virtual void removeFrom(
      entities::ObjectId objectId,
      entities::ContainerId srcContainer) override;

  void recomputeAllowedScopeItemsForGroup(entities::GroupId groupId);
  const entities::Scope& scope_;
  const entities::Partition& partition_;
  std::function<size_t(entities::GroupId, entities::ScopeItemId)> evaluate_;
  // Store counts per group per scope item (items with count > 0)
  entities::Map<entities::GroupId, entities::Map<entities::ScopeItemId, size_t>>
      allowedCounts_;
};

} // namespace facebook::rebalancer
