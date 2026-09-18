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

#include <folly/Benchmark.h>

#include <optional>
#include <string>

namespace facebook {
namespace rebalancer {
namespace interface {
namespace benchmarks {

// Follow these steps when adding a new use case:
// 1. Make the manifold upload persistent:
//     manifold updateExpiration rebalancer/flat/solver_run_{runID} -1
// 2. Add a new BENCHMARK entry below.
// 3. Add the corresponding entry under benchmark_params in the TARGETS file.
void replay(
    const std::string& runId,
    folly::UserCounters* counters = nullptr,
    std::optional<std::string> loggingLabel = std::nullopt);

} // namespace benchmarks
} // namespace interface
} // namespace rebalancer
} // namespace facebook
