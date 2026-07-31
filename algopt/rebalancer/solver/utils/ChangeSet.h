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
#include "algopt/rebalancer/solver/utils/Change.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <folly/container/MapUtil.h>

#include <ranges>
#include <vector>

namespace facebook::rebalancer {

class ChangeSet {
 public:
  ChangeSet();
  explicit ChangeSet(std::vector<Change> changes);

  void insert(const Change& change);

  size_t size() const;
  const Change& at(size_t index) const;
  // Returns a ChangeSet that undoes these changes.
  ChangeSet getInverse() const;
  std::vector<Change>::const_iterator begin() const;
  std::vector<Change>::const_iterator end() const;
  bool empty() const;
  const std::vector<Change>& getChangesByContainer(
      entities::ContainerId containerId) const;
  const std::vector<Change>& getChangesByObject(
      entities::ObjectId objectId) const;

  template <typename T>
  size_t getChangeSizeForContainers(const T& containerIds) const {
    size_t size = 0;
    for (const auto containerId : containerIds) {
      const auto& change =
          folly::get_ref_default(containerToChanges_, containerId, empty_);
      size += change.size();
    }

    return size;
  }

  template <typename T>
  size_t getChangeSizeForObjects(const T& objectsToValue) const {
    size_t size = 0;
    algopt::utils::forEachNonDefault(
        objectsToValue, [&](const auto objectId, const auto& /*value*/) {
          const auto& change =
              folly::get_ref_default(objectToChanges_, objectId, empty_);
          size += change.size();
        });
    return size;
  }

  inline auto getObjectsInChangeSet() const {
    return std::views::keys(objectToChanges_);
  }

  inline auto getContainersInChangeSet() const {
    return std::views::keys(containerToChanges_);
  }

  inline const PackerMap<entities::ContainerId, std::vector<Change>>&
  getContainerToChanges() const {
    return containerToChanges_;
  }

  inline bool representsSingleMove() const {
    // A changeSet represents a single move if it is w.r.t. a single object
    // that moves from a source to destination container. In other words, such a
    // changeSet will have 2 changes, their sum of changes will be 0, and only 1
    // object and 2 containers will be affected by the changes
    return (
        changes_.size() == 2 && objectToChanges_.size() == 1 &&
        containerToChanges_.size() == 2 &&
        changes_[0].getValue() + changes_[1].getValue() == 0);
  }

 private:
  std::vector<Change> changes_;
  PackerMap<entities::ContainerId, std::vector<Change>> containerToChanges_;
  std::vector<Change> empty_;
  PackerMap<entities::ObjectId, std::vector<Change>> objectToChanges_;
};

} // namespace facebook::rebalancer
