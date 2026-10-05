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

#include "algopt/rebalancer/treeprof/ExecutorWrapper.h"

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/treeprof/Event.h"
#include "algopt/rebalancer/treeprof/EventHolder.h"
#include "algopt/rebalancer/treeprof/ThreadMemoryMonitor.h"

#include <folly/Executor.h>
#include <folly/Function.h>
#include <folly/io/async/Request.h>

#include <stdint.h>
#include <utility>

namespace facebook::algopt::treeprof {

ExecutorWrapper::ExecutorWrapper(std::shared_ptr<Executor> executor)
    : executor_(std::move(executor)) {}

void ExecutorWrapper::add(folly::Func function) {
  executor_->add([context = folly::RequestContext::saveContext(),
                  function = std::move(function)]() mutable {
    const folly::RequestContextScopeGuard contextScope{std::move(context)};

    // Read the nowFn from the active EventHolder so that mock clocks used in
    // tests propagate consistently through thread-pool boundaries. Falls back
    // to the real wall clock when no EventHolder is present.
    const auto* holder = static_cast<const EventHolder*>(
        folly::RequestContext::get()->getContextData(EventHolder::key()));
    static const EventHolder::NowFn kDefaultNowFn{algopt::seconds_since_epoch};
    const EventHolder::NowFn& nowFn = holder ? holder->nowFn() : kDefaultNowFn;

    // Step 1: reset metrics
    ThreadMemoryMonitor::reset(nowFn());

    // Step 2: run function
    function();

    // Step 3: collect metrics, either since the start of the function or since
    // the last reset within the function (e.g. at the beginning or end of an
    // event)
    const double lastResetTime = ThreadMemoryMonitor::lastResetTime();
    const int64_t delta = ThreadMemoryMonitor::delta();
    const int64_t peak = ThreadMemoryMonitor::peak();
    ThreadMemoryMonitor::reset(nowFn());
    const double now = ThreadMemoryMonitor::lastResetTime();

    // Step 4: store metrics in parent event, if available
    if (holder == nullptr) {
      return;
    }

    auto event = holder->get();
    if (event == nullptr) {
      return;
    }

    event->addInterval(
        Interval{
            .beginTime = lastResetTime,
            .endTime = now,
            .memoryDelta = delta,
            .memoryPeak = peak});
  });
}

std::shared_ptr<const folly::Executor> ExecutorWrapper::get() const {
  return executor_;
}

} // namespace facebook::algopt::treeprof
