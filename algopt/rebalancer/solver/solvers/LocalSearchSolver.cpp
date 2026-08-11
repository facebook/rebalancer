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

#include "algopt/rebalancer/solver/solvers/LocalSearchSolver.h"

#include "algopt/rebalancer/solver/moves/MoveTypeFactory.h"
#include "algopt/rebalancer/solver/profilers/LocalSearchProfiler.h"
#include "algopt/rebalancer/solver/solvers/CoreLocalSearchSolve.h"
#include <algopt/rebalancer/solver/utils/GlobalObjective.h>

#include <fmt/core.h>
#include <folly/logging/xlog.h>

namespace facebook::rebalancer {

LocalSearchSolver::LocalSearchSolver(interface::LocalSearchSolverSpec configs)
    : configs_(std::move(configs)) {}

bool LocalSearchSolver::solve(Problem& p, Profile /* unused */) {
  auto moveTypes = MoveTypeFactory::createMoveTypes(configs_, p);
  LocalSearchProfiler profiler(moveTypes, p.objective.getValue().toVector());

  int lastObj = p.objective.size();
  if (configs_.objectivesForHottestContainers()) {
    int obj = *configs_.objectivesForHottestContainers();
    if (obj <= 0) {
      throw std::runtime_error(
          "objectives_for_hottest_container must be positive");
    }
    if (obj > lastObj) {
      obj = lastObj;
      XLOG(WARN) << fmt::format(
          "objectives_for_hottest_container must be less than size of objective tuple: {}, value: {}",
          lastObj,
          obj);
    }
    lastObj = obj;
  }

  auto stopAfterMoves =
      configs_.stopAfterMoves() && *configs_.stopAfterMoves() >= 0
      ? configs_.stopAfterMoves().to_optional()
      : std::nullopt;

  const SolveParams solveParams{
      .profiler = profiler,
      .objectiveTupleStart = 0,
      .objectiveTupleEnd = lastObj,
      .solveTime = configs_.solveTime() && *configs_.solveTime() >= 0
          ? *configs_.solveTime()
          : std::numeric_limits<double>::infinity(),
      .stopAfterMoves = stopAfterMoves,
      .solveEventName = "LocalSearch::solve",
      .stopEventName = "solve",

      .enableObjectPotentialSorting = *configs_.enableObjectPotentialSorting(),
      .exploreMovesFromContainersNotInObjective =
          *configs_.exploreMovesFromContainersNotInObjective()};
  const CoreLocalSearchSolve coreLocalSearchSolve(
      configs_, std::move(moveTypes), p, solveParams);

  auto stopInfo = coreLocalSearchSolve.getStopInfo();
  auto solverSummary = SolverSummary{
      .solverType = SolverType::LOCAL_SEARCH,
      .endReason = stopInfo.endReason,
      .auxInfo = stopInfo.auxInfo,
      .evalStats = coreLocalSearchSolve.getEvalStats(),
      .moveStats = coreLocalSearchSolve.getMoveStats(),
  };

  p.configs.logger->log(std::move(solverSummary));
  for (auto& profile : profiler.getProfiles()) {
    p.configs.logger->log(profile);
  }

  p.configs.logger->log(coreLocalSearchSolve.makeFinalEvaluationSummary());
  totalEvals = coreLocalSearchSolve.getTotalEvals();

  return coreLocalSearchSolve.getSolved();
}

bool LocalSearchSolver::needs_continuous_expressions() {
  return true;
}

} // namespace facebook::rebalancer
