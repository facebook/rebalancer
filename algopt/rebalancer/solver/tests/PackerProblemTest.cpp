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

#include "algopt/rebalancer/interface/Constants.h"
#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/interface/tests/utils.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSolver_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/tests/SolverTestUtils.h"

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <fmt/format.h>
#include <folly/ConstexprMath.h>
#include <folly/container/F14Map.h>
#include <folly/container/irange.h>
#include <folly/container/MapUtil.h>
#include <folly/ExceptionWrapper.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/lang/Assume.h>
#include <folly/lang/Bits.h>
#include <folly/lang/Hint.h>
#include <folly/logging/Init.h>
#include <folly/logging/LogStreamProcessor.h>
#include <folly/logging/xlog.h>
#include <folly/Range.h>
#include <folly/tracing/AsyncStack.h>
#include <folly/Traits.h>
#include <folly/Utility.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/FieldRef.h>
#include <thrift/lib/cpp2/op/Get.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <ostream>
#include <ranges>
#include <stdint.h>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

/************
 * This file provides unit tests for complete problem definition
 * using rebalancer library
 * from algopt/rebalancer/interface/...
 * to algopt/rebalancer/solver/...
 *
 * it is a copied from packer_test.py, but there it has
 * look_at_imbalance_goals_independently = false
 * here restrict_moving_object_only_once = true
 * since some of the specs use square which is handled differently in Local and
 * Optimal; some values for optimal differ slightly
 * TODO -> enable MIP tests after resolving T43936150
 ************/

using namespace folly;
using namespace std;
using namespace facebook::rebalancer::interface;

class PackerProblemTestSolvers
    : public ::testing::TestWithParam<
          std::tuple<SolverAlgoType, OptimalSolverPackage>> {
  void SetUp() override {
    folly::initLoggingOrDie("DBG1");
    const auto [algoType, solver] = GetParam();
    if (algoType == OPTIMAL) {
      if (facebook::algopt::isSolverUnavailable(solver)) {
        GTEST_SKIP() << "Solver " << facebook::algopt::solverName(solver)
                     << " not available";
      }
    }
  }
};

// using optimal solver
INSTANTIATE_TEST_CASE_P(
    Optimal,
    PackerProblemTestSolvers,
    ::testing::Combine(
        ::testing::Values(OPTIMAL),
        facebook::algopt::testSolverPackages()));

// using localSearch solver
INSTANTIATE_TEST_CASE_P(
    LocalSearch,
    PackerProblemTestSolvers,
    ::testing::Combine(
        ::testing::Values(LOCALSEARCH),
        ::testing::Values(OptimalSolverPackage::GUROBI)));

/*
 * utility APIs
 */

static std::unique_ptr<ProblemSolver> makeProblem(
    const std::map<std::string, std::vector<std::string>>& initial_assignment) {
  auto executor = make_shared<CPUThreadPoolExecutor>(1);
  auto problem =
      std::make_unique<ProblemSolver>(executor, "rebalancer", "tests");
  problem->setObjectName("object");
  problem->setContainerName("container");
  problem->setAssignment(initial_assignment);
  problem->shouldUseDynamicObjectOrdering(true);

  auto params = ManifoldBackupParams();
  params.uploadPolicy() = ManifoldUploadPolicy::NEVER;

  problem->setManifoldBackupParams(params);
  problem->enableStableAsMuchAsPossible();

  return problem;
}
static std::map<std::string, int> genHostCounts(
    const AssignmentSolution& solution) {
  std::map<std::string, int> hostCount;
  for (const auto& it : *solution.assignment()) {
    hostCount[it.second]++;
  }
  return hostCount;
}

static AssignmentSolution localSearchSolveWithDefinedMoves(
    const std::unique_ptr<ProblemSolver>& problem,
    std::vector<MoveTypeSpec>&& moveTypes) {
  LocalSearchSolverSpec localSearch;
  localSearch.solveTime() = 10;
  localSearch.moveTypeList() = std::move(moveTypes);
  problem->addSolver(localSearch);
  return problem->solve();
}

static AssignmentSolution solve(
    const std::unique_ptr<ProblemSolver>& problem,
    SolverAlgoType solverAlgoType,
    std::optional<OptimalSolverPackage> solverPackage = std::nullopt) {
  switch (solverAlgoType) {
    case OPTIMAL: {
      OptimalSolverSpec optimalSpec;
      if (solverPackage.has_value()) {
        optimalSpec.solverPackage() = solverPackage.value();
      }
      problem->addSolver(optimalSpec);
      break;
    }
    case LOCALSEARCH: {
      LocalSearchSolverSpec lsSpec;
      lsSpec.moveTypeList()->push_back(
          ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
      lsSpec.moveTypeList()->push_back(
          ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec()));
      lsSpec.moveTypeList()->push_back(
          ProblemSolver::makeMoveTypeSpec(TripleLoopMoveTypeSpec()));
      lsSpec.moveTypeList()->push_back(
          ProblemSolver::makeMoveTypeSpec(KLSearchMoveTypeSpec()));
      problem->addSolver(lsSpec);
      break;
    }
  }

  return problem->solve();
}

static std::unique_ptr<ProblemSolver> create3PerfectShardsProblem(
    const std::map<std::string, std::vector<std::string>>& initial_assignment) {
  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "a",
      std::vector<std::pair<std::string, double>>(
          {{"t1", 4.0}, {"t2", 3.0}, {"t3", 1.0}}));
  problem->addObjectDimension(
      "b", std::map<std::string, double>{{"t1", 4}, {"t2", 2}, {"t3", 2}});
  return problem;
}

static std::unique_ptr<ProblemSolver> create4PerfectShardsProblem(
    const std::map<std::string, std::vector<std::string>>& initial_assignment) {
  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "a",
      folly::F14FastMap<std::string, double>(
          {{"t1", 4}, {"t2", 1}, {"t3", 2}, {"t4", 3}}));
  problem->addObjectDimension(
      "b",
      std::map<std::string, double>{
          {"t1", 1}, {"t2", 1}, {"t3", 1}, {"t4", 1}});
  return problem;
}

static std::unique_ptr<ProblemSolver> createSimplyBalanceableProblem(
    const std::map<std::string, std::vector<std::string>>& initial_assignment) {
  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "a",
      std::map<std::string, double>{
          {"t1", 12}, {"t2", 4}, {"t3", 1}, {"t4", 4}, {"t5", 4}});
  return problem;
}

static std::unique_ptr<ProblemSolver> create2DimOppositeProblem(
    const std::map<std::string, std::vector<std::string>>& initial_assignment) {
  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "a",
      std::map<std::string, double>{
          {"t1", 2},
          {"t2", 2},
          {"t3", 2},
          {"t4", 2},
          {"t5", 3},
          {"t6", 3},
          {"t7", 3},
          {"t8", 3}});
  problem->addObjectDimension(
      "b",
      std::unordered_map<std::string, double>{
          {"t1", 3},
          {"t2", 3},
          {"t3", 3},
          {"t4", 3},
          {"t5", 2},
          {"t6", 2},
          {"t7", 2},
          {"t8", 2}});
  return problem;
}

static void addCapacityGoal(
    const std::unique_ptr<ProblemSolver>& problem,
    const std::string& dimension,
    CapacitySpecDefinition definition,
    CapacitySpecBound bound,
    double limitRatio,
    bool absoluteLimit = false,
    const std::string& scope = "container") {
  CapacitySpec capacitySpec;

  Limit limit;
  limit.type() = absoluteLimit ? LimitType::ABSOLUTE : LimitType::RELATIVE;
  limit.globalLimit() = limitRatio;

  capacitySpec.scope() = scope;
  capacitySpec.dimension() = dimension;
  capacitySpec.definition() = definition;
  capacitySpec.bound() = bound;
  capacitySpec.limit() = limit;
  problem->addGoal(capacitySpec);
}

static void addCapacityConstraint(
    const std::unique_ptr<ProblemSolver>& problem,
    const std::string& dimension,
    CapacitySpecDefinition definition,
    CapacitySpecBound bound,
    double limitRatio,
    const std::string& scope = "container") {
  CapacitySpec capacitySpec;

  Limit limit;
  limit.type() = LimitType::RELATIVE;
  limit.globalLimit() = limitRatio;

  capacitySpec.scope() = scope;
  capacitySpec.dimension() = dimension;
  capacitySpec.definition() = definition;
  capacitySpec.bound() = bound;
  capacitySpec.limit() = limit;

  problem->addConstraint(capacitySpec);
}

static void addAfterConstraint(
    const std::unique_ptr<ProblemSolver>& problem,
    const std::string& dimension,
    double limitRatio,
    const std::string& scope = "container") {
  addCapacityConstraint(
      problem,
      dimension,
      CapacitySpecDefinition::AFTER,
      CapacitySpecBound::MAX,
      limitRatio,
      scope);
}

static void addDuringAndAfterConstraint(
    const std::unique_ptr<ProblemSolver>& problem,
    const std::string& dimension,
    double limitRatio,
    const std::string& scope = "container") {
  addCapacityConstraint(
      problem,
      dimension,
      CapacitySpecDefinition::DURING_AND_AFTER,
      CapacitySpecBound::MAX,
      limitRatio,
      scope);
}

static void assertFinalAssignment(
    const std::map<std::string, std::vector<std::string>>& final_assignment,
    const AssignmentSolution& solution) {
  map<std::string, std::string> expected_assignment;
  for (auto& it : final_assignment) {
    for (auto& object : it.second) {
      expected_assignment[object] = it.first;
    }
  }
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
}

static void assertGroupAssignment(
    const AssignmentSolution& solution,
    const std::map<std::string, std::string>& partition,
    const int group_limit,
    const int groups_allowed) {
  map<std::string, map<std::string, int>> groupStats;
  for (auto& it : *solution.assignment()) {
    groupStats[it.second][partition.at(it.first)]++;
  }
  for (auto& it : groupStats) {
    int violators = 0;
    for (auto& groupInfo : it.second) {
      if (groupInfo.second > group_limit) {
        violators++;
      }
    }

    EXPECT_TRUE(violators <= groups_allowed);
  }
}

static void checkGroupCapacitySolution(
    GroupCapacitySpecBound bound,
    std::map<std::string, double> groupLimits,
    std::map<std::string, double> solution_values) {
  for (const auto& [job, ovalue] : solution_values) {
    if (bound == GroupCapacitySpecBound::MAX) {
      EXPECT_TRUE(ovalue <= groupLimits[job]);
    }
    if (bound == GroupCapacitySpecBound::MIN) {
      EXPECT_TRUE(ovalue >= groupLimits[job]);
    }
    if (bound == GroupCapacitySpecBound::EXACT) {
      EXPECT_NEAR(ovalue, groupLimits[job], 1e-10);
    }
  }
}

template <typename T>
constexpr bool is_valid_rb_interface_dimension_type =
    std::is_same<T, std::vector<std::pair<std::string, double>>>::value ||
    std::is_same<T, folly::F14FastMap<std::string, double>>::value;

template <
    typename T,
    typename _U = std::enable_if<is_valid_rb_interface_dimension_type<T>>>
std::pair<
    AssignmentSolution,
    std::map<std::string, std::map<std::string, int8_t>>>
solveGroupCapacityProblem(
    const std::map<std::string, std::vector<std::string>>& initial,
    GroupCapacitySpecDefinition definition,
    GroupCapacitySpecBound bound,
    const std::map<std::string, double>& groupLimits,
    const std::map<std::string, std::map<std::string, double>>&
        scopeItemContributions,
    const T& hostCapacity,
    bool expected_feasible,
    SolverAlgoType solverAlgoType,
    std::optional<OptimalSolverPackage> solverPackage = std::nullopt) {
  auto problem = makeProblem(initial);
  problem->addObjectDimension(
      "count_dim",
      std::unordered_map<std::string, double>{
          {"t1", 1},
          {"t2", 1},
          {"t3", 1},
          {"t4", 1},
          {"t5", 1},
          {"t6", 1},
          {"t7", 1},
          {"t8", 1}});
  problem->addScopeDimension("count_dim", "container", hostCapacity);

  std::map<std::string, std::string> rack_map = {
      {"h1", "rack1"}, {"h2", "rack1"}, {"h3", "rack2"}};
  const std::map<std::string, std::string> job_map = {
      {"t1", "job1"},
      {"t2", "job1"},
      {"t3", "job2"},
      {"t4", "job2"},
      {"t5", "job3"},
      {"t6", "job4"},
      {"t7", "job4"},
      {"t8", "job4"}};
  problem->addPartition("job", job_map);
  problem->addScope("rack", rack_map);

  CapacitySpec capacitySpec;
  capacitySpec.scope() = "container";
  capacitySpec.dimension() = "count_dim";
  problem->addConstraint(capacitySpec);

  GroupCapacitySpec groupCapacitySpec;
  groupCapacitySpec.scope() = "rack";
  groupCapacitySpec.partitionName() = "job";
  groupCapacitySpec.contributionPartition() = "job";
  groupCapacitySpec.definition() = definition;
  groupCapacitySpec.bound() = bound;

  Limit limit;
  limit.type() = LimitType::ABSOLUTE;
  limit.globalLimit() = 1;
  limit.groupLimits() = groupLimits;
  groupCapacitySpec.limit() = limit;

  Limit contribution;
  contribution.type() = LimitType::ABSOLUTE;
  contribution.globalLimit() = 0.0;
  contribution.scopeItemToGroupLimits() = scopeItemContributions;
  groupCapacitySpec.contribution() = contribution;

  groupCapacitySpec.limit() = limit;
  groupCapacitySpec.contribution() = contribution;
  problem->addConstraint(groupCapacitySpec);

  auto solution = solve(problem, solverAlgoType, solverPackage);
  std::map<std::string, double> job_limits;
  std::map<std::string, std::map<std::string, int8_t>> rack_spread;
  for (const auto& [task, job] : job_map) {
    auto assigned_rack = rack_map.at(solution.assignment()->at(task));
    rack_spread[job][assigned_rack] += 1;
    job_limits[job] += scopeItemContributions.at(assigned_rack).at(job);
  }

  if (expected_feasible) {
    EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
    checkGroupCapacitySolution(bound, groupLimits, job_limits);
  } else {
    EXPECT_TRUE(*solution.finalConstraint()->brokenCount() > 0);
  }

  return std::make_pair(solution, rack_spread);
}

/*
 * START of TESTs
 */

TEST_P(PackerProblemTestSolvers, MovesInProgress) {
  auto problem = makeProblem({{"h1", {"t1"}}, {"h2", {}}});

  auto move = MoveInProgress();
  move.objName() = "t1";
  move.toContainer() = "h2";

  auto spec = MovesInProgressSpec();
  spec.moves() = {move};

  problem->addConstraint(spec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  const map<std::string, std::string> expected_assignment = {{"t1", "h2"}};
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
}

// initially capacity allocated to h1: dim(a) = 201, dim(b) = 102. the requested
// capacity dim(a) >= 150, dim(b) >= 2. The upper limit is
// dim(a) <= 200 || dim(b) <= 3. so t1 is moved to h2 (blacklisted so that the
// constraints do not apply)
TEST_P(PackerProblemTestSolvers, MultipleOrCapacity) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t3"}}, {"h2", {}}};
  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "a",
      std::unordered_map<std::string, double>{
          {"t1", 1}, {"t2", 100}, {"t3", 100}});
  problem->addObjectDimension(
      "b",
      std::unordered_map<std::string, double>{
          {"t1", 100}, {"t2", 1}, {"t3", 1}});
  {
    auto spec = CapacitySpec();
    spec.scope() = "container";
    spec.dimension() = "a";
    spec.definition() = CapacitySpecDefinition::AFTER;
    spec.bound() = CapacitySpecBound::MIN;
    spec.limit()->type() = LimitType::RELATIVE;
    spec.limit()->globalLimit() = 150;
    spec.filter()->itemsBlacklist() = {{"h2"}};

    problem->addConstraint(spec);
  }
  {
    auto spec = CapacitySpec();
    spec.scope() = "container";
    spec.dimension() = "b";
    spec.definition() = CapacitySpecDefinition::AFTER;
    spec.bound() = CapacitySpecBound::MIN;
    spec.limit()->type() = LimitType::RELATIVE;
    spec.limit()->globalLimit() = 2;
    spec.filter()->itemsBlacklist() = {{"h2"}};

    problem->addConstraint(spec);
  }

  std::vector<CapacitySpec> capacitySpecs;
  {
    auto spec = CapacitySpec();
    spec.scope() = "container";
    spec.dimension() = "a";
    spec.definition() = CapacitySpecDefinition::AFTER;
    spec.bound() = CapacitySpecBound::MAX;
    spec.limit()->type() = LimitType::RELATIVE;
    spec.limit()->globalLimit() = 200;
    spec.filter()->itemsBlacklist() = {{"h2"}};

    capacitySpecs.push_back(spec);
  }
  {
    auto spec = CapacitySpec();
    spec.scope() = "container";
    spec.dimension() = "b";
    spec.definition() = CapacitySpecDefinition::AFTER;
    spec.bound() = CapacitySpecBound::MAX;
    spec.limit()->type() = LimitType::RELATIVE;
    spec.limit()->globalLimit() = 3;
    spec.filter()->itemsBlacklist() = {{"h2"}};

    capacitySpecs.push_back(spec);
  }

  auto spec = MultipleOrCapacitySpec();
  spec.capacitySpecs() = capacitySpecs;
  problem->addConstraint(spec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  const map<std::string, std::string> expected_assignment = {
      {"t1", "h2"}, {"t2", "h1"}, {"t3", "h1"}};
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
}

TEST_P(PackerProblemTestSolvers, TestEmptyContainer) {
  auto problem = makeProblem({{"h1", {"t1", "t2"}}, {"h2", {}}});
  BalanceSpec balanceSpec;
  balanceSpec.scope() = "container";
  balanceSpec.dimension() = "object_count";
  balanceSpec.formula() = BalanceSpecFormula::LEGACY;
  balanceSpec.fixAverageToInitial() = true;
  problem->addGoal(balanceSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  EXPECT_EQ(0, *solution.finalObjective()->value());
}

TEST_P(PackerProblemTestSolvers, TestMovePerfectBalance) {
  auto problem =
      create3PerfectShardsProblem({{"h1", {"t1", "t2"}}, {"h2", {"t3"}}});
  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "a";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }
  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "b";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  EXPECT_NEAR(
      isOptimal(solverAlgoType) ? 1.2506249999999999 : 1.25040625,
      *solution.initialObjective()->value(),
      1e-8);
  EXPECT_EQ(0, *solution.finalObjective()->value());
}

