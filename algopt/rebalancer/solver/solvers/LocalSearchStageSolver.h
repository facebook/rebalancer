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

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/solver/moves/EvalSummary.h"
#include "algopt/rebalancer/solver/solvers/Solver.h"
#include "algopt/rebalancer/solver/utils/Util.h"

namespace facebook::rebalancer {

class Problem;

std::tuple<std::optional<int>, bool> getStageMoveLimit(
    const interface::LocalSearchStageSolverSpec& globalConfig,
    const interface::LocalSearchStageSpec& stageConfig,
    int totalMoves,
    size_t stageId,
    const std::vector<interface::StageSummary>& stageSummaries);

std::optional<double> getMultiStageTime(
    const interface::LocalSearchStageSolverSpec& globalConfig,
    size_t stageId,
    const std::vector<interface::StageSummary>& stageSummaries);

class LocalSearchStageSolver : public Solver {
 private:
  using Timer = facebook::algopt::Timer;

 public:
  explicit LocalSearchStageSolver(
      interface::LocalSearchStageSolverSpec configs);

  bool solve(Problem& p, Profile profile = std::nullopt) override;

  // all non-debug logs will be written to this file
  void setSolverOutputLogger(std::shared_ptr<SolverOutputLogger> logger);

  // if solverOutputLogger was specified, append this log line to it
  void writeToOutputLoggerMaybe(const std::string& logLine) const;

  bool needs_continuous_expressions() override;

 private:
  interface::LocalSearchStageSolverSpec configs_;

  int64_t totalMoves_ = 0;
  // global solver timer
  Timer timer_;
  // total find worst container time across all stages
  double totalFindTime_ = 0.0;
  //  total evaluate move sets time across all stages
  double totalEvalTime_ = 0.0;
  //  total apply move sets time across all stages
  double totalApplyTime_ = 0.0;
  //  total time to check for global and local optimum bounds across all stages
  double totalPruneTime_ = 0.0;

  // metrics of the solver algorithm
  std::vector<std::pair<double, int>> moveTimes_;
  // evaluation summary during all stages of solver
  EvalSummary solverEvalSummary_;

  double stageSolveTime_ = 0;
  std::shared_ptr<SolverOutputLogger> logger_ = nullptr;
};

} // namespace facebook::rebalancer
