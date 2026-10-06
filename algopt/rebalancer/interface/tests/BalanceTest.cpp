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

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "algopt/rebalancer/interface/tests/utils.h"
#include "algopt/rebalancer/tests/SolverTestUtils.h"
#include <algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSolver_types.h>
#include <algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h>

#include <fmt/core.h>
#include <folly/container/irange.h>
#include <folly/logging/Init.h>
#include <gtest/gtest.h>

#include <map>
#include <string>
#include <tuple>
#include <vector>

using namespace folly;
using namespace facebook::rebalancer::interface;
using facebook::algopt::isSolverUnavailable;
using facebook::algopt::solverName;
using facebook::algopt::testSolverPackages;

class BalanceTest : public ::testing::TestWithParam<int> {
  void SetUp() override {
    folly::initLoggingOrDie("DBG1");
  }

 public:
  static std::unique_ptr<ProblemSolver> createTrickyBalanceScenario() {
    // The tricky balance scenario is one where:
    // - Containers have different capacities.
    // - The ideal average is unattainable due to other constraints.
    auto solver =
        initializeTestProblemSolver({.executorThreadCount = GetParam()});
    solver->setObjectName("task");
    solver->setContainerName("host");

    // There are 5 hosts and 84 tasks. Initially, tasks are all in host0.
    std::map<std::string, std::vector<std::string>> assignment = {
        {"host0", {}},
        {"host1", {}},
        {"host2", {}},
        {"host3", {}},
        {"host4", {}},
    };
    for (const auto i : folly::irange(84)) {
      const std::string host = fmt::format("host{}", 0);
      const std::string task = fmt::format("task{}", i);
      assignment[host].push_back(task);
    }
    solver->setAssignment(assignment);

    // Hosts have different cpu capacities, host4 being the largest. This gives
    // us the first property of the tricky balance scenario.
    solver->addContainerDimension(
        "cpu",
        std::unordered_map<std::string, double>{
            {"host0", 100},
            {"host1", 20},
            {"host2", 10},
            {"host3", 10},
            {"host4", 1000},
        });

    // Each task requires 1 cpu.
    std::map<std::string, double> taskCpu;
    for (const auto i : folly::irange(84)) {
      const std::string task = fmt::format("task{}", i);
      taskCpu[task] = 1;
    }
    solver->addObjectDimension("cpu", taskCpu);

    // The largest host doesn't take any tasks. This gives us the second
    // property of the tricky balance scenario: the utilization of host4 is
    // pegged at 0, so some of the remaining hosts are forced to have a
    // higher-than-average utilization.
    auto spec = ToFreeSpec();
    spec.containers() = {"host4"};
    solver->addConstraint(spec);

    return solver;
  }

  static std::unique_ptr<ProblemSolver> createExampleScenario() {
    // this scenario is based off the given example in this wiki:
    // https://www.internalfb.com/intern/wiki/ReBalancer/API/Tutorial:_A_tour_of_the_Explorer/
    auto solver =
        initializeTestProblemSolver({.executorThreadCount = GetParam()});

    solver->setObjectName("task");
    solver->setContainerName("host");

    // All tasks are assigned to the same host for the initial assignment.
    std::map<std::string, std::vector<std::string>> initialAssignment;
    for (const auto i : folly::irange(200)) {
      initialAssignment["host0"].push_back(fmt::format("task{}", i));
    }
    initialAssignment["host1"] = {};
    initialAssignment["host2"] = {};
    initialAssignment["host3"] = {};
    solver->setAssignment(initialAssignment);

    auto freeSpec = ToFreeSpec();
    freeSpec.containers() = {"host0"};
    solver->addConstraint(freeSpec);

    std::map<std::string, double> taskMemory;
    for (const auto i : folly::irange(200)) {
      taskMemory[fmt::format("task{}", i)] = 1;
    }
    solver->addObjectDimension("memory", taskMemory);

    std::map<std::string, double> hostMemory;
    hostMemory["host0"] = 1000;
    hostMemory["host1"] = 2000;
    hostMemory["host2"] = 1000;
    hostMemory["host3"] = 1000;
    solver->addContainerDimension("memory", hostMemory);

    return solver;
  }

  static std::unique_ptr<ProblemSolver> createNegativeScenario() {
    auto solver =
        initializeTestProblemSolver({.executorThreadCount = GetParam()});

    solver->setObjectName("task");
    solver->setContainerName("host");

    // All tasks are assigned to the same host for the initial assignment.
    std::map<std::string, std::vector<std::string>> initialAssignment;
    for (const auto i : folly::irange(200)) {
      initialAssignment["host0"].push_back(fmt::format("task{}", i));
    }
    initialAssignment["host1"] = {};
    initialAssignment["host2"] = {};
    initialAssignment["host3"] = {};
    solver->setAssignment(initialAssignment);

    auto freeSpec = ToFreeSpec();
    freeSpec.containers() = {"host0"};
    solver->addConstraint(freeSpec);

    std::map<std::string, double> taskMemory;
    for (const auto i : folly::irange(200)) {
      taskMemory[fmt::format("task{}", i)] = -1;
    }
    solver->addObjectDimension("memory", taskMemory);

    std::map<std::string, double> hostMemory;
    hostMemory["host0"] = 1000;
    hostMemory["host1"] = 2000;
    hostMemory["host2"] = 1000;
    hostMemory["host3"] = 1000;
    solver->addContainerDimension("memory", hostMemory);

    return solver;
  }
  static std::unique_ptr<ProblemSolver> createNoTasksScenario() {
    auto solver =
        initializeTestProblemSolver({.executorThreadCount = GetParam()});
    solver->setObjectName("task");
    solver->setContainerName("host");

    // All tasks are assigned to the same host for the initial assignment.
    std::map<std::string, std::vector<std::string>> initialAssignment;
    initialAssignment["host1"] = {};
    initialAssignment["host1"] = {};
    initialAssignment["host2"] = {};
    initialAssignment["host3"] = {};
    solver->setAssignment(initialAssignment);
    return solver;
  }

  static std::unique_ptr<ProblemSolver> createNoObjectsInScopeItemsScenario() {
    auto solver =
        initializeTestProblemSolver({.executorThreadCount = GetParam()});
    solver->setObjectName("task");
    solver->setContainerName("host");

    // All tasks are assigned to the same host for the initial assignment.
    std::map<std::string, std::vector<std::string>> initialAssignment;
    for (const auto i : folly::irange(200)) {
      initialAssignment["host0"].push_back(fmt::format("task{}", i));
    }
    initialAssignment["host1"] = {};
    initialAssignment["host2"] = {};
    initialAssignment["host3"] = {};
    solver->setAssignment(initialAssignment);

    std::map<std::string, std::string> hostToRack;
    hostToRack["host2"] = "rack0";
    hostToRack["host3"] = "rack1";

    solver->addScope("rack", hostToRack);

    std::map<std::string, double> taskCpu;
    for (const auto i : folly::irange(84)) {
      const std::string task = fmt::format("task{}", i);
      taskCpu[task] = 1;
    }
    solver->addObjectDimension("cpu", taskCpu);

    return solver;
  }

  static std::unique_ptr<ProblemSolver> createScenarioWithManySolutions() {
    // 20 hosts, 5 tasks in host 0, 5 tasks in host 1
    // There can be multiple solutions, but because now we break ties with a
    // hash function, we should consistently have 1 solution. Previously, there
    // could be multiple solutions.
    auto solver =
        initializeTestProblemSolver({.executorThreadCount = GetParam()});
    solver->setObjectName("task");
    solver->setContainerName("host");

    std::unordered_map<std::string, std::vector<std::string>> assignment;
    for (const auto i : folly::irange(20)) {
      auto host = fmt::format("host{}", i);
      assignment[host] = {};
    }
    for (const auto i : folly::irange(5)) {
      auto host = fmt::format("host{}", 0);
      auto host1 = fmt::format("host{}", 1);
      assignment[host].push_back(fmt::format("task{}_{}", 0, i));
      assignment[host1].push_back(fmt::format("task{}_{}", 1, i));
    }
    solver->setAssignment(assignment);

    std::map<std::string, double> taskCpu;
    for (const auto i : folly::irange(5)) {
      const std::string task = fmt::format("task{}_{}", 0, i);
      const std::string task1 = fmt::format("task{}_{}", 1, i);
      taskCpu[task] = 1;
      taskCpu[task1] = 1;
    }
    solver->addObjectDimension("cpu", taskCpu);

    return solver;
  }
};

INSTANTIATE_TEST_CASE_P(NumThreads, BalanceTest, testThreadCounts());

class BalanceOptimalTest
    : public ::testing::TestWithParam<std::tuple<int, OptimalSolverPackage>> {
 protected:
  void SetUp() override {
    auto [threads, solver] = GetParam();
    if (isSolverUnavailable(solver)) {
      GTEST_SKIP() << solverName(solver) << " solver not available";
    }
  }
};

INSTANTIATE_TEST_CASE_P(
    AllSolvers,
    BalanceOptimalTest,
    ::testing::Combine(testThreadCounts(), testSolverPackages()));

