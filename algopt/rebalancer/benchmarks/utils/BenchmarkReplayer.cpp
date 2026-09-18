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

#include "algopt/rebalancer/benchmarks/utils/BenchmarkReplayer.h"

#include "algopt/rebalancer/common/replayer/RebalancerReplayer.h"

#include <fmt/core.h>
#include <folly/Benchmark.h>
#include <folly/container/Enumerate.h>

#include <string>

namespace facebook {
namespace rebalancer {
namespace interface {
namespace benchmarks {

void replay(
    const std::string& runId,
    folly::UserCounters* counters,
    std::optional<std::string> loggingLabel) {
  interface::AssignmentProblem problem;

  BENCHMARK_SUSPEND {
    problem = RebalancerReplayer::downloadFromManifold(runId);
  }

  const auto solution =
      RebalancerReplayer::replay(std::move(problem), std::move(loggingLabel));

  if (counters != nullptr) {
    BENCHMARK_SUSPEND {
      const auto& objectives = *solution.finalGlobalObjective()->goals();
      for (const auto [position, objective] : folly::enumerate(objectives)) {
        (*counters)[fmt::format("objective_{}", position)] = folly::UserMetric(
            *objective.value(), folly::UserMetric::Type::METRIC);
      }
    }
  }
}

} // namespace benchmarks
} // namespace interface
} // namespace rebalancer
} // namespace facebook
