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

#include <gtest/gtest.h>

#include <map>
#include <string>

using namespace facebook::rebalancer::interface;

class SRBufferCapacityTest
    : public testing::TestWithParam<std::tuple<int, bool>> {
 protected:
  static int getThreadCount() {
    return std::get<0>(GetParam());
  }
};

INSTANTIATE_TEST_SUITE_P(
    NumThreads,
    SRBufferCapacityTest,
    ::testing::Combine(testThreadCounts(), ::testing::Bool()));

TEST_P(SRBufferCapacityTest, Basic) {
  /* In this example we have 6 hosts and 2 tasks. Host1 is buffer host
  hence it should have enough capacity.
  */
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = getThreadCount()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // There are 2 hosts and 6 tasks.
  solver->setAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task1", "task3", "task4", "task5"}},
          {"host1", {"task2"}}});

  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::vector<std::string>>{
          {"job0", {"task0", "task2", "task3"}},
          {"job1", {"task1", "task4", "task5"}}});

  std::map<std::string, double> lowerBoundMatchingErrors = {};

  std::vector<ScopeItemPair> pairs;
  auto pair = ScopeItemPair();
  pair.scopeItem1() = "host0";
  pair.scopeItem2() = "host1";
  pairs.push_back(pair);

  SRBufferCapacitySpec spec;
  spec.name() = "test";
  spec.dimension() = "task_count";
  spec.filter() = Filter();
  spec.lowerBoundMatchingErrors() = lowerBoundMatchingErrors;
  spec.matchingError() = 0.0;
  spec.name() = "name";
  spec.partitionName() = "job";
  spec.scope() = "host";
  spec.scopeItemPairs() = pairs;
  spec.useHeuristics() = false;
  auto useAsObjective = std::get<1>(GetParam());
  if (useAsObjective) {
    solver->addGoal(spec);
  } else {
    solver->addConstraint(spec);
  }

  solver->addSolver(makeDefaultLocalSearchSolver());

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  std::map<std::string, int> hostToTaskCount;
  for (auto& [_, host] : assignment) {
    hostToTaskCount[host]++;
  }

  // ensure one task moved from host0 to host1 in
  // order to satisfy the constraint
  EXPECT_EQ(3, hostToTaskCount["host1"]);
  EXPECT_EQ(3, hostToTaskCount["host0"]);
}

TEST_P(SRBufferCapacityTest, EmptyPartition) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = getThreadCount()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // There no tasks in the problem.
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {}}, {"host1", {}}});

  // Add an empty partition.
  solver->addPartition("job", std::map<std::string, std::string>());

  std::vector<ScopeItemPair> pairs;
  auto pair = ScopeItemPair();
  pair.scopeItem1() = "host0";
  pair.scopeItem2() = "host1";
  pairs.push_back(pair);

  SRBufferCapacitySpec spec;
  spec.dimension() = "task_count";
  spec.partitionName() = "job";
  spec.scope() = "host";
  spec.scopeItemPairs() = pairs;
  auto useAsObjective = std::get<1>(GetParam());
  if (useAsObjective) {
    solver->addGoal(spec);
  } else {
    solver->addConstraint(spec);
  }

  solver->addSolver(makeDefaultLocalSearchSolver());

  // Get a solution from Rebalancer.
  auto solution = solver->solve();

  EXPECT_EQ(0, *solution.initialConstraint()->brokenVal());
}

TEST_P(SRBufferCapacityTest, LocalSearchFriendliness) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = getThreadCount()});
  solver->setObjectName("server");
  solver->setContainerName("container");

  solver->setAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"reservation0", {"server0", "server1"}},
          {"reservation_buffer0", {}},
          {"unassigned_container", {"server2", "server3"}}});

  const std::map<std::string, std::vector<std::string>> msbToServers = {
      {"msb0", {"server0", "server2"}}, {"msb1", {"server1", "server3"}}};
  solver->addPartition("msb", msbToServers);

  const std::map<std::string, std::string> srBufferContainers = {
      {"reservation0", "srf_reservation0"},
      {"reservation_buffer0", "srf_reservation_buffer0"}};
  // Create data center scope
  solver->addScope("srf_buffer", srBufferContainers);

  // Avoid moving objects in the reservation container
  AvoidMovingSpec avoidMoving;
  avoidMoving.objects()->emplace_back("server0");
  avoidMoving.objects()->emplace_back("server1");
  solver->addConstraint(avoidMoving);

  // Now add SR Buffer constraints
  std::vector<ScopeItemPair> pairs;
  auto pair = ScopeItemPair();
  pair.scopeItem1() = "srf_reservation0";
  pair.scopeItem2() = "srf_reservation_buffer0";
  pairs.push_back(pair);

  SRBufferCapacitySpec spec;
  spec.dimension() = "server_count";
  spec.partitionName() = "msb";
  spec.scope() = "srf_buffer";
  spec.scopeItemPairs() = pairs;
  auto useAsObjective = std::get<1>(GetParam());
  if (useAsObjective) {
    solver->addGoal(spec);
  } else {
    solver->addConstraint(spec);
  }

  // Local search with SINGLE moves cannot fix the SR buffer constraint
  LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  solver->addSolver(solverSpec);

  // Get a solution from Rebalancer
  auto solution = solver->solve();
  std::map<std::string, int> containerToServerCount;
  for (auto [server, container] : *solution.assignment()) {
    XLOG(INFO) << fmt::format("{} => {}", server, container);
    containerToServerCount[container]++;
  }

  std::map<std::string, int> expectedContainerToServerCount;

  // No constraints should be broken
  EXPECT_EQ(0, *solution.finalConstraint()->brokenVal());
  expectedContainerToServerCount = {
      {"reservation0", 2},
      {"reservation_buffer0", 2},
      {"unassigned_container", 0}};

  for (const auto& [container, expectedServerCount] :
       expectedContainerToServerCount) {
    auto actualServerCount =
        folly::get_default(containerToServerCount, container, 0);
    EXPECT_EQ(expectedServerCount, actualServerCount);
  }
}