TEST_P(BalanceTest, TrickyScenarioWithIdeal) {
  auto solver = createTrickyBalanceScenario();
  // The ideal balance formula has the following property:
  // - The largest utilization is minimal.
  // - While the above holds true, the second largest utilization is minimal.
  // - While the above holds true, the third largest utilization is minimal.
  // - And so on.
  BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = BalanceSpecFormula::IDEAL;

  solver->addGoal(balanceSpec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  std::map<std::string, int> taskCount;
  for (auto& [_, host] : *solution.assignment()) {
    ++taskCount[host];
  }

  // Each host has exactly a 60% utilization.
  EXPECT_EQ(60, taskCount["host0"]);
  EXPECT_EQ(12, taskCount["host1"]);
  EXPECT_EQ(6, taskCount["host2"]);
  EXPECT_EQ(6, taskCount["host3"]);

  // Except host4, which is not allowed to get any tasks.
  EXPECT_EQ(0, taskCount["host4"]);
}

TEST_P(BalanceTest, TrickyScenarioWithSquares) {
  auto solver = createTrickyBalanceScenario();
  // The squares definition a bias when capacities are different and the average
  // is not attainable: hosts with higher capacities have an incentive to get a
  // higher relative utilization than others.
  BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = BalanceSpecFormula::SQUARES;

  solver->addGoal(balanceSpec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  std::map<std::string, int> taskCount;
  for (auto& [_, host] : *solution.assignment()) {
    ++taskCount[host];
  }

  // Higher capacity hosts are expected to have a higher relative utilization.
  EXPECT_EQ(79, taskCount["host0"]); // 79%
  EXPECT_EQ(3, taskCount["host1"]); // 15%
  EXPECT_EQ(1, taskCount["host2"]); // 10%
  EXPECT_EQ(1, taskCount["host3"]); // 10%

  // Except host4, which is not allowed to get any tasks.
  EXPECT_EQ(0, taskCount["host4"]);
}

TEST_P(BalanceTest, IdealFormulaWithRelativeUtilBound) {
  // This example shows the usage of RELATIVE_UTIL bound type.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // Initially, all 100 tasks are placed in host0, and host1 is empty.
  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(100)) {
    initialAssignment["host0"].push_back(fmt::format("task{}", i));
  }
  initialAssignment["host1"] = {};
  solver->setAssignment(initialAssignment);

  std::map<std::string, double> taskCpu;
  for (const auto i : folly::irange(100)) {
    taskCpu[fmt::format("task{}", i)] = 1;
  }
  solver->addObjectDimension("cpu", taskCpu);

  const std::map<std::string, double> hostCpu = {
      {"host0", 100}, {"host1", 100}};
  solver->addContainerDimension("cpu", hostCpu);

  // Define an ideal balance goal with an upper bound of type relative util set
  // to 0.7. This means there is no incentive to balance the relative
  // utilization below 70%.
  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.formula() = BalanceSpecFormula::IDEAL;
  auto& upperBounds = spec.upperBounds().ensure();
  upperBounds.type() = LimitType::ABSOLUTE;
  upperBounds.globalLimit() = 0.7;
  spec.boundType() = BalanceSpecBoundType::RELATIVE_UTIL;
  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  std::map<std::string, int> taskCount;
  for (auto& [_, host] : *solution.assignment()) {
    ++taskCount[host];
  }

  EXPECT_EQ(70, taskCount["host0"]); // 70%
  EXPECT_EQ(30, taskCount["host1"]); // 30%
}

TEST_P(BalanceTest, Legacy) {
  // Simple scenario with 2 hosts and 3 tasks where the best balance possible
  // can be achieved by moving a single task.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});

  solver->setObjectName("task");
  solver->setContainerName("host");

  solver->setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0"}},
          {"host1", {"task1", "task2"}},
      });

  solver->addObjectDimension(
      "cpu",
      std::unordered_map<std::string, double>{
          {"task0", 10},
          {"task1", 20},
          {"task2", 40},
      });

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.formula() = BalanceSpecFormula::LEGACY;
  spec.fixAverageToInitial() = true;
  auto& upperBounds = spec.upperBounds().ensure();
  upperBounds.type() = LimitType::ABSOLUTE;
  upperBounds.globalLimit() = 1.1;

  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  // The best balance possible is achieved by moving task1 from host1 to host0.
  EXPECT_EQ("host0", assignment["task0"]);
  EXPECT_EQ("host0", assignment["task1"]);
  EXPECT_EQ("host1", assignment["task2"]);

  EXPECT_NEAR(0.04285806, *solution.finalObjective()->value(), 1e-8);
}

TEST_P(BalanceTest, ZeroCapacityScopeItem) {
  // Create a balancing scneario where one of the scope items has a capacity of
  // zero. Since relative utilization of a scope item is undefined when capacity
  // is zero, it is expected for Rebalancer to automatically filter out such
  // scope items.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});

  solver->setObjectName("task");
  solver->setContainerName("host");
  solver->setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task1", "task2", "task3"}},
          {"host1", {}},
          {"host2", {}}});

  solver->addObjectDimension("cpu", std::map<std::string, double>(), 10);
  solver->addContainerDimension(
      "cpu",
      std::map<std::string, double>{
          {"host0", 100}, {"host1", 100}, {"host2", 0}});

  {
    CapacitySpec spec;
    spec.scope() = "host";
    spec.dimension() = "cpu";
    solver->addConstraint(spec);
  }

  {
    BalanceSpec spec;
    spec.scope() = "host";
    spec.dimension() = "cpu";
    spec.formula() = BalanceSpecFormula::IDEAL;
    solver->addGoal(spec);
  }

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  std::map<std::string, int> hostToTaskCount;
  for (auto& [_, hostName] : assignment) {
    ++hostToTaskCount[hostName];
  }

  EXPECT_EQ(2, hostToTaskCount["host0"]);
  EXPECT_EQ(2, hostToTaskCount["host1"]);
  EXPECT_EQ(0, hostToTaskCount["host2"]);

  EXPECT_NEAR(0.16, *solution.initialObjective()->value(), 1e-8);
  EXPECT_NEAR(0.08, *solution.finalObjective()->value(), 1e-8);
}

TEST_P(BalanceTest, AllZeroCapacityScopeItem) {
  // Create a balancing scneario where all scope items have a capacity of zero.
  // Since relative utilization of a scope item is undefined when capacity is
  // zero, it is expected for Rebalancer to automatically filter out such scope
  // items. In this case, the formula is expected to be empty and it evaluates
  // to zero.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});

  solver->setObjectName("task");
  solver->setContainerName("host");
  solver->setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task1"}}, {"host1", {"task2"}}, {"host2", {}}});

  solver->addObjectDimension("cpu", std::map<std::string, double>(), 10);
  solver->addContainerDimension(
      "cpu",
      std::map<std::string, double>{{"host0", 0}, {"host1", 0}, {"host2", 0}});

  {
    BalanceSpec spec;
    spec.scope() = "host";
    spec.dimension() = "cpu";
    spec.formula() = BalanceSpecFormula::IDEAL;
    solver->addGoal(spec);
  }

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  // No moves expected.
  EXPECT_EQ("host0", assignment.at("task0"));
  EXPECT_EQ("host0", assignment.at("task1"));
  EXPECT_EQ("host1", assignment.at("task2"));

  EXPECT_NEAR(0, *solution.initialObjective()->value(), 1e-8);
  EXPECT_NEAR(0, *solution.finalObjective()->value(), 1e-8);
}

