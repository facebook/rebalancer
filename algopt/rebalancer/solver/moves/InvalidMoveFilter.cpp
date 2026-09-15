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
#include <folly/container/Enumerate.h>

#include <atomic>
#include <stdexcept>
#include <utility>

namespace facebook::rebalancer {

InvalidMoveFilter::InvalidMoveFilter(size_t numObjects, size_t numContainers)
    : containerToInvalidObjects_(numContainers), numObjects_(numObjects) {}

InvalidMoveFilter::InvalidMoveFilter(const InvalidMoveFilter& other)
    : containerToInvalidObjects_(other.containerToInvalidObjects_.size()),
      numObjects_(other.numObjects_),
      isEmpty_(other.isEmpty_.load(std::memory_order_relaxed)) {
  for (const auto& [containerIndex, otherRow] :
       folly::enumerate(other.containerToInvalidObjects_)) {
    if (otherRow.has_value()) {
      containerToInvalidObjects_.at(containerIndex).try_emplace(*otherRow);
    }
  }
}

InvalidMoveFilter::InvalidMoveFilter(InvalidMoveFilter&& other) noexcept
    : containerToInvalidObjects_(std::move(other.containerToInvalidObjects_)),
      numObjects_(std::exchange(other.numObjects_, 0)),
      isEmpty_(other.isEmpty_.exchange(true, std::memory_order_relaxed)) {}

void InvalidMoveFilter::markInvalid(
    entities::ObjectId objectId,
    entities::ContainerId containerId) {
  containerToInvalidObjects_.at(containerId.asIndex())
      .try_emplace(numObjects_)
      .atomicSet(objectId.asIndex());
  if (isEmpty_.load(std::memory_order_relaxed)) {
    isEmpty_.store(false, std::memory_order_relaxed);
  }
}

void InvalidMoveFilter::mergeFrom(const InvalidMoveFilter& other) {
  if (other.empty()) {
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
  for (const auto& [containerIndex, otherRow] :
       folly::enumerate(other.containerToInvalidObjects_)) {
    if (!otherRow.has_value()) {
      continue;
    }
    auto& row = containerToInvalidObjects_.at(containerIndex);
    row.try_emplace(numObjects_).mergeFrom(*otherRow);
  }
  isEmpty_.store(false, std::memory_order_relaxed);
}

} // namespace facebook::rebalancer
