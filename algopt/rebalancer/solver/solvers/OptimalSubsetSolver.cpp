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

#include "algopt/rebalancer/solver/solvers/OptimalSubsetSolver.h"

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/iterators/ExpressionContainersIterator.h"
#include "algopt/rebalancer/solver/solvers/OptimalSolver.h"
#include "algopt/rebalancer/solver/solvers/Solver.h"
#include "algopt/rebalancer/solver/summary/GlobalLabeledObjectives.h"
#include "algopt/rebalancer/solver/summary/LabeledExpressions.h"
#include "algopt/rebalancer/solver/summary/LabeledObjectives.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/Change.h"
#include "algopt/rebalancer/solver/utils/ChangeSet.h"
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"
#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"
#include "algopt/rebalancer/solver/utils/Precision.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/solver/utils/ProblemConfigs.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <folly/container/F14Map.h>
#include <folly/container/HeterogeneousAccess.h>
#include <folly/hash/Hash.h>
#include <folly/lang/Assume.h>
#include <folly/lang/Hint.h>
#include <folly/logging/LogStreamProcessor.h>
#include <folly/logging/xlog.h>
#include <folly/Random.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <functional>
#include <map>
#include <math.h>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <stdint.h>

using namespace std;
using apache::thrift::can_throw;

namespace facebook::rebalancer {

namespace {

folly::F14FastMap<ExprPtr, double> getLabeledObjectiveValues(const Problem& p) {
  folly::F14FastMap<ExprPtr, double> objectiveValues;
  for (const auto& labeled_objective : p.getLabeledObjectives()) {
    for (auto& obj : labeled_objective) {
      objectiveValues.emplace(obj->expression, obj->expression->value);
    }
  }
  return objectiveValues;
}

void logImprovedObjectives(
    const Problem& p,
    const folly::F14FastMap<ExprPtr, double>& prevValues) noexcept {
  const auto& precision = p.getUniverse().getPrecision();
  for (const auto& labeled_objective : p.getLabeledObjectives()) {
    for (auto& obj : labeled_objective) {
      auto expr = obj->expression;
      auto prevValue = prevValues.at(expr);
      auto delta = expr->value - prevValue;
      if (precision.isStrictlyGtZero(std::abs(delta))) {
        XLOG(DBG1) << fmt::format(
            "Objective {} {} {} by {} from {} to {}",
            obj->name,
            expr->description,
            delta < 0 ? "improved" : "worsened",
            delta,
            prevValue,
            expr->value);
      }
    }
  }
}

} // namespace

OptimalSubsetSolver::OptimalSubsetSolver(
    const OptimalSubsetSolverSpec& inputConfigs) {
  configs = inputConfigs;
  for (auto& it : *configs.containerChoice()) {
    subset_makeup.emplace_back(it.first, it.second);
  }
}

// update subset solver profile with data from optimal solver at each stage
static void updateSolverProfile(
    const interface::ProblemProfile& opt_solver_profile,
    Profile profile) {
  if (!profile) {
    return;
  }
  auto stageProfile = opt_solver_profile.optimalSolverProfile();
  if (!stageProfile) {
    return;
  }

  auto& solverProfile =
      can_throw(profile->get().optimalSolverProfile()).value();
  *solverProfile.xpressBuildSec() += *stageProfile->xpressBuildSec();
  *solverProfile.xpressApplyInitialStateSec() +=
      *stageProfile->xpressApplyInitialStateSec();
  solverProfile.xpressMipOptimizeSec()->insert(
      solverProfile.xpressMipOptimizeSec()->end(),
      stageProfile->xpressMipOptimizeSec()->begin(),
      stageProfile->xpressMipOptimizeSec()->end());
  *solverProfile.postProcessingSec() += *stageProfile->postProcessingSec();

  // setting appropriate gaps. max of the absolute,
  // and max of the relative for worst stage
  solverProfile.gap()->absolute() = std::max(
      *solverProfile.gap()->absolute(), *stageProfile->gap()->absolute());
  solverProfile.gap()->relative() = std::max(
      *solverProfile.gap()->relative(), *stageProfile->gap()->relative());

  *solverProfile.dynamicContainers() += *stageProfile->dynamicContainers();
  *solverProfile.dynamicEquivalenceSets() +=
      *stageProfile->dynamicEquivalenceSets();

  if (stageProfile->isWarmStartSuccessful()) {
    if (!solverProfile.isWarmStartSuccessful()) {
      solverProfile.isWarmStartSuccessful() = true;
    }
    solverProfile.isWarmStartSuccessful() =
        (*solverProfile.isWarmStartSuccessful() &&
         *stageProfile->isWarmStartSuccessful());
  }
  if (stageProfile->warmStartProcessingTimeSec()) {
    if (!solverProfile.warmStartProcessingTimeSec()) {
      solverProfile.warmStartProcessingTimeSec() = 0;
    }
    *solverProfile.warmStartProcessingTimeSec() +=
        *stageProfile->warmStartProcessingTimeSec();
  }
}

bool OptimalSubsetSolver::solve(Problem& p, Profile profile) {
  const facebook::algopt::Timer timer(true);

  if (profile) {
    interface::OptimalSolverProfile solverProfile;
    profile->get().optimalSolverProfile() = solverProfile;
  }

  // TODO(pavanka): support objective tuple
  auto objective = p.objective.getOnlyObjective();

  // has any subproblem thrown error
  int32_t subProblemErrorCt = 0;

  // Use run id to seed randomness, purpose:
  //  1) New containers chosen on different rebalancer runs with different
  //     run-ids
  //  2) Reproducibility if we have a corner-case triggered in production
  XLOG(INFO) << fmt::format(
      "Seeding random generator with {}", p.configs.runId.run_id);
  std::seed_seq seed(
      p.configs.runId.run_id.begin(), p.configs.runId.run_id.end());
  std::mt19937 randomGenerator(seed);

  stringstream ss;
  ss << "Initial objective " << p.objective.getValue().toString();

  int round_cnt = 1;
  PackerSet<entities::ContainerId> chosen_this_round;

  PackerMap<
      entities::ContainerId /* container id */,
      size_t /* num times chosen as hot container */>
      hot_choices;

  double budget_coefficient = 1.0;
  auto numDynamicEquivSets = p.getDynamicEquivalentSets(p.containers).size();
  if (auto assignment_var_budget = configs.assignmentVarBudget()) {
    double subset_selection = std::accumulate(
        subset_makeup.begin(),
        subset_makeup.end(),
        0.0,
        [](const auto& total, const auto& iter) {
          return total + iter.second;
        });
    const double assignment_vars =
        numDynamicEquivSets * p.containers.size() * subset_selection;
    if (assignment_vars > 0) {
      budget_coefficient =
          std::min(1.0, *assignment_var_budget / assignment_vars);
    }
    XLOG(INFO) << fmt::format(
        "assignment_var_budget={}, equiv_sets={}, containers={}, "
        "subset_selection={}, budget_coefficient={}",
        *assignment_var_budget,
        numDynamicEquivSets,
        p.containers.size(),
        subset_selection,
        budget_coefficient);
  }

  // Regrettable moves are move hints to be used on non-dynamic containers. The
  // solver is free to ignore regrettable moves affecting dynamic containers and
  // override them with better ones.
  ChangeSet regrettableChanges;
  for (auto& [objectName, dstContainerName] : *configs.regrettableMoves()) {
    auto objectId = p.objectId(objectName);
    if (p.assignment.isDynamic(objectId)) {
      auto srcContainerId = p.assignment.getContainer(objectId);
      auto dstContainerId = p.containerId(dstContainerName);
      if (srcContainerId != dstContainerId) {
        regrettableChanges.insert(Change(objectId, srcContainerId, -1));
        regrettableChanges.insert(Change(objectId, dstContainerId, 1));
      }
    }
  }

  if (!regrettableChanges.empty()) {
    // By applying the regrettable moves before doing any optimization with the
    // solver we achieve 2 things:
    // 1. The constants used in place of variables for non-dynamic containers
    //    will match this assignment, so they will reflect the regrettable
    //    moves.
    // 2. The variables for dynamic containers will be initialized for warm
    //    start with the regrettable moves, which typically represents a
    //    partially optimized state.
    p.apply(regrettableChanges);
  }

  // The current assignment (after applying regrettable changes) is an
  // interesting assignment for the subset solver. In particular, subset solver
  // tries to improve upon this assignment and corresponding objective value.
  ss << "\nInitial objective after applying regrettable moves: "
     << p.objective.getValue().toString();

  const double beforeSubsetSolveObjValue = p.objective.getValue().get(0);
  auto labeledObjValuesBeforeSubsetSolve = getLabeledObjectiveValues(p);

  for (int iter = 1; (*configs.overallTime() == 0 ||
                      timer.getSeconds() < *configs.overallTime()) &&

       (*configs.maxSubsetRuns() == 0 || iter <= *configs.maxSubsetRuns());
       iter++) {
    PackerSet<entities::ContainerId> containers;
    auto& all = objective->getAllAffectedContainers();
    if (all.empty() || p.initial_assignment.getObjects().size() == 0) {
      // If there are no objects or no containers that affect the objective, the
      // objective is constant, there is nothing to solve.
      return true;
    }
    vector<entities::ContainerId> sorted_order(all.begin(), all.end());
    std::sort(sorted_order.begin(), sorted_order.end());
    std::vector<entities::ContainerId> descending;
    if (p.getDescendingHotnessContainersOverride().empty()) {
      const DescendingExpressionContainersTraversal temp(p.objective.getView());
      descending = std::vector<entities::ContainerId>(temp.begin(), temp.end());
    } else {
      descending = p.getDescendingHotnessContainersOverride();
    }

    std::vector<entities::ContainerId> ascending(
        descending.begin(), descending.end());
    std::reverse(ascending.begin(), ascending.end());

    auto inserter = [&](entities::ContainerId container) {
      chosen_this_round.insert(container);
      containers.insert(container);
    };

    for (const auto& container_name : *configs.alwaysChosenContainers()) {
      inserter(p.containerId(container_name));
    }

    for (auto& type_num : subset_makeup) {
      int num = round(type_num.second);
      if (type_num.second <= 1.0) {
        num = ceil(budget_coefficient * type_num.second * p.containers.size());
      }
      XLOGF(INFO, "{} {}", type_num.first, num);
      const size_t target = min((int)containers.size() + num, (int)all.size());
      if (type_num.first == "HOT") {
        auto it = descending.begin();
        while (containers.size() < target) {
          hot_choices[*it]++;
          inserter(*it);
          ++it;
        }
      } else if (type_num.first == "COLD") {
        auto it = ascending.begin();
        while (containers.size() < target) {
          inserter(*it);
          ++it;
        }
      } else if (type_num.first == "RANDOM") {
        while (containers.size() < target) {
          if (chosen_this_round.size() == sorted_order.size()) {
            round_cnt++;
            chosen_this_round.clear();
          }
          std::optional<entities::ContainerId> choice = std::nullopt;
          do {
            choice = sorted_order.at(
                folly::Random::rand32(sorted_order.size(), randomGenerator));
          } while (chosen_this_round.contains(*choice));
          inserter(*choice);
        }
      } else {
        throw std::runtime_error(
            fmt::format("Unrecognized makeup type {}", type_num.first));
      }
    }

    XLOG(INFO) << fmt::format(
        "Chose {} of {} total containers to be dynamic, "
        "their hash is {:#x}. This is round {} where we have chosen {} "
        "containers so far.",
        containers.size(),
        all.size(),
        folly::hash::twang_32from64(
            folly::hash::commutative_hash_combine_range(
                containers.begin(), containers.end())),
        round_cnt,
        chosen_this_round.size());
    const algopt::Timer inner_timer(true);
    auto tmp_assign = p.assignment;
    configs.optimalConfig()->solveTime() = *configs.perSubsetTime();

    if (*configs.overallTime() > 0) {
      configs.optimalConfig()->solveTime() =
          min(*configs.perSubsetTime(),
              *configs.overallTime() - timer.getSeconds());
    }
    OptimalSolver opt_solver(*configs.optimalConfig());
    size_t num_containers_dynamic = containers.size();

    interface::ProblemProfile stage_profile;
    try {
      opt_solver.solve(p, std::move(containers), std::ref(stage_profile));
      updateSolverProfile(stage_profile, profile);

      auto numDynamicEquivSetsSelected =
          opt_solver.getNumDynamicEquivalenceSets();
      const auto& optimalSolverProfile =
          *can_throw(stage_profile.optimalSolverProfile());
      ss << fmt::format(
          "\nIteration {}, objective {}, moves {}, total moves {}, "
          "equivalence sets ({}/{}), containers ({}/{}), solve time {}, "
          "relative gap {}, absolute gap {}",
          iter,
          p.objective.getValue().toString(),
          p.assignment.delta(tmp_assign),
          p.assignment.delta(p.initial_assignment),
          numDynamicEquivSetsSelected,
          numDynamicEquivSets,
          num_containers_dynamic,
          p.containers.size(),
          inner_timer.getSeconds(),
          *optimalSolverProfile.gap()->relative(),
          *optimalSolverProfile.gap()->absolute());
    } catch (std::exception&) {
      subProblemErrorCt++;
      if (subProblemErrorCt > *configs.maxSubproblemErrors()) {
        XLOG(INFO) << ss.str();
        XLOG(ERR) << fmt::format(
            "Number of infeasible subproblem excceds threshold {} ",
            *configs.maxSubproblemErrors());
        throw;
      }
      XLOG(INFO) << fmt::format(
          "Iteration {}, found an infeasible solution so ignoring the solution",
          iter);
      iter--;
    }
  }
  XLOG(INFO) << ss.str();
  XLOG(INFO) << "Improvement in objective due to subset solve :"
             << beforeSubsetSolveObjValue - p.objective.getValue().get(0);
  XLOG(DBG1) << "Specific Objective changes after subset solve: ";
  logImprovedObjectives(p, labeledObjValuesBeforeSubsetSolve);

  std::map<size_t /* hot choices */, size_t /* num containers */> histogram;
  std::vector<std::pair<
      size_t /* hot choices */,
      entities::ContainerId /* container id */>>
      ordered;
  for (auto& [container, cnt] : hot_choices) {
    ordered.emplace_back(cnt, container);
    histogram[cnt]++;
  }
  std::sort(ordered.begin(), ordered.end());

  XLOG(INFO) << "Histogram: Hot choices -> number of containers";
  for (auto it = histogram.rbegin(); it != histogram.rend(); it++) {
    XLOG(INFO) << fmt::format("{}: {}", it->first, it->second);
  }
  XLOG(INFO) << "Top10 hot containers: Container -> number times chosen as hot";
  for (auto it = ordered.rbegin();
       it != ordered.rend() && it - ordered.rbegin() < 10;
       it++) {
    XLOG(INFO) << fmt::format(
        "  {}: {}", p.containerName(it->second), it->first);
  }

  return true;
}
} // namespace facebook::rebalancer