TEST(PackerProblemLocalSearchTest, TestSwapPerfectBalance) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t3"}}, {"h2", {"t2", "t4"}}};
  const map<std::string, std::string> expected_assignment1 = {
      {"t1", "h1"}, {"t2", "h2"}, {"t3", "h1"}, {"t4", "h2"}};
  const map<std::string, std::string> expected_assignment2 = {
      {"t1", "h1"}, {"t2", "h1"}, {"t3", "h2"}, {"t4", "h2"}};
  const map<std::string, std::string> expected_assignment3 = {
      {"t1", "h1"}, {"t3", "h1"}, {"t2", "h2"}, {"t4", "h2"}};

  {
    auto problem = create4PerfectShardsProblem(initial_assignment);
    {
      BalanceSpec balanceSpec;
      balanceSpec.scope() = "container";
      balanceSpec.dimension() = "a";
      balanceSpec.formula() = BalanceSpecFormula::LEGACY;
      balanceSpec.fixAverageToInitial() = true;
      problem->addGoal(balanceSpec);
    }
    {
      BalanceSpec balanceSpec;
      balanceSpec.scope() = "container";
      balanceSpec.dimension() = "b";
      balanceSpec.formula() = BalanceSpecFormula::LEGACY;
      balanceSpec.fixAverageToInitial() = true;
      problem->addGoal(balanceSpec);
    }
    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = {"t1"};
    problem->addConstraint(avoidMovingSpec);

    auto solution = localSearchSolveWithDefinedMoves(
        problem, {ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec())});
    EXPECT_EQ(expected_assignment1, toOrderedMap(*solution.assignment()));
  }

  {
    auto problem = create4PerfectShardsProblem(initial_assignment);
    {
      BalanceSpec balanceSpec;
      balanceSpec.scope() = "container";
      balanceSpec.dimension() = "a";
      balanceSpec.formula() = BalanceSpecFormula::LEGACY;
      balanceSpec.fixAverageToInitial() = true;
      problem->addGoal(balanceSpec);
    }
    {
      BalanceSpec balanceSpec;
      balanceSpec.scope() = "container";
      balanceSpec.dimension() = "b";
      balanceSpec.formula() = BalanceSpecFormula::LEGACY;
      balanceSpec.fixAverageToInitial() = true;
      problem->addGoal(balanceSpec);
    }
    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = {"t1"};
    problem->addConstraint(avoidMovingSpec);

    auto solution = localSearchSolveWithDefinedMoves(
        problem, {ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec())});
    EXPECT_EQ(expected_assignment2, toOrderedMap(*solution.assignment()));
    EXPECT_EQ(0, *solution.finalObjective()->value());
  }

  {
    auto problem = create4PerfectShardsProblem(initial_assignment);
    addAfterConstraint(problem, "a", 5);
    addAfterConstraint(problem, "b", 2);

    auto spec = AvoidMovingSpec();
    spec.objects() = {"t1"};

    problem->addConstraint(spec);

    auto solution = localSearchSolveWithDefinedMoves(
        problem, {ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec())});
    EXPECT_EQ(expected_assignment2, toOrderedMap(*solution.assignment()));
    EXPECT_EQ(0, *solution.finalObjective()->value());
    EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  }

  {
    auto problem = create4PerfectShardsProblem(initial_assignment);
    addAfterConstraint(problem, "a", 3);
    addAfterConstraint(problem, "b", 2);

    auto spec = AvoidMovingSpec();
    spec.objects() = {"t1"};

    problem->addConstraint(spec);

    auto solution = localSearchSolveWithDefinedMoves(
        problem, {ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec())});
    EXPECT_EQ(expected_assignment1, toOrderedMap(*solution.assignment()));
    EXPECT_EQ(1, *solution.finalConstraint()->brokenCount());
    EXPECT_EQ(4, *solution.finalConstraint()->brokenVal());
  }

  {
    auto problem = create4PerfectShardsProblem(initial_assignment);

    {
      auto spec = AvoidMovingSpec();
      spec.objects() = {"t1"};

      problem->addConstraint(spec);
    }

    {
      auto spec = AvoidMovingSpec();
      spec.objects() = {"t2"};

      problem->addConstraint(spec);
    }

    {
      auto spec = AvoidMovingSpec();
      spec.objects() = {"t3", "t4"};

      problem->addConstraint(spec);
    }

    auto solution = localSearchSolveWithDefinedMoves(
        problem, {ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec())});
    EXPECT_EQ(expected_assignment3, toOrderedMap(*solution.assignment()));
    EXPECT_EQ(0, *solution.finalObjective()->value());
    EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  }
}

TEST(PackerProblemLocalSearchTest, TestDuringAndAfterCapacity) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t3"}}, {"h2", {"t2", "t4"}}};
  const map<std::string, std::string> expected_assignment = {
      {"t1", "h1"}, {"t2", "h2"}, {"t3", "h1"}, {"t4", "h2"}};

  auto problem = create4PerfectShardsProblem(initial_assignment);
  addDuringAndAfterConstraint(problem, "a", 3);
  addDuringAndAfterConstraint(problem, "b", 2);

  auto spec = AvoidMovingSpec();
  spec.objects() = {"t1"};

  problem->addConstraint(spec);

  auto solution = localSearchSolveWithDefinedMoves(
      problem, {ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec())});
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
  EXPECT_EQ(1, *solution.finalConstraint()->brokenCount());
  EXPECT_EQ(8, *solution.finalConstraint()->brokenVal());
}

TEST_P(PackerProblemTestSolvers, TestCapacitySkipItems) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1"}}, {"h2", {"t2"}}};

  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "s", std::unordered_map<std::string, double>{{"t1", 10}, {"t2", 10}});
  problem->addContainerDimension(
      "s", std::map<std::string, double>{{"h1", 20}, {"h2", 1}});

  {
    auto spec = AvoidMovingSpec();
    spec.objects() = {"t1"};

    problem->addConstraint(spec);
  }

  {
    auto spec = CapacitySpec();
    spec.scope() = "container";
    spec.dimension() = "s";
    spec.definition() = CapacitySpecDefinition::AFTER;
    spec.bound() = CapacitySpecBound::MAX;
    spec.limit()->type() = LimitType::RELATIVE;
    spec.limit()->globalLimit() = 1;
    spec.filter()->itemsBlacklist() = {{"h2"}};

    problem->addConstraint(spec);
  }

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  EXPECT_EQ(0, *solution.initialObjective()->value());
  EXPECT_EQ(0, *solution.finalObjective()->value());
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
}

/*
 * three containers
 * c1: a(1.0) b(0.5)
 * c2: c(0.7)
 * c3: d(0.5) e(0.3)
 * First B moves c1->c2. Then without restrict_moving_object_only_once, B and E
 * would want to swap. Looking at this, it's not clear how initial apply is
 * handled differently (and if it's not, than all objects are just marked
 * immutable
 */
TEST_P(PackerProblemTestSolvers, TestMoveOnlyOnce) {
  /* Problem with this config set */
  {
    auto problem =
        makeProblem({{"c1", {"a", "b"}}, {"c2", {"c"}}, {"c3", {"d", "e"}}});
    problem->addObjectDimension(
        "dimension",
        std::unordered_map<std::string, double>{
            {"a", 1}, {"b", 0.5}, {"c", 0.7}, {"d", 0.5}, {"e", 0.3}});

    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "dimension";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);

    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = {"c", "d"};
    problem->addConstraint(avoidMovingSpec);

    problem->enableRestrictMovingObjectOnlyOnce();

    const auto [solverAlgoType, solverPackage] = GetParam();
    auto solution = solve(problem, solverAlgoType, solverPackage);
    map<std::string, std::string> expected_assignment;
    expected_assignment = {
        {"a", "c1"}, {"b", "c2"}, {"c", "c2"}, {"d", "c3"}, {"e", "c3"}};
    if (!isOptimal(solverAlgoType)) {
      // TODO: Check optimal and enableRestrictMovingObjectOnlyOnce effect
      EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
    }
  }

  /* Problem without this config set */
  {
    auto problem =
        makeProblem({{"c1", {"a", "b"}}, {"c2", {"c"}}, {"c3", {"d", "e"}}});
    problem->addObjectDimension(
        "dimension",
        std::unordered_map<std::string, double>{
            {"a", 1}, {"b", 0.5}, {"c", 0.7}, {"d", 0.5}, {"e", 0.3}});
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "dimension";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);

    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = {"c", "d"};
    problem->addConstraint(avoidMovingSpec);

    const auto [solverAlgoType, solverPackage] = GetParam();
    auto solution = solve(problem, solverAlgoType, solverPackage);
    const std::map<std::string, std::string> expected_assignment = {
        {"a", "c1"}, {"b", "c3"}, {"c", "c2"}, {"d", "c3"}, {"e", "c2"}};
    EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
  }
}

TEST_P(PackerProblemTestSolvers, TestDrainHasPerfectBalance) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t3"}}, {"h2", {"t2", "t4"}}};
  const map<std::string, std::string> expected_assignment = {
      {"t1", "h1"}, {"t2", "h1"}, {"t3", "h1"}, {"t4", "h1"}};

  auto problem = create4PerfectShardsProblem(initial_assignment);
  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "a";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }
  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "b";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }
  ToFreeSpec toFreeSpec;
  toFreeSpec.containers() = {"h2"};
  problem->addConstraint(toFreeSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
  EXPECT_NEAR(
      isOptimal(solverAlgoType) ? 10200.2001 : 10200.20002,
      *solution.initialObjective()->value(),
      1e-8);
  EXPECT_NEAR(2.001, *solution.finalObjective()->value(), 1e-8);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
}

TEST_P(PackerProblemTestSolvers, TestDimensionVectorDefinition) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t3"}}, {"h2", {"t4"}}};
  const map<std::string, std::string> expected_assignment = {
      {"t1", "h1"}, {"t2", "h2"}, {"t3", "h2"}, {"t4", "h2"}};

  auto problem = create3PerfectShardsProblem(initial_assignment);
  problem->addObjectDimension(
      "vec",
      std::unordered_map<std::string, std::vector<double>>{
          {"t1", {1, 1}}, {"t2", {2, 2}}, {"t3", {3, 3}}, {"t4", {4, 4}}});
  problem->addContainerDimension(
      "vec",
      std::vector<std::pair<std::string, double>>({{"h1", 1}, {"h2", 10}}));

  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "a";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }
  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "b";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }
  addAfterConstraint(problem, "vec", 1);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  XLOG(ERR) << "pol debug assignment size: " << solution.assignment()->size();
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
}

TEST_P(PackerProblemTestSolvers, TestReplicas) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2"}}, {"h2", {"t3"}}};
  const map<std::string, std::string> expected_assignment = {
      {"t1", "h2"}, {"t2", "h1"}, {"t3", "h2"}};

  auto problem = create3PerfectShardsProblem(initial_assignment);
  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "a";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }

  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "b";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }

  problem->addPartition(
      "shard_replica",
      std::unordered_map<std::string, std::string>{
          {"t2", "replica1"}, {"t3", "replica1"}});

  GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "container";
  groupCountSpec.partitionName() = "shard_replica";
  problem->addConstraint(groupCountSpec);

  AvoidMovingSpec avoidMovingSpec;
  avoidMovingSpec.objects() = {"t2"};
  problem->addConstraint(avoidMovingSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
  EXPECT_NEAR(
      isOptimal(solverAlgoType) ? 0.7503750 : 0.75015625,
      *solution.finalObjective()->value(),
      1e-8);
}

TEST(PackerProblemTest, TestGroupLimitSpread) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t4", "t5", "t7", "t8", "t9", "t10"}},
      {"h2", {"t3", "t6"}}};

  auto problem = makeProblem(initial_assignment);
  problem->addPartition(
      "task_group",
      std::unordered_map<std::string, std::string>{
          {"t1", "j1"},
          {"t2", "j1"},
          {"t3", "j1"},
          {"t4", "j2"},
          {"t5", "j2"},
          {"t6", "j2"},
          {"t7", "j3"},
          {"t8", "j3"},
          {"t9", "j3"},
          {"t10", "j3"}});

  GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "container";
  groupCountSpec.partitionName() = "task_group";
  groupCountSpec.definition() = GroupCountSpecDefinition::AFTER;
  groupCountSpec.bound() = GroupCountSpecBound::MAX;
  groupCountSpec.squares() = true;

  Limit limit;
  limit.type() = LimitType::ABSOLUTE;
  limit.globalLimit() = 1;
  groupCountSpec.limit() = limit;

  problem->addGoal(groupCountSpec, 0.001);

  {
    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = {"t7", "t8", "t9"};
    problem->addConstraint(avoidMovingSpec);
  }

  {
    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = {"t7", "t8"};
    problem->addConstraint(avoidMovingSpec);
  }

  auto solution = solve(problem, LOCALSEARCH);
  assertFinalAssignment(
      {{"h1", {"t1", "t2", "t4", "t5", "t7", "t8", "t9"}},
       {"h2", {"t3", "t6", "t10"}}},
      solution);
  // Normalized squares evaluate to (1/3)^2 + (1/3)^2 + (2/4)^2 for the goal
  EXPECT_NEAR(0.000472222, *solution.finalObjective()->value(), 1e-8);
}

/*
 * end state with perfect balance respects replicas constraint,
 * but there is no way to apply moves to not break replicas constraint in the
 * process
 */
TEST_P(PackerProblemTestSolvers, TestSwapBlockedByReplicas) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t3"}}, {"h2", {"t2", "t4"}}};

  auto problem = create4PerfectShardsProblem(initial_assignment);
  problem->addPartition(
      "shard_replica",
      std::unordered_map<std::string, std::string>{
          {"t2", "replica1"},
          {"t3", "replica1"},
          {"t1", "replica2"},
          {"t4", "replica2"}});

  GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "container";
  groupCountSpec.partitionName() = "shard_replica";
  groupCountSpec.definition() = GroupCountSpecDefinition::DURING_AND_AFTER;
  problem->addConstraint(groupCountSpec);

  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "a";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }

  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "b";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  assertFinalAssignment(initial_assignment, solution);
  // Normalized squares evaluate to (1/3)^2 + (1/3)^2 + (2/4)^2 for the goal
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(
      isOptimal(solverAlgoType) ? 0.2001 : 0.20002,
      *solution.finalObjective()->value(),
      1e-8);
}

TEST(PackerProblemTest, TestMinimizedContainersWithMovingTwoContainersToOne) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t3"}}, {"h2", {"t4", "t5", "t6", "t7", "t8"}}};

  auto problem = create4PerfectShardsProblem(initial_assignment);

  MinimizeContainersSpec minimizeContainersSpec;
  minimizeContainersSpec.scope() = "container";
  minimizeContainersSpec.dimension() = "object_count";
  minimizeContainersSpec.formula() = MinimizeContainerSpecFormula::LEGACY;
  problem->addGoal(minimizeContainersSpec);

  auto solution = solve(problem, LOCALSEARCH);
  auto hostCount = genHostCounts(solution);
  EXPECT_TRUE(hostCount["h1"] == 8 || hostCount["h2"] == 8);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(-1.494926960451048, *solution.finalObjective()->value(), 1e-8);
}

TEST_P(PackerProblemTestSolvers, TestMovingFromOne) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8"}}, {"h2", {}}};

  auto problem = create2DimOppositeProblem(initial_assignment);

  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "a";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }

  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "b";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }

  problem->addObjectDimension(
      "mov",
      std::unordered_map<std::string, double>{
          {"t1", 1},
          {"t2", 0.9},
          {"t3", 0.8},
          {"t4", 0.7},
          {"t5", 0.6},
          {"t6", 0.5},
          {"t7", 0.4},
          {"t8", 0.3}});

  MinimizeMovementSpec minimizeMovementSpec;
  minimizeMovementSpec.scope() = "container";
  minimizeMovementSpec.dimension() = "mov";
  problem->addGoal(minimizeMovementSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  assertFinalAssignment(
      {{"h1", {"t1", "t2", "t5", "t6"}}, {"h2", {"t3", "t4", "t7", "t8"}}},
      solution);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(0.0011, *solution.finalObjective()->value(), 1e-8);
}

TEST_P(PackerProblemTestSolvers, TestMovingFromOneMoveLimit) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8"}}, {"h2", {}}};

  auto problem = create2DimOppositeProblem(initial_assignment);

  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "a";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }

  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "b";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }

  problem->addObjectDimension(
      "mov",
      std::unordered_map<std::string, double>{
          {"t1", 1},
          {"t2", 0.9},
          {"t3", 0.8},
          {"t4", 0.7},
          {"t5", 0.6},
          {"t6", 0.5},
          {"t7", 0.4},
          {"t8", 0.3}});
  MinimizeMovementSpec minimizeMovementSpec;
  minimizeMovementSpec.scope() = "container";
  minimizeMovementSpec.dimension() = "mov";
  problem->addGoal(minimizeMovementSpec);

  addCapacityConstraint(
      problem,
      "object_count",
      CapacitySpecDefinition::NEW,
      CapacitySpecBound::MAX,
      2);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  assertFinalAssignment(
      {{"h1", {"t1", "t2", "t3", "t5", "t6", "t4"}}, {"h2", {"t7", "t8"}}},
      solution);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(
      isOptimal(solverAlgoType) ? 1.00085 : 1.000610,
      *solution.finalObjective()->value(),
      1e-8);
}

TEST_P(PackerProblemTestSolvers, TestMovingFromOneAsGroups) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8"}}, {"h2", {}}};

  auto problem = create2DimOppositeProblem(initial_assignment);

  BalanceSpec balanceSpec;
  balanceSpec.scope() = "container";
  balanceSpec.dimension() = "a";
  balanceSpec.formula() = BalanceSpecFormula::LEGACY;
  balanceSpec.fixAverageToInitial() = true;
  problem->addGoal(balanceSpec);

  AvoidMovingSpec avoidMovingSpec;
  avoidMovingSpec.objects() = {"t2"};

  problem->addConstraint(avoidMovingSpec);
  problem->addPartition(
      "grouping",
      std::unordered_map<std::string, std::string>{
          {"t1", "g1"},
          {"t2", "g1"},
          {"t3", "g1"},
          {"t4", "g2"},
          {"t5", "g2"},
          {"t6", "g2"},
          {"t7", "g3"},
          {"t8", "g3"}});

  MoveGroupSpec moveGroupSpec;
  moveGroupSpec.partitionName() = "grouping";
  problem->addConstraint(moveGroupSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);

  // Local search is unable to make progress by moving one object at a time, so
  // the final assignment is the same as the initial.
  auto expectedAssignment = isOptimal(solverAlgoType)
      ? std::map<std::string, std::vector<std::string>>(
            {{"h1", {"t1", "t2", "t3", "t7", "t8"}},
             {"h2", {"t4", "t5", "t6"}}})
      : initial_assignment;

  assertFinalAssignment(expectedAssignment, solution);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(
      isOptimal(solverAlgoType) ? 0.2001 : 1.0005,
      *solution.finalObjective()->value(),
      1e-8);
}

TEST_P(PackerProblemTestSolvers, TestMovingAsGroup) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t4", "t5", "t6", "t7", "t8"}}, {"h2", {"t3"}}};

  auto problem = create2DimOppositeProblem(initial_assignment);
  problem->addPartition(
      "grouping",
      std::unordered_map<std::string, std::string>{
          {"t1", "g1"},
          {"t2", "g1"},
          {"t3", "g2"},
          {"t4", "g4"},
          {"t5", "g3"},
          {"t6", "g3"},
          {"t7", "g4"},
          {"t8", "g4"}});

  problem->addPartition(
      "sharding",
      std::unordered_map<std::string, std::string>{
          {"t1", "s1"},
          {"t2", "s2"},
          {"t3", "s4"},
          {"t4", "s4"},
          {"t5", "s5"},
          {"t6", "s6"},
          {"t7", "s4"},
          {"t8", "s4"}});

  GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "container";
  groupCountSpec.partitionName() = "sharding";

  Limit limit;
  limit.type() = LimitType::ABSOLUTE;
  limit.globalLimit() = 3;
  groupCountSpec.limit() = limit;
  problem->addConstraint(groupCountSpec);

  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "a";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }

  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "b";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }

  {
    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = {"t1", "t5"};
    problem->addConstraint(avoidMovingSpec);
  }

  {
    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = {"t1"};
    problem->addConstraint(avoidMovingSpec);
  }

  MoveGroupSpec moveGroupSpec;
  moveGroupSpec.partitionName() = "grouping";
  problem->addConstraint(moveGroupSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);

  // Local search is unable to make progress by moving one object at a time, so
  // the final assignment is the same as the initial.
  auto expectedAssignment = isOptimal(solverAlgoType)
      ? std::map<std::string, std::vector<std::string>>(
            {{"h1", {"t1", "t2", "t3", "t5", "t6"}},
             {"h2", {"t4", "t7", "t8"}}})
      : initial_assignment;

  assertFinalAssignment(expectedAssignment, solution);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(
      isOptimal(solverAlgoType) ? 0.50025 : 1.500565,
      *solution.finalObjective()->value(),
      1e-8);
}

