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

#include <folly/io/async/Request.h>

#include <functional>
#include <memory>
#include <string>

namespace facebook::algopt::treeprof {

class EventHolder : public folly::RequestData {
 public:
  using NowFn = std::function<double()>;

  // nowFn defaults to algopt::seconds_since_epoch when empty.
  explicit EventHolder(std::shared_ptr<Event> event, NowFn nowFn = {});

  bool hasCallback() override;

  std::shared_ptr<Event> get() const;
  const NowFn& nowFn() const;

  static const std::string& key();

 private:
  std::shared_ptr<Event> event_;
  NowFn nowFn_;
};

} // namespace facebook::algopt::treeprof
