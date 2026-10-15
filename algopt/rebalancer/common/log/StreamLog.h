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

namespace facebook {
namespace rebalancer {

class StreamLog : public RebalancerLog {
 public:
  /* high level APIs */
  void log(const ProblemDefinition& info) override;
  void log(const interface::GlobalObjectiveSummary& info) override;
  void log(const interface::ConstraintSummary& info) override;
  void log(const interface::MovesSummary& info) override;
  void log(const SolverSummary& info) override;
  void log(const BenchmarkInfo& info) override;
};

} // namespace rebalancer
} // namespace facebook
