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

#include "algopt/rebalancer/common/log/RebalancerLog.h"

#include <memory>
#include <string>
#include <vector>

namespace facebook::rebalancer {

class LogCollector final {
 public:
  explicit LogCollector(
      std::vector<std::shared_ptr<RebalancerLog>> loggers = {});
  explicit LogCollector(std::shared_ptr<RebalancerLog> logger);

  template <class T>
  void log(const T& info) {
    notify(info);
  }

  void setLoggingLabel(const std::string& label);

 private:
  template <class T>
  void notify(const T& info) {
    for (const auto& logger : loggers_) {
      logger->log(info);
    }
  }

  std::vector<std::shared_ptr<RebalancerLog>> loggers_;
};

} // namespace facebook::rebalancer
