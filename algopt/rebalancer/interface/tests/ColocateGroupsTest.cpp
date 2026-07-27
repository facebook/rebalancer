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
#include "algopt/rebalancer/tests/SolverTestUtils.h"

#include <fmt/core.h>
#include <folly/container/irange.h>
#include <folly/Portability.h>
#include <folly/Random.h>
#include <folly/String.h>
#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <tuple>

using facebook::algopt::isSolverUnavailable;
using facebook::algopt::solverName;
using facebook::algopt::testSolverPackages;
using namespace facebook::rebalancer::interface;

class ColocateGroupsTest
    : public ::testing::TestWithParam<std::tuple<int, OptimalSolverPackage>> {
 protected:
  void SetUp() override {
    const auto [threads, solverPkg] = GetParam();
    if (isSolverUnavailable(solverPkg)) {
      GTEST_SKIP() << solverName(solverPkg) << " solver not available";
    }
    if (folly::kIsSanitizeThread && solverPkg == OptimalSolverPackage::GUROBI) {
      // TSAN data races are from inside libgurobi and apparently known false
      // positives; see T236804704.
      GTEST_SKIP() << "Skipping Gurobi under TSAN (T236804704)";
    }
  }
};

INSTANTIATE_TEST_CASE_P(
    NumThreads,
    ColocateGroupsTest,
    ::testing::Combine(testThreadCounts(), testSolverPackages()));

class ColocateGroupsLocalSearchTest : public ::testing::TestWithParam<int> {};

INSTANTIATE_TEST_CASE_P(
    NumThreads,
    ColocateGroupsLocalSearchTest,
    testThreadCounts());

TEST_P(ColocateGroupsTest, PutSameGroupObjectsInSameContainer) {
  // In this problem we have 9 tasks initially placed randomly in 3 hosts. Tasks
  // are divided into equally sized jobs. We impose that tasks of the same job
  // must be placed in the same host with a ColocateGroupsSpec.
  auto [threads, solverPkg] = GetParam();
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // There are 9 tasks and 3 hosts. The initial assignment is random.
  std::map<std::string, std::vector<std::string>> initialAssignment = {
      {"host0", {}},
      {"host1", {}},
      {"host2", {}},
  };
  for (const auto i : folly::irange(9)) {
    auto task = fmt::format("task{}", i);
    auto host = fmt::format("host{}", folly::Random::rand32(0, 3));
    initialAssignment[host].push_back(task);
  }
  solver->setAssignment(initialAssignment);

  // Tasks are evenly split into 3 jobs.
  solver->addPartition(
      "job",
      std::map<std::string, std::string>{
          {"task0", "job0"},
          {"task1", "job0"},
          {"task2", "job0"},
          {"task3", "job1"},
          {"task4", "job1"},
          {"task5", "job1"},
          {"task6", "job2"},
          {"task7", "job2"},
          {"task8", "job2"},
      });

  // Tasks of the same job must be placed in the same host.
  ColocateGroupsSpec colocateGroupsSpec;
  colocateGroupsSpec.name() = "test";
  colocateGroupsSpec.scope() = "host";
  colocateGroupsSpec.partitionName() = "job";
  solver->addConstraint(colocateGroupsSpec);

  // Use the optimal solver, rather than the default (local search).
  OptimalSolverSpec optimalSpec;
  optimalSpec.solverPackage() = solverPkg;
  solver->addSolver(optimalSpec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // We expect each job to be fully contained within a single host.
  std::map<std::string, std::set<std::string>> jobToHosts;
  for (const auto i : folly::irange(9)) {
    auto task = fmt::format("task{}", i);
    auto job = fmt::format("job{}", i / 3);
    const auto& host = assignment.at(task);
    jobToHosts[job].insert(host);
  }
  EXPECT_EQ(1, jobToHosts["job0"].size());
  EXPECT_EQ(1, jobToHosts["job1"].size());
  EXPECT_EQ(1, jobToHosts["job2"].size());
}

TEST_P(ColocateGroupsTest, NonDefaultLimit) {
  // In this example we show how setting a limit larger than 1 forces a group to
  // be spread across at most that number of containers, even in the presence of
  // a goal that would benefit from wider spread.
  auto [threads, solverPkg] = GetParam();
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // There are 4 hosts and 4 tasks, all of them initially in host0.
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1", "task2", "task3"}},
          {"host1", {}},
          {"host2", {}},
          {"host3", {}},
      });

  // To keep the example simple, all tasks are grouped into the same job.
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{
          {"task0", "job0"},
          {"task1", "job0"},
          {"task2", "job0"},
          {"task3", "job0"},
      });

  // A job can only be spread across 2 different hosts.
  ColocateGroupsSpec colocateGroupsSpec;
  colocateGroupsSpec.name() = "test";
  colocateGroupsSpec.scope() = "host";
  colocateGroupsSpec.partitionName() = "job";
  colocateGroupsSpec.limits()->globalLimit() = 2;

  solver->addConstraint(colocateGroupsSpec);

  // A balance goal adds an incentive to spread tasks across the largest number
  // of hosts possible.
  BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "task_count";
  balanceSpec.formula() = BalanceSpecFormula::MAX;

  solver->addGoal(balanceSpec);

  // Use the optimal solver, rather than the default (local search).
  OptimalSolverSpec optimalSpec;
  optimalSpec.solverPackage() = solverPkg;
  solver->addSolver(optimalSpec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // Even though the balance goal would benefit from placing tasks in all 4
  // hosts, the colocate groups constraint is limiting the spread to 2 hosts.
  std::set<std::string> nonEmptyHosts;
  for (auto& [_task, host] : assignment) {
    nonEmptyHosts.insert(host);
  }
  EXPECT_EQ(2, nonEmptyHosts.size());
}