TEST_P(BalanceOptimalTest, Old) {
  auto [threads, solver] = GetParam();
  // We want to assign tasks to owners. Tasks belong to different buckets, and
  // we want the distribution of assigned tasks to be well spread among buckets.
  auto problemSolver =
      initializeTestProblemSolver({.executorThreadCount = threads});

  problemSolver->setObjectName("task");
  problemSolver->setContainerName("container");

  problemSolver->setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"bucket0", {"task0", "task1"}},
          {"bucket1", {"task2", "task3"}},
          {"bucket2", {"task4", "task5"}},
          {"bucket3", {"task6", "task7"}},
          {"owner0", {}},
          {"owner1", {}},
      });

  // We define the owner scope as a subset of containers, which we can later use
  // in the capacity spec. An alternative would be to not define this scope, and
  // just use the container scope with a filter in the capacity spec.
  problemSolver->addScope(
      "owner",
      std::unordered_map<std::string, std::string>{
          {"owner0", "owner0"}, {"owner1", "owner1"}});

  // Similarly, we define the bucket scope as a subset of containers.
  problemSolver->addScope(
      "bucket",
      std::map<std::string, std::string>{
          {"bucket0", "bucket0"},
          {"bucket1", "bucket1"},
          {"bucket2", "bucket2"},
          {"bucket3", "bucket3"}});

  // Add weight dimension.
  problemSolver->addObjectDimension(
      "weight", std::map<std::string, double>(), 1);
  problemSolver->addScopeDimension(
      "weight",
      "owner",
      std::unordered_map<std::string, double>{{"owner0", 1}, {"owner1", 3}});

  // Enforce capacity of owners.
  {
    CapacitySpec spec;
    spec.scope() = "owner";
    spec.dimension() = "weight";
    problemSolver->addConstraint(spec);
  }

  // Force buckets to become empty (as long as the moves don't break initially
  // satisfied constraints, like capacity).
  {
    ToFreeSpec spec;
    spec.containers() = {"bucket0", "bucket1", "bucket2", "bucket3"};
    // Force invalidState to be zero, otherwise the solver will be biased
    // towards solutions where the maximum number of containers are freed, which
    // goes against the balance incentive.
    problemSolver->addConstraint(spec, std::nullopt, std::nullopt, 0);
  }

  // Add an incentive to spread well given out tasks among different buckets.
  {
    BalanceSpec spec;
    spec.scope() = "bucket";
    spec.dimension() = "weight";
    spec.definition() = BalanceSpecDefinition::OLD;
    spec.formula() = BalanceSpecFormula::MAX;
    problemSolver->addGoal(spec);
  }

  // Use the optimal solver.
  {
    OptimalSolverSpec spec;
    spec.solverPackage() = solver;
    problemSolver->addSolver(spec);
  }

  auto solution = problemSolver->solve();
  auto& assignment = *solution.assignment();

  // The solution should give 1 task to owner0 and 3 tasks to owner1. Each
  // bucket should be giving out exactly 1 task.
  std::map<std::string, int> containerToTaskCount;
  for (auto& [_, containerName] : assignment) {
    ++containerToTaskCount[containerName];
  }

  EXPECT_EQ(1, containerToTaskCount["bucket0"]);
  EXPECT_EQ(1, containerToTaskCount["bucket1"]);
  EXPECT_EQ(1, containerToTaskCount["bucket2"]);
  EXPECT_EQ(1, containerToTaskCount["bucket3"]);
  EXPECT_EQ(1, containerToTaskCount["owner0"]);
  EXPECT_EQ(3, containerToTaskCount["owner1"]);
}

TEST_P(BalanceTest, AverageRelativeUtilizationOfScopedItems) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});

  solver->setObjectName("task");
  solver->setContainerName("host");

  // All tasks are assigned to the same host for the initial assignment.
  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(12)) {
    initialAssignment["host0"].push_back(fmt::format("task{}", i));
  }
  initialAssignment["host1"] = {};
  initialAssignment["host2"] = {};
  initialAssignment["host3"] = {};
  initialAssignment["host4"] = {};
  solver->setAssignment(initialAssignment);

  auto freeSpec = ToFreeSpec();
  freeSpec.containers() = {"host0"};
  solver->addConstraint(freeSpec);

  std::map<std::string, double> taskMemory;
  for (const auto i : folly::irange(12)) {
    taskMemory[fmt::format("task{}", i)] = 1;
  }
  solver->addObjectDimension("memory", taskMemory);

  std::map<std::string, double> hostMemory;
  hostMemory["host0"] = 10;
  hostMemory["host1"] = 20;
  hostMemory["host2"] = 10;
  hostMemory["host3"] = 10;
  solver->addContainerDimension("memory", hostMemory);

  std::map<std::string, std::string> hostToRack;
  hostToRack["host0"] = "rack0";
  hostToRack["host1"] = "rack0";
  hostToRack["host2"] = "rack1";
  hostToRack["host3"] = "rack1";

  solver->addScope("rack", hostToRack);

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "memory";
  spec.formula() = BalanceSpecFormula::IDEAL;
  solver->addGoal(spec);

  std::map<std::string, std::vector<std::string>> jobToTask;

  for (const auto i : folly::irange(12)) {
    jobToTask[fmt::format("job{}", i / 2)].push_back(fmt::format("task{}", i));
  }
  solver->addPartition("job", jobToTask);

  GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "rack";
  groupCountSpec.partitionName() = "job";
  groupCountSpec.bound() = GroupCountSpecBound::MAX;
  facebook::rebalancer::interface::Limit globalLimit;
  globalLimit.globalLimit() = 1;
  groupCountSpec.limit() = globalLimit;

  solver->addGoal(groupCountSpec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  std::map<std::string, int> hostToTaskCount;

  for (auto& [_, hostName] : assignment) {
    ++hostToTaskCount[hostName];
  }
  EXPECT_EQ(0, hostToTaskCount["host0"]);
  EXPECT_EQ(6, hostToTaskCount["host1"]);
  EXPECT_EQ(3, hostToTaskCount["host2"]);
  EXPECT_EQ(3, hostToTaskCount["host3"]);
}

TEST_P(BalanceTest, BalanceSpecUseLegacyAverage) {
  auto solver = createExampleScenario();

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "memory";
  spec.useLegacyAverage() = true;
  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  std::map<std::string, int> hostToTaskCount;

  for (auto& [_, hostName] : assignment) {
    ++hostToTaskCount[hostName];
  }

  // Using legacy average, the average relative utilization of scope items
  // is sum of absolute utilization of scope items divided by sum of capacity
  // 200 / 5000 = 0.04
  EXPECT_EQ(0, hostToTaskCount["host0"]);
  EXPECT_EQ(120, hostToTaskCount["host1"]);
  EXPECT_EQ(40, hostToTaskCount["host2"]);
  EXPECT_EQ(40, hostToTaskCount["host3"]);
}

TEST_P(BalanceTest, BalanceSpecNotUseLegacyAverage) {
  auto solver = createExampleScenario();

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "memory";
  spec.useLegacyAverage() = false;
  solver->addGoal(spec);
  solver->addSolver(makeDefaultLocalSearchSolver());
  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  std::map<std::string, int> hostToTaskCount;

  for (auto& [_, hostName] : assignment) {
    ++hostToTaskCount[hostName];
  }
  // Using the new average, the average relative utilization of scope items
  // is the sum of relative utilization of scope items divided by number of
  // scope items
  // 100 / 2000 = 0.05
  // 50 / 1000 = 0.05
  // 50 / 1000 = 0.05
  EXPECT_EQ(0, hostToTaskCount["host0"]);
  EXPECT_EQ(100, hostToTaskCount["host1"]);
  EXPECT_EQ(50, hostToTaskCount["host2"]);
  EXPECT_EQ(50, hostToTaskCount["host3"]);
}

TEST_P(BalanceTest, NewFormulaWithNegativeObjectDimensions) {
  auto solver = createNegativeScenario();

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "memory";
  spec.useLegacyAverage() = false;
  solver->addGoal(spec);
  solver->addSolver(makeDefaultLocalSearchSolver());
  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  std::map<std::string, int> hostToTaskCount;

  for (auto& [_, hostName] : assignment) {
    ++hostToTaskCount[hostName];
  }
  EXPECT_EQ(0, hostToTaskCount["host0"]);
  EXPECT_EQ(100, hostToTaskCount["host1"]);
  EXPECT_EQ(50, hostToTaskCount["host2"]);
  EXPECT_EQ(50, hostToTaskCount["host3"]);
}

TEST_P(BalanceTest, NewFormulaWithAllBlackListedScopeItems) {
  auto solver = createExampleScenario();

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "memory";
  spec.useLegacyAverage() = false;
  spec.filter()->itemsBlacklist() = {"host0", "host1", "host2", "host3"};
  solver->addGoal(spec);
  solver->addSolver(makeDefaultLocalSearchSolver());
  solver->solve();
  // End assignment is unpredictable because of the negative object dimension in
  // combination with black list, goal of this test is to make sure that balance
  // spec does not crash
}

TEST_P(BalanceTest, NewFormulaWithAllWhiteListedScopeItems) {
  auto solver = createNegativeScenario();

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "memory";
  spec.useLegacyAverage() = false;
  spec.filter()->itemsWhitelist() = {"host0", "host1", "host2", "host3"};
  solver->addGoal(spec);
  solver->addSolver(makeDefaultLocalSearchSolver());
  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  std::map<std::string, int> hostToTaskCount;
  for (auto& [_, hostName] : assignment) {
    ++hostToTaskCount[hostName];
  }
  EXPECT_EQ(0, hostToTaskCount["host0"]);
  EXPECT_EQ(100, hostToTaskCount["host1"]);
  EXPECT_EQ(50, hostToTaskCount["host2"]);
  EXPECT_EQ(50, hostToTaskCount["host3"]);
}

TEST_P(BalanceTest, NewFormulaWithNoTasks) {
  auto solver = createNoTasksScenario();

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  spec.useLegacyAverage() = false;
  solver->addGoal(spec);
  solver->addSolver(makeDefaultLocalSearchSolver());
  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  std::map<std::string, int> hostToTaskCount;
  for (auto& [_, hostName] : assignment) {
    ++hostToTaskCount[hostName];
  }
  EXPECT_EQ(0, hostToTaskCount["host0"]);
  EXPECT_EQ(0, hostToTaskCount["host1"]);
  EXPECT_EQ(0, hostToTaskCount["host2"]);
  EXPECT_EQ(0, hostToTaskCount["host3"]);
}

