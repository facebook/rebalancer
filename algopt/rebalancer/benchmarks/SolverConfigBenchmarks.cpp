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

#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/interface/ProblemSolverFactory.h"

#include <folly/Benchmark.h>
#include <folly/container/irange.h>
#include <folly/init/Init.h>
#include <gtest/gtest.h>

namespace interface = facebook::rebalancer::interface;

BENCHMARK(DisableDynamicHotContainerOrdering) {
  // This benchmark is to track if recomputeContainerOrderingAfterEveryMove()
  // option works as expected. If it is not set to 'false', this solve will take
  // 10s of minutes to get to ~10K moves. This is because, updating the dynamic
  // hottest container ordering after every move takes around 0.2 seconds, and
  // this problem has at least ~10K moves.
  auto solver =
      interface::ProblemSolverFactory::makeProblemSolver("rebalancer", "tests");
  solver->setObjectName("task");
  solver->setContainerName("host");

  const int objectCount = 10e3 + 1;
  const int containerCount =
      100e3; // large number of containers that so that the BalanceSpec util
             // expression will have many children and hence each hottest
             // container ordering will take significant time

  // initially, all objects are in host0
  std::map<std::string, std::vector<std::string>> containerToObjects;
  std::vector<std::string> allObjects;

  allObjects.reserve(objectCount);
  for (const auto i : folly::irange(objectCount)) {
    allObjects.emplace_back(fmt::format("task{}", i));
  }
  containerToObjects.emplace("host0", std::move(allObjects));

  std::vector<std::string> allContainers;
  allContainers.emplace_back("host0");

  for (const auto i : folly::irange(1, containerCount)) {
    auto hostName = fmt::format("host{}", i);
    containerToObjects.emplace(hostName, std::vector<std::string>{});
    allContainers.push_back(hostName);
  }

  solver->setAssignment(containerToObjects);

  // Stage 0: balance
  interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "task_count";
  balanceSpec.formula() = interface::BalanceSpecFormula::IDEAL;
  solver->addGoal(std::move(balanceSpec));

  interface::LocalSearchStageSolverSpec localSearchStageSolverSpec;
  // set recomputeContainerOrderingAfterEveryMove() to false, otherwise this
  // solve will take several minutes; stop after (objectCount - 1) moves
  localSearchStageSolverSpec.recomputeContainerOrderingAfterEveryMove() = false;
  localSearchStageSolverSpec.stopAfterMoves() = (objectCount - 1);

  interface::LocalSearchStageSpec localSearchStageSpec1;
  localSearchStageSpec1.begin() = 0;
  localSearchStageSpec1.end() = 1;
  localSearchStageSpec1.solverSpec()
      ->exploreMovesFromContainersNotInObjective() = false;
  localSearchStageSolverSpec.stageSpecs() = {std::move(localSearchStageSpec1)};

  localSearchStageSolverSpec.stageSpecs()
      ->at(0)
      .solverSpec()
      ->moveTypeList()
      ->push_back(
          interface::ProblemSolver::makeMoveTypeSpec(
              "SINGLE_RANDOM_STRATIFIED"));
  localSearchStageSolverSpec.stageSpecs()
      ->at(0)
      .solverSpec()
      ->stratifiedSampleSize() = 1;

  solver->addSimilarContainers({std::move(allContainers)});

  // Configure local search stages solver
  solver->addSolver(localSearchStageSolverSpec);

  // Solve
  auto solution = solver->solve();
}

