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

#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/solver/utils/ParallelExecution.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace facebook::rebalancer {

// Chooses sliding-window or batching execution for one move type. Under AUTO,
// it alternates the two strategies on the first evaluation calls and then keeps
// the one with higher evaluation throughput. Only the owning solver thread may
// call it; worker threads never see it.
class ParallelExecutionSelector {
 public:
  enum class Strategy {
    SlidingWindow,
    Batching,
  };

  struct Observation {
    int64_t evaluations;
    double durationSecs;
  };

  explicit ParallelExecutionSelector(
      const std::optional<interface::ParallelExecutionConfig>& config);

  Strategy strategy() const {
    return strategy_;
  }
  const BatchingExecutionOptions& batchingOptions() const {
    return batchingOptions_;
  }
  // True while AUTO is still comparing strategies and wants observations.
  bool isExploring() const {
    return experiment_.has_value();
  }
  // Records an evaluation call that ran with the current strategy().
  void record(const Observation& observation, std::string_view moveTypeName);

 private:
  struct Measurements {
    int64_t evaluations = 0;
    double durationSecs = 0;

    double evaluationsPerSecond() const;
  };

  struct Experiment {
    size_t samples = 0;
    Measurements slidingWindow;
    Measurements batching;

    Measurements& measurements(Strategy strategy);
  };

  Strategy strategy_ = Strategy::SlidingWindow;
  BatchingExecutionOptions batchingOptions_;
  std::optional<Experiment> experiment_;
};

} // namespace facebook::rebalancer
