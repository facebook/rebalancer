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

#include <folly/Synchronized.h>

#include <memory>
#include <optional>
#include <stdint.h>
#include <string>
#include <vector>

namespace facebook::algopt::treeprof {

struct Interval {
  double beginTime;
  double endTime;
  int64_t memoryDelta;
  int64_t memoryPeak;
};

class Event {
 public:
  explicit Event(std::string name);
  void addChild(std::shared_ptr<const Event> child);
  void addInterval(const Interval& interval);
  void finalize();

  const std::string& getName() const;
  std::vector<std::shared_ptr<const Event>> getChildren() const;
  double getBeginTime() const;
  double getEndTime() const;
  double duration() const;
  int64_t getMemoryDelta() const;
  int64_t getMemoryPeak() const;

 private:
  void checkFinalized() const;

 private:
  std::string name_;
  folly::Synchronized<std::vector<std::shared_ptr<const Event>>> children_;
  folly::Synchronized<std::vector<Interval>> intervals_;
  double beginTime_ = 0;
  double endTime_ = 0;
  // The values 'memoryDelta_' and 'memoryPeak_' respectively capture the change
  // in allocated memory and change in peak memory since this event started.
  // We compute these values by aggregating the same 2 metrics for a series of
  // disjoint intervals which cover the entire event, breaking it at interesting
  // points: when a child event begins or finishes, and when a coroutine starts
  // or finishes running on a different thread.
  int64_t memoryDelta_ = 0;
  int64_t memoryPeak_ = 0;
  bool finalized_ = false;
};

} // namespace facebook::algopt::treeprof
