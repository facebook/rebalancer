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
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/solver/moves/MoveStatsAggregator.h"
#include "algopt/rebalancer/solver/utils/Problem.h"

namespace facebook::rebalancer {

class FinalEvaluationSummaryHelper {
 public:
  static interface::FinalEvaluationSummary makeSummary(
      const Problem& problem,
      const MoveStatsAggregator& stats);

 private:
  static interface::FinalEvaluationStats makeStats(const MoveStats& stats);
};

} // namespace facebook::rebalancer