TEST_P(BalanceTest, NewFormulaWithNoObjectsInScopeItems) {
  auto solver = createNoObjectsInScopeItemsScenario();

  BalanceSpec spec;
  spec.scope() = "rack";
  spec.dimension() = "cpu";
  spec.useLegacyAverage() = false;
  spec.filter()->itemsBlacklist() = {"rack0", "rack1"};
  solver->addGoal(spec);
  solver->addSolver(makeDefaultLocalSearchSolver());
  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  std::map<std::string, int> hostToTaskCount;
  for (auto& [_, hostName] : assignment) {
    ++hostToTaskCount[hostName];
  }
  EXPECT_EQ(200, hostToTaskCount["host0"]);
  EXPECT_EQ(0, hostToTaskCount["host1"]);
  EXPECT_EQ(0, hostToTaskCount["host2"]);
  EXPECT_EQ(0, hostToTaskCount["host3"]);
}

TEST_P(BalanceTest, NewFormulaNoHostNoTasks) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");
  solver->setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{});

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  spec.useLegacyAverage() = false;
  solver->addGoal(spec);
  solver->addSolver(makeDefaultLocalSearchSolver());
  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  std::map<std::string, int> hostToTaskCount;
  for (auto& [_, hostName] : assignment) {
    ++hostToTaskCount[hostName];
  }
  EXPECT_EQ(0, hostToTaskCount.size());
}

TEST_P(BalanceTest, DeterministicMoves) {
  auto solver = createScenarioWithManySolutions();

  auto solver2 = createScenarioWithManySolutions();

  BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = BalanceSpecFormula::SQUARES;

  solver->addGoal(balanceSpec);
  solver2->addGoal(balanceSpec);

  auto searchSolve = LocalSearchSolverSpec();
  searchSolve.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  solver->addSolver(searchSolve);
  solver2->addSolver(searchSolve);

  auto solution = solver->solve();
  auto solution2 = solver2->solve();

  auto movesSummary = solution.movesSummary();
  auto movesSummary2 = solution2.movesSummary();

  ASSERT_TRUE(movesSummary.has_value());
  ASSERT_TRUE(movesSummary2.has_value());
  EXPECT_EQ(8, movesSummary.value().size());
  EXPECT_EQ(8, movesSummary2.value().size());

  for (const auto i : folly::irange(8)) {
    auto move0_i = movesSummary.value().at(i).moves().value().at(0);
    auto move1_i = movesSummary2.value().at(i).moves().value().at(0);
    EXPECT_EQ(move0_i.object().value(), move1_i.object().value());
    EXPECT_EQ(move0_i.srcContainer().value(), move1_i.srcContainer().value());
    EXPECT_EQ(move0_i.dstContainer().value(), move1_i.dstContainer().value());
  }

  auto& assignment = *solution.assignment();
  std::map<std::string, int> hostToTaskCount;
  for (auto& [_, hostName] : assignment) {
    ++hostToTaskCount[hostName];
  }

  auto& assignment2 = *solution2.assignment();
  std::map<std::string, int> hostToTaskCount2;
  for (auto& [_, hostName] : assignment2) {
    ++hostToTaskCount2[hostName];
  }

  EXPECT_EQ(10, hostToTaskCount.size());
  EXPECT_EQ(10, hostToTaskCount2.size());
  for (const auto i : folly::irange(20)) {
    EXPECT_EQ(
        hostToTaskCount[fmt::format("host{}", i)],
        hostToTaskCount2[fmt::format("host{}", i)]);
  }
}

TEST_P(BalanceTest, MaxFormulaBigUpperboundNoFixAverageToInitial) {
  // Since the upper bound is much bigger than the average, we expect there to
  // be no moves.

  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(20)) {
    initialAssignment[fmt::format("host{}", i % 4)].push_back(
        fmt::format("task{}", i));
  }
  solver->setAssignment(initialAssignment);

  std::map<std::string, double> taskCpu;
  for (const auto i : folly::irange(20)) {
    taskCpu[fmt::format("task{}", i)] = 0.01 * i + 1;
  }
  solver->addObjectDimension("cpu", taskCpu);

  std::map<std::string, double> hostCpu;
  for (const auto i : folly::irange(4)) {
    hostCpu[fmt::format("host{}", i)] = 100;
  }

  solver->addContainerDimension("cpu", hostCpu);

  BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = BalanceSpecFormula::MAX;
  auto& upperBounds = balanceSpec.upperBounds().ensure();
  upperBounds.type() = LimitType::ABSOLUTE;
  upperBounds.globalLimit() = 0.9;
  balanceSpec.definition() = BalanceSpecDefinition::AFTER;
  balanceSpec.boundType() = BalanceSpecBoundType::ABSOLUTE;

  solver->addGoal(balanceSpec);

  auto searchSolve = LocalSearchSolverSpec();
  searchSolve.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  solver->addSolver(searchSolve);

  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  for (const auto i : folly::irange(20)) {
    auto task = fmt::format("task{}", i);
    auto host = fmt::format("host{}", i % 4);
    EXPECT_EQ(host, assignment[task]);
  }
}

TEST_P(BalanceTest, MaxFormulaSmallMoves) {
  // This test demonstrates that even with Max formula set, moves with extremely
  // small "gains" can still be made

  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(10)) {
    initialAssignment[fmt::format("host{}", i)].push_back(
        fmt::format("task{}", i));
  }

  // last 4 hosts have an additional task with CPU = 90
  for (const auto i : folly::irange(10, 14)) {
    initialAssignment[fmt::format("host{}", i - 10)].push_back(
        fmt::format("task{}", i));
  }

  solver->setAssignment(initialAssignment);

  // first 10 tasks have small CPU
  std::map<std::string, double> taskCpu;
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("task{}", i)] = 0.001 * i;
  }

  for (const auto i : folly::irange(10, 14)) {
    taskCpu[fmt::format("task{}", i)] = 90;
  }
  solver->addObjectDimension("cpu", taskCpu);

  std::map<std::string, double> hostCpu;
  for (const auto i : folly::irange(4)) {
    hostCpu[fmt::format("host{}", i)] = 100;
  }

  solver->addContainerDimension("cpu", hostCpu);

  BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = BalanceSpecFormula::MAX;
  auto& upperBounds = balanceSpec.upperBounds().ensure();
  upperBounds.type() = LimitType::ABSOLUTE;
  upperBounds.globalLimit() = 0.2;
  balanceSpec.definition() = BalanceSpecDefinition::AFTER;
  balanceSpec.boundType() = BalanceSpecBoundType::ABSOLUTE;

  solver->addGoal(balanceSpec);

  auto searchSolve = LocalSearchSolverSpec();
  searchSolve.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  solver->addSolver(searchSolve);

  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  std::map<std::string, int> hostToTaskCount;
  for (auto& [_, hostName] : assignment) {
    ++hostToTaskCount[hostName];
  }

  // Small improvements are made. Hosts 0-3 have capacity 100 and each holds
  // one big task (cpu=90). Exact redistribution of small tasks (cpu~0) among
  // uncapacitated hosts 4-9 is solver-order-dependent.
  int totalTasks = 0;
  for (const auto& [host, count] : hostToTaskCount) {
    totalTasks += count;
  }
  EXPECT_EQ(14, totalTasks);
  // Hosts 0-3 keep their big tasks; host0 gets one extra small task
  EXPECT_EQ(2, hostToTaskCount["host0"]);
  EXPECT_EQ(1, hostToTaskCount["host1"]);
  EXPECT_EQ(1, hostToTaskCount["host2"]);
  EXPECT_EQ(1, hostToTaskCount["host3"]);
  // Hosts 4-9 collectively hold the remaining 9 small tasks
  int uncapacitatedTotal = 0;
  for (const auto i : folly::irange(4, 10)) {
    uncapacitatedTotal += hostToTaskCount[fmt::format("host{}", i)];
  }
  EXPECT_EQ(9, uncapacitatedTotal);
}

TEST_P(BalanceTest, IdealWithRelativeUpperBound) {
  auto solver = initializeTestProblemSolver(
      {.useParallelMaterializer = static_cast<bool>(GetParam())});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // Initially, all 100 tasks are placed in host0, and host1 is empty.
  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(100)) {
    initialAssignment["host0"].push_back(fmt::format("task{}", i));
  }
  initialAssignment["host1"] = {};
  solver->setAssignment(initialAssignment);

  std::map<std::string, double> taskCpu;
  for (const auto i : folly::irange(100)) {
    taskCpu[fmt::format("task{}", i)] = 1;
  }
  solver->addObjectDimension("cpu", taskCpu);

  const std::map<std::string, double> hostCpu = {
      {"host0", 100}, {"host1", 100}};
  solver->addContainerDimension("cpu", hostCpu);

  // Define an ideal balance goal with an upper bound of type relative set
  // to 1.4.
  // This means there is no incentive to balance the relative utilization
  // above 1.4 * 50% = 70%.
  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.formula() = BalanceSpecFormula::IDEAL;
  spec.upperBound() = 1.4;
  spec.ignoreUpperBoundForIdealWithAbsOrRelBoundTypes() = false;
  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());
  auto solution = solver->solve();

  std::map<std::string, int> taskCount;
  for (auto& [_, host] : *solution.assignment()) {
    ++taskCount[host];
  }

  EXPECT_EQ(70, taskCount["host0"]); // 70%
  EXPECT_EQ(30, taskCount["host1"]); // 30%
}

