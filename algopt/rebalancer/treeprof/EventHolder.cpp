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

#include "algopt/rebalancer/treeprof/EventHolder.h"

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/treeprof/Event.h"

#include <utility>

namespace facebook::algopt::treeprof {

const std::string kEventKey = "facebook::algopt::treeprof::event";

EventHolder::EventHolder(std::shared_ptr<Event> event, NowFn nowFn)
    : event_(std::move(event)),
      nowFn_(nowFn ? std::move(nowFn) : algopt::seconds_since_epoch) {}

bool EventHolder::hasCallback() {
  return false;
}

std::shared_ptr<Event> EventHolder::get() const {
  return event_;
}

const EventHolder::NowFn& EventHolder::nowFn() const {
  return nowFn_;
}

const std::string& EventHolder::key() {
  return kEventKey;
}

} // namespace facebook::algopt::treeprof
