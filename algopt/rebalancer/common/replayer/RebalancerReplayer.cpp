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

#include "algopt/rebalancer/common/replayer/RebalancerReplayer.h"

#include "algopt/rebalancer/common/log/RebalancerLog.h"
#include "algopt/rebalancer/interface/CoreSolver.h"
#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/interface/standalone/BackwardCompatabilityUtils.h"

#include <folly/FileUtil.h>
#include <folly/system/HardwareConcurrency.h>

#include <memory>
#include <string>

#ifndef REBALANCER_OSS_BUILD
#include "algopt/rebalancer/common/log/fb/ScubaLog.h"
#include "algopt/rebalancer/interface/fb/Manifold.h"
#endif

using namespace facebook::rebalancer::interface;

namespace facebook::rebalancer {

namespace {
constexpr std::string_view kDefaultThreadPoolNamePrefix = "CPUThreadPool";
} // namespace

static void disableStdout(OptimalSolverSpec& optimal) {
  optimal.suppressLogs() = true;
}
static void disableStdout(OptimalSubsetSolverSpec& subset) {
  subset.suppressLogs() = true;
}

static void disableStdout(AssignmentProblem& problem) {
  for (auto& solver : *problem.strategy()->solvers()) {
    if (solver.getType() == SolverT::Type::optimalSolverSpec) {
      disableStdout(solver.mutable_optimalSolverSpec());
    }
    if (solver.getType() == SolverT::Type::optimalSubsetSolverSpec) {
      disableStdout(solver.mutable_optimalSubsetSolverSpec());
    }
  }
}

void RebalancerReplayer::clearSolverLogFile(AssignmentProblem& problem) {
  for (auto& solver : *problem.strategy()->solvers()) {
    if (solver.getType() == SolverT::Type::rasHybridSolverSpec) {
      solver.mutable_rasHybridSolverSpec().solverLogFile() = "";
    }
  }
}

interface::AssignmentProblem RebalancerReplayer::downloadFromManifold(
    const std::string& runId) {
#ifndef REBALANCER_OSS_BUILD
  auto bundle = Manifold::download(runId);
  return std::move(*bundle.problem());
#else
  throw std::runtime_error("Manifold download is not supported in OSS build");
#endif
}

AssignmentSolution RebalancerReplayer::replay(
    AssignmentProblem&& problem,
    std::optional<std::string> loggingLabel,
    std::shared_ptr<const InvalidMoveFilter> learnedInvalidMoveFilter) {
  disableStdout(problem);
  clearSolverLogFile(problem);
  std::shared_ptr<RebalancerLog> logger;
  if (loggingLabel) {
    problem.enableScubaLogger() = true;
    problem.scubaLoggingLabel() = std::move(*loggingLabel);
#ifndef REBALANCER_OSS_BUILD
    logger = std::make_shared<ScubaLog>(problem);
#endif
  } else {
    problem.enableScubaLogger() = false;
  }
  auto executor = ProblemSolver::makeCPUThreadPoolExecutor(
      kDefaultThreadPoolNamePrefix, folly::available_concurrency());
  if (!problem.universe()) {
    throw std::runtime_error("Universe is not set");
  }
  BackwardCompatabilityUtils::possiblyModify(*problem.universe());
  const auto universe =
      std::make_shared<entities::Universe>(*problem.universe());
  return CoreSolver::solve(
      problem,
      executor,
      /*enableParallelizedNewMaterializer=*/true,
      universe,
      logger,
      std::move(learnedInvalidMoveFilter));
}

} // namespace facebook::rebalancer