TEST_P(ColocateGroupsTest, ScopeItemWeights) {
  // In this example we use scope item weights to express that different hosts
  // are penalized differently by the ColocateGroups spec when a group is
  // present in them. We also add Balance and AssignmentAffinities goals to make
  // the optimal solution unique.
  auto [threads, solverPkg] = GetParam();
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // There are 6 tasks and 3 hosts. All tasks are initially in host0.
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1", "task2", "task3", "task4", "task5"}},
          {"host1", {}},
          {"host2", {}},
      });

  // Tasks are divided into 2 equally sized jobs.
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{
          {"task0", "job0"},
          {"task1", "job0"},
          {"task2", "job0"},
          {"task3", "job1"},
          {"task4", "job1"},
          {"task5", "job1"},
      });

  // Each job can be spread across at most 3 weighted hosts. host0 and host2
  // have a weight of 2, and host1 has a default weight of 1. For example,
  // a job could be spread across host0 (2) and host1 (1) and stay within the
  // limit (3), but the same job can't be spread across host0 (2) and host2 (2)
  // because the sum of weights (4) would exceed the limit (3) in that case.
  ColocateGroupsSpec colocateGroupsSpec;
  colocateGroupsSpec.name() = "test";
  colocateGroupsSpec.scope() = "host";
  colocateGroupsSpec.partitionName() = "job";
  colocateGroupsSpec.limits()->globalLimit() = 3;
  colocateGroupsSpec.scopeItemWeights() = {
      {"host0", 2},
      {"host2", 2},
  };
  solver->addConstraint(colocateGroupsSpec);

  // Prefer a solution where each host contains the same number of tasks.
  BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "task_count";
  balanceSpec.formula() = BalanceSpecFormula::MAX;
  solver->addGoal(balanceSpec, /*weight=*/1000);

  // Task0 prefers host0 and host2, and other job0 tasks prefer host0. If the
  // constraint with weights is applied correctly, then job0 will occupy only
  // task0 and task1, since occupying task0 and task2 violates the limit
  AssignmentAffinitiesSpec assignmentAffinitiesSpec;
  assignmentAffinitiesSpec.scope() = "host";
  assignmentAffinitiesSpec.affinities() = {
      makeAssignmentAffinity("task0", "host0", 1),
      makeAssignmentAffinity("task0", "host2", 1),
      makeAssignmentAffinity("task1", "host0", 1),
      makeAssignmentAffinity("task2", "host0", 1),
  };
  solver->addGoal(assignmentAffinitiesSpec);

  // Use the optimal solver, rather than the default (local search).
  OptimalSolverSpec optimalSpec;
  optimalSpec.solverPackage() = solverPkg;
  solver->addSolver(optimalSpec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // The only optima solution places 2 tasks in each host. job0 is spread
  // across host0 and host1, while job1 is spread across host1 and host2.
  std::map<std::string, std::map<std::string, int>> jobToHostCount;
  for (const auto i : folly::irange(6)) {
    auto task = fmt::format("task{}", i);
    auto job = fmt::format("job{}", i / 3);
    const auto& host = assignment.at(task);
    ++jobToHostCount[job][host];
  }
  EXPECT_EQ(2, jobToHostCount["job0"]["host0"]);
  EXPECT_EQ(1, jobToHostCount["job0"]["host1"]);
  EXPECT_EQ(0, jobToHostCount["job0"]["host2"]);
  EXPECT_EQ(0, jobToHostCount["job1"]["host0"]);
  EXPECT_EQ(1, jobToHostCount["job1"]["host1"]);
  EXPECT_EQ(2, jobToHostCount["job1"]["host2"]);
}

TEST_P(ColocateGroupsTest, PrimarySecondaryAndDependent) {
  // In this example we define triplets of tasks:
  // (primary{i}, secondary{i}, dependent{i})
  // We require primary and secondary tasks with the same ID to be placed in
  // different hosts. The dependent task has to be placed in the same host as
  // either the primary or the secondary with the same ID.
  auto [threads, solverPkg] = GetParam();
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // There are 2 triplets of tasks and 4 hosts. Initially all tasks are placed
  // in host0.
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0",
           {
               "primary0",
               "secondary0",
               "dependent0",
               "primary1",
               "secondary1",
               "dependent1",
           }},
          {"host1", {}},
          {"host2", {}},
          {"host3", {}},
      });

  // First, we require primaries and secondaries to be placed in separate hosts.
  // We can achieve this by grouping primary and secondary tasks by ID, and then
  // imposing that at most 1 task of each group can exist in the same host with
  // a GroupCount constraint.
  solver->addPartition(
      "primary_and_secondary_tasks_by_id",
      std::unordered_map<std::string, std::string>{
          {"primary0", "group0"},
          {"secondary0", "group0"},
          {"primary1", "group1"},
          {"secondary1", "group1"},
      });

  GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "host";
  groupCountSpec.partitionName() = "primary_and_secondary_tasks_by_id";

  Limit limit;
  limit.type() = LimitType::ABSOLUTE;
  limit.globalLimit() = 1;
  groupCountSpec.limit() = limit;

  solver->addConstraint(groupCountSpec);

  // Secondly, we require that dependent tasks must coexist in the same host
  // with either the primary or the secondary task with the same ID. A different
  // way of phrasing the same is: a given triplet (primary{i}, secondary{i},
  // dependent{i}) must be spread across no more than 2 different host. We can
  // constrain this property with a ColocateGroups constraint.
  solver->addPartition(
      "tasks_by_id",
      std::map<std::string, std::string>{
          {"primary0", "group0"},
          {"secondary0", "group0"},
          {"dependent0", "group0"},
          {"primary1", "group1"},
          {"secondary1", "group1"},
          {"dependent1", "group1"},
      });

  ColocateGroupsSpec colocateGroupsSpec;
  colocateGroupsSpec.scope() = "host";
  colocateGroupsSpec.partitionName() = "tasks_by_id";
  colocateGroupsSpec.limits()->globalLimit() = 2;
  solver->addConstraint(colocateGroupsSpec);

  // Use the optimal solver, rather than the default (local search).
  OptimalSolverSpec optimalSpec;
  optimalSpec.solverPackage() = solverPkg;
  solver->addSolver(optimalSpec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // We expect primary and secondary tasks with the same ID to be placed in
  // different hosts.
  EXPECT_TRUE(assignment.at("primary0") != assignment.at("secondary0"));
  EXPECT_TRUE(assignment.at("primary1") != assignment.at("secondary1"));

  // We also expect dependent tasks to be placed in the same host as either
  // the primary or the secondary task with the same ID.
  EXPECT_TRUE(
      assignment.at("dependent0") == assignment.at("primary0") ||
      assignment.at("dependent0") == assignment.at("secondary0"));
  EXPECT_TRUE(
      assignment.at("dependent1") == assignment.at("primary1") ||
      assignment.at("dependent1") == assignment.at("secondary1"));
}

