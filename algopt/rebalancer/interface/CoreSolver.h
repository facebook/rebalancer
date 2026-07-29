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

#include "algopt/rebalancer/common/log/InMemoryLog.h"
#include "algopt/rebalancer/common/log/RebalancerLog.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/AssignmentProblem_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/treeprof/EventRecorder.h"

#include <folly/executors/ThreadPoolExecutor.h>

#include <functional>
#include <memory>

namespace facebook {
namespace rebalancer {
namespace interface {

class CoreSolver {
 public:
  // Optional caller-built learned (GNN) move filter, unioned with the
  // constraint filter; building it in the caller keeps CoreSolver torch-free.
  static AssignmentSolution solve(
      const AssignmentProblem& problemSpec,
      std::shared_ptr<folly::ThreadPoolExecutor> executor,
      bool enableParallelizedNewMaterializer,
      std::shared_ptr<const entities::Universe> universe,
      std::shared_ptr<RebalancerLog> logger = nullptr,
      std::shared_ptr<const InvalidMoveFilter> learnedInvalidMoveFilter =
          nullptr);

  // TODO: make object ordering dimension a global setting of the problem.
  static std::optional<std::string> getObjectOrderingDimensionName(
      const StrategyT& strategy);

  static void printAndLogHierachicalProfile(
      std::shared_ptr<const algopt::treeprof::Event> hierarchyTreeRoot,
      ProblemProfile& problemProfile,
      std::shared_ptr<RebalancerLog> logger = nullptr);

 private:
  static AssignmentSolution materializeAndSolve(
      const AssignmentProblem& problemSpec,
      std::shared_ptr<folly::ThreadPoolExecutor> executor,
      std::shared_ptr<RebalancerLog> logger,
      bool enableParallelizedNewMaterializer,
      std::shared_ptr<const entities::Universe> universe,
      std::shared_ptr<const InvalidMoveFilter> learnedInvalidMoveFilter);

  static ProblemConfigs makeProblemConfig(
      const AssignmentProblem& problemSpec,
      std::shared_ptr<RebalancerLog> logger,
      std::shared_ptr<folly::ThreadPoolExecutor> executor,
      bool enableParallelMaterializer = false);

  static void initSubproblemDecomposition(
      Problem& problem,
      const entities::Universe& universe,
      const std::string& decompositionScopeName);

  static void appendLoggedData(
      AssignmentSolution& solution,
      InMemoryLog& memoryLogger);
};

} // namespace interface
} // namespace rebalancer
} // namespace facebook
