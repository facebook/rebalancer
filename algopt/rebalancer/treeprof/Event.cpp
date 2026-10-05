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

#include "algopt/rebalancer/treeprof/Event.h"

#include <folly/Synchronized.h>

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace facebook::algopt::treeprof {

Event::Event(std::string name) : name_(std::move(name)) {}

void Event::addChild(std::shared_ptr<const Event> child) {
  children_.wlock()->push_back(child);
}

void Event::addInterval(const Interval& interval) {
  intervals_.wlock()->push_back(interval);
}

void Event::finalize() {
  auto children = children_.rlock();
  auto intervals = intervals_.wlock();
  // A point represents a memory usage increase or decrease at a given time.
  // Format: {time, memory delta}
  std::vector<std::pair<double, int64_t>> points;
  points.reserve(2 * intervals->size() + 2 * children->size());

  // Collect points from intervals.
  for (auto& interval : *intervals) {
    // Pessimistic assumption: the memory peak is reached at the beginning
    // of the interval and maintained until the end.
    // Every interval contributes two observation points, each corresponding
    // to its endpoints. As per our assumption, memory usage increase at
    // T=start is 'interval.peakMemory'. By T=finish, the net change is
    // 'interval.memoryDelta' but relative  to the start point, the change
    // 'interval.memoryDelta - interval.memoryPeak', which is the value we
    // record.
    points.emplace_back(interval.beginTime, interval.memoryPeak);
    points.emplace_back(
        interval.endTime, interval.memoryDelta - interval.memoryPeak);
  }

  // Collect points from children events.
  for (const auto& event : *children) {
    // Pessimistic assumption: the memory peak is reached at the beginning
    // of the event and maintained until the end.
    // Trade-off: here we could process individual intervals of children
    // events for better accuracy at the cost of extra computation.
    points.emplace_back(event->getBeginTime(), event->getMemoryPeak());
    points.emplace_back(
        event->getEndTime(), event->getMemoryDelta() - event->getMemoryPeak());
  }

  if (points.empty()) {
    throw std::runtime_error("empty event");
  }

  // Sort points increasingly by time.
  std::sort(points.begin(), points.end());

  // Store begin and end times.
  beginTime_ = points.front().first;
  endTime_ = points.back().first;

  // Compute the overall memory delta and peak.
  memoryDelta_ = 0;
  memoryPeak_ = 0;
  for (auto& [time, delta] : points) {
    memoryDelta_ += delta;
    memoryPeak_ = std::max(memoryPeak_, memoryDelta_);
  }

  // Drop intervals from memory.
  *intervals = {};

  finalized_ = true;
}

const std::string& Event::getName() const {
  return name_;
}

std::vector<std::shared_ptr<const Event>> Event::getChildren() const {
  return *children_.rlock();
}

double Event::getBeginTime() const {
  checkFinalized();
  return beginTime_;
}

double Event::getEndTime() const {
  checkFinalized();
  return endTime_;
}

double Event::duration() const {
  checkFinalized();
  return endTime_ - beginTime_;
}

int64_t Event::getMemoryDelta() const {
  checkFinalized();
  return memoryDelta_;
}

int64_t Event::getMemoryPeak() const {
  checkFinalized();
  return memoryPeak_;
}

void Event::checkFinalized() const {
  if (!finalized_) {
    throw std::runtime_error("event not finalized");
  }
}

} // namespace facebook::algopt::treeprof