TEST_P(ColocateGroupsTest, MinOverMax) {
  // In this test we define 2 contradicting specs: a minimum limit of 5 as a
  // constraint, and a maximum limit of 3 as a goal. The constraint wins, but
  // the goal keeps the limit at exactly 5 and not over.
  auto [threads, solverPkg] = GetParam();
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("task");
  solver->setContainerName("host");

  const std::map<std::string, std::vector<std::string>> initialAssignment = {
      {"host0", {"task0", "task1", "task2", "task3", "task4", "task5"}},
      {"host1", {}},
      {"host2", {}},
      {"host3", {}},
      {"host4", {}},
      {"host5", {}},
  };
  solver->setAssignment(initialAssignment);

  solver->addPartition(
      "job",
      std::map<std::string, std::string>{
          {"task0", "job0"},
          {"task1", "job0"},
          {"task2", "job0"},
          {"task3", "job0"},
          {"task4", "job0"},
          {"task5", "job0"},
      });

  // Constraint: jobs must spread across at least 5 different hosts.
  {
    ColocateGroupsSpec spec;
    spec.scope() = "host";
    spec.partitionName() = "job";
    spec.limits()->globalLimit() = 5;
    spec.bound() = ColocateGroupsSpecBound::MIN;
    solver->addConstraint(spec);
  }

  // Goal: jobs must spread across at most 3 different hosts.
  {
    ColocateGroupsSpec spec;
    spec.scope() = "host";
    spec.partitionName() = "job";
    spec.limits()->globalLimit() = 3;
    spec.bound() = ColocateGroupsSpecBound::MAX;
    solver->addGoal(spec);
  }

  // Use the optimal solver, rather than the default (local search).
  OptimalSolverSpec optimalSpec;
  optimalSpec.solverPackage() = solverPkg;
  solver->addSolver(optimalSpec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // We expect the only job to be spread across exactly 5 hosts.
  std::set<std::string> usedHosts;
  for (auto& [taskName, hostName] : assignment) {
    usedHosts.insert(hostName);
  }
  EXPECT_EQ(5, usedHosts.size());
}

TEST_P(ColocateGroupsTest, MaxOverMin) {
  // In this test we define 2 contradicting specs: a maximum limit of 3 as a
  // constraint, and a maximum limit of 5 as a goal. The constraint wins, but
  // the goal keeps the limit at exactly 3 and not under.
  auto [threads, solverPkg] = GetParam();
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("task");
  solver->setContainerName("host");

  const std::map<std::string, std::vector<std::string>> initialAssignment = {
      {"host0", {"task0", "task1", "task2", "task3", "task4", "task5"}},
      {"host1", {}},
      {"host2", {}},
      {"host3", {}},
      {"host4", {}},
      {"host5", {}},
  };
  solver->setAssignment(initialAssignment);

  solver->addPartition(
      "job",
      std::map<std::string, std::string>{
          {"task0", "job0"},
          {"task1", "job0"},
          {"task2", "job0"},
          {"task3", "job0"},
          {"task4", "job0"},
          {"task5", "job0"},
      });

  // Constraint: jobs must spread across at most 3 different hosts.
  {
    ColocateGroupsSpec spec;
    spec.scope() = "host";
    spec.partitionName() = "job";
    spec.limits()->globalLimit() = 3;
    spec.bound() = ColocateGroupsSpecBound::MAX;
    solver->addConstraint(spec);
  }

  // Goal: jobs must spread across at least 5 different hosts.
  {
    ColocateGroupsSpec spec;
    spec.scope() = "host";
    spec.partitionName() = "job";
    spec.limits()->globalLimit() = 5;
    spec.bound() = ColocateGroupsSpecBound::MIN;
    solver->addGoal(spec);
  }

  // Use the optimal solver, rather than the default (local search).
  OptimalSolverSpec optimalSpec;
  optimalSpec.solverPackage() = solverPkg;
  solver->addSolver(optimalSpec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // We expect the only job to be spread across exactly 3 hosts.
  std::set<std::string> usedHosts;
  for (auto& [taskName, hostName] : assignment) {
    usedHosts.insert(hostName);
  }
  EXPECT_EQ(3, usedHosts.size());
}

TEST_P(ColocateGroupsTest, ExactConstraintWithVariableLimits) {
  // In this problem we have 10 tasks initially placed in host0. There are three
  // groups, job0, job1, job2, where job0 and job1 have 3 tasks each, and job2
  // has 4 tasks. We impose that job0, job1, and job2 must be exactly spread
  // over 2, 3, and 4 hosts, respectively.
  auto [threads, solverPkg] = GetParam();
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // There are 9 tasks and 3 hosts. All tasks are in host0
  const std::map<std::string, std::vector<std::string>> initialAssignment = {
      {
          "host0",
          {"task0",
           "task1",
           "task2",
           "task3",
           "task4",
           "task5",
           "task6",
           "task7",
           "task8",
           "task9"},
      },
      {"host1", {}},
      {"host2", {}},
      {"host3", {}},
  };

  solver->setAssignment(initialAssignment);

  std::map<std::string, std::string> taskToJob = {
      {"task0", "job0"},
      {"task1", "job0"},
      {"task2", "job0"},
      {"task3", "job1"},
      {"task4", "job1"},
      {"task5", "job1"},
      {"task6", "job2"},
      {"task7", "job2"},
      {"task8", "job2"},
      {"task9", "job2"},
  };
  solver->addPartition("job", taskToJob);

  {
    ColocateGroupsSpec colocateGroupsSpec;
    colocateGroupsSpec.name() = "min constraint";
    colocateGroupsSpec.scope() = "host";
    colocateGroupsSpec.partitionName() = "job";
    colocateGroupsSpec.limits()->globalLimit() = 3.0;
    colocateGroupsSpec.limits()->groupLimits() = {{"job0", 2.0}, {"job2", 4.0}};
    colocateGroupsSpec.bound() = ColocateGroupsSpecBound::MIN;
    solver->addConstraint(colocateGroupsSpec);
  }

  {
    ColocateGroupsSpec colocateGroupsSpec;
    colocateGroupsSpec.name() = "max constraint";
    colocateGroupsSpec.scope() = "host";
    colocateGroupsSpec.partitionName() = "job";
    colocateGroupsSpec.limits()->globalLimit() = 4.0;
    colocateGroupsSpec.limits()->groupLimits() = {{"job0", 2.0}, {"job1", 3.0}};
    colocateGroupsSpec.bound() = ColocateGroupsSpecBound::MAX;
    solver->addConstraint(colocateGroupsSpec);
  }

  // Use the optimal solver, rather than the default (local search).
  OptimalSolverSpec optimalSpec;
  optimalSpec.solverPackage() = solverPkg;
  solver->addSolver(optimalSpec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // We expect the number of hosts job0, job1, and job2 are in to be exactly 2,
  // 3, and 4, respectively
  std::map<std::string, std::set<std::string>> jobToHosts;
  for (const auto i : folly::irange(10)) {
    auto task = fmt::format("task{}", i);
    const auto& job = taskToJob.at(task);
    const auto& host = assignment.at(task);
    jobToHosts[job].insert(host);
  }
  EXPECT_EQ(2, jobToHosts["job0"].size());
  EXPECT_EQ(3, jobToHosts["job1"].size());
  EXPECT_EQ(4, jobToHosts["job2"].size());
}

TEST_P(ColocateGroupsTest, CustomDimension) {
  auto [threads, solverPkg] = GetParam();
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // There are 6 tasks, iniially assigned to 2 hosts.
  solver->setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task2", "task3"}},
          {"host1", {"task1", "task4", "task5"}}});

  // Tasks are divided into 2 equally sized jobs.
  solver->addPartition(
      "job",
      std::map<std::string, std::string>{
          {"task0", "job0"},
          {"task1", "job0"},
          {"task2", "job0"},
          {"task3", "job1"},
          {"task4", "job1"},
          {"task5", "job1"},
      });

  solver->addObjectDimension(
      "weight", std::unordered_map<std::string, double>{{"task1", 0.0}}, 1.0);

  // We want to minimize the total movement of tasks.
  solver->addGoal(MinimizeMovementSpec{});

  ColocateGroupsSpec colocateGroupsSpec;
  colocateGroupsSpec.name() = "test";
  colocateGroupsSpec.scope() = "host";
  colocateGroupsSpec.dimension() = "weight";
  colocateGroupsSpec.partitionName() = "job";
  colocateGroupsSpec.limits()->globalLimit() = 1;
  solver->addConstraint(colocateGroupsSpec);

  // Use the optimal solver, rather than the default (local search).
  OptimalSolverSpec optimalSpec;
  optimalSpec.solverPackage() = solverPkg;
  solver->addSolver(optimalSpec);

  // Without a custom dimension, the optimal solver should generate 2 moves,
  // task1 to host0 and task3 to host1. However, with the custom dimension,
  // where task1 has a weight of 0, we should not see it move.
  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  std::map<std::string, std::map<std::string, int>> jobToHostCount;
  for (const auto i : folly::irange(6)) {
    auto task = fmt::format("task{}", i);
    auto job = fmt::format("job{}", i / 3);
    const auto& host = assignment.at(task);
    ++jobToHostCount[job][host];
  }

  EXPECT_EQ(2, jobToHostCount["job0"]["host0"]);
  EXPECT_EQ(1, jobToHostCount["job0"]["host1"]);
  EXPECT_EQ(0, jobToHostCount["job1"]["host0"]);
  EXPECT_EQ(3, jobToHostCount["job1"]["host1"]);
}

TEST_P(ColocateGroupsTest, CustomDynamicDimension) {
  auto [threads, solverPkg] = GetParam();
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // There are 6 tasks, iniially assigned to 2 hosts.
  solver->setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task2", "task3"}},
          {"host1", {"task1", "task4", "task5"}}});

  // Tasks are divided into 2 equally sized jobs.
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{
          {"task0", "job0"},
          {"task1", "job0"},
          {"task2", "job0"},
          {"task3", "job1"},
          {"task4", "job1"},
          {"task5", "job1"},
      });

  solver->addDynamicObjectDimension(
      "weight",
      "host",
      folly::F14FastMap<std::string, std::map<std::string, double>>{
          {"host0", {{"task1", 1}, {"task3", 1}}},
          {"host1", {{"task1", 0}, {"task3", 0}}}},
      1.0);

  // We want to minimize the total movement of tasks.
  solver->addGoal(MinimizeMovementSpec{});

  ColocateGroupsSpec colocateGroupsSpec;
  colocateGroupsSpec.name() = "test";
  colocateGroupsSpec.scope() = "host";
  colocateGroupsSpec.dimension() = "weight";
  colocateGroupsSpec.partitionName() = "job";
  colocateGroupsSpec.limits()->globalLimit() = 1;
  solver->addConstraint(colocateGroupsSpec);

  // Use the optimal solver, rather than the default (local search).
  OptimalSolverSpec optimalSpec;
  optimalSpec.solverPackage() = solverPkg;
  solver->addSolver(optimalSpec);

  // With just the count dimension, the optimal solver should generate 2 moves,
  // task1 to host0 and task3 to host1. However, with the custom dynamic
  // dimension, where task1 has a weight of 0 for host1, we should not see it
  // move. Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  std::map<std::string, std::map<std::string, int>> jobToHostCount;
  for (const auto i : folly::irange(6)) {
    auto task = fmt::format("task{}", i);
    auto job = fmt::format("job{}", i / 3);
    const auto& host = assignment.at(task);
    ++jobToHostCount[job][host];
  }

  EXPECT_EQ(2, jobToHostCount["job0"]["host0"]);
  EXPECT_EQ(1, jobToHostCount["job0"]["host1"]);
  EXPECT_EQ(0, jobToHostCount["job1"]["host0"]);
  EXPECT_EQ(3, jobToHostCount["job1"]["host1"]);
}