BENCHMARK(ResetSkipContainersCorrectly) {
  // This benchmark is to make sure that skipContainers is reset correctly at
  // the start of every cycle. If done correctly, this benchamrk will only
  // explore moves with three containers
  auto solver =
      interface::ProblemSolverFactory::makeProblemSolver("rebalancer", "tests");
  solver->setObjectName("task");
  solver->setContainerName("host");

  constexpr int nSpecialHosts = 3;
  const int containerCount = 50e3 + nSpecialHosts + 1;
  const int objectCount =
      150e3 + nSpecialHosts; // two of the special hosts will have one
                             // object each, one special host will have
                             // zero; one other host will one object, others
                             // will have 3 objects each

  std::map<std::string, std::vector<std::string>> containerToObjects;
  containerToObjects.emplace("specialHost1", std::vector<std::string>{"task1"});
  containerToObjects.emplace("specialHost2", std::vector<std::string>{"task2"});
  containerToObjects.emplace("specialHost3", std::vector<std::string>{});
  containerToObjects.emplace(
      "unfixedHost", std::vector<std::string>{"dummyTask"});

  std::vector<std::string> nonSpecialHosts = {"unfixedHost"};
  std::vector<std::string> tasksOutsideSpecialHosts;
  std::map<std::string, double> taskToId;
  for (int i = nSpecialHosts + 1; i <= objectCount; ++i) {
    auto taskName = fmt::format("task{}", i);
    auto hostName =
        fmt::format("host{}", i % (containerCount - (nSpecialHosts + 1)));

    taskToId.emplace(taskName, i);
    nonSpecialHosts.push_back(hostName);
    tasksOutsideSpecialHosts.push_back(taskName);

    containerToObjects[hostName].push_back(taskName);
  }

  solver->setAssignment(containerToObjects);

  // add a dimension to make all the objects distinct
  solver->addObjectDimension("taskToId", taskToId);

  {
    // add a capacitySpec on all nonSpecialHosts to make them hotter
    // than all the special hosts
    interface::CapacitySpec capacitySpec;
    capacitySpec.scope() = "host";
    capacitySpec.dimension() = "taskToId";
    capacitySpec.limit()->globalLimit() = 1;
    solver->addConstraint(capacitySpec);
  }

  {
    // we don't want the object in "unfixedHost" to move
    interface::CapacitySpec capacitySpec;
    capacitySpec.scope() = "host";
    capacitySpec.dimension() = "task_count";
    capacitySpec.limit()->globalLimit() = 1;
    capacitySpec.filter()->itemsWhitelist() = {"unfixedHost"};
    capacitySpec.bound() = interface::CapacitySpecBound::MIN;
    solver->addConstraint(capacitySpec);
  }

  {
    interface::CapacitySpec capacitySpec;
    capacitySpec.scope() = "host";
    capacitySpec.dimension() = "task_count";
    capacitySpec.limit()->globalLimit() = 1;
    capacitySpec.filter()->itemsWhitelist() = {
        "specialHost1", "specialHost2", "specialHost3"};
    solver->addConstraint(capacitySpec);
  }

  {
    // this avoid moving spec along with nonAcceptingSpec below makes all the
    // non-special hosts except "unfixedHost" as fixed.
    interface::AvoidMovingSpec avoidMovingSpec;
    avoidMovingSpec.objects() = tasksOutsideSpecialHosts;
    solver->addConstraint(avoidMovingSpec);

    interface::NonAcceptingSpec nonAcceptingSpec;
    nonAcceptingSpec.scope() = "host";
    nonAcceptingSpec.items() = nonSpecialHosts;
    solver->addConstraint(nonAcceptingSpec);
  }

  {
    // create an affinitySpec so that we force a swap between specialHost1 and
    // specialHost2; at the same time provide some incentive for at least one of
    // the objects to first move to specialHost3
    interface::AssignmentAffinity aff1;
    aff1.objectName() = "task1";
    aff1.scopeItemName() = "specialHost2";
    aff1.affinity() = 2;

    interface::AssignmentAffinity aff2;
    aff2.objectName() = "task2";
    aff2.scopeItemName() = "specialHost1";
    aff2.affinity() = 2;

    interface::AssignmentAffinity aff3;
    aff3.objectName() = "task2";
    aff3.scopeItemName() = "specialHost3";
    aff3.affinity() = 1;

    interface::AssignmentAffinitiesSpec affinitiesSpec;
    affinitiesSpec.scope() = "host";
    affinitiesSpec.affinities() = {
        std::move(aff1), std::move(aff2), std::move(aff3)};
    solver->addGoal(affinitiesSpec, 1, 1);
  }

  {
    interface::LocalSearchSolverSpec localSearchSolverSpec;
    localSearchSolverSpec.moveTypeList()->push_back(
        interface::ProblemSolver::makeMoveTypeSpec(
            interface::SingleMoveTypeSpec()));
    solver->addSolver(localSearchSolverSpec);
  }

  // Solve
  auto solution = solver->solve();
}