TEST_P(BalanceTest, IdealWithRelativeUpperBoundTrickyScenario) {
  auto solver = createTrickyBalanceScenario();
  BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = BalanceSpecFormula::IDEAL;
  balanceSpec.ignoreUpperBoundForIdealWithAbsOrRelBoundTypes() = false;
  auto& upperBounds = balanceSpec.upperBounds().ensure();
  upperBounds.type() = LimitType::ABSOLUTE;
  upperBounds.globalLimit() = 4;

  solver->addGoal(balanceSpec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  std::map<std::string, int> taskCount;
  for (auto& [_, host] : *solution.assignment()) {
    ++taskCount[host];
  }

  // With the tricky scenario, the average utilization is total util/ # scope
  // items In this scenario, it is 0.84 / 5 = 0.168
  // this means the upper bound is 0.168 * 4 = 0.672 and we will stop balancing
  // once all util is below 0.672
  // Exact task distribution among hosts depends on solver iteration order,
  // but structural properties must hold.
  int totalTasks = 0;
  for (const auto& [host, count] : taskCount) {
    totalTasks += count;
  }
  EXPECT_EQ(84, totalTasks);

  // host4 is constrained by ToFreeSpec to have no tasks.
  EXPECT_EQ(0, taskCount["host4"]);

  // Each host must be within its capacity.
  EXPECT_LE(taskCount["host0"], 100);
  EXPECT_LE(taskCount["host1"], 20);
  EXPECT_LE(taskCount["host2"], 10);
  EXPECT_LE(taskCount["host3"], 10);

  // host0 gets the bulk since other hosts have limited capacity.
  EXPECT_GE(taskCount["host0"], 44); // at least 84 - (20+10+10)
}

TEST_P(BalanceTest, IdealWithAbsoluteUpperBound) {
  auto solver = initializeTestProblemSolver(
      {.useParallelMaterializer = static_cast<bool>(GetParam())});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // Initially, all 100 tasks are placed in host0, and host1 is empty.
  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(100)) {
    initialAssignment["host0"].push_back(fmt::format("task{}", i));
  }
  initialAssignment["host1"] = {};
  solver->setAssignment(initialAssignment);

  std::map<std::string, double> taskCpu;
  for (const auto i : folly::irange(100)) {
    taskCpu[fmt::format("task{}", i)] = 1;
  }
  solver->addObjectDimension("cpu", taskCpu);

  const std::map<std::string, double> hostCpu = {
      {"host0", 100}, {"host1", 100}};
  solver->addContainerDimension("cpu", hostCpu);

  // Define an ideal balance goal with an upper bound of type absolute set
  // to 0.2.
  // This means there is no incentive to balance the relative utilization
  // above 0.2 + 0.5 = 70%.
  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.formula() = BalanceSpecFormula::IDEAL;
  spec.boundType() = BalanceSpecBoundType::ABSOLUTE;
  auto& upperBounds = spec.upperBounds().ensure();
  upperBounds.type() = LimitType::ABSOLUTE;
  upperBounds.globalLimit() = 0.2;
  spec.ignoreUpperBoundForIdealWithAbsOrRelBoundTypes() = false;
  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());
  auto solution = solver->solve();

  std::map<std::string, int> taskCount;
  for (auto& [_, host] : *solution.assignment()) {
    ++taskCount[host];
  }

  EXPECT_EQ(70, taskCount["host0"]); // 70%
  EXPECT_EQ(30, taskCount["host1"]); // 30%
}

