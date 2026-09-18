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

#ifndef REBALANCER_OSS_BUILD
#include "algopt/rebalancer/interface/fb/Manifold.h"
#endif

#include <fmt/core.h>
#include <folly/Benchmark.h>
#include <folly/container/Enumerate.h>
#include <folly/logging/xlog.h>

#include <chrono>
#include <string>

namespace facebook {
namespace rebalancer {
namespace interface {
namespace benchmarks {

namespace {

// A TTL of 0 means "never expire". A failed pin must not fail the benchmark.
void pinBundle(const std::string& runId) {
#ifndef REBALANCER_OSS_BUILD
  try {
    Manifold::extendExpiration(runId, std::chrono::seconds(0));
  } catch (const std::exception& e) {
    XLOGF(
        WARN, "Failed to pin Manifold bundle for run {}: {}", runId, e.what());
  }
#else
  (void)runId;
#endif
}

} // namespace

void replay(
    const std::string& runId,
    folly::UserCounters* counters,
    std::optional<std::string> loggingLabel,
    BundleRetention retention) {
  interface::AssignmentProblem problem;

  BENCHMARK_SUSPEND {
    if (retention == BundleRetention::Pinned) {
      pinBundle(runId);
    }
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