BENCHMARK(EnableDynamicObjectOrderingInLocalSearch) {
  // This benchmark is to track if AccessOrderedFastSet is used and if it is
  // working as expected. If we are using usual sets, then this solve will take
  // several hours to get to ~10K moves (as opposed to ~25s).
  folly::BenchmarkSuspender suspend;
  auto solver =
      interface::ProblemSolverFactory::makeProblemSolver("rebalancer", "tests");
  solver->setObjectName("task");
  solver->setContainerName("host");

  const int objectCount = 1e6;
  constexpr int containerCount = 2;

  // initially, all objects are in host0
  std::map<std::string, std::vector<std::string>> containerToObjects;
  std::vector<std::string> allObjects;
  allObjects.reserve(objectCount);
  for (const auto i : folly::irange(objectCount)) {
    allObjects.push_back(fmt::format("task{}", i));
  }
  containerToObjects.emplace("host0", std::move(allObjects));

  std::vector<std::string> allContainers;
  allContainers.emplace_back("host0");

  for (const auto i : folly::irange(1, containerCount)) {
    auto hostName = fmt::format("host{}", i);
    containerToObjects.emplace(hostName, std::vector<std::string>{});
    allContainers.push_back(hostName);
  }

  solver->setAssignment(containerToObjects);

  // set object dimension; every 100th object is special and has a value of i;
  // all other objects have a value of i + objectCount^2
  folly::F14FastMap<std::string, double> objectToLoadValue;
  folly::F14FastMap<std::string, double> objectToNonSpecialValue;
  for (const auto i : folly::irange(objectCount)) {
    auto objectName = fmt::format("task{}", i);
    if (i % 100 == 0) {
      objectToNonSpecialValue[objectName] = 0;
    }
    objectToLoadValue[objectName] = 1 + i / 100.0;
  }
  solver->addObjectDimension("load", objectToLoadValue);
  solver->addObjectDimension(
      "non_special_object", objectToNonSpecialValue, 1.0);

  // Stage 0: host1 has capacity of 0 for non-special objects
  interface::CapacitySpec capacitySpecConstraint;
  capacitySpecConstraint.scope() = "host";
  capacitySpecConstraint.dimension() = "non_special_object";
  capacitySpecConstraint.filter()->itemsWhitelist() = {"host1"};
  capacitySpecConstraint.limit()->globalLimit() = 0;
  solver->addConstraint(std::move(capacitySpecConstraint));

  // host0 needs to be emptied as much as possible; deliberately adding it using
  // load dimension to make all the objects unique (i.e., they will be in
  // different equivalence sets)
  interface::CapacitySpec capacitySpecGoal;
  capacitySpecGoal.scope() = "host";
  capacitySpecGoal.dimension() = "load";
  capacitySpecGoal.filter()->itemsWhitelist() = {"host0"};
  capacitySpecGoal.limit()->globalLimit() = 0;
  solver->addGoal(std::move(capacitySpecGoal));

  interface::LocalSearchStageSolverSpec localSearchStageSolverSpec;
  interface::LocalSearchStageSpec localSearchStageSpec1;
  localSearchStageSpec1.begin() = 0;
  localSearchStageSpec1.end() = 1;
  localSearchStageSolverSpec.stageSpecs() = {localSearchStageSpec1};

  localSearchStageSolverSpec.stageSpecs()
      ->at(0)
      .solverSpec()
      ->moveTypeList()
      ->push_back(
          interface::ProblemSolver::makeMoveTypeSpec(
              interface::SingleFastMoveTypeSpec()));

  // Configure local search stages solver
  solver->addSolver(localSearchStageSolverSpec);

  // enables dynamic object ordering in local search which uses
  // AccessOrderedFastSet
  solver->shouldUseDynamicObjectOrdering(true);

  suspend.dismiss();

  // Solve
  auto solution = solver->solve();

  // expect all special objects to be in host1
  auto& finalAssignment = *solution.assignment();
  for (const auto i : folly::irange(objectCount)) {
    auto objectName = fmt::format("task{}", i);
    if (i % 100 == 0) {
      EXPECT_EQ(finalAssignment.at(objectName), "host1");
    } else {
      EXPECT_EQ(finalAssignment.at(objectName), "host0");
    }
  }
}

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  folly::runBenchmarks();

  return 0;
}