TEST_P(PackerProblemTestSolvers, TestGroupsWithDimensionLimit) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"unused", {"s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8"}}, {"r1", {}}};
  const std::map<std::string, std::vector<std::string>> wanted_assignment = {
      {"unused", {"s2", "s3", "s4", "s5", "s6"}}, {"r1", {"s1", "s7", "s8"}}};

  auto problem = makeProblem(initial_assignment);
  problem->addPartition(
      "fd",
      std::unordered_map<std::string, std::string>{
          {"s1", "fd1"},
          {"s2", "fd1"},
          {"s3", "fd1"},
          {"s4", "fd2"},
          {"s5", "fd2"},
          {"s6", "fd2"},
          {"s7", "fd3"},
          {"s8", "fd3"}});
  problem->addObjectDimension(
      "rcu",
      std::unordered_map<std::string, double>{
          {"s1", 5},
          {"s2", 3},
          {"s3", 3},
          {"s4", 10},
          {"s5", 8},
          {"s6", 7},
          {"s7", 3},
          {"s8", 2}});
  GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "container";
  groupCountSpec.partitionName() = "fd";
  groupCountSpec.dimension() = "rcu";

  {
    Limit limit;
    limit.type() = LimitType::ABSOLUTE;
    limit.globalLimit() = 5;
    groupCountSpec.limit() = limit;

    Filter filter;
    filter.itemsBlacklist() = {std::vector<std::string>{"unused"}};
    groupCountSpec.filter() = filter;
    problem->addConstraint(groupCountSpec);
  }

  {
    CapacitySpec capacitySpec;
    capacitySpec.scope() = "container";
    capacitySpec.dimension() = "rcu";

    Limit limit;
    limit.globalLimit() = 12;
    capacitySpec.limit() = limit;

    Filter filter;
    filter.itemsBlacklist() = {std::vector<std::string>{"unused"}};
    capacitySpec.filter() = filter;
    problem->addConstraint(capacitySpec);
  }

  {
    CapacitySpec capacitySpec;
    capacitySpec.scope() = "container";
    capacitySpec.dimension() = "rcu";
    capacitySpec.bound() = CapacitySpecBound::MIN;

    Limit limit;
    limit.globalLimit() = 10;
    capacitySpec.limit() = limit;

    Filter filter;
    filter.itemsBlacklist() = {std::vector<std::string>{"unused"}};
    capacitySpec.filter() = filter;
    problem->addConstraint(capacitySpec);
  }

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);

  assertFinalAssignment(wanted_assignment, solution);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(0, *solution.finalObjective()->value(), 1e-8);
}

TEST_P(PackerProblemTestSolvers, TestSwaps) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t3", "t4"}}, {"h2", {"t5", "t6", "t7", "t8"}}};

  auto problem = create2DimOppositeProblem(initial_assignment);

  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "a";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }

  {
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "b";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
  }

  addCapacityConstraint(
      problem,
      "object_count",
      CapacitySpecDefinition::NEW,
      CapacitySpecBound::MAX,
      2);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(0, *solution.finalObjective()->value(), 1e-8);
}

TEST_P(PackerProblemTestSolvers, TestMachineCapacityPerfectBalance) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"a"}}, {"h2", {"b"}}};

  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "s", std::unordered_map<std::string, double>{{"a", 20}, {"b", 10}});
  problem->addContainerDimension(
      "s", folly::F14FastMap<std::string, double>{{"h1", 200}, {"h2", 100}});
  BalanceSpec balanceSpec;
  balanceSpec.scope() = "container";
  balanceSpec.dimension() = "s";
  balanceSpec.formula() = BalanceSpecFormula::LEGACY;
  balanceSpec.fixAverageToInitial() = true;
  problem->addGoal(balanceSpec);
  addAfterConstraint(problem, "s", 0.1);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  assertFinalAssignment(initial_assignment, solution);
  EXPECT_EQ(0, *solution.initialObjective()->value());
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_EQ(0, *solution.finalObjective()->value());
}

TEST_P(PackerProblemTestSolvers, TestMachineCapacitySwap) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"b"}}, {"h2", {"a"}}};

  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "s", std::unordered_map<std::string, double>{{"a", 20}, {"b", 10}});
  problem->addContainerDimension(
      "s", std::map<std::string, double>{{"h1", 200}, {"h2", 100}});
  BalanceSpec balanceSpec;
  balanceSpec.scope() = "container";
  balanceSpec.dimension() = "s";
  balanceSpec.formula() = BalanceSpecFormula::LEGACY;
  balanceSpec.fixAverageToInitial() = true;
  problem->addGoal(balanceSpec);
  addAfterConstraint(problem, "s", 0.1);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  assertFinalAssignment({{"h1", {"a"}}, {"h2", {"b"}}}, solution);
  EXPECT_NEAR(10007.66716666, *solution.initialObjective()->value(), 1e-8);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_EQ(0, *solution.finalObjective()->value());
}

/*
 * still keep this unit test, because it caught a bug previously
 *
 * below test is failing before the fix of
 * [problem.scope_map[config.container_name][container].new() == 0
 * to <= 0.5 was introduced
 * but for c++, new is not used for constrain this
 */
TEST(PackerProblemTest, TestNotAcceptingNoOp) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto solverPackage = facebook::algopt::getAvailableMIPSolver().value();
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1"}}, {"h2", {"t2"}}, {"h3", {"t3"}}, {"h4", {"t4"}}};

  auto problem = makeProblem(initial_assignment);
  NonAcceptingSpec nonAcceptingSpec;
  nonAcceptingSpec.scope() = "container";
  nonAcceptingSpec.items() = {"h1", "h2"};
  problem->addConstraint(nonAcceptingSpec);

  auto solution = solve(problem, OPTIMAL, solverPackage);
  EXPECT_EQ(0, *solution.initialObjective()->value());
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_EQ(0, *solution.finalObjective()->value());
}

/* Aim to keep 1 container free. Minimize containers can free upto 2.*/
TEST(PackerProblemTest, TestMinimizeContainersLimit) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto solverPackage = facebook::algopt::getAvailableMIPSolver().value();
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"a", "b", "c"}},
      {"h2", {"p", "q", "r"}},
      {"h3", {"s"}},
      {"h4", {"d"}}};

  auto problem = makeProblem(initial_assignment);
  MinimizeContainersSpec minimizeContainersSpec;
  minimizeContainersSpec.scope() = "container";
  minimizeContainersSpec.dimension() = "object_count";
  minimizeContainersSpec.containerCosts() = {};
  minimizeContainersSpec.formula() = MinimizeContainerSpecFormula::LEGACY;
  MinimizeContainersTarget target;
  target.set_maxFreeLimit(1);
  minimizeContainersSpec.target() = std::move(target);
  problem->addGoal(minimizeContainersSpec);

  addAfterConstraint(problem, "object_count", 4);

  auto solution = solve(problem, OPTIMAL, solverPackage);
  auto hostCount = genHostCounts(solution);
  EXPECT_TRUE(
      hostCount["h1"] == 0 || hostCount["h2"] == 0 || hostCount["h3"] == 0 ||
      hostCount["h4"] == 0);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(0, *solution.finalObjective()->value(), 1e-8);
}

/* Expect two containers free and two with 4 objects each */
TEST(PackerProblemTest, TestMinimizeContainersSmall) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"a", "b", "c"}},
      {"h2", {"p", "q", "r"}},
      {"h3", {"s"}},
      {"h4", {"d"}}};

  auto problem = makeProblem(initial_assignment);

  MinimizeContainersSpec minimizeContainersSpec;
  minimizeContainersSpec.scope() = "container";
  minimizeContainersSpec.dimension() = "object_count";
  minimizeContainersSpec.formula() = MinimizeContainerSpecFormula::LEGACY;
  problem->addGoal(minimizeContainersSpec);

  addAfterConstraint(problem, "object_count", 4);

  auto solution = solve(problem, LOCALSEARCH);
  auto hostCount = genHostCounts(solution);

  const std::unordered_set<int> oc_h1_h2_h3_h4 = {4, 0};
  EXPECT_TRUE(oc_h1_h2_h3_h4.contains(hostCount["h1"]));
  EXPECT_TRUE(oc_h1_h2_h3_h4.contains(hostCount["h2"]));
  EXPECT_TRUE(oc_h1_h2_h3_h4.contains(hostCount["h3"]));
  EXPECT_TRUE(oc_h1_h2_h3_h4.contains(hostCount["h4"]));
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(-1.2570787221094177, *solution.finalObjective()->value(), 1e-8);
}

/* Slight variant of test_minimize_containers_small */
TEST(PackerProblemTest, TestMinimizeContainersDimensionSmall) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"a", "b", "c"}},
      {"h2", {"p", "q", "r"}},
      {"h3", {"s"}},
      {"h4", {"d"}}};

  auto problem = makeProblem(initial_assignment);
  std::map<std::string, double> containerCosts = {
      {"h1", 10}, {"h2", 1}, {"h3", 1}, {"h4", 10}};

  MinimizeContainersSpec minimizeContainersSpec;
  minimizeContainersSpec.scope() = "container";
  minimizeContainersSpec.dimension() = "object_count";
  minimizeContainersSpec.containerCosts() = containerCosts;
  minimizeContainersSpec.formula() = MinimizeContainerSpecFormula::LEGACY;
  problem->addGoal(minimizeContainersSpec);

  auto solution = solve(problem, LOCALSEARCH);
  auto hostCount = genHostCounts(solution);
  EXPECT_EQ(hostCount["h1"], 0);
  EXPECT_EQ(hostCount["h4"], 0);
  EXPECT_EQ(hostCount["h2"] + hostCount["h3"], 8);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(-1.494926960451048, *solution.finalObjective()->value(), 1e-8);
}

TEST(PackerProblemTest, TestMinimizeContainersLarge) {
  std::map<std::string, std::vector<std::string>> initial_assignment;
  int i;
  for (i = 0; i < 100; i++) {
    initial_assignment["h1"].push_back(fmt::format("s{}", i));
  }
  for (; i < 200; i++) {
    initial_assignment["h2"].push_back(fmt::format("s{}", i));
  }
  initial_assignment["h3"] = {"s201", "s202"};

  auto problem = makeProblem(initial_assignment);

  MinimizeContainersSpec minimizeContainersSpec;
  minimizeContainersSpec.scope() = "container";
  minimizeContainersSpec.dimension() = "object_count";
  minimizeContainersSpec.formula() = MinimizeContainerSpecFormula::LEGACY;
  problem->addGoal(minimizeContainersSpec);

  addAfterConstraint(problem, "object_count", 101);

  auto solution = solve(problem, LOCALSEARCH);
  auto hostCount = genHostCounts(solution);
  const std::unordered_set<int> oc_h1_h2_h3 = {101, 0};
  EXPECT_TRUE(oc_h1_h2_h3.contains(hostCount["h1"]));
  EXPECT_TRUE(oc_h1_h2_h3.contains(hostCount["h2"]));
  EXPECT_TRUE(oc_h1_h2_h3.contains(hostCount["h3"]));
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(-3.1545373581477119, *solution.finalObjective()->value(), 1e-8);
}

TEST_P(PackerProblemTestSolvers, TestMinCapacityObjective) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2"}}, {"h2", {}}};

  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "dim", std::unordered_map<std::string, double>{{"t1", 1}, {"t2", 2}});
  problem->addContainerDimension(
      "dim", folly::F14FastMap<std::string, double>{{"h1", 1}, {"h2", 3}});
  addCapacityGoal(
      problem,
      "dim",
      CapacitySpecDefinition::AFTER,
      CapacitySpecBound::MIN,
      1.5,
      true);

  // make final solution stable
  AssignmentAffinitiesSpec assignmentAffinitiesSpec;
  assignmentAffinitiesSpec.affinities() = {
      makeAssignmentAffinity("t1", "h2", -1)};
  problem->addGoal(assignmentAffinitiesSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  assertFinalAssignment({{"h1", {"t1"}}, {"h2", {"t2"}}}, solution);
  EXPECT_NEAR(0.25, *solution.finalObjective()->value(), 1e-8);
}

TEST_P(PackerProblemTestSolvers, TestMaxCapacityObjective) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2"}}, {"h2", {}}};

  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "dim", std::unordered_map<std::string, double>{{"t1", 1}, {"t2", 3}});
  problem->addContainerDimension(
      "dim", folly::F14FastMap<std::string, double>{{"h1", 1}, {"h2", 2}});
  addCapacityGoal(
      problem,
      "dim",
      CapacitySpecDefinition::AFTER,
      CapacitySpecBound::MAX,
      1.0);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  assertFinalAssignment({{"h1", {"t1"}}, {"h2", {"t2"}}}, solution);
  EXPECT_NEAR(2.0 / 3.0, *solution.finalObjective()->value(), 1e-8);
}

TEST_P(PackerProblemTestSolvers, TestMaxCapacityObjectiveBig) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t4"}}, {"h2", {"t3"}}, {"h3", {}}, {"h4", {}}};

  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "hw_dim",
      std::unordered_map<std::string, double>{
          {"t1", 1}, {"t2", 1}, {"t3", 1}, {"t4", 1}});
  problem->addContainerDimension(
      "hw_dim",
      folly::F14FastMap<std::string, double>{
          {"h1", 0.1}, {"h2", 0.1}, {"h3", 100}, {"h4", 1}});
  addCapacityGoal(
      problem,
      "hw_dim",
      CapacitySpecDefinition::AFTER,
      CapacitySpecBound::MAX,
      1.0);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(0, *solution.finalObjective()->value(), 1e-8);
  EXPECT_EQ(solution.assignment()["h1"].size(), 0);
  EXPECT_EQ(solution.assignment()["h2"].size(), 0);
}

TEST_P(PackerProblemTestSolvers, TestColocateGroupMoveConstraint) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t4"}}, {"h2", {"t3"}}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h1", {"t1", "t2", "t3"}}, {"h2", {"t4"}}};

  {
    auto problem = create4PerfectShardsProblem(initial_assignment);
    problem->addPartition(
        "shard_replica",
        std::map<std::string, std::string>{
            {"t1", "replica1"},
            {"t2", "replica1"},
            {"t3", "replica1"},
            {"t4", "replica2"}});

    {
      BalanceSpec balanceSpec;
      balanceSpec.scope() = "container";
      balanceSpec.dimension() = "a";
      balanceSpec.formula() = BalanceSpecFormula::LEGACY;
      balanceSpec.fixAverageToInitial() = true;
      problem->addGoal(balanceSpec);
    }

    {
      BalanceSpec balanceSpec;
      balanceSpec.scope() = "container";
      balanceSpec.dimension() = "b";
      balanceSpec.formula() = BalanceSpecFormula::LEGACY;
      balanceSpec.fixAverageToInitial() = true;
      problem->addGoal(balanceSpec);
    }

    ColocateGroupsSpec colocateGroupsSpec;
    colocateGroupsSpec.scope() = "container";
    colocateGroupsSpec.partitionName() = "shard_replica";
    problem->addConstraint(colocateGroupsSpec);

    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = {"t1"};
    problem->addConstraint(avoidMovingSpec);

    const auto [solverAlgoType, solverPackage] = GetParam();
    auto solution = solve(problem, solverAlgoType, solverPackage);
    assertFinalAssignment(expected_assignment, solution);
    EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
    EXPECT_NEAR(
        isOptimal(solverAlgoType) ? 0.90045 : 0.900205,
        *solution.finalObjective()->value(),
        1e-8);
  }
  // trivial partition
  {
    auto problem = create4PerfectShardsProblem(initial_assignment);
    problem->addPartition(
        "shard_replica",
        std::map<std::string, std::string>{
            {"t1", "replica1"},
            {"t2", "replica2"},
            {"t3", "replica3"},
            {"t4", "replica4"}});

    {
      BalanceSpec balanceSpec;
      balanceSpec.scope() = "container";
      balanceSpec.dimension() = "a";
      balanceSpec.formula() = BalanceSpecFormula::LEGACY;
      balanceSpec.fixAverageToInitial() = true;
      problem->addGoal(balanceSpec);
    }

    {
      BalanceSpec balanceSpec;
      balanceSpec.scope() = "container";
      balanceSpec.dimension() = "b";
      balanceSpec.formula() = BalanceSpecFormula::LEGACY;
      balanceSpec.fixAverageToInitial() = true;
      problem->addGoal(balanceSpec);
    }

    ColocateGroupsSpec colocateGroupsSpec;
    colocateGroupsSpec.scope() = "container";
    colocateGroupsSpec.partitionName() = "shard_replica";
    problem->addConstraint(colocateGroupsSpec);

    const auto [solverAlgoType, solverPackage] = GetParam();
    auto solution = solve(problem, solverAlgoType, solverPackage);
    EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
    EXPECT_EQ(0, *solution.finalObjective()->value());
  }
}

TEST(PackerProblemTest, TestConstraintPerGroupMoveLimit) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t3"}},
      {"h2", {"t4", "t5"}},
      {"h3", {"t6", "t7", "t8"}},
      {"h4", {}}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h1", {"t1", "t2", "t3", "t5", "t7", "t8"}},
      {"h2", {"t4"}},
      {"h3", {"t6"}},
      {"h4", {}}};

  auto problem = makeProblem(initial_assignment);
  problem->addPartition(
      "task_group",
      std::map<std::string, std::string>{
          {"t1", "group1"},
          {"t2", "group1"},
          {"t3", "group1"},
          {"t4", "group1"},
          {"t5", "group1"},
          {"t6", "group2"},
          {"t7", "group2"},
          {"t8", "group2"}});
  MinimizeContainersSpec minimizeContainersSpec;
  minimizeContainersSpec.scope() = "container";
  minimizeContainersSpec.dimension() = "object_count";
  minimizeContainersSpec.formula() = MinimizeContainerSpecFormula::LEGACY;
  problem->addGoal(minimizeContainersSpec);
  Limit groupLimit;
  groupLimit.type() = LimitType::ABSOLUTE;
  groupLimit.groupLimits() = {{"group1", 1}, {"group2", 2}};

  GroupMoveLimitSpec groupMoveLimitSpec;
  groupMoveLimitSpec.partitionName() = "task_group";
  groupMoveLimitSpec.limit() = groupLimit;
  problem->addConstraint(groupMoveLimitSpec);

  problem->addObjectDimension(
      "mov",
      std::unordered_map<std::string, double>{
          {"t1", 0.9},
          {"t2", 0.8},
          {"t3", 0.7},
          {"t4", 0.6},
          {"t5", 0.5},
          {"t6", 0.4},
          {"t7", 0.3},
          {"t8", 0.2}});
  MinimizeMovementSpec minimizeMovementSpec;
  minimizeMovementSpec.scope() = "container";
  minimizeMovementSpec.dimension() = "mov";
  problem->addGoal(minimizeMovementSpec);

  auto solution = solve(problem, LOCALSEARCH);
  assertFinalAssignment(expected_assignment, solution);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(-1.2653619422710802, *solution.finalObjective()->value(), 1e-8);
}

static AssignmentSolution runGroupIsolationCase(
    const SolverAlgoType solverAlgoType,
    const bool feasibleExpected = true,
    const double groupLimit = 0,
    const int maxViolators = 1,
    const std::map<std::string, double>& host_max_tasks = {},
    const std::vector<std::string>& avoidMoving = {},
    std::optional<OptimalSolverPackage> solverPackage = std::nullopt) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"b_1", "b_2", "b_3", "c_1", "c_2", "c_3", "c_4", "a_1", "a_2"}},
      {"h2", {}},
      {"h3", {}}};
  auto problem = makeProblem(initial_assignment);
  const std::map<std::string, std::string> partition = {
      {"a_1", "group1"},
      {"a_2", "group1"},
      {"b_1", "group2"},
      {"b_2", "group2"},
      {"b_3", "group2"},
      {"c_1", "group3"},
      {"c_2", "group3"},
      {"c_3", "group3"},
      {"c_4", "group3"}};
  problem->addPartition("task_group", partition);

  GroupIsolationLimitSpec groupIsolationLimitSpec;
  groupIsolationLimitSpec.scope() = "container";
  groupIsolationLimitSpec.partitionName() = "task_group";
  groupIsolationLimitSpec.groupsAllowed() = maxViolators;

  Limit limit;
  limit.type() = LimitType::ABSOLUTE;
  limit.globalLimit() = groupLimit;
  groupIsolationLimitSpec.limit() = limit;
  problem->addConstraint(groupIsolationLimitSpec);

  if (!host_max_tasks.empty()) {
    problem->addContainerDimension("object_count", host_max_tasks);
    addAfterConstraint(problem, "object_count", 1);
  }
  if (!avoidMoving.empty()) {
    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = avoidMoving;
    problem->addConstraint(avoidMovingSpec);
  }
  auto solution = solve(problem, solverAlgoType, solverPackage);
  if (feasibleExpected) {
    assertGroupAssignment(solution, partition, groupLimit, maxViolators);
    EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  } else {
    EXPECT_TRUE(0 < *solution.finalConstraint()->brokenCount());
  }
  return solution;
}

