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

#include "algopt/rebalancer/common/log/LogCollector.h"

#include <stdexcept>
#include <utility>

namespace facebook::rebalancer {

namespace {

void throwIfNull(const std::shared_ptr<RebalancerLog>& logger) {
  if (!logger) {
    throw std::runtime_error(
        "Expected loggers passed to LogCollector to be non-null");
  }
}

} // namespace

LogCollector::LogCollector(
    std::vector<std::shared_ptr<RebalancerLog>> loggers) {
  for (const auto& logger : loggers) {
    throwIfNull(logger);
  }
  loggers_ = std::move(loggers);
}

LogCollector::LogCollector(std::shared_ptr<RebalancerLog> logger) {
  throwIfNull(logger);
  loggers_.push_back(std::move(logger));
}

void LogCollector::setLoggingLabel(const std::string& label) {
  for (const auto& logger : loggers_) {
    logger->setLoggingLabel(label);
  }
}

} // namespace facebook::rebalancer
