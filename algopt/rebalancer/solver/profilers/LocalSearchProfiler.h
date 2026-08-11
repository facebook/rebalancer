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

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/solver/moves/MoveType.h"

namespace facebook::rebalancer {

class LocalSearchProfiler {
 public:
  LocalSearchProfiler(
      const std::vector<std::shared_ptr<MoveType>>& moves,
      std::vector<double> initialValue);

  LocalSearchProfiler(
      const std::vector<std::string>& moveTypeNames,
      std::vector<double> initialValue);

  void add(const std::vector<interface::MoveTypeEvent>& events);

  const std::vector<double>& getCurrentValue() const;

  std::vector<interface::LocalSearchProfile> takeProfiles();

  int getMoveTypeIndex(const std::string& moveTypeName) const;

 private:
  void init(const std::vector<std::string>& moveTypeNames);

  static void add(
      const interface::MoveTypeEvent& event,
      double& currentValue,
      interface::LocalSearchProfile& profile);

  std::vector<double> currentValue_;
  std::vector<interface::LocalSearchProfile> profiles_;
  folly::F14FastMap<std::string, int> moveTypeNameToIndex_;
};

class MoveTypeProfiler {
 public:
  MoveTypeProfiler(
      LocalSearchProfiler& profiler,
      const std::string& moveTypeName);

  ~MoveTypeProfiler();

  MoveTypeProfiler(const MoveTypeProfiler&) = delete;
  MoveTypeProfiler& operator=(const MoveTypeProfiler&) = delete;
  MoveTypeProfiler(MoveTypeProfiler&&) = delete;
  MoveTypeProfiler& operator=(MoveTypeProfiler&&) = delete;

  void updateValue(const std::vector<double>& value);

 private:
  facebook::algopt::Timer timer_;
  LocalSearchProfiler& profiler_;
  std::vector<interface::MoveTypeEvent> events_;
};

} // namespace facebook::rebalancer