/* For GroupIsolationLimitSpec, test groups_allowed = 1, group limit = 0 */
TEST(PackerProblemOptimalTest, TestConstraintGroupIsolationA) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto solverPackage = facebook::algopt::getAvailableMIPSolver().value();
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h1", {"a_1", "a_2"}},
      {"h2", {"b_1", "b_2", "b_3"}},
      {"h3", {"c_1", "c_2", "c_3", "c_4"}}};
  const std::map<std::string, double> host_limits = {
      {"h1", 2}, {"h2", 3}, {"h3", 4}};
  auto solution = runGroupIsolationCase(
      OPTIMAL, true, 0, 1, host_limits, {"a_1", "a_2"}, solverPackage);
  assertFinalAssignment(expected_assignment, solution);
  const std::map<std::string, double> host_limits2 = {
      {"h1", 3}, {"h2", 3}, {"h3", 3}};
  // total capacity is still feasible but max group violations are not.
  auto solution2 = runGroupIsolationCase(
      OPTIMAL, false, 0, 1, host_limits2, {"a_1", "a_2"}, solverPackage);
}

/* For GroupIsolationLimitSpec, test groups_allowed = 0 */
TEST(PackerProblemTestSolvers, TestConstraintGroupIsolationB) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto solverPackage = facebook::algopt::getAvailableMIPSolver().value();
  const std::map<std::string, double> host_limits = {
      {"h1", 4}, {"h2", 3}, {"h3", 2}};
  auto solution = runGroupIsolationCase(
      SolverAlgoType::OPTIMAL,
      true,
      2,
      0,
      host_limits,
      {"a_1", "a_2", "b_1", "b_2"},
      solverPackage);

  // we have 3 containers with max group limit 1 and group 3 has 4 objects.
  // so the problem is infeasible
  auto solution2 = runGroupIsolationCase(
      SolverAlgoType::OPTIMAL,
      false,
      1,
      0,
      host_limits,
      {"a_1", "b_1"},
      solverPackage);
}

TEST_P(PackerProblemTestSolvers, TestConstraintGroupIsolationC) {
  const std::map<std::string, double> host_limits = {
      {"h1", 4}, {"h2", 4}, {"h3", 4}};
  const auto [solverAlgoType, solverPackage] = GetParam();
  // group isolation is feasible but avoid moving makes it infeasible
  auto solution = runGroupIsolationCase(
      solverAlgoType,
      false,
      2,
      0,
      host_limits,
      {"b_1", "b_2", "b_3"},
      solverPackage);
}

TEST_P(PackerProblemTestSolvers, TestConstraintGroupIsolation) {
  const std::map<std::string, double> host_limits = {
      {"h1", 3}, {"h2", 3}, {"h3", 3}};
  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = runGroupIsolationCase(
      solverAlgoType, true, 2, 2, host_limits, {}, solverPackage);
}

TEST_P(PackerProblemTestSolvers, TestConstraintGroupIsolationTao) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1",
       {"a_1",
        "a_2",
        "a_3",
        "a_4",
        "a_5",
        "a_6",
        "a_7",
        "a_8",
        "b_1",
        "b_2",
        "b_3",
        "b_4",
        "b_5",
        "b_6",
        "b_7",
        "b_8"}},
      {"h2", {}}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h1",
       {"a_6",
        "a_7",
        "a_8",
        "b_1",
        "b_2",
        "b_3",
        "b_4",
        "b_5",
        "b_6",
        "b_7",
        "b_8"}},
      {"h2", {"a_1", "a_2", "a_3", "a_4", "a_5"}}};

  auto problem = makeProblem(initial_assignment);
  problem->addPartition(
      "task_group",
      std::map<std::string, std::string>{
          {"a_1", "group1"},
          {"a_2", "group1"},
          {"a_3", "group1"},
          {"a_4", "group1"},
          {"a_5", "group1"},
          {"a_6", "group1"},
          {"a_7", "group1"},
          {"a_8", "group1"},
          {"b_1", "group2"},
          {"b_2", "group2"},
          {"b_3", "group2"},
          {"b_4", "group2"},
          {"b_5", "group2"},
          {"b_6", "group2"},
          {"b_7", "group2"},
          {"b_8", "group2"}});

  GroupIsolationLimitSpec groupIsolationLimitSpec;
  groupIsolationLimitSpec.scope() = "container";
  groupIsolationLimitSpec.partitionName() = "task_group";
  groupIsolationLimitSpec.groupsAllowed() = 1;

  Limit limit;
  limit.type() = LimitType::ABSOLUTE;
  limit.globalLimit() = 3;
  groupIsolationLimitSpec.limit() = limit;

  problem->addConstraint(groupIsolationLimitSpec);

  AvoidMovingSpec avoidMovingSpec;
  avoidMovingSpec.objects() = {"b_1"};
  problem->addConstraint(avoidMovingSpec);

  problem->addObjectDimension(
      "mov",
      std::unordered_map<std::string, double>{
          {"a_1", 0.1},
          {"a_2", 0.2},
          {"a_3", 0.3},
          {"a_4", 0.4},
          {"a_5", 0.5},
          {"a_6", 0.6},
          {"a_7", 0.7},
          {"a_8", 0.8},
          {"b_1", 0.1},
          {"b_2", 0.2},
          {"b_3", 0.3},
          {"b_4", 0.4},
          {"b_5", 0.5},
          {"b_6", 0.6},
          {"b_7", 0.7},
          {"b_8", 0.8}});

  MinimizeMovementSpec minimizeMovementSpec;
  minimizeMovementSpec.scope() = "container";
  minimizeMovementSpec.dimension() = "mov";
  problem->addGoal(minimizeMovementSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  assertFinalAssignment(expected_assignment, solution);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(0.00075, *solution.finalObjective()->value(), 1e-8);
}

TEST_P(PackerProblemTestSolvers, TestObjectiveGroupIsolationTao) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1",
       {"a_1",
        "a_2",
        "a_3",
        "a_4",
        "a_5",
        "a_6",
        "a_7",
        "a_8",
        "b_1",
        "b_2",
        "b_3",
        "b_4",
        "b_5",
        "b_6",
        "b_7",
        "b_8"}},
      {"h2", {}}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h1",
       {"a_6",
        "a_7",
        "a_8",
        "b_1",
        "b_2",
        "b_3",
        "b_4",
        "b_5",
        "b_6",
        "b_7",
        "b_8"}},
      {"h2", {"a_1", "a_2", "a_3", "a_4", "a_5"}}};

  auto problem = makeProblem(initial_assignment);
  problem->addPartition(
      "task_group",
      std::map<std::string, std::string>{
          {"a_1", "group1"},
          {"a_2", "group1"},
          {"a_3", "group1"},
          {"a_4", "group1"},
          {"a_5", "group1"},
          {"a_6", "group1"},
          {"a_7", "group1"},
          {"a_8", "group1"},
          {"b_1", "group2"},
          {"b_2", "group2"},
          {"b_3", "group2"},
          {"b_4", "group2"},
          {"b_5", "group2"},
          {"b_6", "group2"},
          {"b_7", "group2"},
          {"b_8", "group2"}});
  GroupIsolationLimitSpec groupIsolationLimitSpec;
  groupIsolationLimitSpec.scope() = "container";
  groupIsolationLimitSpec.partitionName() = "task_group";
  groupIsolationLimitSpec.groupsAllowed() = 1;

  Limit limit;
  limit.type() = LimitType::ABSOLUTE;
  limit.globalLimit() = 3;
  groupIsolationLimitSpec.limit() = limit;

  problem->addConstraint(groupIsolationLimitSpec);

  AvoidMovingSpec avoidMovingSpec;
  avoidMovingSpec.objects() = {"b_1"};
  problem->addConstraint(avoidMovingSpec);

  problem->addObjectDimension(
      "mov",
      std::unordered_map<std::string, double>{
          {"a_1", 0.1},
          {"a_2", 0.2},
          {"a_3", 0.3},
          {"a_4", 0.4},
          {"a_5", 0.5},
          {"a_6", 0.6},
          {"a_7", 0.7},
          {"a_8", 0.8},
          {"b_1", 0.1},
          {"b_2", 0.2},
          {"b_3", 0.3},
          {"b_4", 0.4},
          {"b_5", 0.5},
          {"b_6", 0.6},
          {"b_7", 0.7},
          {"b_8", 0.8}});

  MinimizeMovementSpec minimizeMovementSpec;
  minimizeMovementSpec.scope() = "container";
  minimizeMovementSpec.dimension() = "mov";
  problem->addGoal(minimizeMovementSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  assertFinalAssignment(expected_assignment, solution);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(0.00075, *solution.finalObjective()->value(), 1e-8);
}

TEST_P(PackerProblemTestSolvers, TestMaximizeAllocationGoal) {
  std::map<std::string, std::vector<std::string>> initial_assignment;
  int i;
  for (i = 0; i < 10; i++) {
    initial_assignment["h1"].push_back(fmt::format("t{}", i));
  }
  for (; i < 16; i++) {
    initial_assignment["h2"].push_back(fmt::format("t{}", i));
  }
  for (; i < 18; i++) {
    initial_assignment["h3"].push_back(fmt::format("t{}", i));
  }

  auto problem = makeProblem(initial_assignment);

  MaximizeAllocationSpec maximizeAllocationSpec;
  maximizeAllocationSpec.scope() = "container";
  maximizeAllocationSpec.dimension() = "object_count";

  Filter filter;
  filter.itemsBlacklist() = {std::vector<std::string>{"h2", "h3"}};

  maximizeAllocationSpec.filter() = filter;
  problem->addGoal(maximizeAllocationSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  EXPECT_EQ(solution.assignment()["h2"].size(), 0);
  EXPECT_EQ(solution.assignment()["h3"].size(), 0);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_EQ(-1, *solution.finalObjective()->value());
}

TEST(PackerProblemLocalSearchTest, TestTripleLoop) {
  std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"A", "b"}}, {"h2", {"B", "c"}}, {"h3", {"C", "a"}}};

  auto initLocalProblem = [&initial_assignment]() {
    auto problem = makeProblem(initial_assignment);
    problem->addObjectDimension(
        "x",
        std::vector<std::pair<std::string, double>>{
            {"A", 13}, {"a", 1}, {"B", 12}, {"b", 2}, {"C", 11}, {"c", 3}});
    problem->addObjectDimension(
        "y",
        std::vector<std::pair<std::string, double>>{
            {"A", 12}, {"a", 2}, {"B", 11}, {"b", 3}, {"C", 13}, {"c", 1}});
    problem->addObjectDimension(
        "z",
        std::vector<std::pair<std::string, double>>{
            {"A", 11}, {"a", 3}, {"B", 13}, {"b", 1}, {"C", 12}, {"c", 2}});

    {
      BalanceSpec balanceSpec;
      balanceSpec.scope() = "container";
      balanceSpec.dimension() = "x";
      balanceSpec.formula() = BalanceSpecFormula::LEGACY;
      balanceSpec.fixAverageToInitial() = true;
      problem->addGoal(balanceSpec);
    }

    {
      BalanceSpec balanceSpec;
      balanceSpec.scope() = "container";
      balanceSpec.dimension() = "y";
      balanceSpec.formula() = BalanceSpecFormula::LEGACY;
      balanceSpec.fixAverageToInitial() = true;
      problem->addGoal(balanceSpec);
    }

    {
      BalanceSpec balanceSpec;
      balanceSpec.scope() = "container";
      balanceSpec.dimension() = "z";
      balanceSpec.formula() = BalanceSpecFormula::LEGACY;
      balanceSpec.fixAverageToInitial() = true;
      problem->addGoal(balanceSpec);
    }

    MinimizeMovementSpec minimizeMovementSpec;
    minimizeMovementSpec.scope() = "container";
    minimizeMovementSpec.dimension() = "x";
    problem->addGoal(minimizeMovementSpec);

    return problem;
  };
  {
    auto problem = initLocalProblem();
    auto solution = localSearchSolveWithDefinedMoves(
        problem, {ProblemSolver::makeMoveTypeSpec(TripleLoopMoveTypeSpec())});
    EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
    assertFinalAssignment(
        {{"h1", {"A", "a"}}, {"h2", {"B", "b"}}, {"h3", {"C", "c"}}}, solution);
    EXPECT_NEAR(0.002, *solution.finalObjective()->value(), 1e-8);
  }
  {
    auto problem = initLocalProblem();
    auto solution = localSearchSolveWithDefinedMoves(
        problem,
        {ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()),
         ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec())});
    EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
    assertFinalAssignment(
        {{"h1", {"A", "b"}}, {"h2", {"B", "c"}}, {"h3", {"C", "a"}}}, solution);
    EXPECT_NEAR(0.2142959183, *solution.finalObjective()->value(), 1e-8);
  }
}

TEST(PackerProblemLocalSearchTest, TestKLSearch) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2"}}, {"h2", {"t3", "t4", "t5", "t6", "t7", "t8"}}};

  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "x",
      std::vector<std::pair<std::string, double>>{
          {"t1", 5},
          {"t2", 5},
          {"t3", 1},
          {"t4", 1},
          {"t5", 1},
          {"t6", 1},
          {"t7", 1},
          {"t8", 1}});
  problem->addObjectDimension(
      "mov",
      std::vector<std::pair<std::string, double>>{
          {"t1", 1},
          {"t2", 0.5},
          {"t3", 1},
          {"t4", 0.8},
          {"t5", 0.6},
          {"t6", 0.4},
          {"t7", 0.2},
          {"t8", 0}});

  BalanceSpec balanceSpec;
  balanceSpec.scope() = "container";
  balanceSpec.dimension() = "x";
  balanceSpec.formula() = BalanceSpecFormula::LEGACY;
  balanceSpec.fixAverageToInitial() = true;
  problem->addGoal(balanceSpec);

  MinimizeMovementSpec minimizeMovementSpec;
  minimizeMovementSpec.scope() = "container";
  minimizeMovementSpec.dimension() = "mov";
  problem->addGoal(minimizeMovementSpec);

  auto solution = localSearchSolveWithDefinedMoves(
      problem, {ProblemSolver::makeMoveTypeSpec(KLSearchMoveTypeSpec())});
  assertFinalAssignment(
      {{"h1", {"t1", "t6", "t7", "t8"}}, {"h2", {"t2", "t3", "t4", "t5"}}},
      solution);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(0.00055, *solution.finalObjective()->value(), 1e-8);
}

TEST_P(PackerProblemTestSolvers, TestMinimizeMovement) {
  std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"a", "b"}}, {"h2", {"c"}}, {"h3", {"d"}}, {"h4", {"e"}}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h1", {"a"}}, {"h2", {"c"}}, {"h3", {"b"}}, {"h4", {"d", "e"}}};

  auto initLocalProblem = [&initial_assignment]() {
    auto problem = makeProblem(initial_assignment);
    problem->addObjectDimension(
        "x",
        std::vector<std::pair<std::string, double>>{
            {"a", 10}, {"b", 10}, {"c", 5}, {"d", 5.1}, {"e", 5.2}});
    problem->addObjectDimension(
        "y",
        std::vector<std::pair<std::string, double>>{
            {"a", 1}, {"b", 0}, {"c", 1}, {"d", 0.3}, {"e", 0.9}});

    {
      BalanceSpec balanceSpec;
      balanceSpec.scope() = "container";
      balanceSpec.dimension() = "x";
      balanceSpec.formula() = BalanceSpecFormula::LEGACY;
      balanceSpec.fixAverageToInitial() = true;
      problem->addGoal(balanceSpec);
    }

    {
      BalanceSpec balanceSpec;
      balanceSpec.scope() = "container";
      balanceSpec.dimension() = "y";
      balanceSpec.formula() = BalanceSpecFormula::LEGACY;
      balanceSpec.fixAverageToInitial() = true;
      problem->addGoal(balanceSpec);
    }

    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = {"a", "e"};
    problem->addConstraint(avoidMovingSpec);

    return problem;
  };

  {
    auto problem = initLocalProblem();

    MinimizeMovementSpec minimizeMovementSpec;
    minimizeMovementSpec.scope() = "container";
    minimizeMovementSpec.dimension() = "object_count";
    problem->addGoal(minimizeMovementSpec, 2);

    const auto [solverAlgoType, solverPackage] = GetParam();
    auto solution = solve(problem, solverAlgoType, solverPackage);
    assertFinalAssignment(expected_assignment, solution);
    EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
    EXPECT_NEAR(
        isOptimal(solverAlgoType) ? 0.66849716713881036 : 0.66824840,
        *solution.finalObjective()->value(),
        1e-8);
  }
  { // not sure the difference compared to above
    // still copy, since it is in packet_test.py
    auto problem = initLocalProblem();
    problem->addObjectDimension(
        "tc",
        std::vector<std::pair<std::string, double>>{
            {"a", 1}, {"b", 1}, {"c", 1}, {"d", 1}, {"e", 1}});

    MinimizeMovementSpec minimizeMovementSpec;
    minimizeMovementSpec.scope() = "container";
    minimizeMovementSpec.dimension() = "tc";
    problem->addGoal(minimizeMovementSpec, 2);

    const auto [solverAlgoType, solverPackage] = GetParam();
    auto solution = solve(problem, solverAlgoType, solverPackage);
    assertFinalAssignment(expected_assignment, solution);
    EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
    EXPECT_NEAR(
        isOptimal(solverAlgoType) ? 0.66849716713881036 : 0.66824840,
        *solution.finalObjective()->value(),
        1e-8);
  }
}

TEST_P(PackerProblemTestSolvers, TestMinCapacityConstraint) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2"}}, {"h2", {"t3", "t4"}}, {"h3", {}}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h1", {"t1"}}, {"h2", {}}, {"h3", {"t2", "t3", "t4"}}};
  const std::map<std::string, double> task_count_contribution = {
      {"t1", 1}, {"t2", 1}, {"t3", 1}, {"t4", 1}};
  const std::map<std::string, double> host_min_task_constraint = {
      {"h1", 1}, {"h2", -0.1}, {"h3", 2}};
  const std::map<std::string, double> host_max_task_constraint = {
      {"h1", 1}, {"h2", 0.1}, {"h3", 3}};

  auto initLocalProblem = [&initial_assignment,
                           &task_count_contribution,
                           &host_max_task_constraint]() {
    auto problem = makeProblem(initial_assignment);
    problem->addObjectDimension("min_count", task_count_contribution);
    problem->addObjectDimension("max_count", task_count_contribution);
    problem->addContainerDimension("max_count", host_max_task_constraint);
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "min_count";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;
    problem->addGoal(balanceSpec);
    addAfterConstraint(problem, "max_count", 1);
    problem->addObjectDimension(
        "mov",
        std::vector<std::pair<std::string, double>>{
            {"t1", 1}, {"t2", 0.8}, {"t3", 0.6}, {"t4", 0.4}});

    MinimizeMovementSpec minimizeMovementSpec;
    minimizeMovementSpec.scope() = "container";
    minimizeMovementSpec.dimension() = "mov";
    problem->addGoal(minimizeMovementSpec);
    return problem;
  };

  {
    auto problem = initLocalProblem();
    problem->addContainerDimension("min_count", host_min_task_constraint);
    addCapacityConstraint(
        problem,
        "min_count",
        CapacitySpecDefinition::AFTER,
        CapacitySpecBound::MIN,
        0);
    const auto [solverAlgoType, solverPackage] = GetParam();
    auto solution = solve(problem, solverAlgoType, solverPackage);
    assertFinalAssignment(expected_assignment, solution);
    EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
    EXPECT_NEAR(
        isOptimal(solverAlgoType) ? 0.088129166666666578 : 0.08810255208,
        *solution.finalObjective()->value(),
        1e-8);
  }
  { // use limit_count_file instead of min_count capacity on hosts
    auto problem = initLocalProblem();
    addCapacityConstraint(
        problem,
        "min_count",
        CapacitySpecDefinition::AFTER,
        CapacitySpecBound::MIN,
        0);
    const auto [solverAlgoType, solverPackage] = GetParam();
    auto solution = solve(problem, solverAlgoType, solverPackage);

    assertFinalAssignment(expected_assignment, solution);
    EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
    EXPECT_NEAR(
        isOptimal(solverAlgoType) ? 1.2510166666666667 : 1.25112083333,
        *solution.finalObjective()->value(),
        1e-8);
  }
}