TEST_P(BalanceTest, IdealWithAbsoluteUpperBoundTrickyScenario) {
  auto solver = createTrickyBalanceScenario();
  BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = BalanceSpecFormula::IDEAL;
  balanceSpec.boundType() = BalanceSpecBoundType::ABSOLUTE;
  balanceSpec.ignoreUpperBoundForIdealWithAbsOrRelBoundTypes() = false;
  auto& upperBounds = balanceSpec.upperBounds().ensure();
  upperBounds.type() = LimitType::ABSOLUTE;
  upperBounds.globalLimit() = 0.5;

  solver->addGoal(balanceSpec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  std::map<std::string, int> taskCount;
  for (auto& [_, host] : *solution.assignment()) {
    ++taskCount[host];
  }
  // With the tricky scenario, the average utilization is total util/ # scope
  // items In this scenario, it is 0.84 / 5 = 0.168
  // this means the upper bound is 0.168 + 0.5 = 0.668 and we will stop
  // balancing once all util is below 0.668
  // Exact task distribution depends on solver iteration order.
  int totalTasks = 0;
  for (const auto& [host, count] : taskCount) {
    totalTasks += count;
  }
  EXPECT_EQ(84, totalTasks);

  // host4 is constrained by ToFreeSpec to have no tasks.
  EXPECT_EQ(0, taskCount["host4"]);

  // Each host must be within its capacity.
  EXPECT_LE(taskCount["host0"], 100);
  EXPECT_LE(taskCount["host1"], 20);
  EXPECT_LE(taskCount["host2"], 10);
  EXPECT_LE(taskCount["host3"], 10);

  // host0 gets the bulk since other hosts have limited capacity.
  EXPECT_GE(taskCount["host0"], 44); // at least 84 - (20+10+10)
}

TEST_P(
    BalanceTest,
    IdealWithRelativeUpperBoundTrickyScenarioNoFixAverageToInitial) {
  auto solver = createTrickyBalanceScenario();
  BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = BalanceSpecFormula::IDEAL;
  balanceSpec.ignoreUpperBoundForIdealWithAbsOrRelBoundTypes() = false;
  balanceSpec.upperBound() = 1.5;
  balanceSpec.fixAverageToInitial() = false;
  balanceSpec.filter()->itemsBlacklist() = {"host4"};

  solver->addGoal(balanceSpec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  std::map<std::string, int> taskCount;
  for (auto& [_, host] : *solution.assignment()) {
    ++taskCount[host];
  }

  // With the tricky scenario, the average utilization is total util/ # scope
  // items In this scenario, it is 0.84 / 4 = 0.21. However, because
  // fixAverageToInitial() = false, the average utilization is dynamic and not
  // fixed to the initial utilization. This means once the assignment is
  // (72,0,6,6), we will stop balancing since the average utilization is 1.92/4
  // = 0.48 and the upper bound is 1.5* 0.48 = 0.72

  EXPECT_EQ(72, taskCount["host0"]);
  EXPECT_EQ(0, taskCount["host1"]);
  // The exact split between host2 and host3 depends on solver iteration order
  // which can vary across platforms, but they should total 12
  EXPECT_EQ(12, taskCount["host2"] + taskCount["host3"]);
  EXPECT_GE(taskCount["host2"], 5);
  EXPECT_GE(taskCount["host3"], 5);

  // Except host4, which is not allowed to get any tasks.
  EXPECT_EQ(0, taskCount["host4"]);
}

TEST_P(BalanceTest, TrickyScenarioWithRelativeUtilVariance) {
  // Run the same tricky scenario (host capacities: 100, 20, 10, 10, 1000)
  // with RELATIVE_UTIL_VARIANCE. The tricky scenario has host4 excluded
  // (ToFree), so 84 tasks must be distributed across hosts with capacities
  // 100+20+10+10=140. Equal 60% relUtil means (60, 12, 6, 6).
  std::map<std::string, int> pureCount;
  auto solver = createTrickyBalanceScenario();
  BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = BalanceSpecFormula::RELATIVE_UTIL_VARIANCE;
  balanceSpec.boundType() = BalanceSpecBoundType::RELATIVE_UTIL;
  auto& upperBounds = balanceSpec.upperBounds().ensure();
  upperBounds.type() = LimitType::ABSOLUTE;
  upperBounds.globalLimit() = 0;
  // Exclude ToFree'd host4 from variance; otherwise its relUtil=0 distorts
  // the average and prevents equalization.
  balanceSpec.filter()->itemsBlacklist() = {"host4"};
  solver->addGoal(balanceSpec);
  solver->addSolver(makeDefaultLocalSearchSolver());
  auto solution = solver->solve();
  for (const auto& [_, host] : *solution.assignment()) {
    ++pureCount[host];
  }
  // RELATIVE_UTIL_VARIANCE equalizes relative utilization at 60%.
  EXPECT_EQ(60, pureCount["host0"]);
  EXPECT_EQ(12, pureCount["host1"]);
  EXPECT_EQ(6, pureCount["host2"]);
  EXPECT_EQ(6, pureCount["host3"]);
  EXPECT_EQ(0, pureCount["host4"]);
}

TEST_P(BalanceTest, DifferentAbsCostIdealAssignsToHotterContainer) {
  // Design doc scenario: dynamic dimension where objectA costs 1 in A, 2 in B.
  //   ContainerA: cap=50, 15 used (30%)  |  ContainerB: cap=120, 18 used (15%)
  // IDEAL picks A (hotter) due to capacity weighting in the gradient.
  // RELATIVE_UTIL_VARIANCE picks B (cooler) since variance gradient has no
  // capacity term.
  auto createScenario = [&]() {
    auto solver =
        initializeTestProblemSolver({.executorThreadCount = GetParam()});
    solver->setObjectName("task");
    solver->setContainerName("container");

    // 15 tasks in containerA, 18 tasks in containerB, plus objectA in A.
    std::map<std::string, std::vector<std::string>> assignment;
    assignment["containerA"] = {};
    for (const auto i : folly::irange(15)) {
      assignment["containerA"].emplace_back(fmt::format("task{}", i));
    }
    assignment["containerA"].emplace_back("objectA");

    assignment["containerB"] = {};
    for (const auto i : folly::irange(15, 33)) {
      assignment["containerB"].emplace_back(fmt::format("task{}", i));
    }
    solver->setAssignment(assignment);

    // Container capacities.
    solver->addContainerDimension(
        "cpu",
        std::map<std::string, double>{{"containerA", 50}, {"containerB", 120}});

    // Dynamic object dimension: all existing tasks cost 1 everywhere
    // (via defaultValue=1). objectA costs 1 in containerA, 2 in containerB.
    solver->addDynamicObjectDimension(
        "cpu",
        "container",
        std::map<std::string, std::map<std::string, double>>{
            {"containerA", {{"objectA", 1}}}, {"containerB", {{"objectA", 2}}}},
        /*defaultValue=*/1);

    // Freeze all existing tasks so only objectA can move.
    AvoidMovingSpec avoidSpec;
    std::vector<std::string> frozenTasks;
    for (const auto i : folly::irange(33)) {
      frozenTasks.emplace_back(fmt::format("task{}", i));
    }
    avoidSpec.objects() = frozenTasks;
    solver->addConstraint(avoidSpec);

    return solver;
  };

  // --- Run with IDEAL ---
  std::string idealPlacement;
  {
    auto solver = createScenario();
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "cpu";
    balanceSpec.formula() = BalanceSpecFormula::IDEAL;
    solver->addGoal(balanceSpec);
    solver->addSolver(makeDefaultLocalSearchSolver());
    auto solution = solver->solve();
    for (const auto& [task, container] : *solution.assignment()) {
      if (task == "objectA") {
        idealPlacement = container;
      }
    }
  }

  // --- Run with RELATIVE_UTIL_VARIANCE ---
  std::string relUtilVarPlacement;
  {
    auto solver = createScenario();
    BalanceSpec balanceSpec;
    balanceSpec.scope() = "container";
    balanceSpec.dimension() = "cpu";
    balanceSpec.formula() = BalanceSpecFormula::RELATIVE_UTIL_VARIANCE;
    solver->addGoal(balanceSpec);
    solver->addSolver(makeDefaultLocalSearchSolver());
    auto solution = solver->solve();
    for (const auto& [task, container] : *solution.assignment()) {
      if (task == "objectA") {
        relUtilVarPlacement = container;
      }
    }
  }

  EXPECT_EQ("containerA", idealPlacement); // IDEAL: hotter (30%)
  EXPECT_EQ(
      "containerB",
      relUtilVarPlacement); // RELATIVE_UTIL_VARIANCE: cooler (15%)
}

TEST_P(BalanceTest, RelativeUtilVarianceWithRelativeUtilBound) {
  // This example shows RELATIVE_UTIL_VARIANCE with RELATIVE_UTIL bound type.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // Initially, all 100 tasks are placed in host0, and host1 is empty.
  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(100)) {
    initialAssignment["host0"].emplace_back(fmt::format("task{}", i));
  }
  initialAssignment["host1"] = {};
  solver->setAssignment(initialAssignment);

  std::map<std::string, double> taskCpu;
  for (const auto i : folly::irange(100)) {
    taskCpu[fmt::format("task{}", i)] = 1;
  }
  solver->addObjectDimension("cpu", taskCpu);

  const std::map<std::string, double> hostCpu = {
      {"host0", 100}, {"host1", 100}};
  solver->addContainerDimension("cpu", hostCpu);

  // RELATIVE_UTIL bound at 0.7: no incentive to balance below 70%.
  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.formula() = BalanceSpecFormula::RELATIVE_UTIL_VARIANCE;
  spec.upperBound() = 0.7;
  spec.boundType() = BalanceSpecBoundType::RELATIVE_UTIL;
  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  std::map<std::string, int> taskCount;
  for (auto& [_, host] : *solution.assignment()) {
    ++taskCount[host];
  }

  EXPECT_EQ(70, taskCount["host0"]); // 70%
  EXPECT_EQ(30, taskCount["host1"]); // 30%
}

TEST_P(
    BalanceTest,
    IdealWithRelativeUpperBoundTrickyScenarioFixAverageToInitial) {
  auto solver = createTrickyBalanceScenario();
  BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = BalanceSpecFormula::IDEAL;
  balanceSpec.ignoreUpperBoundForIdealWithAbsOrRelBoundTypes() = false;
  auto& upperBounds = balanceSpec.upperBounds().ensure();
  upperBounds.type() = LimitType::ABSOLUTE;
  upperBounds.globalLimit() = 1.5;
  balanceSpec.fixAverageToInitial() = true;
  balanceSpec.filter()->itemsBlacklist() = {"host4"};

  solver->addGoal(balanceSpec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  std::map<std::string, int> taskCount;
  for (auto& [_, host] : *solution.assignment()) {
    ++taskCount[host];
  }

  // With the tricky scenario, the average utilization is total util/ # scope
  // items In this scenario, it is 0.84 / 4 = 0.21
  // this means the upper bound is 0.21 * 1.5 = 0.315 and since we have
  // fixAverageToInitial() = true, we will stop balancing once all util is below
  // 0.315 or when no improvement is possible

  EXPECT_EQ(60, taskCount["host0"]);
  EXPECT_EQ(12, taskCount["host1"]);
  EXPECT_EQ(6, taskCount["host2"]);
  EXPECT_EQ(6, taskCount["host3"]);

  // Except host4, which is not allowed to get any tasks.
  EXPECT_EQ(0, taskCount["host4"]);
}

TEST_P(BalanceTest, RelativeUtilVarianceWithRelativeUpperBound) {
  // RELATIVE_UTIL_VARIANCE with RELATIVE bound type.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(100)) {
    initialAssignment["host0"].emplace_back(fmt::format("task{}", i));
  }
  initialAssignment["host1"] = {};
  solver->setAssignment(initialAssignment);

  std::map<std::string, double> taskCpu;
  for (const auto i : folly::irange(100)) {
    taskCpu[fmt::format("task{}", i)] = 1;
  }
  solver->addObjectDimension("cpu", taskCpu);

  const std::map<std::string, double> hostCpu = {
      {"host0", 100}, {"host1", 100}};
  solver->addContainerDimension("cpu", hostCpu);

  // RELATIVE bound=1.4 → threshold = 1.4 * avgUtil = 1.4 * 0.5 = 0.7
  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.formula() = BalanceSpecFormula::RELATIVE_UTIL_VARIANCE;
  spec.upperBound() = 1.4;
  spec.boundType() = BalanceSpecBoundType::RELATIVE;
  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  std::map<std::string, int> taskCount;
  for (auto& [_, host] : *solution.assignment()) {
    ++taskCount[host];
  }

  EXPECT_EQ(70, taskCount["host0"]);
  EXPECT_EQ(30, taskCount["host1"]);
}

TEST_P(BalanceTest, RelativeUtilVarianceWithAbsoluteUpperBound) {
  // RELATIVE_UTIL_VARIANCE with ABSOLUTE bound type.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(100)) {
    initialAssignment["host0"].emplace_back(fmt::format("task{}", i));
  }
  initialAssignment["host1"] = {};
  solver->setAssignment(initialAssignment);

  std::map<std::string, double> taskCpu;
  for (const auto i : folly::irange(100)) {
    taskCpu[fmt::format("task{}", i)] = 1;
  }
  solver->addObjectDimension("cpu", taskCpu);

  const std::map<std::string, double> hostCpu = {
      {"host0", 100}, {"host1", 100}};
  solver->addContainerDimension("cpu", hostCpu);

  // ABSOLUTE bound=0.2 → threshold = avgUtil + 0.2 = 0.5 + 0.2 = 0.7
  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.formula() = BalanceSpecFormula::RELATIVE_UTIL_VARIANCE;
  auto& upperBounds = spec.upperBounds().ensure();
  upperBounds.type() = LimitType::ABSOLUTE;
  upperBounds.globalLimit() = 0.2;
  spec.boundType() = BalanceSpecBoundType::ABSOLUTE;
  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  std::map<std::string, int> taskCount;
  for (auto& [_, host] : *solution.assignment()) {
    ++taskCount[host];
  }

  EXPECT_EQ(70, taskCount["host0"]);
  EXPECT_EQ(30, taskCount["host1"]);
}

