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

#include "algopt/rebalancer/interface/tests/utils.h"
#include <algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h>

#include <gtest/gtest.h>

using namespace facebook::rebalancer::interface;
using namespace facebook::rebalancer::entities;

class SingleColdestStratifiedTest : public ::testing::TestWithParam<int> {};

INSTANTIATE_TEST_CASE_P(
    NumThreads,
    SingleColdestStratifiedTest,
    testThreadCounts());

TEST_P(SingleColdestStratifiedTest, SampleOfOne) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");
  solver->setAssignment(
      std::map<std::string, std::vector<std::string>>{
          {"unassigned", {"task0", "task1", "task2", "task3"}},
          {"host0", {}},
          {"host1", {}},
          {"host2", {}},
          {"host3", {}}});

  solver->addObjectDimension(
      "cpu",
      std::map<std::string, double>{
          {"task0", 10}, {"task1", 10}, {"task2", 10}, {"task3", 10}});
  solver->addContainerDimension(
      "cpu",
      std::map<std::string, double>{
          {"host0", 100}, {"host1", 100}, {"host2", 100}, {"host3", 100}});

  {
    ToFreeSpec spec;
    spec.containers() = {"unassigned"};
    solver->addConstraint(spec);
  }

  solver->addGoalBoundary();

  {
    CapacitySpec spec;
    spec.scope() = "host";
    spec.dimension() = "cpu";
    spec.filter()->itemsBlacklist() = {"unassigned"};
    solver->addGoal(spec);
  }

  solver->addGoalBoundary();

  {
    BalanceSpec spec;
    spec.scope() = "host";
    spec.dimension() = "cpu";
    // NOTE: SingleColdestStratifiedMoveType works best if the final goal
    // expr has the property that removing a container (along with objects it
    // holds) does not make the goal value(s) worse. An example goal expr where
    // such a property does not hold is the resulting formula when using
    // BalanceSpecFormula::Linear. So to illustrate the moveType we use
    // BalanceSpecFormula::IDEAL below.
    spec.formula() = BalanceSpecFormula::IDEAL;
    spec.filter()->itemsBlacklist() = {"unassigned"};
    solver->addGoal(spec);
  }

  {
    LocalSearchSolverSpec spec;
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec("SINGLE_COLDEST_STRATIFIED"));
    spec.stratifiedSampleSize() = 1;
    solver->addSolver(spec);
  }

  solver->addSimilarContainers({{"host0", "host1", "host2", "host3"}});

  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  std::map<std::string, int> hostToTaskCount;
  for (auto& [task, host] : assignment) {
    ++hostToTaskCount[host];
  }

  EXPECT_EQ(0, hostToTaskCount["unassigned"]);
  EXPECT_EQ(1, hostToTaskCount["host0"]);
  EXPECT_EQ(1, hostToTaskCount["host1"]);
  EXPECT_EQ(1, hostToTaskCount["host2"]);
  EXPECT_EQ(1, hostToTaskCount["host3"]);
}

TEST_P(SingleColdestStratifiedTest, EmptySimilarContainers) {
  // If similarContainers is added, but it is completely empty then the desired
  // behaviour is to not throw an error, but just not make any moves with it
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");
  solver->setAssignment(
      std::map<std::string, std::vector<std::string>>{
          {"unassigned", {"task0", "task1", "task2", "task3"}}, {"host0", {}}});

  {
    ToFreeSpec spec;
    spec.containers() = {"unassigned"};
    solver->addConstraint(spec);
  }

  {
    LocalSearchSolverSpec spec;
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec("SINGLE_COLDEST_STRATIFIED"));
    spec.stratifiedSampleSize() = 2;
    solver->addSolver(spec);
  }

  solver->addSimilarContainers(std::vector<std::vector<std::string>>({}));

  auto solution = solver->solve();

  EXPECT_EQ(solution.movesSummary()->size(), 0);
}

TEST_P(SingleColdestStratifiedTest, EmptyContainer) {
  // If a container is empty, we would expect the moveType to find that
  // container colder than the hot container

  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  Map<std::string, std::vector<std::string>> assignment;
  assignment["unassigned"] = {
      "task0", "task1", "task2", "task3", "task4", "task5"};
  assignment["host0"] = {"task6", "task7", "task8", "task9", "task10"};
  assignment["host1"] = {"task11", "task12", "task13", "task14"};
  assignment["host2"] = {"task15", "task16", "task17"};
  assignment["host3"] = {"task18", "task19"};
  assignment["host4"] = {"task20"};
  assignment["host5"] = {};

  solver->setAssignment(assignment);

  {
    ToFreeSpec spec;
    spec.containers() = {"unassigned"};
    solver->addConstraint(spec);
  }
  {
    LocalSearchSolverSpec spec;
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec("SINGLE_COLDEST_STRATIFIED"));
    spec.stratifiedSampleSize() = 1;
    spec.stopAfterMoves() = 6;
    solver->addSolver(spec);
  }

  solver->addSimilarContainers(
      {{"host0", "host1", "host2", "host3", "host4", "host5"}});
  auto solution = solver->solve();

  EXPECT_EQ(solution.movesSummary()->size(), 6);

  auto& actualAssignment = *solution.assignment();

  std::map<std::string, int> hostToTaskCount;
  for (auto& [task, host] : actualAssignment) {
    ++hostToTaskCount[host];
  }
  EXPECT_EQ(0, hostToTaskCount["unassigned"]);
  EXPECT_EQ(5, hostToTaskCount["host0"]);
  EXPECT_EQ(4, hostToTaskCount["host1"]);
  EXPECT_EQ(3, hostToTaskCount["host2"]);
  EXPECT_EQ(3, hostToTaskCount["host3"]);
  EXPECT_EQ(3, hostToTaskCount["host4"]);
  EXPECT_EQ(3, hostToTaskCount["host5"]);
}