TEST_P(ColocateGroupsLocalSearchTest, LocalSearchFriendliness) {
  // In this problem we have 9 tasks initially placed randomly in 3 hosts. Tasks
  // are divided into equally sized jobs. We impose that tasks of the same job
  // must be placed in the same host with a ColocateGroupsSpec.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // There are 10 tasks and 3 hosts. The initial assignment is chosen so that
  // single moves won't make local progress without continuous penalty
  constexpr int numTasks = 10;
  const std::map<std::string, std::vector<std::string>> initialAssignment = {
      {"host0", {"task0", "task1", "task2"}},
      {"host1", {"task3", "task4", "task5", "task6", "task7"}},
      {"host2", {"task8", "task9"}},
  };
  solver->setAssignment(initialAssignment);

  // 10 tasks are evenly split into 2 jobs.
  std::map<std::string, std::string> taskToJob = {};
  for (const auto i : folly::irange(numTasks)) {
    taskToJob[fmt::format("task{}", i)] = fmt::format("job{}", i / 5);
  }
  solver->addPartition("job", taskToJob);

  // Tasks of the same job must be placed in the same host.
  ColocateGroupsSpec colocateGroupsSpec;
  colocateGroupsSpec.name() = "test";
  colocateGroupsSpec.scope() = "host";
  colocateGroupsSpec.partitionName() = "job";
  solver->addConstraint(colocateGroupsSpec);

  // Local search always explores the containers not in objective
  // whereas local search stage solver does not
  // Using local search stage solver to also verify that we can make
  // progress by only using the containers that are affected by
  // colocate group spec.
  LocalSearchSolverSpec localSearchSolverSpec;
  localSearchSolverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  LocalSearchStageSolverSpec localSearchStageSolverSpec;
  LocalSearchStageSpec stageSpec;
  stageSpec.name() = "only stage";
  stageSpec.solverSpec() = std::move(localSearchSolverSpec);
  stageSpec.begin() = 0;
  stageSpec.end() = 1;
  localSearchStageSolverSpec.stageSpecs()->push_back(stageSpec);

  solver->addSolver(localSearchStageSolverSpec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // We expect each job to be fully contained within a single host.
  std::map<std::string, std::set<std::string>> jobToHosts;
  std::map<std::string, int> hostToTaskCount;
  for (const auto& [task, host] : *solution.assignment()) {
    XLOG(INFO) << task << " -> " << host;
    const auto& job = taskToJob.at(task);
    jobToHosts[job].insert(host);
    hostToTaskCount[host]++;
  }

  // Since job0 had more tasks in host0, local search would incentivize moves to
  // host0. Eventually all tasks of job0 would be placed in host0
  EXPECT_EQ(1, jobToHosts["job0"].size());
  EXPECT_EQ("host0", *jobToHosts["job0"].begin());
  // Since job1 had more tasks in host1, local search would incentivize moves to
  // host1. Eventually all tasks of job1 would be placed in host1
  EXPECT_EQ(1, jobToHosts["job1"].size());
  EXPECT_EQ("host1", *jobToHosts["job1"].begin());
  EXPECT_EQ(0, hostToTaskCount["host2"]);
}

TEST_P(ColocateGroupsTest, QuadraticPenaltyForMIPS) {
  auto [threads, solverPkg] = GetParam();
  if (solverPkg == OptimalSolverPackage::HIGHS) {
    GTEST_SKIP() << "HiGHS does not support quadratic expressions";
  }
  // In this problem we have 4 tasks initially placed 2:2 in 2 hosts. We impose
  // that tasks of the same job must be placed in the same host with a
  // ColocateGroupsSpec.
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // There are 4 tasks and 2 hosts. The initial assignment is chosen so that
  // single moves won't make local progress without continuous penalty
  constexpr int numTasks = 4;
  const std::map<std::string, std::vector<std::string>> initialAssignment = {
      {"host0", {"task0", "task1"}}, {"host1", {"task2", "task3"}}};
  solver->setAssignment(initialAssignment);

  // 4 tasks assigned to 1 jobs.
  std::map<std::string, std::string> taskToJob = {};
  for (const auto i : folly::irange(numTasks)) {
    taskToJob[fmt::format("task{}", i)] = fmt::format("job{}", 0);
  }
  solver->addPartition("job", taskToJob);

  // Tasks of the same job must be placed in the same host.
  ColocateGroupsSpec colocateGroupsSpec;
  colocateGroupsSpec.name() = "test";
  colocateGroupsSpec.scope() = "host";
  colocateGroupsSpec.partitionName() = "job";
  colocateGroupsSpec.useContinuousPenaltyWithOptimal() = true;
  solver->addGoal(colocateGroupsSpec, 1, 1);

  GroupMoveLimitSpec groupMoveLimitSpec;
  groupMoveLimitSpec.partitionName() = "job";
  groupMoveLimitSpec.limit()->globalLimit() = 1;
  solver->addConstraint(groupMoveLimitSpec);

  OptimalSolverSpec mipSpec;
  mipSpec.solverPackage() = solverPkg;
  solver->addSolver(mipSpec);
  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // We expect each job to be fully contained within a single host. But that
  // won't happen (even with OptimalSolver) because we can only move at most one
  // object of job0. However, it should still make partial progress towards that
  // goal.
  std::map<std::string, std::set<std::string>> jobToHosts;
  std::map<std::string, int> hostToTaskCount;
  for (const auto& [task, host] : *solution.assignment()) {
    XLOG(INFO) << task << " -> " << host;
    const auto& job = taskToJob.at(task);
    jobToHosts[job].insert(host);
    hostToTaskCount[host]++;
  }

  EXPECT_EQ(2, jobToHosts["job0"].size());
  EXPECT_EQ(1, std::min(hostToTaskCount["host0"], hostToTaskCount["host1"]));
}

TEST_P(ColocateGroupsTest, SpreadWhileMinimizingOverlap) {
  // 4 tenants, each with 4 replicas = 16 objects
  // Assign them to 4 shards (container) such that:
  // * Each tenant is spread across 2 hosts
  // * For every pair of tenants, the number of hosts they overlap in is
  // minimized.
  auto [threads, solverPkg] = GetParam();
  if (solverPkg == OptimalSolverPackage::HIGHS) {
    GTEST_SKIP() << "HiGHS does not support quadratic expressions";
  }
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("object");
  solver->setContainerName("shard");

  const std::map<std::string, std::vector<std::string>> initialAssignment = {
      {"shard0", {"object_0_0", "object_0_1", "object_1_0", "object_1_1"}},
      {"shard1", {"object_0_2", "object_0_3", "object_1_2", "object_1_3"}},
      {"shard2", {"object_2_0", "object_2_1", "object_3_0", "object_3_1"}},
      {"shard3", {"object_2_2", "object_2_3", "object_3_2", "object_3_3"}},
  };
  solver->setAssignment(initialAssignment);

  std::map<std::string, std::string> objectsToTenants;
  std::map<std::string, std::vector<std::string>> tenantToObjects;
  for (const auto i : folly::irange(4)) {
    for (const auto j : folly::irange(4)) {
      auto tenantName = fmt::format("tenant_{}", i);
      auto objName = fmt::format("object_{}_{}", i, j);
      objectsToTenants[objName] = tenantName;
      tenantToObjects[tenantName].push_back(objName);
    }
  }
  solver->addPartition("tenant", tenantToObjects);
  std::map<std::string, std::vector<std::string>> tenantPairsToObjects;
  for (const auto i : folly::irange(4)) {
    for (const auto j : folly::irange(4)) {
      if (i == j) {
        // skip self-pairs, but keep redundant pairs such as (a, b) and (b, a)
        continue;
      }
      auto ti = fmt::format("tenant_{}", i);
      auto tj = fmt::format("tenant_{}", j);
      std::vector<std::string> pairObjects = tenantToObjects.at(ti);
      pairObjects.insert(
          pairObjects.end(),
          tenantToObjects.at(tj).begin(),
          tenantToObjects.at(tj).end());
      tenantPairsToObjects[fmt::format("tenant_{}_{}", i, j)] = pairObjects;
    }
  }
  solver->addPartition("tenant_pairs", tenantPairsToObjects);

  constexpr int numShardsPerTenant = 2;
  int numObjectsPerTenantPerShard = 4 / numShardsPerTenant;
  GroupCountSpec groupCountSpec;
  groupCountSpec.name() = "spread tenants across exactly 2 shards";
  groupCountSpec.scope() = "shard";
  groupCountSpec.partitionName() = "tenant";
  groupCountSpec.dimension() = "object_count";
  groupCountSpec.limit()->globalLimit() = numObjectsPerTenantPerShard;
  groupCountSpec.bound() = GroupCountSpecBound::EXACT;
  groupCountSpec.zeroAllowed() = true;
  solver->addConstraint(groupCountSpec);

  // Trying to capture A intersect B
  ColocateGroupsSpec overlappingScopeItems;
  overlappingScopeItems.name() = "Limit groups with both tenant_i and tenant_j";
  overlappingScopeItems.scope() = "shard";
  overlappingScopeItems.partitionName() = "tenant_pairs";
  overlappingScopeItems.dimension() = "object_count";
  overlappingScopeItems.limits()->globalLimit() = 2 * numShardsPerTenant;
  // UTIL = |scopeItems_with_tenant_i U scopeItems_with_tenant_j|
  // LIMIT = |scopeItems_with_tenant_i| + |scopeItems_with_tenant_j|
  // LIMIT - UTIL = number of scopeItems with both tenant_i and tenant_j
  // Therefore, overlap is represented by (LIMIT - UTIL)
  overlappingScopeItems.bound() = ColocateGroupsSpecBound::MIN;
  overlappingScopeItems.squares() = true;
  solver->addGoal(overlappingScopeItems);

  MinimizeMovementSpec minimizeMovementSpec;
  minimizeMovementSpec.scope() = "shard";
  minimizeMovementSpec.dimension() = "object_count";
  solver->addGoal(minimizeMovementSpec);

  // ManifoldBackupParams manifoldBackupParams;
  // manifoldBackupParams.uploadPolicy() = ManifoldUploadPolicy::ALWAYS;
  // solver->setManifoldBackupParams(manifoldBackupParams);

  // Get a solution from Rebalancer.
  OptimalSolverSpec optimalSolverSpec;
  optimalSolverSpec.solverPackage() = solverPkg;
  solver->addSolver(optimalSolverSpec);
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // We expect each tenant to be spread across at least two shards.
  std::map<std::string, std::set<std::string>> shardToTenants;
  folly::F14FastMap<std::string, std::set<std::string>> tenantToShards;
  for (const auto& [objectName, shard] : assignment) {
    auto tenantName = objectsToTenants.at(objectName);
    shardToTenants[shard].insert(tenantName);
    tenantToShards[tenantName].insert(shard);
  }
  folly::F14FastMap<std::string, int> tenantPairOverlapCount;

  for (const auto& [shard, tenants] : shardToTenants) {
    XLOG(INFO) << fmt::format("{} -> {}", shard, folly::join(", ", tenants));
    std::vector<std::string> tenantNames(tenants.begin(), tenants.end());
    for (const auto i : folly::irange(tenantNames.size())) {
      for (size_t j = i + 1; j < tenantNames.size(); ++j) {
        auto pairName =
            fmt::format("{}_{}", tenantNames.at(i), tenantNames.at(j));
        tenantPairOverlapCount[pairName]++;
      }
    }
  }

  // Check if tenants are spread across exactly 2 shards
  // Check if tenant_i and tenant_j overlap in no more than 1 shard
  for (const auto& [pairName, count] : tenantPairOverlapCount) {
    EXPECT_LE(count, 1);
  }
  for (const auto& [tenantName, shards] : tenantToShards) {
    EXPECT_EQ(numShardsPerTenant, shards.size());
  }
}

TEST_P(ColocateGroupsTest, GroupWeights) {
  // In this test we demonstrate how group weights affect the penalty for
  // violating colocationGroups goal. We have 3 jobs with different priorities:
  // - job0: high priority (weight 3.0) - should be strongly penalized for
  // spreading
  // - job1: low priority (weight 0.5, default) - - should be lightly penalized
  // for spreading
  // - job2: standard priority (weight 1.0)
  auto [threads, solverPkg] = GetParam();
  if (solverPkg == OptimalSolverPackage::HIGHS) {
    GTEST_SKIP() << "HiGHS does not support quadratic expressions";
  }
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // There are 5 tasks and 2 hosts. All tasks are initially in host0.
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1", "task2", "task3", "task4", "task5"}},
          {"host1", {}},
      });

  // Tasks are divided into 3 jobs of 2 tasks each.
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{
          {"task0", "job0"},
          {"task1", "job0"},
          {"task2", "job1"},
          {"task3", "job1"},
          {"task4", "job2"},
          {"task5", "job2"},
      });

  // Balance goal; most important goal
  BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "task_count";
  balanceSpec.formula() = BalanceSpecFormula::IDEAL;
  solver->addGoal(balanceSpec, /*weight=*/1, /*tuplePos=*/0);

  // Each job should ideally be placed in only 1 host, but we different groups
  // have different weights. Also, this is lower priority than the balance goal.
  ColocateGroupsSpec colocateGroupsSpec;
  colocateGroupsSpec.name() = "test";
  colocateGroupsSpec.scope() = "host";
  colocateGroupsSpec.partitionName() = "job";
  colocateGroupsSpec.limits()->globalLimit() = 1;
  // Set different weights for different groups
  colocateGroupsSpec.groupToWeight() = {
      {"job0", 3.0}, // High priority job - heavily penalized for spreading
      {"job1", 0.5}, // Low priority job - lightly penalized for spreading
      // job2 gets default weight of 1.0
  };
  solver->addGoal(colocateGroupsSpec, /*weight=*/1, /*tuplePos=*/1);

  // Use the optimal solver to find the best trade-off
  OptimalSolverSpec optimalSpec;
  optimalSpec.solverPackage() = solverPkg;
  solver->addSolver(optimalSpec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // Count how many hosts each job is spread across
  std::map<std::string, std::set<std::string>> jobToHosts;
  for (const auto i : folly::irange(6)) {
    auto task = fmt::format("task{}", i);
    auto job = fmt::format("job{}", i / 2);
    const auto& host = assignment.at(task);
    jobToHosts[job].insert(host);
  }

  // Since balance is most important goal, we expect that each host will have
  // exactly 3 objects. Since jobs have different weights, we expect:
  // - job0 (weight 3.0) and job2 (weight 1.0) to be colocated.
  // - job1 (weight 0.5) should be spread across 2 hosts to achieve perfect
  // balance
  EXPECT_EQ(1, jobToHosts["job0"].size()) << fmt::format(
      "Expected job0 (weight 3.0) to be colocated, but it is spread across {} hosts",
      jobToHosts["job0"].size());
  EXPECT_EQ(1, jobToHosts["job2"].size()) << fmt::format(
      "Expected job2 (weight 1.0) to be colocated, but it is spread across {} hosts",
      jobToHosts["job2"].size());
  EXPECT_EQ(2, jobToHosts["job1"].size()) << fmt::format(
      "Expected job1 (weight 0.5) to be spread across 2 hosts, but it is spread across {} hosts",
      jobToHosts["job1"].size());
}

