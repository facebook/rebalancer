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
#include <string>

namespace facebook::algopt::treeprof {

class Profiler {
 public:
  // nowFn defaults to algopt::seconds_since_epoch (real wall clock). Pass a
  // ManualClock::asFn() in tests to advance time deterministically.
  explicit Profiler(std::string name, EventHolder::NowFn nowFn = {});

  void stop();

  std::shared_ptr<const Event> getRoot() const;

 private:
  std::shared_ptr<Event> root_;
  std::optional<folly::ShallowCopyRequestContextScopeGuard> guard_;
  EventHolder::NowFn nowFn_;
};

} // namespace facebook::algopt::treeprof
