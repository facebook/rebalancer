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

#include "algopt/rebalancer/algopt_common/DynamicBitSet.h"
#include "algopt/rebalancer/entities/Identifiers.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace facebook::rebalancer {

class InvalidMoveFilter {
 public:
  InvalidMoveFilter(size_t numObjects, size_t numContainers);

  void markInvalid(
      entities::ObjectId objectId,
      entities::ContainerId containerId);

  // Union `other` into this filter: afterwards a pair is invalid if it was
  // invalid in either filter. Both must have the same object/container
  // dimensions.
  void mergeFrom(const InvalidMoveFilter& other);

  bool isMarkedInvalid(
      entities::ObjectId objectId,
      entities::ContainerId containerId) const {
    if (isEmpty_) {
      return false;
    }
    const auto& row = containerToInvalidObjects_[containerId.asIndex()];
    return row.has_value() && row->isSet(objectId.asIndex());
  }

  bool empty() const;

 private:
  std::vector<std::optional<algopt::DynamicBitSet>> containerToInvalidObjects_;
  size_t numObjects_{0};
  bool isEmpty_{true};
};

// Returns true if any object in `objectBundle` is marked invalid for
// `destContainer`. Returns false if `filter` is null.
inline bool anyMoveInvalid(
    const InvalidMoveFilter* filter,
    const std::vector<entities::ObjectId>& objectBundle,
    entities::ContainerId destContainer) {
  if (filter == nullptr) {
    return false;
  }
  return std::any_of(
      objectBundle.begin(), objectBundle.end(), [&](auto objectId) {
        return filter->isMarkedInvalid(objectId, destContainer);
      });
}

} // namespace facebook::rebalancer