// The following tests verify the behavior of ColocateGroupsSpec as a goal vs
// constraint when using local search solver with single move type.
//
// ColocateGroupsSpec is designed to be "local search friendly" - it uses
// continuous penalty to allow incremental progress even with single moves.
// This works for BOTH goals AND constraints.
//
// The key difference between goal and constraint is:
// - Goal: Soft preference that can be traded off against other goals
// - Constraint: Hard requirement that must be satisfied (but still uses
//   continuous penalty to guide local search toward satisfaction)

TEST_P(ColocateGroupsLocalSearchTest, ColocationConstraintWithBalancingGoal) {
  // This test shows that with a balancing goal PLUS colocation constraint,
  // the solver will spread jobs across different hosts (3 tasks per host)
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // Setup: 6 tasks split across 3 hosts (initially not colocated by job).
  const std::map<std::string, std::vector<std::string>> initialAssignment = {
      {"host0", {"task0", "task3"}},
      {"host1", {"task1", "task4"}},
      {"host2", {"task2", "task5"}},
  };
  solver->setAssignment(initialAssignment);

  // Tasks are divided into 2 jobs of 3 tasks each.
  const std::map<std::string, std::string> taskToJob = {
      {"task0", "job0"},
      {"task1", "job0"},
      {"task2", "job0"},
      {"task3", "job1"},
      {"task4", "job1"},
      {"task5", "job1"},
  };
  solver->addPartition("job", taskToJob);

  // Add ColocateGroupsSpec as a CONSTRAINT (each job in at most 1 host).
  ColocateGroupsSpec colocateGroupsSpec;
  colocateGroupsSpec.name() = "colocation_constraint";
  colocateGroupsSpec.scope() = "host";
  colocateGroupsSpec.partitionName() = "job";
  colocateGroupsSpec.limits()->globalLimit() = 1;
  solver->addConstraint(colocateGroupsSpec);

  // Add a balancing goal to spread tasks across hosts.
  // This should encourage putting job0 on one host and job1 on another,
  // rather than all 6 tasks on one host.
  BalanceSpec balanceSpec;
  balanceSpec.name() = "balance_hosts";
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "task_count";
  balanceSpec.formula() = BalanceSpecFormula::MAX;
  solver->addGoal(balanceSpec);

  // Use local search with single move type only.
  LocalSearchSolverSpec localSearchSolverSpec;
  localSearchSolverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  solver->addSolver(localSearchSolverSpec);

  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // Count how many hosts each job is spread across, and tasks per host.
  std::map<std::string, std::set<std::string>> jobToHosts;
  std::map<std::string, int> hostToTaskCount;
  for (const auto& [task, host] : assignment) {
    const auto& job = taskToJob.at(task);
    jobToHosts[job].insert(host);
    hostToTaskCount[host]++;
    XLOGF(INFO, "[WithBalancing] {} ({}) -> {}", task, job, host);
  }

  // Each job should be colocated in exactly 1 host.
  EXPECT_EQ(1, jobToHosts["job0"].size())
      << "Expected job0 to be colocated in 1 host";
  EXPECT_EQ(1, jobToHosts["job1"].size())
      << "Expected job1 to be colocated in 1 host";

  // With balancing, jobs should be on DIFFERENT hosts (not all 6 on one host).
  EXPECT_NE(*jobToHosts["job0"].begin(), *jobToHosts["job1"].begin())
      << "Expected job0 and job1 to be on different hosts due to balancing";
}