TEST_P(BalanceTest, CapacityPerItem) {
  // CAPACITY_PER_ITEM balances absUtil/numObjects (average object demand)
  // across scope items, ensuring heavy and light objects are evenly mixed.
  // With 10 heavy (cpu=10) and 10 light (cpu=1) tasks, the balanced state
  // has each host with the same average demand (5.5).
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // All heavy tasks on host0, all light tasks on host1.
  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(10)) {
    initialAssignment["host0"].push_back(fmt::format("heavy{}", i));
  }
  for (const auto i : folly::irange(10)) {
    initialAssignment["host1"].push_back(fmt::format("light{}", i));
  }
  solver->setAssignment(initialAssignment);

  std::map<std::string, double> taskCpu;
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("heavy{}", i)] = 10;
  }
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("light{}", i)] = 1;
  }
  solver->addObjectDimension("cpu", taskCpu);

  solver->addContainerDimension(
      "cpu", std::map<std::string, double>{{"host0", 1000}, {"host1", 1000}});

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.balanceMetric() = BalanceSpecMetric::CAPACITY_PER_ITEM;
  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  // Verify both hosts have the same average object demand (capPerItem).
  // Total absUtil = 10*10 + 10*1 = 110, global avg = 110/20 = 5.5.
  // Any distribution preserving the 1:1 heavy:light ratio on each host
  // achieves capPerItem = 5.5 on both hosts (penalty = 0).
  std::map<std::string, double> hostAbsUtil;
  std::map<std::string, int> hostTaskCount;
  for (auto& [task, host] : *solution.assignment()) {
    hostAbsUtil[host] += taskCpu[task];
    ++hostTaskCount[host];
  }

  EXPECT_EQ(20, hostTaskCount["host0"] + hostTaskCount["host1"]);
  EXPECT_GT(hostTaskCount["host0"], 0);
  EXPECT_GT(hostTaskCount["host1"], 0);

  const double avgDemandHost0 = hostAbsUtil["host0"] / hostTaskCount["host0"];
  const double avgDemandHost1 = hostAbsUtil["host1"] / hostTaskCount["host1"];
  EXPECT_NEAR(5.5, avgDemandHost0, 1e-8);
  EXPECT_NEAR(5.5, avgDemandHost1, 1e-8);
}

TEST_P(BalanceOptimalTest, CapacityPerItemThrows) {
  // CAPACITY_PER_ITEM lowers to a nonlinear quotient; the MIP path can't
  // represent it, so the spec builder must reject this combination.
  auto [threads, solverPackage] = GetParam();
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("task");
  solver->setContainerName("host");
  solver->setAssignment(
      std::map<std::string, std::vector<std::string>>{
          {"host0", {"task0"}}, {"host1", {"task1"}}});
  solver->addObjectDimension(
      "cpu", std::map<std::string, double>{{"task0", 1}, {"task1", 1}});

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.balanceMetric() = BalanceSpecMetric::CAPACITY_PER_ITEM;
  solver->addGoal(spec);

  OptimalSolverSpec optimalSpec;
  optimalSpec.solverPackage() = solverPackage;
  solver->addSolver(optimalSpec);

  REBALANCER_EXPECT_RUNTIME_ERROR_CONTAINS(
      solver->solve(),
      "BalanceSpec with CAPACITY_PER_ITEM metric is not currently supported");
}

TEST_P(BalanceTest, CapacityPerItemWithEmptyContainer) {
  // Same scenario as CapacityPerItem but with a third empty host.
  // The formula should still balance the average object demand across the
  // non-empty hosts, and the empty host gets capPerItem = 0.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(10)) {
    initialAssignment["host0"].push_back(fmt::format("heavy{}", i));
  }
  for (const auto i : folly::irange(10)) {
    initialAssignment["host1"].push_back(fmt::format("light{}", i));
  }
  initialAssignment["host2"] = {};
  solver->setAssignment(initialAssignment);

  std::map<std::string, double> taskCpu;
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("heavy{}", i)] = 10;
  }
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("light{}", i)] = 1;
  }
  solver->addObjectDimension("cpu", taskCpu);

  solver->addContainerDimension(
      "cpu",
      std::map<std::string, double>{
          {"host0", 1000}, {"host1", 1000}, {"host2", 1000}});

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.balanceMetric() = BalanceSpecMetric::CAPACITY_PER_ITEM;
  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  // With capPerItem = absUtil/numObjects and empty host2 having capPerItem = 0,
  // the average across 3 hosts is dragged down, so the threshold is lower.
  // Non-empty hosts with equal avg demand will both be above the threshold,
  // but equally so — the solver equalizes their capPerItem.
  std::map<std::string, double> hostAbsUtil;
  std::map<std::string, int> hostTaskCount;
  for (auto& [task, host] : *solution.assignment()) {
    hostAbsUtil[host] += taskCpu[task];
    ++hostTaskCount[host];
  }

  EXPECT_EQ(
      20,
      hostTaskCount["host0"] + hostTaskCount["host1"] + hostTaskCount["host2"]);

  // All three hosts must have tasks — the solver must move items to the
  // initially empty host2 to equalize capPerItem across all scope items.
  // With penalty = 0, all hosts have the same avg demand of 5.5.
  EXPECT_GT(hostTaskCount["host0"], 0);
  EXPECT_GT(hostTaskCount["host1"], 0);
  EXPECT_GT(hostTaskCount["host2"], 0);

  const double avgDemandHost0 = hostAbsUtil["host0"] / hostTaskCount["host0"];
  const double avgDemandHost1 = hostAbsUtil["host1"] / hostTaskCount["host1"];
  const double avgDemandHost2 = hostAbsUtil["host2"] / hostTaskCount["host2"];
  EXPECT_NEAR(5.5, avgDemandHost0, 1e-8);
  EXPECT_NEAR(5.5, avgDemandHost1, 1e-8);
  EXPECT_NEAR(5.5, avgDemandHost2, 1e-8);
}

TEST_P(BalanceTest, CapacityPerItemSquares) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(10)) {
    initialAssignment["host0"].push_back(fmt::format("heavy{}", i));
  }
  for (const auto i : folly::irange(10)) {
    initialAssignment["host1"].push_back(fmt::format("light{}", i));
  }
  solver->setAssignment(initialAssignment);

  std::map<std::string, double> taskCpu;
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("heavy{}", i)] = 10;
  }
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("light{}", i)] = 1;
  }
  solver->addObjectDimension("cpu", taskCpu);

  solver->addContainerDimension(
      "cpu", std::map<std::string, double>{{"host0", 1000}, {"host1", 1000}});

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.balanceMetric() = BalanceSpecMetric::CAPACITY_PER_ITEM;
  spec.formula() = BalanceSpecFormula::SQUARES;
  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  std::map<std::string, double> hostAbsUtil;
  std::map<std::string, int> hostTaskCount;
  for (auto& [task, host] : *solution.assignment()) {
    hostAbsUtil[host] += taskCpu[task];
    ++hostTaskCount[host];
  }

  EXPECT_GT(hostTaskCount["host0"], 0);
  EXPECT_GT(hostTaskCount["host1"], 0);
  const double avgDemandHost0 = hostAbsUtil["host0"] / hostTaskCount["host0"];
  const double avgDemandHost1 = hostAbsUtil["host1"] / hostTaskCount["host1"];
  EXPECT_NEAR(5.5, avgDemandHost0, 1e-8);
  EXPECT_NEAR(5.5, avgDemandHost1, 1e-8);
}

TEST_P(BalanceTest, CapacityPerItemMax) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(10)) {
    initialAssignment["host0"].push_back(fmt::format("heavy{}", i));
  }
  for (const auto i : folly::irange(10)) {
    initialAssignment["host1"].push_back(fmt::format("light{}", i));
  }
  solver->setAssignment(initialAssignment);

  std::map<std::string, double> taskCpu;
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("heavy{}", i)] = 10;
  }
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("light{}", i)] = 1;
  }
  solver->addObjectDimension("cpu", taskCpu);

  solver->addContainerDimension(
      "cpu", std::map<std::string, double>{{"host0", 1000}, {"host1", 1000}});

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.balanceMetric() = BalanceSpecMetric::CAPACITY_PER_ITEM;
  spec.formula() = BalanceSpecFormula::MAX;
  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  std::map<std::string, double> hostAbsUtil;
  std::map<std::string, int> hostTaskCount;
  for (auto& [task, host] : *solution.assignment()) {
    hostAbsUtil[host] += taskCpu[task];
    ++hostTaskCount[host];
  }

  EXPECT_GT(hostTaskCount["host0"], 0);
  EXPECT_GT(hostTaskCount["host1"], 0);
  const double avgDemandHost0 = hostAbsUtil["host0"] / hostTaskCount["host0"];
  const double avgDemandHost1 = hostAbsUtil["host1"] / hostTaskCount["host1"];
  EXPECT_NEAR(5.5, avgDemandHost0, 1e-8);
  EXPECT_NEAR(5.5, avgDemandHost1, 1e-8);
}

