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

#include "algopt/rebalancer/treeprof/Profiler.h"

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/treeprof/Event.h"
#include "algopt/rebalancer/treeprof/EventHolder.h"
#include "algopt/rebalancer/treeprof/ThreadMemoryMonitor.h"

#include <folly/io/async/Request.h>

#include <functional>
#include <stdexcept>
#include <stdint.h>
#include <utility>

namespace facebook::algopt::treeprof {

Profiler::Profiler(std::string name, EventHolder::NowFn nowFn)
    : root_(std::make_shared<Event>(std::move(name))),
      nowFn_(nowFn ? std::move(nowFn) : algopt::seconds_since_epoch) {
  auto currentContext = folly::RequestContext::try_get();
  if (currentContext && currentContext->hasContextData(EventHolder::key())) {
    throw std::runtime_error(
        "Nested profilers are not allowed, one treeprof instance already seems to be active in this scope");
  }
  guard_.emplace(
      EventHolder::key(), std::make_unique<EventHolder>(root_, nowFn_));
  ThreadMemoryMonitor::reset(nowFn_());
}

void Profiler::stop() {
  guard_.reset();

  const int64_t peak = ThreadMemoryMonitor::peak();
  const int64_t delta = ThreadMemoryMonitor::delta();
  const double lastResetTime = ThreadMemoryMonitor::lastResetTime();
  ThreadMemoryMonitor::reset(nowFn_());
  const double now = ThreadMemoryMonitor::lastResetTime();

  root_->addInterval(
      Interval{
          .beginTime = lastResetTime,
          .endTime = now,
          .memoryDelta = delta,
          .memoryPeak = peak});

  root_->finalize();
}

std::shared_ptr<const Event> Profiler::getRoot() const {
  return root_;
}

} // namespace facebook::algopt::treeprof