TEST_P(ColocateGroupsLocalSearchTest, DrainWithColocationConstraint) {
  // This test verifies behavior when:
  // 1. All tasks are initially colocated on host0 (colocation already
  // satisfied)
  // 2. A ToFreeSpec GOAL wants to drain host0
  // 3. ColocateGroupsSpec is a CONSTRAINT
  //
  // RESULT: With SINGLE moves, drain is BLOCKED because moving any single task
  // would spread a job across 2 hosts, violating the colocation constraint.
  // The colocation constraint is already satisfied (limit=1, both jobs in 1
  // host), so any move that worsens it is rejected.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // Setup: All 6 tasks in host0 (both jobs already colocated).
  const std::map<std::string, std::vector<std::string>> initialAssignment = {
      {"host0", {"task0", "task1", "task2", "task3", "task4", "task5"}},
      {"host1", {}},
      {"host2", {}},
  };
  solver->setAssignment(initialAssignment);

  // Tasks are divided into 2 jobs of 3 tasks each.
  const std::map<std::string, std::string> taskToJob = {
      {"task0", "job0"},
      {"task1", "job0"},
      {"task2", "job0"},
      {"task3", "job1"},
      {"task4", "job1"},
      {"task5", "job1"},
  };
  solver->addPartition("job", taskToJob);

  // Add ColocateGroupsSpec as a CONSTRAINT (each job in at most 1 host).
  ColocateGroupsSpec colocateGroupsSpec;
  colocateGroupsSpec.name() = "colocation_constraint";
  colocateGroupsSpec.scope() = "host";
  colocateGroupsSpec.partitionName() = "job";
  colocateGroupsSpec.limits()->globalLimit() = 1;
  solver->addConstraint(colocateGroupsSpec);

  // Add ToFreeSpec as a GOAL to drain host0.
  ToFreeSpec toFreeSpec;
  toFreeSpec.name() = "drain_host0";
  toFreeSpec.containers() = {"host0"};
  solver->addGoal(toFreeSpec);

  // Use local search with single move type only.
  LocalSearchSolverSpec localSearchSolverSpec;
  localSearchSolverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  solver->addSolver(localSearchSolverSpec);

  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // Count how many hosts each job is spread across, and where tasks are.
  std::map<std::string, std::set<std::string>> jobToHosts;
  int host0TaskCount = 0;
  for (const auto& [task, host] : assignment) {
    const auto& job = taskToJob.at(task);
    jobToHosts[job].insert(host);
    if (host == "host0") {
      host0TaskCount++;
    }
    XLOGF(INFO, "[DrainWithConstraint] {} ({}) -> {}", task, job, host);
  }

  XLOGF(
      INFO,
      "[DrainWithConstraint] host0 has {} tasks (expected: 6, drain blocked)",
      host0TaskCount);

  // Each job should still be colocated in exactly 1 host.
  EXPECT_EQ(1, jobToHosts["job0"].size())
      << "Expected job0 to remain colocated in 1 host";
  EXPECT_EQ(1, jobToHosts["job1"].size())
      << "Expected job1 to remain colocated in 1 host";

  // SINGLE moves CANNOT drain because moving any single task would violate
  // the colocation constraint (job would span 2 hosts).
  // This is the expected behavior - colocation blocks single-task draining.
  EXPECT_EQ(6, host0TaskCount)
      << "Expected drain to be BLOCKED - single moves can't drain "
         "colocated groups without violating colocation";
}

