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
#include "algopt/rebalancer/solver/moves/EvalSummary.h"
#include "algopt/rebalancer/solver/moves/MoveType.h"
#include "algopt/rebalancer/solver/profilers/LocalSearchProfiler.h"
#include "algopt/rebalancer/solver/solvers/HotContainerSelector.h"
#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"
#include "algopt/rebalancer/solver/utils/SearchHintsFactory.h"

#include <fmt/core.h>
#include <folly/logging/xlog.h>
#include <folly/stats/Histogram.h>
#include <folly/String.h>

namespace facebook::rebalancer {

// One move set may contain multiple moves. For some move types, it is also
// useful to report the exact number of times a given move was applied.
struct MoveApplyInfo {
  size_t applyCount = 0;
  size_t moveCount = 0;
  // record relevent info from the event of applying a moveset
  void recordApply(size_t numMoves) {
    applyCount++;
    moveCount += numMoves;
  }
};

// The reason this is defined as a macro as opposed to a function because the
// xlog macro will capture the line number of the log function call
#define REBALANCER_LOG_SOLVER_OUTPUT(level, msg) \
  XLOG(level) << msg;                            \
  writeToOutputLoggerMaybe(msg)

struct StopInfo {
  interface::EndReason endReason;
  // more information about the end reason
  std::string auxInfo{};
};

struct SolveParams {
  LocalSearchProfiler& profiler;

  int objectiveTupleStart;
  int objectiveTupleEnd;

  double solveTime;
  std::optional<int> stopAfterMoves;
  std::string solveEventName;
  std::string stopEventName;

  bool enableObjectPotentialSorting = false;
  bool exploreMovesFromContainersNotInObjective = true;
  std::optional<int> stageId = std::nullopt;
  std::optional<algopt::common::thrift::HigherPriorityObjectivesConfig>
      higherPriorityObjConfig = std::nullopt;
};

struct SolveState {
  int cyclesStarted = 0;
  int movesInCycle = 0;
  double lastImprovedTime = 0.0;
  GlobalObjectiveValue objectiveAtCycleStart;
  Problem& problem;
  MovesEvaluator evaluator;
  HotContainerSelector hotContainerSelector_;
  SearchHints hints;
  PackerMap<size_t, MoveApplyInfo> moveCounts;
  PackerSet<entities::ContainerId> skipContainers;
  PackerSet<entities::ContainerId> unfixableContainers;

  explicit SolveState(
      Problem& problem,
      const SolveParams& solveParams,
      const interface::LocalSearchSolverSpec& spec)
      : cyclesStarted(0),
        movesInCycle(0),
        problem(problem),
        evaluator(MovesEvaluator(
            problem,
            std::max(0, solveParams.objectiveTupleStart),
            std::min(solveParams.objectiveTupleEnd, problem.objective.size()),
            solveParams.solveEventName,
            solveParams.higherPriorityObjConfig)),
        hotContainerSelector_{HotContainerSelector(
            problem,
            *spec.randomSeed(),
            solveParams.enableObjectPotentialSorting,
            solveParams.exploreMovesFromContainersNotInObjective,
            evaluator.getObjective().getView(),
            *spec.hottestTraversalConfig())},
        hints(SearchHintsFactory::create(spec)) {}

  void startNewCycle() {
    cyclesStarted++;
    movesInCycle = 0;
    objectiveAtCycleStart = problem.objective.getValue();
    skipContainers = problem.getFixedContainers();
    skipContainers.insert(
        unfixableContainers.begin(), unfixableContainers.end());
    hotContainerSelector_.reset();
    hints.reset(problem, evaluator);
  }
};

class CoreLocalSearchSolve {
 public:
  CoreLocalSearchSolve(
      interface::LocalSearchSolverSpec spec,
      std::vector<std::shared_ptr<MoveType>> allowedMoves,
      Problem& problem,
      const SolveParams& solveParams,
      std::shared_ptr<SolverOutputLogger> logger = nullptr);

  bool getSolved() const;
  double getEvalTime() const;
  double getApplyTime() const;
  double getFindTime() const;
  double getPruneTime() const;
  double getTotalDuration() const;
  StopInfo getStopInfo() const;
  std::vector<std::pair<double, int>> getMoveTimes() const;
  interface::SolverMoveStats getMoveStats() const;
  interface::FinalEvaluationSummary getFinalEvaluationSummary() const;
  std::optional<interface::SolverEvalStats> getEvalStats() const;
  size_t getTotalEvals() const;
  int getTotalMoves() const;
  const MoveStats& getGlobalMoveStats() const;

  static interface::SolverMoveStats getMovesHistogram(
      const std::vector<std::pair<double, int>>& moveTimes,
      const double stageDuration,
      const double timeToApplyMoves,
      const std::vector<std::shared_ptr<MoveType>>& moveTypes);

 private:
  bool solve();
  bool improveHotContainer(
      entities::ContainerId hotContainer,
      double& lastImprovedTime);
  bool reachedGlobalOptimum();
  bool reachedLocalOptimum();
  bool shouldRecomputeHottestOrderingAfterMove(int nContainersToOrder) const;
  // Returns whether the move remains applied.
  bool tryApplyMove(size_t moveIndex, const MoveResult& moveResult);
  bool finalizeAndReturn(bool solved);
  bool shouldTerminate(double curTime, double lastImprovedTime);
  bool shouldStartNextCycle();
  bool hasSufficientObjectiveImprovement();
  double getTimePerMove();
  MoveResult findBestMove(
      entities::ContainerId hotContainer,
      const std::shared_ptr<MoveType>& allowedMove);
  void writeToOutputLoggerMaybe(const std::string& logLine) const;
  std::string summarizeProgress();
  std::string summarizeEvalStatsAndObjective();
  std::string getStopLog(const std::string& stopEventName);

 private:
  interface::LocalSearchSolverSpec spec_;
  Problem& problem_;
  const SolveParams& solveParams_;
  std::shared_ptr<SolverOutputLogger> logger_ = nullptr;
  std::vector<std::shared_ptr<MoveType>> allowedMoves_;
  SolveState solveState_;

  // timers
  algopt::Timer timer_;
  algopt::Timer applyTimer_;
  algopt::Timer evalTimer_;
  algopt::Timer pruneTimer_;
  algopt::Timer boundsCheckTimer_;

  // progress trackers
  bool solved_;
  StopInfo stopInfo_;
  std::optional<std::string> lastApplyRejectionReason_;

  // captures moves histogram
  std::vector<std::pair<double, int>> moveTimes_;
  interface::SolverMoveStats moveStats_;
  interface::FinalEvaluationSummary finalEvaluationSummary_;
  MoveStatsAggregator finalEvaluationStats_;
  MoveStatsAggregator perMoveStatsAggregator_;
  EvalSummary solverEvalSummary_;
  std::optional<interface::SolverEvalStats> evalStats_;
  size_t totalEvals_ = 0;
  int totalMoves_ = 0;
  size_t localSearchIterations_ = 0;
  MoveStats globalMoveStats_;
};
} // namespace facebook::rebalancer
