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

#include "algopt/rebalancer/solver/profilers/LocalSearchProfiler.h"

#include <folly/container/irange.h>
#include <folly/logging/xlog.h>

using namespace facebook::rebalancer::interface;

namespace facebook::rebalancer {

LocalSearchProfiler::LocalSearchProfiler(
    const std::vector<std::shared_ptr<MoveType>>& moves,
    std::vector<double> initialValue)
    : currentValue_(std::move(initialValue)) {
  folly::F14FastSet<std::string> seen;
  std::vector<std::string> moveTypeNames;
  seen.reserve(moves.size());
  moveTypeNames.reserve(moves.size());
  for (const auto& move : moves) {
    const auto& name = move->name();
    auto [_, inserted] = seen.insert(name);
    if (inserted) {
      moveTypeNames.push_back(name);
    }
  }
  init(moveTypeNames);
}

LocalSearchProfiler::LocalSearchProfiler(
    const std::vector<std::string>& moveTypeNames,
    std::vector<double> initialValue)
    : currentValue_(std::move(initialValue)) {
  init(moveTypeNames);
}

void LocalSearchProfiler::init(const std::vector<std::string>& moveTypeNames) {
  profiles_.resize(currentValue_.size());
  for (auto& profile : profiles_) {
    profile.moveTypeNames() = moveTypeNames;
  }

  for (const auto i : folly::irange(moveTypeNames.size())) {
    auto insertSuccess =
        moveTypeNameToIndex_.emplace(moveTypeNames.at(i), i).second;
    if (!insertSuccess) {
      XLOG(ERR) << fmt::format(
          "move type '{}' appears more than once in the moves list",
          moveTypeNames.at(i));
    }
  }
}

void LocalSearchProfiler::add(
    const std::vector<interface::MoveTypeEvent>& events) {
  if (events.size() != profiles_.size()) {
    throw std::runtime_error("event count mismatch");
  }

  for (const auto i : folly::irange(events.size())) {
    add(events.at(i), currentValue_.at(i), profiles_.at(i));
  }
}

const std::vector<double>& LocalSearchProfiler::getCurrentValue() const {
  return currentValue_;
}

std::vector<LocalSearchProfile> LocalSearchProfiler::takeProfiles() {
  return std::move(profiles_);
}

int LocalSearchProfiler::getMoveTypeIndex(
    const std::string& moveTypeName) const {
  auto indexPtr = folly::get_ptr(moveTypeNameToIndex_, moveTypeName);
  if (!indexPtr) {
    throw std::runtime_error(fmt::format("unknown move type {}", moveTypeName));
  }

  return *indexPtr;
}

void LocalSearchProfiler::add(
    const interface::MoveTypeEvent& event,
    double& currentValue,
    interface::LocalSearchProfile& profile) {
  auto& events = *profile.moveTypeEvents();

  // Group events with the same move type in a plateau into a single event.
  if (currentValue == *event.finalValue()) {
    // Traverse the other events from most to least recent.
    for (int i = events.size() - 1; i >= 0; --i) {
      auto& other = events.at(i);
      if (*other.initialValue() != currentValue) {
        // The other event is outside of the current plateau.
        break;
      }
      if (*other.moveTypeIndex() == *event.moveTypeIndex()) {
        // Merge event into the other with the same move type in the plateau.
        *other.duration() += *event.duration();
        return;
      }
    }
  }

  currentValue = *event.finalValue();
  events.push_back(event);
}

MoveTypeProfiler::MoveTypeProfiler(
    LocalSearchProfiler& profiler,
    const std::string& moveTypeName)
    : timer_(true), profiler_(profiler) {
  auto& currentValue = profiler_.getCurrentValue();

  events_.resize(currentValue.size());

  for (const auto i : folly::irange(events_.size())) {
    auto& event = events_.at(i);
    event.moveTypeIndex() = profiler.getMoveTypeIndex(moveTypeName);
    event.initialValue() = currentValue.at(i);
    event.finalValue() = currentValue.at(i);
  }
}

MoveTypeProfiler::~MoveTypeProfiler() {
  const auto now = timer_.getSeconds();
  for (auto& event : events_) {
    event.duration() = now;
  }
  profiler_.add(events_);
}

void MoveTypeProfiler::updateValue(const std::vector<double>& values) {
  if (values.size() != events_.size()) {
    throw std::runtime_error("value count mismatch");
  }
  for (const auto i : folly::irange(values.size())) {
    events_.at(i).finalValue() = values.at(i);
  }
}

} // namespace facebook::rebalancer