TEST_P(PackerProblemTestSolvers, TestTwInclusionLock) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t4"}}, {"h2", {"t3"}}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h1", {"t1", "t2", "t3"}}, {"h2", {"t4"}}};

  auto problem = makeProblem(initial_assignment);
  problem->addPartition(
      "task_group",
      std::map<std::string, std::string>{
          {"t1", "group1"},
          {"t2", "group1"},
          {"t3", "group1"},
          {"t4", "group2"}});

  GroupIsolationLimitSpec groupIsolationLimitSpec;
  groupIsolationLimitSpec.scope() = "container";
  groupIsolationLimitSpec.partitionName() = "task_group";
  groupIsolationLimitSpec.groupsAllowed() = 1;

  Limit limit;
  limit.type() = LimitType::ABSOLUTE;
  limit.globalLimit() = 0;
  groupIsolationLimitSpec.limit() = limit;
  problem->addConstraint(groupIsolationLimitSpec);

  AvoidMovingSpec avoidMovingSpec;
  avoidMovingSpec.objects() = {"t1"};
  problem->addConstraint(avoidMovingSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  assertFinalAssignment(expected_assignment, solution);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_EQ(0, *solution.finalObjective()->value());
}

TEST_P(PackerProblemTestSolvers, TestOldData) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t3"}}, {"h2", {"t2", "t4"}}};

  auto problem = create4PerfectShardsProblem(initial_assignment);

  BalanceSpec balanceSpec;
  balanceSpec.scope() = "container";
  balanceSpec.dimension() = "a";
  balanceSpec.formula() = BalanceSpecFormula::LEGACY;
  balanceSpec.fixAverageToInitial() = true;
  problem->addGoal(balanceSpec);

  addCapacityConstraint(
      problem, "b", CapacitySpecDefinition::OLD, CapacitySpecBound::MAX, 0);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  assertFinalAssignment(initial_assignment, solution);
}

TEST_P(PackerProblemTestSolvers, TestAssignmentAffinities) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t2"}}, {"h2", {"t1"}}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h1", {"t1"}}, {"h2", {"t2"}}};

  auto problem = makeProblem(initial_assignment);
  AssignmentAffinitiesSpec assignmentAffinitiesSpec;
  assignmentAffinitiesSpec.affinities() = {
      makeAssignmentAffinity("t1", "h1", 8),
      makeAssignmentAffinity("t1", "h1", 4),
      makeAssignmentAffinity("t2", "h1", 1),
      makeAssignmentAffinity("t2", "h2", 2),
  };
  problem->addGoal(assignmentAffinitiesSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  assertFinalAssignment(expected_assignment, solution);
}

TEST_P(PackerProblemTestSolvers, TestAssignmentAffinitiesWithScope) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t2"}}, {"h2", {"t1"}}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h1", {"t1"}}, {"h2", {"t2"}}};

  auto problem = makeProblem(initial_assignment);
  // add a simple scope that is equivalent to container scope
  problem->addScope(
      "hosts",
      std::unordered_map<std::string, std::string>{
          {"h1", "host1"}, {"h2", "host2"}});

  AssignmentAffinitiesSpec assignmentAffinitiesSpec;
  assignmentAffinitiesSpec.affinities() = {
      makeAssignmentAffinity("t1", "host1", 8),
      makeAssignmentAffinity("t1", "host2", 4),
      makeAssignmentAffinity("t2", "host1", 1),
      makeAssignmentAffinity("t2", "host2", 2),
  };
  assignmentAffinitiesSpec.scope() = "hosts";

  problem->addGoal(assignmentAffinitiesSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  assertFinalAssignment(expected_assignment, solution);
}

TEST_P(PackerProblemTestSolvers, TestMinimizeSquares) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t3"}}, {"h2", {"t4"}}, {"h3", {}}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h1", {"t2", "t3"}}, {"h2", {"t4"}}, {"h3", {"t1"}}};

  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "units",
      std::vector<std::pair<std::string, double>>{
          {"t1", 100}, {"t2", 5}, {"t3", 5}, {"t4", 10}});
  problem->addContainerDimension(
      "units",
      folly::F14FastMap<std::string, double>{
          {"h1", 200}, {"h2", 200}, {"h3", 200}});

  MinimizeMovementSpec minimizeMovementSpec;
  minimizeMovementSpec.scope() = "container";
  minimizeMovementSpec.dimension() = "object_count";
  problem->addGoal(minimizeMovementSpec);

  MinimizeSquaresSpec minimizeSquaresSpec;
  minimizeSquaresSpec.scope() = "container";
  minimizeSquaresSpec.dimension() = "units";
  problem->addGoal(minimizeSquaresSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  assertFinalAssignment(expected_assignment, solution);
  EXPECT_NEAR(0.085333333, *solution.finalObjective()->value(), 1e-8);
}

TEST_P(PackerProblemTestSolvers, TestScopeAffinities) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t3"}}, {"h2", {}}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h1", {"t3"}}, {"h2", {"t1", "t2"}}};

  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "units",
      std::vector<std::pair<std::string, double>>{
          {"t1", 60}, {"t2", 20}, {"t3", 50}});
  problem->addContainerDimension(
      "units",
      folly::F14FastMap<std::string, double>{{"h1", 100}, {"h2", 100}});
  addAfterConstraint(problem, "units", 1);

  ScopeAffinitiesSpec scopeAffinitiesSpec;
  scopeAffinitiesSpec.scope() = "container";
  scopeAffinitiesSpec.dimension() = "units";
  scopeAffinitiesSpec.affinities() = {{"h1", 1}, {"h2", 2}};
  problem->addGoal(scopeAffinitiesSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  assertFinalAssignment(expected_assignment, solution);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(-2.1, *solution.finalObjective()->value(), 1e-8);
}

TEST(PackerProblemTest, TestMissingObjectsWeight) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1"}}};

  auto problem = makeProblem(initial_assignment);
  const std::map<std::string, double> object_to_value;
  constexpr int missing_objects_weight = 7;
  problem->addObjectDimension("dim", object_to_value, missing_objects_weight);

  CapacitySpec capacitySpec;
  capacitySpec.scope() = "container";
  capacitySpec.dimension() = "dim";

  Limit limit;
  limit.globalLimit() = 0;
  capacitySpec.limit() = limit;

  problem->addGoal(capacitySpec);
  auto solution = solve(problem, LOCALSEARCH);
  EXPECT_EQ(missing_objects_weight, *solution.finalObjective()->value());
}

TEST(PackerProblemTest, TestWorkingSet) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t3"}}, {"h2", {}}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h1", {"t1", "t2"}}, {"h2", {"t3"}}};

  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "dim",
      std::vector<std::pair<std::string, double>>{
          {"t1", 10}, {"t2", 20}, {"t3", 40}});
  std::vector<WorkingUnit> workingUnits;
  {
    WorkingUnit wUnit;
    wUnit.endpoints() = std::vector<string>{"t1", "t2", "t3"};
    wUnit.weight() = 1.0;
    workingUnits.push_back(wUnit);
  }

  {
    WorkingUnit wUnit;
    wUnit.endpoints() = std::vector<string>{"t1", "t2"};
    wUnit.weight() = 10.0;
    workingUnits.push_back(wUnit);
  }

  {
    WorkingUnit wUnit;
    wUnit.endpoints() = std::vector<string>{"t2", "t3"};
    wUnit.weight() = 2.0;
    workingUnits.push_back(wUnit);
  }

  WorkingSetSpec workingSetSpec;
  workingSetSpec.scope() = "container";
  workingSetSpec.dimension() = "dim";
  workingSetSpec.workingUnits() = workingUnits;
  problem->addGoal(workingSetSpec);

  auto solution = solve(problem, LOCALSEARCH);
  assertFinalAssignment(expected_assignment, solution);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_EQ(510, *solution.finalObjective()->value());
}

TEST_P(PackerProblemTestSolvers, TestDefaultLimit) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1"}}, {"h2", {"t2"}}};

  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "dim",
      std::vector<std::pair<std::string, double>>{{"t1", 10}, {"t2", 20}});
  Limit limit;
  limit.type() = LimitType::RELATIVE;
  limit.globalLimit() = 2;
  limit.scopeItemLimits() = {{"h1", 4}};

  CapacitySpec capacitySpec;
  capacitySpec.scope() = "container";
  capacitySpec.dimension() = "dim";
  capacitySpec.definition() = CapacitySpecDefinition::AFTER;
  capacitySpec.bound() = CapacitySpecBound::MAX;
  capacitySpec.limit() = limit;

  problem->addGoal(capacitySpec);
  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_EQ(24, *solution.finalObjective()->value());
}

TEST(PackerProblemTest, TestForceSwap) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto solverPackage = facebook::algopt::getAvailableMIPSolver().value();
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t3"}}, {"h2", {"t2", "t4"}}};

  auto problem = create4PerfectShardsProblem(initial_assignment);
  BalanceSpec balanceSpec;
  balanceSpec.scope() = "container";
  balanceSpec.dimension() = "a";
  balanceSpec.formula() = BalanceSpecFormula::LEGACY;
  balanceSpec.fixAverageToInitial() = true;
  problem->addGoal(balanceSpec);

  Limit limit;
  limit.scopeItemLimits() = {{"h1", 3}, {"h2", 1}};

  CapacitySpec capacitySpec;
  capacitySpec.scope() = "container";
  capacitySpec.dimension() = "b";
  capacitySpec.definition() = CapacitySpecDefinition::AFTER;
  capacitySpec.bound() = CapacitySpecBound::MAX;
  capacitySpec.limit() = limit;
  problem->addConstraint(capacitySpec);

  problem->addConstraint(ExclusiveSwapsSpec{});

  auto solution = solve(problem, OPTIMAL, solverPackage);
  EXPECT_EQ(1, *solution.finalConstraint()->brokenCount());
}

TEST(PackerProblemTest, TestForceSwapSubsetBalanced1) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t3"}}, {"h2", {"t2", "t4"}}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h2", {"t1", "t2"}}, {"h1", {"t3", "t4"}}};

  auto problem = create4PerfectShardsProblem(initial_assignment);

  BalanceSpec balanceSpec;
  balanceSpec.scope() = "container";
  balanceSpec.dimension() = "a";
  balanceSpec.formula() = BalanceSpecFormula::LEGACY;
  balanceSpec.fixAverageToInitial() = true;
  problem->addGoal(balanceSpec);

  Limit limit;
  limit.scopeItemLimits() = {{"h1", 2}, {"h2", 2}};

  CapacitySpec capacitySpec;
  capacitySpec.scope() = "container";
  capacitySpec.dimension() = "b";
  capacitySpec.definition() = CapacitySpecDefinition::AFTER;
  capacitySpec.bound() = CapacitySpecBound::MAX;
  capacitySpec.limit() = limit;
  problem->addConstraint(capacitySpec);

  ExclusiveSwapsSpec exclusiveSwapsSpec;
  exclusiveSwapsSpec.subsetObjects() = {"t1"};
  exclusiveSwapsSpec.name() = "swap";
  problem->addConstraint(exclusiveSwapsSpec);

  auto solution = solve(problem, LOCALSEARCH);
  EXPECT_EQ(true, *solution.finalConstraint()->solved());
  assertFinalAssignment(expected_assignment, solution);
}

TEST(PackerProblemTest, TestForceSwapSubsetUnbalanced) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto solverPackage = facebook::algopt::getAvailableMIPSolver().value();
  /* Initial assignment can easily be balanced by placing t1 (weight 12)
   by itself (or optionally adding the small t3, weight 1,  with it.
   However, the swap subset constraint on t3 makes this balance impossible.)*/
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t3"}}, {"h2", {"t4", "t5"}}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h1", {"t1", "t2", "t3"}}, {"h2", {"t4", "t5"}}};

  auto problem = createSimplyBalanceableProblem(initial_assignment);

  BalanceSpec balanceSpec;
  balanceSpec.scope() = "container";
  balanceSpec.dimension() = "a";
  balanceSpec.formula() = BalanceSpecFormula::LEGACY;
  balanceSpec.fixAverageToInitial() = true;
  problem->addGoal(balanceSpec);

  ExclusiveSwapsSpec exclusiveSwapsSpec;
  exclusiveSwapsSpec.subsetObjects() = {"t3"};
  exclusiveSwapsSpec.name() = "swap";
  problem->addConstraint(exclusiveSwapsSpec);

  auto solution = solve(problem, OPTIMAL, solverPackage);
  EXPECT_EQ(true, *solution.finalConstraint()->solved());
  assertFinalAssignment(expected_assignment, solution);
}

TEST(PackerProblemTest, TestForceSwapSubsetBalanced2) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t3"}}, {"h2", {"t2", "t4"}}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h1", {"t1", "t2"}}, {"h2", {"t3", "t4"}}};

  auto problem = create4PerfectShardsProblem(initial_assignment);

  BalanceSpec balanceSpec;
  balanceSpec.scope() = "container";
  balanceSpec.dimension() = "a";
  balanceSpec.formula() = BalanceSpecFormula::LEGACY;
  balanceSpec.fixAverageToInitial() = true;
  problem->addGoal(balanceSpec);

  Limit limit;
  limit.scopeItemLimits() = {{"h1", 2}, {"h2", 2}};

  CapacitySpec capacitySpec;
  capacitySpec.scope() = "container";
  capacitySpec.dimension() = "b";
  capacitySpec.definition() = CapacitySpecDefinition::AFTER;
  capacitySpec.bound() = CapacitySpecBound::MAX;
  capacitySpec.limit() = limit;
  problem->addConstraint(capacitySpec);

  ExclusiveSwapsSpec exclusiveSwapsSpec;
  exclusiveSwapsSpec.subsetObjects() = {"t2"};
  exclusiveSwapsSpec.name() = "swap";
  problem->addConstraint(exclusiveSwapsSpec);

  auto solution = solve(problem, LOCALSEARCH);
  EXPECT_EQ(true, *solution.finalConstraint()->solved());
  assertFinalAssignment(expected_assignment, solution);
}

TEST_P(PackerProblemTestSolvers, TestBalanceSoftUpperBound) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h0", {"t1", "t2"}}, {"h1", {"t3"}}};
  const std::map<std::string, double> object_units = {
      {"t1", 1}, {"t2", 4}, {"t3", 2}};
  const std::map<std::string, double> container_units = {{"h0", 6}, {"h1", 6}};

  {
    auto problem = makeProblem(initial_assignment);
    problem->addObjectDimension("units", object_units);
    problem->addContainerDimension("units", container_units);

    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "units";
    balanceSpec.softUpperBound() = 0.9;
    balanceSpec.formula() = BalanceSpecFormula::MAX;
    problem->addGoal(balanceSpec);

    MinimizeMovementSpec minimizeMovementSpec;
    minimizeMovementSpec.scope() = "container";
    minimizeMovementSpec.dimension() = "object_count";
    problem->addGoal(minimizeMovementSpec, 0.001);

    const auto [solverAlgoType, solverPackage] = GetParam();
    auto solution = solve(problem, solverAlgoType, solverPackage);

    // since soft-upper-bound is set to 5.4 we will not balance even though h0
    // is above the average of 3.5
    const std::map<std::string, std::vector<std::string>> expected_assignment =
        {{"h0", {"t1", "t2"}}, {"h1", {"t3"}}};
    EXPECT_NEAR(0, *solution.finalObjective()->value(), 1e-8);
    assertFinalAssignment(expected_assignment, solution);
  }

  // since soft-upper-bound is set to 3 and average is 3.5, we use 3.5 as our
  // threshold for rebalancing
  {
    auto problem = makeProblem(initial_assignment);
    problem->addObjectDimension("units", object_units);
    problem->addContainerDimension("units", container_units);

    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "units";
    balanceSpec.softUpperBound() = 0.5;
    balanceSpec.formula() = BalanceSpecFormula::MAX;
    problem->addGoal(balanceSpec);

    MinimizeMovementSpec minimizeMovementSpec;
    minimizeMovementSpec.scope() = "container";
    minimizeMovementSpec.dimension() = "object_count";
    problem->addGoal(minimizeMovementSpec, 0.01);

    const auto [solverAlgoType, solverPackage] = GetParam();
    auto solution = solve(problem, solverAlgoType, solverPackage);

    const std::map<std::string, std::vector<std::string>> expected_assignment =
        {{"h0", {"t2"}}, {"h1", {"t1", "t3"}}};
    EXPECT_NEAR(0.0833383333, *solution.finalObjective()->value(), 1e-8);
    assertFinalAssignment(expected_assignment, solution);
  }
}

TEST_P(PackerProblemTestSolvers, TestBalance) {
  std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h0", {"t2", "t3"}}, {"h1", {"t1"}}, {"h2", {}}};
  std::map<std::string, double> object_units = {
      {"t1", 2}, {"t2", 4}, {"t3", 5}};
  std::map<std::string, double> container_units = {
      {"h0", 6}, {"h1", 6}, {"h2", 6}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h0", {}}, {"h1", {"t1", "t2"}}, {"h2", {"t3"}}};

  {
    auto problem = makeProblem(initial_assignment);
    problem->addObjectDimension("units", object_units);
    problem->addContainerDimension("units", container_units);

    ToFreeSpec toFreeSpec;
    toFreeSpec.containers() = {"h0"};
    problem->addConstraint(toFreeSpec);

    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = {"t1"};
    problem->addConstraint(avoidMovingSpec);

    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "units";

    Filter filter;
    filter.itemsBlacklist() = std::vector<std::string>{"h0"};
    balanceSpec.filter() = filter;
    problem->addGoal(balanceSpec);

    const auto [solverAlgoType, solverPackage] = GetParam();
    auto solution = solve(problem, solverAlgoType, solverPackage);
    EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
    EXPECT_NEAR(
        (1.0 - 11.0 / 12.0) / 2.0, *solution.finalObjective()->value(), 1e-8);
    assertFinalAssignment(expected_assignment, solution);
  }

  {
    auto problem = makeProblem(initial_assignment);
    problem->addObjectDimension("units", object_units);
    problem->addContainerDimension("units", container_units);

    ToFreeSpec toFreeSpec;
    toFreeSpec.containers() = {"h0"};
    problem->addConstraint(toFreeSpec);

    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = {"t1"};
    problem->addConstraint(avoidMovingSpec);

    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "units";
    balanceSpec.upperBound() = 0.05;
    balanceSpec.boundType() = BalanceSpecBoundType::ABSOLUTE;

    Filter filter;
    filter.itemsBlacklist() = std::vector<std::string>{"h0"};
    balanceSpec.filter() = filter;
    problem->addGoal(balanceSpec);

    const auto [solverAlgoType, solverPackage] = GetParam();
    auto solution = solve(problem, solverAlgoType, solverPackage);
    EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
    EXPECT_NEAR(
        (1.0 - 11.0 / 12.0 - 0.05) / 2.0,
        *solution.finalObjective()->value(),
        1e-8);
    assertFinalAssignment(expected_assignment, solution);
  }

  initial_assignment = {
      {"h0", {"t0", "t1", "t2", "t3"}}, {"h1", {}}, {"h2", {}}};
  object_units = {{"t0", 1}, {"t1", 1}, {"t2", 1}, {"t3", 1}};
  container_units = {{"h0", 4}, {"h1", 8}, {"h2", 4}};

  {
    auto problem = makeProblem(initial_assignment);
    problem->addObjectDimension("units", object_units);
    problem->addContainerDimension("units", container_units);

    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "units";
    problem->addGoal(balanceSpec);

    const auto [solverAlgoType, solverPackage] = GetParam();
    auto solution = solve(problem, solverAlgoType, solverPackage);
    EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
    EXPECT_NEAR(0, *solution.finalObjective()->value(), 1e-8);
    map<string, int> object_count;
    for (auto& object_container : *solution.assignment()) {
      ++object_count[object_container.second];
    }
    EXPECT_EQ(1, object_count["h0"]);
    EXPECT_EQ(2, object_count["h1"]);
    EXPECT_EQ(1, object_count["h2"]);
  }
}

TEST_P(PackerProblemTestSolvers, TestBalanceMax) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h0", {"t0", "t1"}}, {"h1", {"t2"}}, {"h2", {"t3"}}};
  const std::map<std::string, double> object_units = {
      {"t0", 1}, {"t1", 4}, {"t2", 4}, {"t3", 3}};
  const std::map<std::string, double> container_units = {
      {"h0", 6}, {"h1", 6}, {"h2", 6}};
  const std::map<std::string, std::vector<std::string>> expected_assignment = {
      {"h0", {"t1"}}, {"h1", {"t2"}}, {"h2", {"t0", "t3"}}};

  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension("units", object_units);
  problem->addContainerDimension("units", container_units);

  BalanceSpec balanceSpec;
  balanceSpec.scope() = "container";
  balanceSpec.dimension() = "units";
  balanceSpec.formula() = BalanceSpecFormula::MAX;
  problem->addGoal(balanceSpec);

  {
    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = {"t2", "t3"};
    problem->addConstraint(avoidMovingSpec);
  }

  {
    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = {"t2", "t3"};
    problem->addConstraint(avoidMovingSpec);
  }
  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_NEAR(0, *solution.finalObjective()->value(), 1e-8);
  assertFinalAssignment(expected_assignment, solution);
}