TEST_P(ColocateGroupsLocalSearchTest, DrainWithColocationGoal) {
  // This test shows that with ColocateGroupsSpec as a GOAL:
  // - Colocated tasks stay together (can't drain without breaking colocation)
  // - Non-grouped tasks CAN be drained
  //
  // Setup: 6 tasks on host0
  // - task0, task1, task2: Colocated group (job0)
  // - task3, task4, task5: NOT in any colocation group
  //
  // Stage ordering (lexicographic priority):
  // - Stage 0 (tuplePos 0): Colocation goal - optimized first
  // - Stage 1 (tuplePos 1): Drain goal - cannot worsen stage 0
  //
  // Expected: Non-grouped tasks (task3-5) drained, colocated group stays.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  const std::map<std::string, std::vector<std::string>> initialAssignment = {
      {"host0", {"task0", "task1", "task2", "task3", "task4", "task5"}},
      {"host1", {}},
      {"host2", {}},
  };
  solver->setAssignment(initialAssignment);

  // Only task0-2 are in a colocation group. task3-5 are NOT in any group.
  const std::map<std::string, std::string> taskToJob = {
      {"task0", "job0"},
      {"task1", "job0"},
      {"task2", "job0"},
      // task3, task4, task5 intentionally NOT in partition
  };
  solver->addPartition("job", taskToJob);

  // Add ColocateGroupsSpec as a GOAL (job0 should be in 1 host).
  ColocateGroupsSpec colocateGroupsSpec;
  colocateGroupsSpec.name() = "colocation_goal";
  colocateGroupsSpec.scope() = "host";
  colocateGroupsSpec.partitionName() = "job";
  colocateGroupsSpec.limits()->globalLimit() = 1;
  solver->addGoal(colocateGroupsSpec, /*weight=*/1, /*tuplePos=*/0);

  // Add ToFreeSpec as a GOAL to drain host0.
  ToFreeSpec toFreeSpec;
  toFreeSpec.name() = "drain_host0";
  toFreeSpec.containers() = {"host0"};
  solver->addGoal(toFreeSpec, /*weight=*/1, /*tuplePos=*/1);

  // Use local search stage solver with single move type.
  LocalSearchSolverSpec localSearchSolverSpec;
  localSearchSolverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));

  LocalSearchStageSolverSpec localSearchStageSolverSpec;

  LocalSearchStageSpec stage0;
  stage0.name() = "colocation stage";
  stage0.solverSpec() = localSearchSolverSpec;
  stage0.begin() = 0;
  stage0.end() = 1;
  localSearchStageSolverSpec.stageSpecs()->push_back(stage0);

  LocalSearchStageSpec stage1;
  stage1.name() = "drain stage";
  stage1.solverSpec() = localSearchSolverSpec;
  stage1.begin() = 1;
  stage1.end() = 2;
  localSearchStageSolverSpec.stageSpecs()->push_back(stage1);

  solver->addSolver(localSearchStageSolverSpec);

  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // Count tasks on host0 and track where colocated group ends up.
  std::set<std::string> job0Hosts;
  int host0TaskCount = 0;
  int host0ColocatedCount = 0;
  for (const auto& [task, host] : assignment) {
    if (taskToJob.count(task)) {
      job0Hosts.insert(host);
      if (host == "host0") {
        host0ColocatedCount++;
      }
    }
    if (host == "host0") {
      host0TaskCount++;
    }
    XLOGF(INFO, "[DrainWithGoal] {} -> {}", task, host);
  }

  // Colocated group (job0) should remain together in 1 host.
  EXPECT_EQ(1, job0Hosts.size())
      << "Expected job0 to remain colocated in 1 host";

  // Colocated group stays on host0 (can't move without breaking colocation).
  EXPECT_EQ(3, host0ColocatedCount)
      << "Expected colocated group (task0-2) to remain on host0";

  // Non-grouped tasks (task3-5) should be drained to other hosts.
  // Total on host0 should be 3 (only the colocated group).
  EXPECT_EQ(3, host0TaskCount)
      << "Expected only colocated group on host0 - non-grouped tasks drained";
}

