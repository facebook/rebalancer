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

#include "algopt/rebalancer/treeprof/EventRecorder.h"

#include "algopt/rebalancer/treeprof/Event.h"
#include "algopt/rebalancer/treeprof/EventHolder.h"
#include "algopt/rebalancer/treeprof/ThreadMemoryMonitor.h"

#include <fmt/core.h>
#include <folly/io/async/Request.h>
#include <folly/String.h>

#include <limits>
#include <stdexcept>
#include <utility>

namespace facebook::algopt::treeprof {
void updateEvent(Event& event, const EventHolder::NowFn& nowFn) {
  const int64_t peak = ThreadMemoryMonitor::peak();
  const int64_t delta = ThreadMemoryMonitor::delta();
  const double lastResetTime = ThreadMemoryMonitor::lastResetTime();
  const double now = nowFn();
  ThreadMemoryMonitor::reset(now);

  // On a fresh thread (lastResetTime == 0), anchor the interval at `now` so it
  // doesn't span from the unix epoch.
  event.addInterval(
      Interval{
          .beginTime = lastResetTime == 0 ? now : lastResetTime,
          .endTime = now,
          .memoryDelta = delta,
          .memoryPeak = peak});
}

EventRecorder::EventRecorder(std::string name, bool autoStart) {
  name_ = std::move(name);
  if (autoStart) {
    start();
  }
}

std::optional<std::string> EventRecorder::errorMessage() const {
  return errorMsg_;
}

void EventRecorder::start() {
  if (started_) {
    return;
  }

  auto context = folly::RequestContext::try_get();
  if (context == nullptr) {
    // RequestContext not set, ignore event and do nothing
    errorMsg_ = "RequestContext not set";
    return;
  }
  auto holder = static_cast<const EventHolder*>(
      context->getContextData(EventHolder::key()));

  if (holder == nullptr) {
    errorMsg_ = "EventRecorder must be created inside the scope of a profiler";
    return;
  }

  parent_ = holder->get();
  if (parent_ == nullptr) {
    errorMsg_ = "EventRecorder is missing a parent event";
    return;
  }
  nowFn_ = holder->nowFn();
  // Everything is ok at this point, we go ahead and change the context.
  previous_ = folly::RequestContext::setShallowCopyContext();
  event_ = std::make_shared<Event>(name_);
  updateEvent(*parent_, nowFn_);
  folly::RequestContext::get()->overwriteContextData(
      EventHolder::key(), std::make_unique<EventHolder>(event_, nowFn_));
  started_ = true;
}

void EventRecorder::stop() {
  if (event_ && !done_) {
    updateEvent(*event_, nowFn_);
    event_->finalize();
    parent_->addChild(event_);
    folly::RequestContext::setContext(std::move(previous_));
    done_ = true;
  }
}

double EventRecorder::durationInSeconds() const {
  if (!event_) {
    // if a profiler was not initialized (such as from tests and benchmarks),
    // events won't be recorded. Return infinity to indicate that duration is
    // inaccurate
    return std::numeric_limits<double>::infinity();
  }
  checkIfDone();
  return event_->duration();
}

int64_t EventRecorder::getMemoryPeak() const {
  if (!event_) {
    return 0;
  }
  checkIfDone();
  return event_->getMemoryPeak();
}

void EventRecorder::checkIfDone() const {
  if (!done_) {
    throw std::runtime_error(
        fmt::format("Event {} is yet to finish", event_->getName()));
  }
}

std::string EventRecorder::getMemoryUsageInfo() const {
  if (!event_) {
    // if a profiler was not initialized (such as from tests and benchmarks),
    // events won't be recorded. Return infinity to indicate that duration is
    // inaccurate
    return "Unavailable";
  }
  checkIfDone();
  // dynamically choses the right unit KBs, MBs, GBs, etc.
  auto makeHumanReadable = [](int64_t numBytes) {
    return folly::prettyPrint(numBytes, folly::PrettyType::PRETTY_BYTES);
  };
  return fmt::format(
      "Peak: {}, Delta: {}",
      makeHumanReadable(event_->getMemoryPeak()),
      makeHumanReadable(event_->getMemoryDelta()));
}

EventRecorder::~EventRecorder() {
  stop();
}

} // namespace facebook::algopt::treeprof
