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
#include <algopt/rebalancer/interface/thrift/gen-cpp2/Metrics_types.h>

#include <folly/Synchronized.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer {

class LogCollector final {
 public:
  struct Data {
    std::vector<interface::MovesSummary> moves;
    std::vector<interface::GlobalObjectiveSummary> objectiveSummaries;
    std::vector<interface::ConstraintSummary> constraintSummaries;
    std::vector<SolverSummary> solverSummaries;
    std::optional<interface::FinalEvaluationSummary> finalEvaluationSummary;
    std::vector<interface::LocalSearchProfile> localSearchProfiles;
    std::vector<interface::SpecMetadata> specMetadata;
    std::vector<interface::thrift::Metrics> metrics;
  };

  explicit LogCollector(
      std::vector<std::shared_ptr<RebalancerLog>> loggers = {});
  explicit LogCollector(std::shared_ptr<RebalancerLog> logger);

  ~LogCollector() = default;
  LogCollector(const LogCollector&) = delete;
  LogCollector(LogCollector&&) = delete;
  LogCollector& operator=(const LogCollector&) = delete;
  LogCollector& operator=(LogCollector&&) = delete;

  template <class T>
  void log(const T& info) {
    notify(info);
  }

  void log(interface::GlobalObjectiveSummary info);
  void log(interface::ConstraintSummary info);
  void log(interface::MovesSummary info);
  void log(SolverSummary info);
  void log(interface::FinalEvaluationSummary info);
  void log(interface::LocalSearchProfile info);
  void log(interface::SpecMetadata info);
  void log(interface::thrift::Metrics metrics);

  Data takeLoggedData();

 private:
  template <class T>
  void notify(const T& info) {
    for (const auto& logger : loggers_) {
      logger->log(info);
    }
  }

  template <class T>
  void notifyAndStore(T info, std::vector<T> Data::* field) {
    notify(info);
    data_.withWLock(
        [&](Data& data) { (data.*field).push_back(std::move(info)); });
  }

  folly::Synchronized<Data> data_;
  std::vector<std::shared_ptr<RebalancerLog>> loggers_;
};

} // namespace facebook::rebalancer
