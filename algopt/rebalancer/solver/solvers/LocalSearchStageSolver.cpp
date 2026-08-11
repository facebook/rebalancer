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

#include "algopt/rebalancer/solver/solvers/LocalSearchStageSolver.h"

#include "algopt/rebalancer/solver/moves/EvalSummary.h"
#include "algopt/rebalancer/solver/moves/MoveTypeFactory.h"
#include "algopt/rebalancer/solver/solvers/CoreLocalSearchSolve.h"
#include "algopt/rebalancer/treeprof/EventRecorder.h"

#include <folly/container/irange.h>
#include <folly/logging/xlog.h>

#include <cassert>
#include <cstddef>
#include <string>

namespace facebook::rebalancer {

namespace {
int getTotalMovesUsedSoFar(
    const std::set<int>& stageList,
    const std::vector<interface::StageSummary>& stageSummaries,
    const int stageId) {
  int totalMovesUsedSoFar = 0;
  for (auto stage : stageList) {
    if (stage < stageId) {
      if (stage >= static_cast<int>(stageSummaries.size()) ||
          !stageSummaries.at(stage).moveStats().has_value()) {
        throw std::runtime_error(
            fmt::format("Stage {} does not have move stats set", stage));
      }
      totalMovesUsedSoFar += *stageSummaries.at(stage).moveStats()->numMoves();
    }
  }
  return totalMovesUsedSoFar;
}

double getStageSolveTimeSoFar(
    const std::set<int>& stageList,
    const std::vector<interface::StageSummary>& stageSummaries,
    const int stageId) {
  double totalStageSolveTime = 0;
  for (auto stage : stageList) {
    if (stage < stageId) {
      if (stage >= static_cast<int>(stageSummaries.size())) {
        throw std::runtime_error(
            fmt::format("Stage {} does not have duration set", stage));
      }
      totalStageSolveTime += *stageSummaries.at(stage).duration();
    }
  }
  return totalStageSolveTime;
}
} // namespace

LocalSearchStageSolver::LocalSearchStageSolver(
    interface::LocalSearchStageSolverSpec configs)
    : configs_(std::move(configs)) {}

bool LocalSearchStageSolver::solve(Problem& p, Profile /* unused */) {
  const algopt::treeprof::EventRecorder solveEvent("Local Search with stages");
  timer_.start(); //  solver timer starts now

  std::vector<interface::StageSummary> stagesSummaries;

  auto stageName = [](size_t stageId,
                      const interface::LocalSearchStageSpec& stageConfig) {
    auto objStr = fmt::format(
        "objectives:[{}-{})", *stageConfig.begin(), *stageConfig.end());
    return stageConfig.name()
        ? fmt::format("Stage-{}: {} ({})", stageId, *stageConfig.name(), objStr)
        : fmt::format("Stage-{}: {}", stageId, objStr);
  };

  auto stopAfterMoves = configs_.stopAfterMoves();
  auto solveTime = configs_.solveTime();
  auto timeLimitDesc =
      (solveTime && *solveTime >= 0 ? std::to_string(*solveTime) : "UNBOUNDED");
  auto moveLimitDesc =
      (stopAfterMoves && *stopAfterMoves >= 0 ? std::to_string(*stopAfterMoves)
                                              : "UNBOUNDED");
  auto startMsg = fmt::format(
      "Running local search stage algorithm until {} seconds or {} moves",
      timeLimitDesc,
      moveLimitDesc);
  REBALANCER_LOG_SOLVER_OUTPUT(INFO, startMsg);

  using PtrMoveType = std::shared_ptr<MoveType>;
  using MoveTypes = std::vector<PtrMoveType>;
  using MoveTypesPerStage = std::vector<MoveTypes>;

  MoveTypes moveTypesUsedInAllStages;
  MoveTypesPerStage moveTypesPerStage;
  for (const auto stageId : folly::irange(configs_.stageSpecs()->size())) {
    auto& stageConfig = configs_.stageSpecs()->at(stageId);
    auto& spec = *stageConfig.solverSpec();
    auto stageMoveTypes = MoveTypeFactory::createMoveTypes(spec, p);
    moveTypesUsedInAllStages.insert(
        moveTypesUsedInAllStages.end(),
        stageMoveTypes.begin(),
        stageMoveTypes.end());
    moveTypesPerStage.emplace_back(std::move(stageMoveTypes));
  }
  LocalSearchProfiler profiler(
      moveTypesUsedInAllStages, p.objective.getValue().toVector());

  std::optional<size_t> lastStageWithEvaluations;

  SCOPE_EXIT {
    auto msg = fmt::format(
        "\nExiting stage solver...\ntotal moves: {}, {}\n"
        "total time: {:.3f}, find worst {:.3f}s, eval {:.3f}s, apply {:.3f}s\n",
        totalMoves_,
        solverEvalSummary_.distribution(
            EvalMetrics{.evalDuration = totalEvalTime_}),
        timer_.getSeconds(),
        totalFindTime_,
        totalEvalTime_,
        totalApplyTime_);
    REBALANCER_LOG_SOLVER_OUTPUT(INFO, msg);

    assert(!stagesSummaries.empty());
    int32_t totalCycles = 0;
    for (const auto& stage : stagesSummaries) {
      if (stage.evalStats().has_value()) {
        totalCycles += *stage.evalStats()->numCycles();
      }
    }
    auto solverSummary = SolverSummary{
        .solverType = SolverType::LOCAL_SEARCH_STAGES,
        // last stage's end reason is the overall end reason
        .endReason = *stagesSummaries.back().endReason(),
        .auxInfo = *stagesSummaries.back().auxEndInfo(),
        .evalStats = solverEvalSummary_.getSolverEvalStats(
            totalEvalTime_, totalFindTime_, totalCycles),
        .moveStats = CoreLocalSearchSolve::getMovesHistogram(
            moveTimes_,
            timer_.getSeconds(),
            totalApplyTime_,
            moveTypesUsedInAllStages),
        .stagesSummaries = std::move(stagesSummaries),
    };

    // The last stage that evaluated moves explains why improvement stopped.
    // Keep the solution-level field set even when no stage evaluated moves.
    auto finalEvaluationSummary = lastStageWithEvaluations
        ? *solverSummary.stagesSummaries->at(*lastStageWithEvaluations)
               .finalEvaluationSummary()
        : interface::FinalEvaluationSummary{};
    p.configs.logger->log(std::move(solverSummary));
    p.configs.logger->log(std::move(finalEvaluationSummary));

    for (auto& profile : profiler.getProfiles()) {
      p.configs.logger->log(profile);
    }
  };

  for (const auto stageId : folly::irange(configs_.stageSpecs()->size())) {
    auto& stageConfig = configs_.stageSpecs()->at(stageId);
    auto& spec = *stageConfig.solverSpec();

    // Stage specific custom equivalence sets will be applied during the
    // internal core local search solve call. If no stage specific overrides are
    // provided for this stage, use the global config (if available)
    if (!spec.customEquivalenceSetConfig() &&
        configs_.customEquivalenceSetConfig()) {
      p.getEquivalenceSetsStore().initialize(
          *configs_.customEquivalenceSetConfig());
    }

    double maxGlobalTime = configs_.solveTime()
        ? *configs_.solveTime()
        : std::numeric_limits<double>::infinity();
    double maxStageTime = spec.solveTime()
        ? *spec.solveTime()
        : std::numeric_limits<double>::infinity();
    double minStageTime =
        stageConfig.minRuntimeSec() ? *stageConfig.minRuntimeSec() : 0.0;
    const double elapsedGlobalTime = timer_.getSeconds();
    double remainingGlobalTime =
        std::max(0.0, maxGlobalTime - elapsedGlobalTime);
    auto multiStageTimeLimit =
        getMultiStageTime(configs_, stageId, stagesSummaries);

    // if value for 'recomputeContainerOrderingAfterEveryMove' is set in
    // LocalSearchStageSolverSpec, then it overwrites anything mentioned in
    // any of the StageSpecs
    if (configs_.recomputeContainerOrderingAfterEveryMove().has_value()) {
      spec.recomputeContainerOrderingAfterEveryMove() =
          configs_.recomputeContainerOrderingAfterEveryMove().value();
    }

    // if value for 'exploreMovesFromContainersNotInObjective' is set in
    // LocalSearchStageSolverSpec, then it overwrites anything mentioned in
    // any of the LocalSearchSolverSpec
    if (configs_.exploreMovesFromContainersNotInObjective().has_value()) {
      spec.exploreMovesFromContainersNotInObjective() =
          configs_.exploreMovesFromContainersNotInObjective().value();
    }

    // if value for 'hottestTraversalConfig' is set in
    // LocalSearchStageSolverSpec, then it overwrites anything mentioned in any
    // of the LocalSearchSolverSpec
    if (configs_.hottestTraversalConfig().has_value()) {
      spec.hottestTraversalConfig() = configs_.hottestTraversalConfig().value();
    }

    // if value for 'parallelExecutionConfig' is NOT set at stage level
    // (LocalSearchSolverSpec), but IS set at stage solver level
    // (LocalSearchStageSolverSpec), then use the stage solver level value as
    // a fallback. This allows per-stage customization to take precedence.
    if (!spec.parallelExecutionConfig().has_value() &&
        configs_.parallelExecutionConfig().has_value()) {
      spec.parallelExecutionConfig() =
          configs_.parallelExecutionConfig().value();
    }

    // Update the solve time after considering the remaining global and
    // minimum stage times.
    stageSolveTime_ = std::min(maxStageTime, remainingGlobalTime);
    if (multiStageTimeLimit.has_value()) {
      stageSolveTime_ = std::min(stageSolveTime_, multiStageTimeLimit.value());
    }
    // no matter what, if minStageTime is specified, we run the stage for at
    // least as much time
    stageSolveTime_ = std::max(minStageTime, stageSolveTime_);

    auto stage_name = stageName(stageId, stageConfig);
    stageConfig.name() = stage_name;

    std::string stageMoveLimitStr = "UNBOUNDED";
    if (auto stageMoveLimit = spec.stopAfterMoves()) {
      stageMoveLimitStr = std::to_string(*stageMoveLimit);
    }
    auto stageDescStr = fmt::format(
        "\n======= {} solve =======\n"
        "effective stage time limit = {:.2f}s (max global = {:.2f}s, remaining global = {:.2f}s; "
        "max stage = {:.2f}s, min stage = {:.2f}s)\nstage move limit = {}; using {} move type(s)",
        stage_name,
        stageSolveTime_,
        maxGlobalTime,
        remainingGlobalTime,
        maxStageTime,
        minStageTime,
        stageMoveLimitStr,
        spec.moveTypeList()->size());
    REBALANCER_LOG_SOLVER_OUTPUT(INFO, stageDescStr);

    stagesSummaries.emplace_back();
    stagesSummaries.back().name() = stage_name;

    auto [stageMaxMoves, isMaxMovesTillStage] = getStageMoveLimit(
        configs_, stageConfig, totalMoves_, stageId, stagesSummaries);

    const SolveParams solveParams{
        .profiler = profiler,
        .objectiveTupleStart = *stageConfig.begin(),
        .objectiveTupleEnd = *stageConfig.end(),
        .solveTime = stageSolveTime_,
        .stopAfterMoves = stageMaxMoves,
        .solveEventName = stage_name,
        .stopEventName = "stage",
        .enableObjectPotentialSorting =
            *stageConfig.solverSpec()->enableObjectPotentialSorting(),
        .exploreMovesFromContainersNotInObjective =
            *stageConfig.solverSpec()
                 ->exploreMovesFromContainersNotInObjective(),
        .stageId = std::make_optional(stageId),
        .higherPriorityObjConfig =
            stageConfig.higherPriorityObjConfig().to_optional(),
    };
    const auto stageStartTime = timer_.getSeconds();
    auto& stageMoveTypes = moveTypesPerStage.at(stageId);
    const CoreLocalSearchSolve coreLocalSearchSolve(
        spec, stageMoveTypes, p, solveParams, logger_);

    // update global times at the end of a stage
    totalEvalTime_ += coreLocalSearchSolve.getEvalTime();
    totalApplyTime_ += coreLocalSearchSolve.getApplyTime();
    totalFindTime_ += coreLocalSearchSolve.getFindTime();
    totalPruneTime_ += coreLocalSearchSolve.getPruneTime();

    auto& stageSummary = stagesSummaries.back();
    stageSummary.endReason() = coreLocalSearchSolve.getStopInfo().endReason;
    if (isMaxMovesTillStage &&
        *stageSummary.endReason() == interface::EndReason::HIT_MOVE_LIMIT) {
      stageSummary.endReason() = interface::EndReason::HIT_STAGE_MOVE_LIMIT;
    }
    stageSummary.auxEndInfo() = coreLocalSearchSolve.getStopInfo().auxInfo;
    stageSummary.duration() = coreLocalSearchSolve.getTotalDuration();
    stageSummary.moveStats() = coreLocalSearchSolve.getMoveStats();
    stageSummary.finalEvaluationSummary() =
        coreLocalSearchSolve.makeFinalEvaluationSummary();
    stageSummary.evalStats().from_optional(coreLocalSearchSolve.getEvalStats());

    totalMoves_ += coreLocalSearchSolve.getTotalMoves();
    for (auto& moveTime : coreLocalSearchSolve.getMoveTimes()) {
      moveTimes_.emplace_back(stageStartTime + moveTime.first, moveTime.second);
    }

    solverEvalSummary_.aggregate(coreLocalSearchSolve.getGlobalMoveStats());

    if (coreLocalSearchSolve.getTotalEvals() > 0) {
      lastStageWithEvaluations = stageId;
    }
  }

  return true;
}

// Calculate the max moves for a stage based on
// config in LocalSearchSolverSpec (stopAfterMoves)
// config in LocalSearchStageSpec (stopAfterMoves, stopAfterMovesTillStage)
std::tuple<std::optional<int>, bool> getStageMoveLimit(
    const interface::LocalSearchStageSolverSpec& globalConfig,
    const interface::LocalSearchStageSpec& stageConfig,
    int totalMoves,
    size_t stageId,
    const std::vector<interface::StageSummary>& stageSummaries) {
  auto& spec = *stageConfig.solverSpec();
  std::optional<int> maxMoves = std::nullopt;
  bool isMaxMovesTillStage = false;

  if (spec.stopAfterMoves() && *spec.stopAfterMoves() >= 0) {
    maxMoves = *spec.stopAfterMoves();
  }

  if (auto globalMaxMovesConfig = globalConfig.stopAfterMoves();
      globalMaxMovesConfig && *globalMaxMovesConfig >= 0) {
    // maxSteps for stage will be globalMaxMovesConfig - steps done so far
    auto globalMaxMoves = *globalMaxMovesConfig - totalMoves;
    if (!maxMoves || globalMaxMoves <= *maxMoves) {
      maxMoves = globalMaxMoves;
    }
  }

  // multi stage spec
  for (auto& multiStageConfig : *globalConfig.multiStageConfigs()) {
    if (multiStageConfig.stageIds()->contains(stageId) &&
        multiStageConfig.moveLimit().has_value()) {
      auto multiStageMaxMoves = *multiStageConfig.moveLimit() -
          getTotalMovesUsedSoFar(
              *multiStageConfig.stageIds(), stageSummaries, stageId);
      if (!maxMoves || multiStageMaxMoves <= *maxMoves) {
        maxMoves = multiStageMaxMoves;
      }
    }
  }

  if (auto stopAfterMovesTillStage = stageConfig.stopAfterMovesTillStage();
      stopAfterMovesTillStage && *stopAfterMovesTillStage >= 0) {
    // maxSteps for stage will be stopAfterMovesTillStage - steps done so far
    auto maxMovesTillStage = *stopAfterMovesTillStage - totalMoves;
    if (!maxMoves || maxMovesTillStage <= *maxMoves) {
      maxMoves = maxMovesTillStage;
      isMaxMovesTillStage = true;
    }
  }

  return std::make_tuple(maxMoves, isMaxMovesTillStage);
}

std::optional<double> getMultiStageTime(
    const interface::LocalSearchStageSolverSpec& globalConfig,
    size_t stageId,
    const std::vector<interface::StageSummary>& stageSummaries) {
  std::optional<double> maxTime = std::nullopt;
  for (auto& multiStageConfig : *globalConfig.multiStageConfigs()) {
    if (multiStageConfig.stageIds()->contains(stageId) &&
        multiStageConfig.solveTime().has_value()) {
      auto multiStageTime =
          multiStageConfig.solveTime().value() -
          getStageSolveTimeSoFar(
              *multiStageConfig.stageIds(), stageSummaries, stageId);
      if (!maxTime || multiStageTime < maxTime) {
        maxTime = multiStageTime;
      }
    }
  }
  return maxTime;
}

bool LocalSearchStageSolver::needs_continuous_expressions() {
  return true;
}

void LocalSearchStageSolver::setSolverOutputLogger(
    std::shared_ptr<SolverOutputLogger> logger) {
  logger_ = logger;
}

void LocalSearchStageSolver::writeToOutputLoggerMaybe(
    const std::string& logLine) const {
  if (logger_ != nullptr) {
    logger_->appendToLog(logLine);
  }
}
} // namespace facebook::rebalancer
