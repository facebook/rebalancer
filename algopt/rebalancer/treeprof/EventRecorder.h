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

#include "algopt/rebalancer/treeprof/Event.h"
#include "algopt/rebalancer/treeprof/EventHolder.h"

#include <folly/io/async/Request.h>

#include <memory>
#include <optional>
#include <stdint.h>
#include <string>

namespace facebook::algopt::treeprof {

class EventRecorder {
 public:
  explicit EventRecorder(std::string name, bool autoStart = true);
  ~EventRecorder();
  EventRecorder(const EventRecorder&) = delete;
  EventRecorder& operator=(const EventRecorder&) = delete;
  EventRecorder(EventRecorder&&) = delete;
  EventRecorder& operator=(EventRecorder&&) = delete;

  void start();
  void stop();

  // returns the underlying error message if the event recording was not
  // successful, otherwise returns nullopt
  std::optional<std::string> errorMessage() const;

  // should be called only after stop() is called
  double durationInSeconds() const;
  int64_t getMemoryPeak() const;
  // returns the memory usage info namely peak, delta of the recorded event as a
  // human readable string (useful for debugging)
  std::string getMemoryUsageInfo() const;

 private:
  void checkIfDone() const;
  std::shared_ptr<folly::RequestContext> previous_ = nullptr;
  std::shared_ptr<Event> event_ = nullptr;
  std::shared_ptr<Event> parent_ = nullptr;
  EventHolder::NowFn nowFn_;
  bool done_ = false;
  std::optional<std::string> errorMsg_ = std::nullopt;
  std::string name_;
  bool started_ = false;
};

} // namespace facebook::algopt::treeprof
