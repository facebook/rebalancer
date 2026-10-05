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

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Set.h"
#include "algopt/rebalancer/solver/utils/Change.h"

#include <functional>
#include <memory>

namespace facebook::rebalancer {

enum class AffectedByChangeType {
  ALL_CHANGES,
  CONTAINERS_ONLY,
  OBJECTS_ONLY,
  ALL_GIVEN_CONTAINER_OBJECT_PAIRS
};

// Global data which is used to decide the AffectedByChangeType
struct AffectedByChangeDecisionData {
  explicit AffectedByChangeDecisionData(
      int _numTotalObjects,
      int _numTotalContainers)
      : numTotalObjects(_numTotalObjects),
        numTotalContainers(_numTotalContainers) {}
  int numTotalObjects = 0;
  int numTotalContainers = 0;
};

// Filter for skipping irrelevant leaves during change propagation.
using ChangeFilterFn = std::function<bool(const Change&)>;

class AffectedByChange {
 public:
  // used to indicate whether the node is affected by all changes
  explicit AffectedByChange(bool affectedByAllChanges);

  // used to indicate whether the node is affected by only a list of
  // containers, with an optional change filter
  explicit AffectedByChange(
      std::shared_ptr<const entities::Set<entities::ContainerId>> containersPtr,
      ChangeFilterFn changeFilter = nullptr);

  // used to indicate whether the node is affected by only a list of objects,
  // with an optional change filter
  explicit AffectedByChange(
      std::shared_ptr<const entities::Set<entities::ObjectId>> objectsPtr,
      ChangeFilterFn changeFilter = nullptr);

  // used to indicate whether the node is affected by all pairs of objects and
  // containers from the given lists
  explicit AffectedByChange(
      std::shared_ptr<const entities::Set<entities::ContainerId>> containersPtr,
      std::shared_ptr<const entities::Set<entities::ObjectId>> objectsPtr);

  explicit AffectedByChange(
      std::shared_ptr<const entities::Set<entities::ContainerId>> containersPtr,
      std::shared_ptr<const entities::Set<entities::ObjectId>> objectsPt,
      const AffectedByChangeDecisionData& data);

  AffectedByChangeType getType() const;

  const entities::Set<entities::ContainerId>& getContainers() const;

  const entities::Set<entities::ObjectId>& getObjects() const;

  const ChangeFilterFn& getFilter() const {
    return filter_;
  }

 private:
  std::shared_ptr<const entities::Set<entities::ContainerId>> containersPtr_ =
      nullptr;
  std::shared_ptr<const entities::Set<entities::ObjectId>> objectsPtr_ =
      nullptr;
  ChangeFilterFn filter_;
  bool affectedByAllChanges_ = false;
};

} // namespace facebook::rebalancer