TEST(PackerProblemTest, TestBalanceWithUtilIncreaseCost) {
  // Combining UtilIncreaseCost (with squares) with Balance produces the
  // effect of choosing the lowest utilization container as destination, when
  // oterwise multiple destinations produce the same balance improvement.
  vector<string> tasks;
  tasks.reserve(295);
  for (const auto i : folly::irange(295)) {
    tasks.push_back(fmt::format("t{}", i));
  }
  const map<string, vector<string>> host_to_tasks = {
      {"h0", {tasks.begin(), tasks.begin() + 66}}, // 66 tasks
      {"h1", {tasks.begin() + 66, tasks.begin() + 135}}, // 69 tasks
      {"h2", {tasks.begin() + 135, tasks.begin() + 210}}, // 75 tasks
      {"h3", {tasks.begin() + 210, tasks.end()}}, // 85 tasks
      {"h4", {}}};
  const map<string, double> host_to_cpu = {
      {"h0", 100}, {"h1", 100}, {"h2", 100}, {"h3", 100}, {"h4", 100}};
  const map<string, double> host_to_flash = {
      {"h0", 100}, {"h1", 100}, {"h2", 100}, {"h3", 100}, {"h4", 0}};
  map<string, double> task_to_cpu;
  for (auto& t : tasks) {
    task_to_cpu[t] = 1;
  }
  map<string, double> task_to_flash;
  for (auto& t : tasks) {
    task_to_flash[t] = 1;
  }

  auto problem = makeProblem(host_to_tasks);
  problem->addObjectDimension("cpu", task_to_cpu);
  problem->addContainerDimension("cpu", host_to_cpu);
  problem->addObjectDimension("flash", task_to_flash);
  problem->addContainerDimension("flash", host_to_flash);

  CapacitySpec capacitySpec;
  capacitySpec.scope() = "container";
  capacitySpec.dimension() = "flash";
  problem->addConstraint(capacitySpec);

  BalanceSpec balanceSpec;
  balanceSpec.scope() = "container";
  balanceSpec.dimension() = "cpu"; // average is 0.59
  balanceSpec.upperBound() = 0.21; // effectively 0.8 (average + 0.21)
  balanceSpec.boundType() = BalanceSpecBoundType::ABSOLUTE;
  balanceSpec.formula() = BalanceSpecFormula::SQUARES;
  problem->addGoal(balanceSpec);

  UtilIncreaseCostSpec utilIncreaseCostSpec;
  utilIncreaseCostSpec.scope() = "container";
  utilIncreaseCostSpec.dimension() = "cpu";
  utilIncreaseCostSpec.lowerBound() = 0;
  utilIncreaseCostSpec.squares() = true;
  problem->addGoal(utilIncreaseCostSpec);

  auto solution = solve(problem, LOCALSEARCH);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  map<string, int> count;
  for (auto& object_container : *solution.assignment()) {
    ++count[object_container.second];
  }
  EXPECT_EQ(70, count["h0"]);
  EXPECT_EQ(70, count["h1"]);
  EXPECT_EQ(75, count["h2"]);
  EXPECT_EQ(80, count["h3"]);
  EXPECT_EQ(0, count["h4"]);
}

TEST_P(PackerProblemTestSolvers, TestBalanceOnNew) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h0", {"t10", "t11"}},
      {"h1", {"t0", "t1", "t2"}},
      {"h2", {"t3", "t4", "t5", "t6", "t7", "t8", "t9"}}};
  const std::map<std::string, double> object_units = {
      {"t0", 1},
      {"t1", 1},
      {"t2", 1},
      {"t3", 1},
      {"t4", 1},
      {"t5", 1},
      {"t6", 1},
      {"t7", 1},
      {"t8", 1},
      {"t9", 1},
      {"t10", 1},
      {"t11", 1}};
  const std::map<std::string, double> container_units = {
      {"h0", 10}, {"h1", 10}, {"h2", 10}};
  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension("units", object_units);
  problem->addContainerDimension("units", container_units);

  ToFreeSpec toFreeSpec;
  toFreeSpec.containers() = {"h0"};
  problem->addConstraint(toFreeSpec);

  BalanceSpec balance;
  balance.scope() = "container";
  balance.dimension() = "units";
  balance.formula() = BalanceSpecFormula::MAX;
  balance.definition() = BalanceSpecDefinition::NEW;

  Filter filter;
  filter.itemsBlacklist() = std::vector<std::string>{"h0"};
  balance.filter() = filter;
  problem->addGoal(balance);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  std::map<std::string, int> container_counts;
  for (auto& [object, container] : *solution.assignment()) {
    ++container_counts[container];
  }
  const std::map<std::string, int> expected = {{"h1", 4}, {"h2", 8}};
  EXPECT_EQ(expected, container_counts);
}

TEST(PackerProblemLocalSearchTest, TestSingleMoveHottestToColdest) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2"}},
      {"h2", {"t3", "t4"}},
      {"h3", {}},
  };

  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "a",
      folly::F14FastMap<std::string, double>{
          {"t1", 7}, {"t2", 3}, {"t3", 4}, {"t4", 2}});
  // h1 = 10, capacity = 7
  // h2 = 6, capacity = 6
  // h3 = 0, capacity = 4
  // we expect just t2 to move from h1 to h2
  problem->addContainerDimension(
      "a",
      folly::F14FastMap<std::string, double>{{"h1", 7}, {"h2", 6}, {"h3", 4}});

  CapacitySpec capacitySpec;
  capacitySpec.scope() = "container";
  capacitySpec.dimension() = "a";
  problem->addConstraint(capacitySpec);

  BalanceSpec balanceSpec;
  balanceSpec.scope() = "container";
  balanceSpec.dimension() = "a";
  balanceSpec.definition() = BalanceSpecDefinition::AFTER;
  problem->addGoal(std::move(balanceSpec));

  const auto& solution = localSearchSolveWithDefinedMoves(
      problem, {ProblemSolver::makeMoveTypeSpec(SingleGreedyMoveTypeSpec())});

  const std::map<std::string, std::string> expected_assignment = {
      {"t1", "h1"}, {"t3", "h2"}, {"t4", "h2"}, {"t2", "h3"}};
  EXPECT_EQ(
      expected_assignment,
      toOrderedMap(*solution.assignment())); // we get the expected assignment
  EXPECT_EQ(1, solution.movesSummary()->size()); // we expect just one move
  EXPECT_NEAR(
      0.1270,
      *solution.finalObjective()->value(),
      1e-3); // perfect balance is not possible in this problem
}

TEST(PackerProblemLocalSearchTest, TestSwapFullContainersMoveType) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2"}},
      {"h2", {}},
  };
  // Allow h1 to have 1 object,
  // but SWAP_FULL_CONTAINER should still move both objects
  const std::map<std::string, double> host_max_task_constraint = {
      {"h1", 1}, {"h2", 2}};

  auto problem = makeProblem(initial_assignment);
  problem->addContainerDimension("object_count", host_max_task_constraint);
  addAfterConstraint(problem, "object_count", 1);

  const auto& solution = localSearchSolveWithDefinedMoves(
      problem,
      {ProblemSolver::makeMoveTypeSpec(SwapFullContainersMoveTypeSpec())});

  const map<std::string, std::string> expected_assignment = {
      {"t1", "h2"}, {"t2", "h2"}};
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
}

TEST(
    PackerProblemLocalSearchTest,
    TestFailSwapFullWithEmptyContainersMoveType) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2"}}, {"h2", {"t3"}}, {"h3", {"t4"}}};
  // SWAP_FULL_WITH_EMPTY_CONTAINERS can't find any move
  // because all containers are full
  const std::map<std::string, double> host_max_task_constraint = {
      {"h1", 1}, {"h2", 100}, {"h3", 100}};

  auto problem = makeProblem(initial_assignment);
  problem->addContainerDimension("object_count", host_max_task_constraint);
  addAfterConstraint(problem, "object_count", 1);

  const auto& solution = localSearchSolveWithDefinedMoves(
      problem,
      {ProblemSolver::makeMoveTypeSpec(
          SwapFullWithEmptyContainersMoveTypeSpec())});

  const map<std::string, std::string> expected_assignment = {
      {"t1", "h1"}, {"t2", "h1"}, {"t3", "h2"}, {"t4", "h3"}};
  EXPECT_EQ(1, *solution.finalConstraint()->brokenCount());
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
}

TEST(
    PackerProblemLocalSearchTest,
    TestPassSwapFullWithEmptyContainersMoveType) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2"}}, {"h2", {}}, {"h3", {"t4"}}};
  // SWAP_FULL_WITH_EMPTY_CONTAINERS always swaps h1 and h2 because h3 is full
  const std::map<std::string, double> host_max_task_constraint = {
      {"h1", 1}, {"h2", 100}, {"h3", 100}};

  auto problem = makeProblem(initial_assignment);
  problem->addContainerDimension("object_count", host_max_task_constraint);
  addAfterConstraint(problem, "object_count", 1);

  const auto& solution = localSearchSolveWithDefinedMoves(
      problem,
      {ProblemSolver::makeMoveTypeSpec(
          SwapFullWithEmptyContainersMoveTypeSpec())});

  const map<std::string, std::string> expected_assignment = {
      {"t1", "h2"}, {"t2", "h2"}, {"t4", "h3"}};
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
}

TEST(
    PackerProblemLocalSearchTest,
    TestSwapFullContainerMoveTypeWithNotAccepting) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2"}},
      {"h2", {}},
  };
  const std::map<std::string, double> host_max_task_constraint = {
      {"h1", 1}, {"h2", 2}};
  auto problem = makeProblem(initial_assignment);
  problem->addContainerDimension("object_count", host_max_task_constraint);
  // Even if h1 is non_accepting, swap with h2 is allowed because h2 is empty
  NonAcceptingSpec nonAcceptingSpec;
  nonAcceptingSpec.scope() = "container";
  nonAcceptingSpec.items() = {"h1"};
  problem->addConstraint(nonAcceptingSpec);

  addAfterConstraint(problem, "object_count", 1);

  const auto& solution = localSearchSolveWithDefinedMoves(
      problem,
      {ProblemSolver::makeMoveTypeSpec(SwapFullContainersMoveTypeSpec())});

  const map<std::string, std::string> expected_assignment = {
      {"t1", "h2"}, {"t2", "h2"}};
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
}

TEST_P(PackerProblemTestSolvers, AllCapacitiesZero) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t3"}}, {"h2", {}}, {"h3", {}}};
  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "cpu",
      folly::F14FastMap<std::string, double>{
          {"t1", 10}, {"t2", 10}, {"t3", 10}});
  problem->addContainerDimension(
      "cpu",
      folly::F14FastMap<std::string, double>{
          {"h1", 100}, {"h2", 100}, {"h3", 100}});
  problem->addObjectDimension(
      "flash",
      folly::F14FastMap<std::string, double>{
          {"t1", 10}, {"t2", 10}, {"t3", 10}});
  problem->addContainerDimension(
      "flash",
      folly::F14FastMap<std::string, double>{
          {"h1", 100}, {"h2", 0}, {"h3", 0}});

  BalanceSpec balanceSpec;
  balanceSpec.scope() = "container";
  balanceSpec.dimension() = "cpu";
  problem->addGoal(balanceSpec);

  // add a capacity constraint on a set of containers of capacity 0, which is a
  // legit constraint and materializer should not freak out about it
  CapacitySpec capacitySpec;
  capacitySpec.scope() = "container";
  capacitySpec.dimension() = "flash";

  Filter filter;
  filter.itemsBlacklist() = {"h1"};
  capacitySpec.filter() = filter;
  problem->addConstraint(capacitySpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  const std::map<std::string, std::string> expected_assignment = {
      {"t1", "h1"}, {"t2", "h1"}, {"t3", "h1"}};
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
}

TEST(PackerProblemOptimalTest, AllConstraintsStrictInvalidInitial) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2"}}, {"h2", {}}};

  auto problem = makeProblem(initial_assignment);

  problem->setConstraintPolicy(ConstraintPolicy::HARD);

  CapacitySpec capacitySpec;
  capacitySpec.scope() = "container";
  capacitySpec.dimension() = "object_count";

  Filter filter;
  filter.itemsBlacklist() = {"h2"};
  capacitySpec.filter() = filter;
  problem->addConstraint(capacitySpec);

  AvoidMovingSpec avoidMovingSpec;
  avoidMovingSpec.objects() = {"t1"};
  problem->addConstraint(avoidMovingSpec);

  auto optimalSolverSpec = facebook::algopt::makeAvailableOptimalSolverSpec();
  optimalSolverSpec.skipInitialAssignmentHint() = true;
  problem->addSolver(optimalSolverSpec);

  auto solution = solve(problem, LOCALSEARCH);
  const std::map<std::string, std::string> expected_assignment = {
      {"t1", "h1"}, {"t2", "h2"}};
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
}

TEST(PackerProblemOptimalTest, NegativeMaxTime) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2"}}, {"h2", {}}};

  auto problem = makeProblem(initial_assignment);

  problem->setConstraintPolicy(ConstraintPolicy::HARD);

  CapacitySpec capacitySpec;
  capacitySpec.scope() = "container";
  capacitySpec.dimension() = "object_count";

  Filter filter;
  filter.itemsBlacklist() = {"h2"};
  capacitySpec.filter() = filter;
  problem->addConstraint(capacitySpec);

  AvoidMovingSpec avoidMovingSpec;
  avoidMovingSpec.objects() = {"t1"};
  problem->addConstraint(avoidMovingSpec);

  auto optimalSolverSpec = facebook::algopt::makeAvailableOptimalSolverSpec();
  optimalSolverSpec.skipInitialAssignmentHint() = true;
  optimalSolverSpec.solveTime() = -1 * 10;
  problem->addSolver(optimalSolverSpec);

  auto solution = solve(problem, LOCALSEARCH);
  const std::map<std::string, std::string> expected_assignment = {
      {"t1", "h1"}, {"t2", "h2"}};
  EXPECT_EQ(0, *solution.finalConstraint()->brokenCount());
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
}

TEST(PackerProblemTest, UtilIncreaseCost) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h0", {"t0"}}, {"h1", {"t1"}}, {"h2", {"t2"}}};
  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "cpu",
      folly::F14FastMap<std::string, double>{
          {"t0", 20}, {"t1", 30}, {"t2", 50}});
  problem->addContainerDimension(
      "cpu",
      folly::F14FastMap<std::string, double>{
          {"h0", 100}, {"h1", 100}, {"h2", 100}});

  ToFreeSpec toFreeSpec;
  toFreeSpec.containers() = {"h0"};
  problem->addConstraint(toFreeSpec);

  UtilIncreaseCostSpec utilIncreaseCostSpec;
  utilIncreaseCostSpec.scope() = "container";
  utilIncreaseCostSpec.dimension() = "cpu";
  utilIncreaseCostSpec.lowerBound() = 0.4;
  utilIncreaseCostSpec.squares() = true;
  problem->addGoal(utilIncreaseCostSpec);

  ScopeAffinitiesSpec scopeAffinitiesSpec;
  scopeAffinitiesSpec.scope() = "container";
  scopeAffinitiesSpec.dimension() = "cpu";
  scopeAffinitiesSpec.affinities() = {{"h1", 0.001}, {"h2", 0.01}};

  problem->addGoal(scopeAffinitiesSpec);

  auto solution = solve(problem, LOCALSEARCH);

  const std::map<std::string, std::string> expected_assignment = {
      {"t0", "h1"}, {"t1", "h1"}, {"t2", "h2"}};
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
  EXPECT_NEAR(0.028346360382070453, *solution.finalObjective()->value(), 1e-8);
}

TEST(PackerProblemLocalSearchTest, ColdestStratifiedMoveType) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1"}},
      {"h2", {"t2"}},
      {"h3", {"t3"}},
      {"h4", {}},
      {"unassigned", {"t4"}},
  };

  auto problem = makeProblem(initial_assignment);
  problem->addObjectDimension(
      "a",
      folly::F14FastMap<std::string, double>{
          {"t1", 40}, {"t2", 20}, {"t3", 10}, {"t4", 10}});
  problem->addContainerDimension(
      "a",
      folly::F14FastMap<std::string, double>{
          {"h1", 100}, {"h2", 100}, {"h3", 100}, {"h4", 100}});

  MinimizeSquaresSpec minimizeSquaresSpec;
  minimizeSquaresSpec.scope() = "container";
  minimizeSquaresSpec.dimension() = "a";
  problem->addGoal(minimizeSquaresSpec);

  ToFreeSpec toFreeSpec;
  toFreeSpec.containers() = {"unassigned"};
  problem->addConstraint(toFreeSpec);

  LocalSearchSolverSpec localSearchSolverSpec;
  localSearchSolverSpec.solveTime() = 10;
  localSearchSolverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec("SINGLE_COLDEST_STRATIFIED"));
  localSearchSolverSpec.stratifiedSampleSize() = 2;
  problem->addSolver(localSearchSolverSpec);

  problem->addSimilarContainers({{"h1", "h2"}, {"unassigned", "h3", "h4"}});
  auto solution = solve(problem, LOCALSEARCH);

  const std::map<std::string, std::string> expected_assignment = {
      {"t1", "h1"}, {"t2", "h2"}, {"t3", "h3"}, {"t4", "h4"}};
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
  EXPECT_EQ(1, solution.movesSummary()->size());
  EXPECT_EQ(2, *solution.movesSummary()->at(0).evalsCount());
}

