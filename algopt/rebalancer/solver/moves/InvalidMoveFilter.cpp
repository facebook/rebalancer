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

#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"

#include <fmt/core.h>

#include <stdexcept>

namespace facebook::rebalancer {

InvalidMoveFilter::InvalidMoveFilter(size_t numObjects, size_t numContainers)
    : containerToInvalidObjects_(numContainers), numObjects_(numObjects) {}

void InvalidMoveFilter::markInvalid(
    entities::ObjectId objectId,
    entities::ContainerId containerId) {
  auto& row = containerToInvalidObjects_[containerId.asIndex()];
  if (!row.has_value()) {
    row.emplace(numObjects_);
  }
  row->set(objectId.asIndex());
  isEmpty_ = false;
}

void InvalidMoveFilter::mergeFrom(const InvalidMoveFilter& other) {
  if (other.isEmpty_) {
    return;
  }
  if (other.containerToInvalidObjects_.size() !=
          containerToInvalidObjects_.size() ||
      other.numObjects_ != numObjects_) {
    throw std::invalid_argument(
        fmt::format(
            "InvalidMoveFilter::mergeFrom dimension mismatch: this={}c/{}o vs other={}c/{}o",
            containerToInvalidObjects_.size(),
            numObjects_,
            other.containerToInvalidObjects_.size(),
            other.numObjects_));
  }
  for (size_t c = 0; c < containerToInvalidObjects_.size(); ++c) {
    const auto& otherRow = other.containerToInvalidObjects_[c];
    if (!otherRow.has_value()) {
      continue;
    }
    auto& row = containerToInvalidObjects_[c];
    if (!row.has_value()) {
      row.emplace(numObjects_);
    }
    otherRow->forEachSetBit(
        [&](std::size_t objectIndex) { row->set(objectIndex); });
  }
  isEmpty_ = false;
}

bool InvalidMoveFilter::empty() const {
  return isEmpty_;
}

} // namespace facebook::rebalancer