TEST_P(ColocateGroupsLocalSearchTest, MinimalMovesToAchieveColocation) {
  // This test verifies that Rebalancer chooses moves with minimal movement.
  //
  // Setup:
  // - task0 and task1 need to be colocated (same group)
  // - Initial assignment: task0 on host0, task1 on host1, host2 is empty
  //
  // Expected: Rebalancer will move exactly 1 task to colocate them.
  // It will either move task1 to host0 OR move task0 to host1.
  // It will NOT move both tasks to host2 (that would be 2 moves).
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // Initial assignment: task0 on host0, task1 on host1, host2 empty.
  const std::map<std::string, std::vector<std::string>> initialAssignment = {
      {"host0", {"task0"}},
      {"host1", {"task1"}},
      {"host2", {}},
  };
  solver->setAssignment(initialAssignment);

  // Both tasks belong to the same group (must be colocated).
  const std::map<std::string, std::string> taskToJob = {
      {"task0", "group0"},
      {"task1", "group0"},
  };
  solver->addPartition("colocation", taskToJob);

  // Add ColocateGroupsSpec as a CONSTRAINT (group must be in 1 host).
  ColocateGroupsSpec colocateGroupsSpec;
  colocateGroupsSpec.name() = "colocation_constraint";
  colocateGroupsSpec.scope() = "host";
  colocateGroupsSpec.partitionName() = "colocation";
  colocateGroupsSpec.limits()->globalLimit() = 1;
  solver->addConstraint(colocateGroupsSpec);

  // Add MinimizeMovementSpec as a GOAL to prefer minimal moves.
  MinimizeMovementSpec minimizeMovementSpec;
  minimizeMovementSpec.scope() = "host";
  solver->addGoal(minimizeMovementSpec);

  // Use local search stage solver with single move type only.
  LocalSearchSolverSpec localSearchSolverSpec;
  localSearchSolverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  LocalSearchStageSolverSpec localSearchStageSolverSpec;
  LocalSearchStageSpec stageSpec;
  stageSpec.name() = "only stage";
  stageSpec.solverSpec() = std::move(localSearchSolverSpec);
  stageSpec.begin() = 0;
  stageSpec.end() = 1;
  localSearchStageSolverSpec.stageSpecs()->push_back(stageSpec);
  solver->addSolver(localSearchStageSolverSpec);

  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // Log the final assignment.
  for (const auto& [task, host] : assignment) {
    XLOGF(INFO, "[MinimalMoves] {} -> {}", task, host);
  }

  const std::string& host0 = assignment.at("task0");
  const std::string& host1 = assignment.at("task1");

  // Both tasks must be colocated.
  EXPECT_EQ(host0, host1) << "Expected task0 and task1 to be colocated";

  // They should be on either host0 or host1 (not host2).
  // Moving both to host2 would require 2 moves, while moving one to the
  // other's host only requires 1 move.
  EXPECT_TRUE(host0 == "host0" || host0 == "host1")
      << "Expected tasks to stay on host0 or host1, not move both to host2. "
      << "Final host: " << host0;
}

TEST_P(ColocateGroupsLocalSearchTest, OverlappingGroups_TransitiveColocation) {
  // This test verifies that overlapping groups form transitive colocation.
  //
  // Setup:
  // - 4 tasks: taskA, taskB, taskC, taskD
  // - group1: {taskA, taskB} - taskB is in group1
  // - group2: {taskB, taskC} - taskB is also in group2 (overlapping!)
  // - group3: {taskD}        - separate component
  //
  // Initially: taskA, taskD on host0, taskB on host1, taskC on host2
  //
  // Expected: taskA, taskB, taskC all end up on the same host
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // Initial assignment spreads taskA, taskB, taskC across different hosts.
  const std::map<std::string, std::vector<std::string>> initialAssignment = {
      {"host0", {"taskA", "taskD"}},
      {"host1", {"taskB"}},
      {"host2", {"taskC"}},
  };
  solver->setAssignment(initialAssignment);

  // Partition using GroupToObjects overload (map<string, vector<string>>).
  // This allows taskB to be in multiple groups (overlapping).
  const std::map<std::string, std::vector<std::string>> groupToObjects = {
      {"group1", {"taskA", "taskB"}}, // taskB in group1
      {"group2", {"taskB", "taskC"}}, // taskB also in group2 (overlapping!)
      {"group3", {"taskD"}}, // separate component
  };
  solver->addPartition("colocation", groupToObjects);

  // Add ColocateGroupsSpec as a CONSTRAINT (each group in at most 1 host).
  ColocateGroupsSpec colocateGroupsSpec;
  colocateGroupsSpec.name() = "colocation_constraint";
  colocateGroupsSpec.scope() = "host";
  colocateGroupsSpec.partitionName() = "colocation";
  colocateGroupsSpec.limits()->globalLimit() = 1;
  solver->addConstraint(colocateGroupsSpec);

  // Use local search stage solver with single move type only.
  LocalSearchSolverSpec localSearchSolverSpec;
  localSearchSolverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  LocalSearchStageSolverSpec localSearchStageSolverSpec;
  LocalSearchStageSpec stageSpec;
  stageSpec.name() = "only stage";
  stageSpec.solverSpec() = std::move(localSearchSolverSpec);
  stageSpec.begin() = 0;
  stageSpec.end() = 1;
  localSearchStageSolverSpec.stageSpecs()->push_back(stageSpec);
  solver->addSolver(localSearchStageSolverSpec);

  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // Log the final assignment.
  for (const auto& [task, host] : assignment) {
    XLOGF(INFO, "[TransitiveColocation] {} -> {}", task, host);
  }

  // taskA, taskB, taskC should all be on the same host (transitive colocation).
  const std::string& hostA = assignment.at("taskA");
  const std::string& hostB = assignment.at("taskB");
  const std::string& hostC = assignment.at("taskC");
  const std::string& hostD = assignment.at("taskD");

  // taskA,B,C must be colocated.
  EXPECT_EQ(hostA, hostB)
      << "Expected taskA and taskB to be colocated (group1)";
  EXPECT_EQ(hostB, hostC)
      << "Expected taskB and taskC to be colocated (group2)";
  EXPECT_EQ(hostA, hostC)
      << "Expected taskA and taskC to be colocated transitively via taskB";

  // taskD is in a separate group (group3), can be anywhere.
  XLOGF(
      INFO,
      "[TransitiveColocation] taskD is on {} (separate component)",
      hostD);
}