TEST_P(BalanceTest, CapacityPerItemVariance) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(10)) {
    initialAssignment["host0"].push_back(fmt::format("heavy{}", i));
  }
  for (const auto i : folly::irange(10)) {
    initialAssignment["host1"].push_back(fmt::format("light{}", i));
  }
  solver->setAssignment(initialAssignment);

  std::map<std::string, double> taskCpu;
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("heavy{}", i)] = 10;
  }
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("light{}", i)] = 1;
  }
  solver->addObjectDimension("cpu", taskCpu);

  solver->addContainerDimension(
      "cpu", std::map<std::string, double>{{"host0", 1000}, {"host1", 1000}});

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.balanceMetric() = BalanceSpecMetric::CAPACITY_PER_ITEM;
  spec.formula() = BalanceSpecFormula::RELATIVE_UTIL_VARIANCE;
  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  std::map<std::string, double> hostAbsUtil;
  std::map<std::string, int> hostTaskCount;
  for (auto& [task, host] : *solution.assignment()) {
    hostAbsUtil[host] += taskCpu[task];
    ++hostTaskCount[host];
  }

  EXPECT_GT(hostTaskCount["host0"], 0);
  EXPECT_GT(hostTaskCount["host1"], 0);
  const double avgDemandHost0 = hostAbsUtil["host0"] / hostTaskCount["host0"];
  const double avgDemandHost1 = hostAbsUtil["host1"] / hostTaskCount["host1"];
  EXPECT_NEAR(5.5, avgDemandHost0, 1e-8);
  EXPECT_NEAR(5.5, avgDemandHost1, 1e-8);
}

TEST_P(BalanceTest, CapacityPerItemWithZeroObjectsScopeItem) {
  // A scope item constrained to hold zero objects should not cause division
  // by zero. The max(1, numObjects) guard in the expression tree ensures
  // capPerItem evaluates to 0 for empty scope items.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(10)) {
    initialAssignment["host0"].push_back(fmt::format("heavy{}", i));
  }
  for (const auto i : folly::irange(10)) {
    initialAssignment["host1"].push_back(fmt::format("light{}", i));
  }
  initialAssignment["host2"] = {};
  solver->setAssignment(initialAssignment);

  // Prevent any objects from being assigned to host2.
  auto freeSpec = ToFreeSpec();
  freeSpec.containers() = {"host2"};
  solver->addConstraint(freeSpec);

  std::map<std::string, double> taskCpu;
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("heavy{}", i)] = 10;
  }
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("light{}", i)] = 1;
  }
  solver->addObjectDimension("cpu", taskCpu);

  solver->addContainerDimension(
      "cpu",
      std::map<std::string, double>{
          {"host0", 1000}, {"host1", 1000}, {"host2", 1000}});

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.balanceMetric() = BalanceSpecMetric::CAPACITY_PER_ITEM;
  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  // host2 must remain empty due to ToFreeSpec constraint.
  std::map<std::string, double> hostAbsUtil;
  std::map<std::string, int> hostTaskCount;
  for (auto& [task, host] : *solution.assignment()) {
    hostAbsUtil[host] += taskCpu[task];
    ++hostTaskCount[host];
  }

  EXPECT_EQ(0, hostTaskCount["host2"]);
  EXPECT_GT(hostTaskCount["host0"], 0);
  EXPECT_GT(hostTaskCount["host1"], 0);

  // TODO: Assert equal capPerItem on host0 and host1. Currently the LINEAR
  // formula with an empty scope item (capPerItem=0) skews the threshold,
  // causing the solver to NOT equalize the non-empty hosts. Consider excluding
  // empty scope items from the threshold calculation.
}

TEST_P(BalanceTest, CapacityPerItemIgnoresIrrelevantObjects) {
  // 40 total objects: 20 with cpu > 0 (the "partition"), 20 with cpu = 0.
  // Objects with cpu=0 contribute nothing to absUtil, so the solver has no
  // incentive to move them. The balance formula should produce the same
  // equilibrium for relevant objects regardless of irrelevant object placement.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // Initial: all heavy on host0, all light on host1, irrelevant split evenly.
  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(10)) {
    initialAssignment["host0"].push_back(fmt::format("heavy{}", i));
  }
  for (const auto i : folly::irange(10)) {
    initialAssignment["host1"].push_back(fmt::format("light{}", i));
  }
  for (const auto i : folly::irange(15)) {
    initialAssignment["host0"].push_back(fmt::format("irrelevant{}", i));
  }
  for (const auto i : folly::irange(15, 20)) {
    initialAssignment["host1"].push_back(fmt::format("irrelevant{}", i));
  }
  solver->setAssignment(initialAssignment);

  std::map<std::string, double> taskCpu;
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("heavy{}", i)] = 10;
  }
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("light{}", i)] = 1;
  }
  for (const auto i : folly::irange(20)) {
    taskCpu[fmt::format("irrelevant{}", i)] = 0;
  }
  solver->addObjectDimension("cpu", taskCpu);

  solver->addContainerDimension(
      "cpu", std::map<std::string, double>{{"host0", 1000}, {"host1", 1000}});

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.balanceMetric() = BalanceSpecMetric::CAPACITY_PER_ITEM;
  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();

  // Only objects with cpu > 0 should count toward capPerItem.
  // Total relevant absUtil = 10*10 + 10*1 = 110, spread across 20 relevant
  // objects. Balanced state: avgDemand = 5.5 per relevant object on each host.
  std::map<std::string, double> hostAbsUtil;
  std::map<std::string, int> hostRelevantCount;
  for (auto& [task, host] : *solution.assignment()) {
    if (taskCpu[task] > 0) {
      hostAbsUtil[host] += taskCpu[task];
      ++hostRelevantCount[host];
    }
  }

  EXPECT_GT(hostRelevantCount["host0"], 0);
  EXPECT_GT(hostRelevantCount["host1"], 0);

  const double avgDemandHost0 =
      hostAbsUtil["host0"] / hostRelevantCount["host0"];
  const double avgDemandHost1 =
      hostAbsUtil["host1"] / hostRelevantCount["host1"];
  EXPECT_NEAR(5.5, avgDemandHost0, 1e-8);
  EXPECT_NEAR(5.5, avgDemandHost1, 1e-8);
}

TEST_P(BalanceTest, CapacityPerItemWithItemCountDimension) {
  // 40 objects: 10 heavy (cpu=10), 10 light (cpu=1), 20 irrelevant (cpu=0).
  // Irrelevant objects are placed unevenly: 20 on host0, 0 on host1.
  // Since irrelevant objects have cpu=0, the solver won't move them.
  //
  // Without capacityPerItemCountDimension: capPerItem = absUtil / allObjects,
  // so the denominators differ (30 vs 10), and "balanced" means unequal
  // absUtil. With capacityPerItemCountDimension: capPerItem = absUtil /
  // relevantCount, so the denominators are equal (10 vs 10), and balanced means
  // equal absUtil → avgDemand = 5.5 on each host.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(10)) {
    initialAssignment["host0"].push_back(fmt::format("heavy{}", i));
  }
  for (const auto i : folly::irange(10)) {
    initialAssignment["host1"].push_back(fmt::format("light{}", i));
  }
  for (const auto i : folly::irange(20)) {
    initialAssignment["host0"].push_back(fmt::format("irrelevant{}", i));
  }
  solver->setAssignment(initialAssignment);

  std::map<std::string, double> taskCpu;
  std::map<std::string, double> relevantCount;
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("heavy{}", i)] = 10;
    relevantCount[fmt::format("heavy{}", i)] = 1;
  }
  for (const auto i : folly::irange(10)) {
    taskCpu[fmt::format("light{}", i)] = 1;
    relevantCount[fmt::format("light{}", i)] = 1;
  }
  for (const auto i : folly::irange(20)) {
    taskCpu[fmt::format("irrelevant{}", i)] = 0;
    relevantCount[fmt::format("irrelevant{}", i)] = 0;
  }
  solver->addObjectDimension("cpu", taskCpu);
  solver->addObjectDimension("relevant_count", relevantCount);

  solver->addContainerDimension(
      "cpu", std::map<std::string, double>{{"host0", 1000}, {"host1", 1000}});

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.balanceMetric() = BalanceSpecMetric::CAPACITY_PER_ITEM;
  spec.capacityPerItemCountDimension() = "relevant_count";
  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());
  auto solution = solver->solve();

  std::map<std::string, double> hostAbsUtil;
  std::map<std::string, int> hostRelevantCount;
  for (auto& [task, host] : *solution.assignment()) {
    if (taskCpu[task] > 0) {
      hostAbsUtil[host] += taskCpu[task];
      ++hostRelevantCount[host];
    }
  }

  EXPECT_GT(hostRelevantCount["host0"], 0);
  EXPECT_GT(hostRelevantCount["host1"], 0);

  const double avgDemandHost0 =
      hostAbsUtil["host0"] / hostRelevantCount["host0"];
  const double avgDemandHost1 =
      hostAbsUtil["host1"] / hostRelevantCount["host1"];
  EXPECT_NEAR(5.5, avgDemandHost0, 1e-8);
  EXPECT_NEAR(5.5, avgDemandHost1, 1e-8);
}
