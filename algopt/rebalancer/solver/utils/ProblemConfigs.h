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

#include "algopt/rebalancer/common/log/LogCollector.h"
#include "algopt/rebalancer/common/log/StreamLog.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/ThreadPoolExecutor.h>

#include <memory>
#include <optional>
#include <string>

namespace facebook::rebalancer {

class InvalidMoveFilter;

struct RunId {
  std::string scope;
  std::string service;
  std::string run_id;
};

struct ProblemConfigs {
  // TODO: define threadPool as a wrappedExecutor
  std::shared_ptr<folly::ThreadPoolExecutor> threadPool =
      std::make_shared<folly::CPUThreadPoolExecutor>(
          20,
          std::make_unique<folly::LifoSemMPMCQueue<
              folly::CPUThreadPoolExecutor::CPUTask,
              folly::QueueBehaviorIfFull::BLOCK>>(
              folly::CPUThreadPoolExecutor::kDefaultMaxQueueSize),
          std::make_shared<folly::NamedThreadFactory>("CPUThreadPool"));
  std::shared_ptr<LogCollector> logger =
      std::make_shared<LogCollector>(std::make_shared<StreamLog>());
  interface::MoveStatsSpec moveStatsSpec;
  // Used for logging
  RunId runId;
  std::optional<std::string> decompositionScopeName;
  bool enableParallelizedLpBuilding = false;
  bool useDynamicObjectOrdering = false;
  bool validateAppliedMoves = false;
  bool enableParallelizedBoundsComputing = false;
  bool addMetricsExprsToOrchestrator = false;
  // Learned (GNN) move-legality filter, pre-built by the caller (the only
  // torch-dependent seam, kept out of the solver core). When set, the Problem
  // unions it with the constraint-built filter. Null when the learned filter is
  // not enabled.
  std::shared_ptr<const InvalidMoveFilter> learnedInvalidMoveFilter;

  // TEMPORARY: When true, enables strict k:1 ratio swaps in SwapMoveType.
  // When false (default), k:1 swaps degrade to 1:1 (pre-D97512700 behavior).
  // Remove once k:1 swap support is fully validated in production.
  static inline bool enableKToOneSwaps = false;
};

} // namespace facebook::rebalancer