TEST_P(PackerProblemTestSolvers, ThrottlingMoves) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2"}},
      {"h2", {}},
      {"h3", {}},
  };
  auto problem = makeProblem(initial_assignment);

  AssignmentAffinitiesSpec assignmentAffinitiesSpec;
  assignmentAffinitiesSpec.affinities() = {
      makeAssignmentAffinity("t1", "h2", 1.0),
      makeAssignmentAffinity("t2", "h3", 2.0),
  };

  problem->addGoal(assignmentAffinitiesSpec);

  ThrottlingSpec throttlingSpec;
  throttlingSpec.scope() = "container";
  throttlingSpec.dimension() = "object_count";
  throttlingSpec.definition() = ThrottlingSpecDefinition::ANY;
  problem->addConstraint(throttlingSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  const std::map<std::string, std::string> expected_assignment = {
      {"t1", "h1"}, {"t2", "h3"}};
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
  EXPECT_NEAR(-2.0, *solution.finalObjective()->value(), 1e-8);
}

TEST_P(PackerProblemTestSolvers, ThrottlingWithFilter) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t1", "t2", "t3", "t4"}},
      {"h2", {}},
      {"h3", {}},
  };
  auto problem = makeProblem(initial_assignment);

  ThrottlingSpec throttlingSpec;
  throttlingSpec.scope() = "container";
  throttlingSpec.dimension() = "object_count";
  throttlingSpec.definition() = ThrottlingSpecDefinition::IN;

  Filter filter;
  filter.itemsBlacklist() = {"h3"};
  throttlingSpec.filter() = filter;
  problem->addConstraint(throttlingSpec);

  ToFreeSpec toFreeSpec;
  toFreeSpec.containers() = {"h1"};
  problem->addConstraint(toFreeSpec);

  BalanceSpec balanceSpec;
  balanceSpec.scope() = "container";
  balanceSpec.dimension() = "object_count";
  balanceSpec.formula() = BalanceSpecFormula::MAX;
  problem->addGoal(balanceSpec);

  const auto [solverAlgoType, solverPackage] = GetParam();
  auto solution = solve(problem, solverAlgoType, solverPackage);
  auto hostCount = genHostCounts(solution);
  EXPECT_EQ(0, hostCount["h1"]);
  EXPECT_EQ(1, hostCount["h2"]);
  EXPECT_EQ(3, hostCount["h3"]);
  EXPECT_NEAR(5.0 / 3.0, *solution.finalObjective()->value(), 1e-8);
}

TEST(PackerProblemTest, ObjectOrdering) {
  auto problem =
      makeProblem({{"FAKE_HOST", {"o1", "o2", "o3"}}, {"c1", {}}, {"c2", {}}});

  problem->addObjectDimension(
      "cpu",
      folly::F14FastMap<std::string, double>{
          {"o1", 20.0}, {"o2", 90.0}, {"o3", 20.0}});
  problem->addContainerDimension(
      "cpu",
      folly::F14FastMap<std::string, double>{
          {"FAKE_HOST", 0}, {"c1", 100.0}, {"c2", 100.0}});

  problem->enableRestrictMovingObjectOnlyOnce();

  // add a capacity constraint
  CapacitySpec capacitySpec;
  capacitySpec.scope() = "container";
  capacitySpec.dimension() = "cpu";

  Filter filter;
  filter.itemsBlacklist() = {"FAKE_HOST"};
  capacitySpec.filter() = filter;
  problem->addConstraint(capacitySpec);

  ToFreeSpec toFreeSpec;
  toFreeSpec.containers() = {"FAKE_HOST"};
  problem->addConstraint(toFreeSpec);

  // make final solution stable
  AssignmentAffinitiesSpec assignmentAffinitiesSpec;
  assignmentAffinitiesSpec.affinities() = {
      makeAssignmentAffinity("o2", "c2", 100.0)};
  problem->addGoal(assignmentAffinitiesSpec);

  LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleFastMoveTypeSpec()));
  solverSpec.objectOrderingDimension() = "cpu";
  problem->addSolver(solverSpec);
  auto solution = problem->solve();

  const std::map<std::string, std::string> expected_assignment = {
      {"o1", "c1"}, {"o2", "c2"}, {"o3", "c1"}};
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
}

TEST(PackerProblemTest, OverrideObjectCountDimension) {
  auto problem = makeProblem({{"c1", {"o1"}}, {"c2", {"o2"}}});
  try {
    problem->addObjectDimension(
        "object_count",
        folly::F14FastMap<std::string, double>{{"o1", 1}, {"o2", 2}});
  } catch (const std::exception& e) {
    EXPECT_STREQ(
        "Unexpected call to makeObjectDimensionId with a previously added dimension name 'object_count'",
        e.what());
  }
}

TEST(PackerProblemTest, BasicArbiters) {
  auto makeProblemWithBalanceGoal = []() {
    auto problem = makeProblem(
        {{"c1", {"o1"}}, {"c2", {"o2", "o3"}}, {"c3", {"o4", "o5", "o6"}}});
    problem->addContainerDimension(
        "object_count",
        folly::F14FastMap<std::string, double>{
            {"c1", 6}, {"c2", 6}, {"c3", 6}});
    // this goal ensures a move is made to a heavily utilized container

    AssignmentAffinitiesSpec assignmentAffinitiesSpec;
    assignmentAffinitiesSpec.affinities() = {
        makeAssignmentAffinity("o3", "c3", 1.0)};
    problem->addGoal(assignmentAffinitiesSpec, 1.0);

    problem->addGoalBoundary();

    // this goal ensures that no moves are made for balance
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "object_count";
    balanceSpec.fixAverageToInitial() = true;
    balanceSpec.softUpperBound() = 1.0;
    problem->addGoal(balanceSpec, 1);

    return problem;
  };

  auto addUtilGoalToProblem = [](ProblemSolver& problem) {
    UtilIncreaseCostSpec utilIncreaseCostSpec;
    utilIncreaseCostSpec.scope() = "container";
    utilIncreaseCostSpec.dimension() = "object_count";
    utilIncreaseCostSpec.squares() = true;
    problem.addGoal(utilIncreaseCostSpec, 0.1);
  };

  // only 1 move due to affinity
  {
    auto problem = makeProblemWithBalanceGoal();
    LocalSearchSolverSpec localSearchSolverSpec;
    localSearchSolverSpec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
    problem->addSolver(localSearchSolverSpec);

    auto solution = problem->solve();
    auto hostCounts = genHostCounts(solution);
    EXPECT_EQ(1, hostCounts.at("c1"));
    EXPECT_EQ(1, hostCounts.at("c2"));
    EXPECT_EQ(4, hostCounts.at("c3"));
  }

  // 1 move due to affinity and one due to util-increase-cost (+ objective)
  {
    auto problem = makeProblemWithBalanceGoal();
    addUtilGoalToProblem(*problem);

    LocalSearchSolverSpec localSearchSolverSpec;
    localSearchSolverSpec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
    problem->addSolver(localSearchSolverSpec);

    auto solution = problem->solve();
    auto hostCounts = genHostCounts(solution);
    EXPECT_EQ(1, hostCounts.at("c1"));
    EXPECT_EQ(2, hostCounts.at("c2"));
    EXPECT_EQ(3, hostCounts.at("c3"));
  }

  // 1 move due to affinity and one due to util-increase-cost (at pos 2)
  {
    auto problem = makeProblemWithBalanceGoal();
    problem->addGoalBoundary();
    addUtilGoalToProblem(*problem);

    LocalSearchSolverSpec localSearchSolverSpec;
    localSearchSolverSpec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
    problem->addSolver(localSearchSolverSpec);

    auto solution = problem->solve();
    auto hostCounts = genHostCounts(solution);
    EXPECT_EQ(1, hostCounts.at("c1"));
    EXPECT_EQ(2, hostCounts.at("c2"));
    EXPECT_EQ(3, hostCounts.at("c3"));
  }

  // only 1 move due to affinity, improvement to util-increase cost does not
  // result in a move
  {
    auto problem = makeProblemWithBalanceGoal();
    problem->addGoalBoundary();
    auto arbiterStart = problem->getCurrentGoalIndex();
    addUtilGoalToProblem(*problem);

    LocalSearchSolverSpec localSearchSolverSpec;
    localSearchSolverSpec.objectivesForHottestContainers() = arbiterStart;
    localSearchSolverSpec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
    problem->addSolver(localSearchSolverSpec);

    auto solution = problem->solve();
    auto hostCounts = genHostCounts(solution);
    EXPECT_EQ(1, hostCounts.at("c1"));
    EXPECT_EQ(1, hostCounts.at("c2"));
    EXPECT_EQ(4, hostCounts.at("c3"));
  }
}

TEST(OptimalSolverTestInfeasible, TestInfeasibleProblem) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  std::unique_ptr<ProblemSolver> problem =
      makeProblem({{"c1", {"a", "b"}}, {"c2", {"c"}}, {"c3", {"d", "e"}}});
  CapacitySpec cspec;
  cspec.scope() = "container";
  cspec.dimension() = "object_count";
  cspec.definition() = CapacitySpecDefinition::DOUBLE_DURING_AND_AFTER;
  problem->addConstraint(cspec);

  auto optimalSpec = facebook::algopt::makeAvailableOptimalSolverSpec();
  optimalSpec.skipInitialAssignmentHint() = true;
  problem->addSolver(optimalSpec);

  problem->addConstraint(cspec, ConstraintPolicy::HARD);
  EXPECT_ANY_THROW(problem->solve());
}

TEST(PackerProblemTest, LocalSearchStages) {
  folly::initLoggingOrDie("dbg2");
  auto addSpecs = [](ProblemSolver& problem) {
    // ensure no container gets more than its capacity of objects
    CapacitySpec capacitySpec;
    capacitySpec.scope() = "container";
    capacitySpec.dimension() = "object_count";
    problem.addConstraint(capacitySpec);

    // "o1" does not want to be in c1 or c2 and prefers being in c3 but cannot
    // move there since it is full
    AssignmentAffinitiesSpec assignmentAffinitiesSpec;
    assignmentAffinitiesSpec.affinities() = {
        makeAssignmentAffinity("o1", "c1", -4.0),
        makeAssignmentAffinity("o1", "c2", -5.0),
        makeAssignmentAffinity("o1", "c3", 1.0),
        makeAssignmentAffinity("o5", "c1", 1.0),
        makeAssignmentAffinity("o5", "c2", -2.0),
        makeAssignmentAffinity("o5", "c3", -2.0),
    };
    assignmentAffinitiesSpec.name() = "stickiness";
    problem.addGoal(assignmentAffinitiesSpec, 1.0);

    problem.addGoalBoundary();

    // this goal ensures that no moves are made for balance
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "object_count";
    balanceSpec.fixAverageToInitial() = true;
    balanceSpec.name() = "balance";
    problem.addGoal(balanceSpec, 1);
  };
  {
    auto problem = makeProblem(
        {{"c1", {"o1", "o2", "o3"}},
         {"c2", {"o4"}},
         {"c3", {"o5", "o6", "o7"}}});
    problem->addContainerDimension(
        "object_count",
        folly::F14FastMap<std::string, double>{
            {"c1", 3}, {"c2", 3}, {"c3", 3}});
    addSpecs(*problem);
    MoveStatsSpec ms;
    *ms.trackContainers() = true;
    problem->enableMoveStats(ms);

    LocalSearchSolverSpec localSearchSolverSpec;
    localSearchSolverSpec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
    problem->addSolver(localSearchSolverSpec);

    auto solution = problem->solve();
    auto hostCounts = genHostCounts(solution);
    EXPECT_EQ(2, hostCounts.at("c1"));
    EXPECT_EQ(2, hostCounts.at("c2"));
    EXPECT_EQ(3, hostCounts.at("c3"));
    auto& moves = solution.movesSummary().value();
    EXPECT_EQ(3, moves.size());
    // first move is from c1, since stickiness cannot be improved we fix balance
    EXPECT_EQ("c1", *moves.at(0).moves()->at(0).srcContainer());
    EXPECT_EQ("c2", *moves.at(0).moves()->at(0).dstContainer());

    // move is to improve stickiness for c3
    EXPECT_EQ("c3", *moves.at(1).moves()->at(0).srcContainer());
    EXPECT_EQ("c1", *moves.at(1).moves()->at(0).dstContainer());
    EXPECT_EQ("o5", *moves.at(1).moves()->at(0).object());
  }
  // less moves, stickiness moves first, followed by one move for balance
  {
    auto problem = makeProblem(
        {{"c1", {"o1", "o2", "o3"}},
         {"c2", {"o4"}},
         {"c3", {"o5", "o6", "o7", "o8"}}});
    problem->addContainerDimension(
        "object_count",
        folly::F14FastMap<std::string, double>{
            {"c1", 4}, {"c2", 4}, {"c3", 4}});
    addSpecs(*problem);
    MoveStatsSpec ms;
    *ms.trackContainers() = true;
    problem->enableMoveStats(ms);

    std::vector<LocalSearchStageSpec> stageSpecs = {};
    {
      LocalSearchStageSpec localSearchStageSpec;
      localSearchStageSpec.begin() = 0;
      localSearchStageSpec.end() = 1;
      localSearchStageSpec.solverSpec()
          ->exploreMovesFromContainersNotInObjective() = false;
      localSearchStageSpec.solverSpec()->moveTypeList()->push_back(
          ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
      stageSpecs.push_back(localSearchStageSpec);
    }

    {
      LocalSearchStageSpec localSearchStageSpec;
      localSearchStageSpec.begin() = 1;
      localSearchStageSpec.end() = 2;
      localSearchStageSpec.solverSpec()
          ->exploreMovesFromContainersNotInObjective() = false;
      localSearchStageSpec.name() = "Balance";
      localSearchStageSpec.solverSpec()->moveTypeList()->push_back(
          ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
      stageSpecs.push_back(localSearchStageSpec);
    }

    LocalSearchStageSolverSpec localSearchStageSolverSpec;
    localSearchStageSolverSpec.stageSpecs() = stageSpecs;
    problem->addSolver(localSearchStageSolverSpec);

    auto solution = problem->solve();
    auto hostCounts = genHostCounts(solution);
    EXPECT_EQ(3, hostCounts.at("c1"));
    EXPECT_EQ(2, hostCounts.at("c2"));
    EXPECT_EQ(3, hostCounts.at("c3"));
    auto& moves = solution.movesSummary().value();
    EXPECT_EQ(3, moves.size());
    // stickiness for o1 is improved
    EXPECT_EQ("c3", solution.assignment()->at("o1"));
    // stickiness for o5 is improved
    EXPECT_EQ("c1", solution.assignment()->at("o5"));

    // move is to improve stickiness for c3
    EXPECT_EQ("c3", *moves.at(0).moves()->at(0).srcContainer());
    EXPECT_EQ("c1", *moves.at(0).moves()->at(0).dstContainer());
    EXPECT_EQ("o5", *moves.at(0).moves()->at(0).object());
    // move is to improve stickiness for c1
    EXPECT_EQ("c1", *moves.at(1).moves()->at(0).srcContainer());
    EXPECT_EQ("c3", *moves.at(1).moves()->at(0).dstContainer());
    EXPECT_EQ("o1", *moves.at(1).moves()->at(0).object());
    // now c3 is worse off, so fix balance
    EXPECT_EQ("c3", *moves.at(2).moves()->at(0).srcContainer());
    EXPECT_EQ("c2", *moves.at(2).moves()->at(0).dstContainer());

    // see that stage summaries match expected
    EXPECT_EQ(2, solution.solverSummaries()->at(0).stagesSummaries()->size());
    auto stage0Summary =
        solution.solverSummaries()->at(0).stagesSummaries()->at(0);
    EXPECT_EQ("Stage-0: objectives:[0-1)", stage0Summary.name().value());
    EXPECT_LT(0, *stage0Summary.duration());
    EXPECT_EQ(2, *stage0Summary.moveStats().value().numMoves());
    auto stage1Summary =
        solution.solverSummaries()->at(0).stagesSummaries()->at(1);
    EXPECT_EQ(
        "Stage-1: Balance (objectives:[1-2))", stage1Summary.name().value());
    EXPECT_LT(0, *stage1Summary.duration());
    EXPECT_EQ(1, *stage1Summary.moveStats().value().numMoves());
  }
}

TEST(PackerProblemTest, LocalSearchStagesMinRuntime) {
  auto makeProblemWithBalanceGoal = []() {
    auto problem = makeProblem(
        {{"c1", {"o1", "o2", "o3"}},
         {"c2", {"o4"}},
         {"c3", {"o5", "o6", "o7", "o8"}}});
    problem->addContainerDimension(
        "object_count",
        folly::F14FastMap<std::string, double>{
            {"c1", 4}, {"c2", 4}, {"c3", 4}});
    // ensure no container gets more than its capacity of objects
    {
      CapacitySpec capacitySpec;
      capacitySpec.scope() = "container";
      capacitySpec.dimension() = "object_count";
      problem->addConstraint(capacitySpec);
    }

    // "o1" does not want to be in c1 or c2 and prefers being in c3 but cannot
    // move there since it is full
    AssignmentAffinitiesSpec assignmentAffinitiesSpec;
    assignmentAffinitiesSpec.affinities() = {
        makeAssignmentAffinity("o1", "c1", -4.0),
        makeAssignmentAffinity("o1", "c2", -4.0),
        makeAssignmentAffinity("o1", "c3", 1.0),
        makeAssignmentAffinity("o5", "c1", 1.0),
        makeAssignmentAffinity("o5", "c2", -2.0),
        makeAssignmentAffinity("o5", "c3", -2.0)};
    assignmentAffinitiesSpec.name() = "stickiness";
    problem->addGoal(assignmentAffinitiesSpec, 1.0);

    problem->addGoalBoundary();

    {
      CapacitySpec capacitySpec;
      capacitySpec.scope() = "container";
      capacitySpec.name() = "drain c2";

      Limit limit;
      limit.type() = LimitType::RELATIVE;
      limit.scopeItemLimits() =
          std::map<std::string, double>({{"c1", 4}, {"c2", 0}, {"c3", 4}});
      capacitySpec.limit() = limit;
      problem->addGoal(capacitySpec, 1);
    }

    problem->addGoalBoundary();

    // this goal ensures that no moves are made for balance
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "object_count";
    balanceSpec.fixAverageToInitial() = true;
    balanceSpec.name() = "balance";
    problem->addGoal(balanceSpec, 1);

    return problem;
  };

  {
    auto problem = makeProblemWithBalanceGoal();

    LocalSearchSolverSpec localSearchSolverSpec;
    localSearchSolverSpec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));

    std::vector<LocalSearchStageSpec> stageSpecs = {};
    {
      LocalSearchStageSpec localSearchStageSpec;
      localSearchStageSpec.solverSpec() = localSearchSolverSpec;
      localSearchStageSpec.begin() = 0;
      localSearchStageSpec.end() = 1;
      localSearchStageSpec.solverSpec()
          ->exploreMovesFromContainersNotInObjective() = false;
      localSearchStageSpec.name() = "Stickiness";
      stageSpecs.push_back(localSearchStageSpec);
    }

    {
      LocalSearchStageSpec localSearchStageSpec;
      localSearchStageSpec.solverSpec() = localSearchSolverSpec;
      localSearchStageSpec.begin() = 1;
      localSearchStageSpec.end() = 2;
      localSearchStageSpec.solverSpec()
          ->exploreMovesFromContainersNotInObjective() = false;
      localSearchStageSpec.name() = "Drain";
      stageSpecs.push_back(localSearchStageSpec);
    }

    {
      LocalSearchStageSpec localSearchStageSpec;
      localSearchStageSpec.solverSpec() = localSearchSolverSpec;
      localSearchStageSpec.begin() = 2;
      localSearchStageSpec.end() = 3;
      localSearchStageSpec.solverSpec()
          ->exploreMovesFromContainersNotInObjective() = false;
      localSearchStageSpec.name() = "Balance";
      stageSpecs.push_back(localSearchStageSpec);
    }

    LocalSearchStageSolverSpec localSearchStageSolverSpec;
    localSearchStageSolverSpec.stageSpecs() = stageSpecs;
    problem->addSolver(localSearchStageSolverSpec);

    auto solution = problem->solve();
    auto hostCounts = genHostCounts(solution);
    EXPECT_EQ(4, hostCounts.at("c1"));
    EXPECT_EQ(4, hostCounts.at("c3"));
  }

  {
    auto problem = makeProblemWithBalanceGoal();

    std::vector<LocalSearchStageSpec> stageSpecs = {};
    {
      LocalSearchStageSpec localSearchStageSpec;
      localSearchStageSpec.begin() = 0;
      localSearchStageSpec.end() = 1;
      localSearchStageSpec.solverSpec()
          ->exploreMovesFromContainersNotInObjective() = false;
      localSearchStageSpec.name() = "Stickiness";
      localSearchStageSpec.minRuntimeSec() = 600;
      localSearchStageSpec.solverSpec()->moveTypeList()->push_back(
          ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
      stageSpecs.push_back(localSearchStageSpec);
    }

    {
      LocalSearchStageSpec localSearchStageSpec;
      localSearchStageSpec.begin() = 1;
      localSearchStageSpec.end() = 2;
      localSearchStageSpec.solverSpec()
          ->exploreMovesFromContainersNotInObjective() = false;
      localSearchStageSpec.name() = "Drain";
      localSearchStageSpec.solverSpec()->moveTypeList()->push_back(
          ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
      localSearchStageSpec.solverSpec()->solveTime() = 0;
      stageSpecs.push_back(localSearchStageSpec);
    }

    {
      LocalSearchStageSpec localSearchStageSpec;
      localSearchStageSpec.begin() = 2;
      localSearchStageSpec.end() = 3;
      localSearchStageSpec.solverSpec()
          ->exploreMovesFromContainersNotInObjective() = false;
      localSearchStageSpec.name() = "Balance";
      localSearchStageSpec.solverSpec()->moveTypeList()->push_back(
          ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
      localSearchStageSpec.minRuntimeSec() = 600;
      stageSpecs.push_back(localSearchStageSpec);
    }

    LocalSearchStageSolverSpec localSearchStageSolverSpec;
    localSearchStageSolverSpec.stageSpecs() = stageSpecs;
    problem->addSolver(localSearchStageSolverSpec);

    auto solution = problem->solve();
    auto hostCounts = genHostCounts(solution);
    EXPECT_EQ(3, hostCounts.at("c1"));
    EXPECT_EQ(1, hostCounts.at("c2"));
    EXPECT_EQ(4, hostCounts.at("c3"));
  }
}

TEST_P(PackerProblemTestSolvers, TestGroupCapacitySamePartition) {
  const std::map<std::string, std::vector<std::string>> initial_assignment = {
      {"h1", {"t2", "t3"}},
      {"h2", {"t5"}},
      {"h3", {"t1", "t4", "t6", "t7", "t8"}}};
  const std::map<std::string, std::map<string, double>> scopeItemContributions =
      {{"rack1", {{"job1", 1}, {"job2", 5}, {"job4", 5}, {"job3", 3}}},
       {"rack2", {{"job1", 8}, {"job2", 1}, {"job4", 7}, {"job3", 0}}}};
  folly::F14FastMap<std::string, double> host_capacity = {
      {"h1", 3}, {"h2", 3}, {"h3", 3}};

  const auto [solverAlgoType, solverPackage] = GetParam();

  {
    // Feasible group capacity for all group if one of t6/t7/t8 is moved out of
    // h3
    std::vector<std::pair<std::string, double>> host_capacity_pair_vec;
    for (auto& [key, val] : host_capacity) {
      host_capacity_pair_vec.emplace_back(key, val);
    }
    const std::map<std::string, double> groupLimits = {
        {"job1", 2}, {"job2", 6}, {"job3", 3}, {"job4", 19}};
    auto [solution, rack_spread] = solveGroupCapacityProblem(
        initial_assignment,
        GroupCapacitySpecDefinition::AFTER,
        GroupCapacitySpecBound::MAX,
        groupLimits,
        scopeItemContributions,
        host_capacity_pair_vec,
        true,
        solverAlgoType,
        solverPackage);
  }
  {
    // NO WAY TO SATISFY THIS REQUIREMENT
    const std::map<std::string, double> groupLimits = {
        {"job1", 2}, {"job2", 6}, {"job3", 3}, {"job4", 6}}; // TOO LOW FOR job4
    auto [solution, rack_spread] = solveGroupCapacityProblem(
        initial_assignment,
        GroupCapacitySpecDefinition::AFTER,
        GroupCapacitySpecBound::MAX,
        groupLimits,
        scopeItemContributions,
        host_capacity,
        false,
        solverAlgoType,
        solverPackage);
  }
  {
    // flip the bound from max to min and its possible
    const std::map<std::string, double> groupLimits = {
        {"job1", 2}, {"job2", 10}, {"job3", 3}, {"job4", 6}};
    auto [solution, rack_spread] = solveGroupCapacityProblem(
        initial_assignment,
        GroupCapacitySpecDefinition::AFTER,
        GroupCapacitySpecBound::MIN,
        groupLimits,
        scopeItemContributions,
        host_capacity,
        true,
        solverAlgoType,
        solverPackage);
  }
  {
    // only 1 way to satisify exact assignement
    // job1 in rack1, job 2 in rack1, job 3 in rack 1 and job4 in rack 2
    const std::map<std::string, double> groupLimits = {
        {"job1", 2}, {"job2", 6}, {"job3", 3}, {"job4", 21}};
    auto [solution, rack_spread] = solveGroupCapacityProblem(
        initial_assignment,
        GroupCapacitySpecDefinition::AFTER,
        GroupCapacitySpecBound::MIN,
        groupLimits,
        scopeItemContributions,
        host_capacity,
        true,
        solverAlgoType,
        solverPackage);
    for (const auto& [job, racks] : rack_spread) {
      EXPECT_EQ(racks.size(), 1);
    }
    EXPECT_EQ(rack_spread.at("job1").at("rack1"), 2);
    EXPECT_EQ(rack_spread.at("job2").at("rack1"), 2);
    EXPECT_EQ(rack_spread.at("job3").at("rack1"), 1);
    EXPECT_EQ(rack_spread.at("job4").at("rack2"), 3);
  }
  {
    // only 1 way to satisify exact assignement
    // job1 in rack1, job 2 in rack1, job 3 in rack 1 and job4 in rack 2
    // problem should fail if we reduce capacity of rack 2 below 3
    host_capacity["h3"] = 2;
    host_capacity["h2"] = 4;
    const std::map<std::string, double> groupLimits = {
        {"job1", 2}, {"job2", 6}, {"job3", 3}, {"job4", 21}};
    auto [solution, rack_spread] = solveGroupCapacityProblem(
        initial_assignment,
        GroupCapacitySpecDefinition::AFTER,
        GroupCapacitySpecBound::MIN,
        groupLimits,
        scopeItemContributions,
        host_capacity,
        false,
        solverAlgoType,
        solverPackage);
  }
}

TEST(PackerProblemTest, ManifoldBackup) {
  ManifoldBackupParams params;

  ProblemProfile profile;
  EXPECT_FALSE(shouldBackup(params, profile));

  params.uploadPolicy() = ManifoldUploadPolicy::ALWAYS;
  EXPECT_TRUE(shouldBackup(params, profile));

  params.uploadPolicy() = ManifoldUploadPolicy::NEVER;
  EXPECT_FALSE(shouldBackup(params, profile));

  params.uploadPolicy() = ManifoldUploadPolicy::OUTLIER;
  EXPECT_FALSE(shouldBackup(params, profile));

  *profile.solveSec() = 3600;
  params.uploadPolicy() = ManifoldUploadPolicy::OUTLIER;
  params.expectedRuntime() = 1800;
  EXPECT_TRUE(shouldBackup(params, profile));
}

TEST(OptimalSolverTest, StaybleStayed) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  // this unit test is used to catch a stable stayed bug
  // for new of (i1)
  // ==> after(c1) + after(c2) + stayed(c1) + stayed(c2)
  // stayed(c1) should be min{1, after(c1)}
  // the bug is generating stayed(c1) = min{2, after(c1)}
  std::unique_ptr<ProblemSolver> problem =
      makeProblem({{"c1", {"a"}}, {"c2", {"c"}}, {"c3", {"b"}}});
  problem->addScope(
      "test",
      std::unordered_map<std::string, std::string>{
          {"c1", "i1"}, {"c2", "i1"}, {"c3", "i2"}});
  problem->enableStableAsMuchAsPossible();
  {
    CapacitySpec cspec;
    cspec.scope() = "test";
    cspec.dimension() = "object_count";
    cspec.definition() = CapacitySpecDefinition::NEW;

    Limit limit;
    limit.globalLimit() = 0;
    cspec.limit() = limit;

    problem->addConstraint(cspec);
  }

  {
    CapacitySpec ospec;
    ospec.scope() = "container";
    ospec.dimension() = "object_count";
    ospec.definition() = CapacitySpecDefinition::AFTER;

    Limit limit;
    limit.globalLimit() = 0;
    ospec.limit() = limit;

    Filter filter;
    filter.itemsWhitelist() = {"c1"};
    ospec.filter() = filter;
    problem->addGoal(ospec, -1);
  }

  const auto optimalSpec = facebook::algopt::makeAvailableOptimalSolverSpec();
  problem->addSolver(optimalSpec);

  auto solution = problem->solve();
  const map<std::string, std::string> expected_assignment = {
      {"a", "c1"}, {"b", "c3"}, {"c", "c1"}};
  EXPECT_EQ(expected_assignment, toOrderedMap(*solution.assignment()));
}

TEST(PackerProblemTest, MinimizeSumOfMax) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  // f1: s1, s2, s3, s4, s5
  // f2: s6, s7, s8, s9
  // r1: requirement = 4 & r2: requirement = 4
  // minimum sum of maxes = 4 (2+2)
  std::unique_ptr<ProblemSolver> problem = makeProblem(
      {{"ua", {"s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9"}},
       {"r0", {}},
       {"r1", {}}});
  std::map<std::string, std::string> fd_partition = {
      {"s1", "f1"},
      {"s2", "f1"},
      {"s3", "f1"},
      {"s4", "f1"},
      {"s5", "f2"},
      {"s6", "f2"},
      {"s7", "f2"},
      {"s8", "f2"},
      {"s9", "f2"}};
  problem->addPartition("failure_domain", fd_partition);
  CapacitySpec capacitySpec;
  capacitySpec.scope() = "container";
  capacitySpec.dimension() = "object_count";
  capacitySpec.bound() = CapacitySpecBound::MIN;

  Limit limit;
  limit.type() = LimitType::RELATIVE;
  limit.scopeItemLimits() = {{{"ua", 1.0}, {"r0", 4.0}, {"r1", 4.0}}};
  capacitySpec.limit() = limit;
  problem->addConstraint(capacitySpec);

  MinimizeMovementSpec minimizeMovementSpec;
  minimizeMovementSpec.scope() = "container";
  minimizeMovementSpec.dimension() = "object_count";
  problem->addGoal(minimizeMovementSpec);

  SumOfMaxSpec spec;
  spec.scope() = "container";
  spec.partitionName() = "failure_domain";
  spec.dimension() = "object_count";

  Filter filter;
  filter.itemsBlacklist() = {"ua"};
  spec.filter() = filter;

  problem->addGoal(std::move(spec));

  const auto optimalSpec = facebook::algopt::makeAvailableOptimalSolverSpec();
  problem->addSolver(optimalSpec);

  auto solution = problem->solve();
  const auto& assignment = *solution.assignment();
  std::unordered_map<std::string, std::unordered_map<std::string, int>> agg;
  for (const auto& [obj, cont] : assignment) {
    agg[cont][fd_partition.at(obj)] += 1;
  }
  EXPECT_EQ(
      4,
      std::max(agg["r0"]["f1"], agg["r0"]["f2"]) +
          std::max(agg["r1"]["f1"], agg["r1"]["f2"]));
}

TEST(PackerProblemTest, BasicSRBufferCapacity) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  // problem set up
  // 5 MSBs, 15 servers, 3 per msb
  // add goal to minimize sum of max
  // add sr buffer capacity constraint
  auto runTestCase = [](double requirement,
                        bool addUpperBound = true,
                        bool useHeuristics = false,
                        std::optional<AssignmentSolution> prevSolution =
                            std::nullopt) {
    std::map<std::string, std::vector<std::string>> initialAllocation = {
        {"ua",
         {"s1",
          "s2",
          "s3",
          "s4",
          "s5",
          "s6",
          "s7",
          "s8",
          "s9",
          "s10",
          "s11",
          "s12",
          "s13",
          "s14",
          "s15"}},
        {"r0", {}},
        {"b0", {}}};
    if (prevSolution) {
      initialAllocation.clear();
      initialAllocation["ua"] = {};
      initialAllocation["r0"] = {};
      initialAllocation["b0"] = {};
      for (const auto& [obj, cont] : *prevSolution->assignment()) {
        initialAllocation[cont].emplace_back(obj);
      }
    }

    std::unique_ptr<ProblemSolver> problem = makeProblem(initialAllocation);
    std::map<std::string, std::string> fd_partition = {
        {"s1", "f1"},
        {"s2", "f1"},
        {"s3", "f1"},
        {"s4", "f2"},
        {"s5", "f2"},
        {"s6", "f2"},
        {"s7", "f3"},
        {"s8", "f3"},
        {"s9", "f3"},
        {"s10", "f4"},
        {"s11", "f4"},
        {"s12", "f4"},
        {"s13", "f5"},
        {"s14", "f5"},
        {"s15", "f5"}};
    problem->addPartition("failure_domain", fd_partition);

    MinimizeMovementSpec minimizeMovementSpec;
    minimizeMovementSpec.scope() = "container";
    minimizeMovementSpec.dimension() = "object_count";
    problem->addGoal(minimizeMovementSpec);

    CapacitySpec capacitySpec;
    capacitySpec.scope() = "container";
    capacitySpec.dimension() = "object_count";
    capacitySpec.bound() = CapacitySpecBound::MIN;

    Limit limit;
    limit.type() = LimitType::RELATIVE;
    limit.scopeItemLimits() = {{"r0", requirement}};
    capacitySpec.limit() = limit;

    Filter filter;
    filter.itemsWhitelist() = {"r0"};
    capacitySpec.filter() = filter;
    problem->addConstraint(capacitySpec);

    SRBufferCapacitySpec spec;
    spec.scope() = "container";
    spec.partitionName() = "failure_domain";
    spec.matchingError() = 0.01;
    spec.addUpperBound() = addUpperBound;
    spec.useHeuristics() = useHeuristics;

    ScopeItemPair scopeItemPair;
    scopeItemPair.scopeItem1() = "r0";
    scopeItemPair.scopeItem2() = "b0";
    spec.scopeItemPairs() = {scopeItemPair};

    Filter filter2;
    filter2.itemsBlacklist() = {"ua"};
    spec.filter() = filter2;
    problem->addConstraint(std::move(spec));

    const auto optimalSpec = facebook::algopt::makeAvailableOptimalSolverSpec();
    problem->addSolver(optimalSpec);

    auto solution = problem->solve();
    const auto& assignment = *solution.assignment();
    std::unordered_map<std::string, int> containerToObjectCount;
    std::unordered_map<std::string, std::unordered_map<std::string, int>>
        containerToGroupToObjectCount;
    for (const auto& [obj, cont] : assignment) {
      containerToObjectCount[cont]++;

      const auto& group = fd_partition.at(obj);
      containerToGroupToObjectCount[cont][group]++;
    }

    return std::make_tuple(
        solution, containerToObjectCount, containerToGroupToObjectCount);
  };

  // test cases
  const std::unordered_map<int, std::pair<int, int>> testCases = {
      // input: 15 servers, 5 msbs, requirement = 1 servers => buffer >= 1
      {1, {1, 1}},
      // input: 15 servers, 5 msbs, requirement = 2 servers => buffer >= 1
      {2, {2, 1}},
      // input: 15 servers, 5 msbs, requirement = 3 servers => buffer >= 1
      {3, {3, 1}},
      // input: 15 servers, 5 msbs, requirement = 4 servers => buffer >= 1
      {4, {4, 1}},
      // input: 15 servers, 5 msbs, requirement = 5 servers => buffer >= 1
      {5, {5, 1}},
      // input: 15 servers, 5 msbs, requirement = 6 servers => buffer >= 2
      {6, {6, 2}},
      // input: 15 servers, 5 msbs, requirement = 7 servers => buffer >= 2
      {7, {7, 2}},
      // input: 15 servers, 5 msbs, requirement = 8 servers => buffer >= 2
      {8, {8, 2}},
      // input: 15 servers, 5 msbs, requirement = 9 servers => buffer >= 2
      {9, {9, 2}},
      // input: 15 servers, 5 msbs, requirement = 10 servers => buffer >= 2
      {10, {10, 2}},
      // input: 15 servers, 5 msbs, requirement = 11 servers => buffer >= 3
      {11, {11, 3}},
      // input: 15 servers, 5 msbs, requirement = 12 servers => buffer >= 3
      {12, {12, 3}},
  };

  for (const auto& [requirement, expectations] : testCases) {
    auto testStr = fmt::format(
        "{}: {} => {}", requirement, expectations.first, expectations.second);
    auto [_, agg, _1] = runTestCase(requirement, false);
    EXPECT_EQ(agg.at("r0"), expectations.first)
        << testStr << " without upper bound pure srf";
    EXPECT_GE(agg.at("b0"), expectations.second)
        << testStr << " without upper bound pure srf";
  }

  // input: 15 servers, 5 msbs, requirement = 13 servers => buffer = 3 but
  // only 2 servers would be left, so instead it just gives 12 and 3
  // this is because sr buffer constraint is originally satisfied and must
  // also be satisfied at the end
  auto testStr = "13: 12 => 3";
  auto [_, agg, _1] = runTestCase(13, false);
  EXPECT_EQ(agg.at("r0"), 12) << testStr << " without upper bound pure srf";
  EXPECT_GE(agg.at("b0"), 3) << testStr << " without upper bound pure srf";
}

TEST(PackerProblemTest, DefaultConstraintParams) {
  const map<string, vector<string>> initial = {
      {"h1", {"t1", "t4"}}, {"h2", {}}, {"h3", {"t2", "t3"}}, {"h4", {}}};
  const map<string, vector<string>> final = {
      {"h1", {"t1"}}, {"h2", {"t4"}}, {"h3", {"t2"}}, {"h4", {"t3"}}};

  auto setUpProblem = [](ProblemSolver& problem) {
    problem.addPartition(
        "group",
        std::unordered_map<std::string, std::string>{
            {"t1", "g1"}, {"t2", "g2"}, {"t3", "g2"}, {"t4", "g1"}});

    GroupCountSpec groupCountSpec;
    groupCountSpec.scope() = "container";
    groupCountSpec.partitionName() = "group";
    problem.addConstraint(groupCountSpec);

    AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = {"t1", "t2", "t3", "t4"};
    problem.addConstraint(avoidMovingSpec);

    LocalSearchSolverSpec localSearchSolverSpec;
    localSearchSolverSpec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
    problem.addSolver(localSearchSolverSpec);
  };

  {
    auto problem = makeProblem(initial);
    setUpProblem(*problem);
    auto solution = solve(problem, LOCALSEARCH);
    EXPECT_EQ(20200, *solution.finalObjective()->value());
  }

  {
    auto problem = makeProblem(initial);
    ConstraintParams params;
    params.invalidState() = 0;
    problem->setDefaultConstraintParams(params);
    setUpProblem(*problem);
    auto solution = solve(problem, LOCALSEARCH);
    EXPECT_EQ(200, *solution.finalObjective()->value());
  }

  {
    auto problem = makeProblem(initial);
    ConstraintParams params;
    params.invalidState() = 0;
    params.invalidCost() = 10;
    problem->setDefaultConstraintParams(params);
    setUpProblem(*problem);
    auto solution = solve(problem, LOCALSEARCH);
    EXPECT_EQ(20, *solution.finalObjective()->value());
  }
}
