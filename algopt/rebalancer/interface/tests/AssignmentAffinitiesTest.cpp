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
#include <tuple>
#include <utility>

using namespace facebook::rebalancer::interface;

enum class AffinityInput { LIST, DIMENSION };

class AssignmentAffinitiesTest
    : public ::testing::TestWithParam<std::tuple<int, AffinityInput>> {
 protected:
  using ScopeItemNameToObjectNameToAffinity =
      std::map<std::string, std::map<std::string, double>>;

  static int threadCount() {
    return std::get<0>(GetParam());
  }

  static void addAffinityGoal(
      ProblemSolver& solver,
      AssignmentAffinitiesSpec spec) {
    switch (std::get<1>(GetParam())) {
      case AffinityInput::LIST:
        break;
      case AffinityInput::DIMENSION:
        solver.addDynamicObjectDimension(
            "assignment_affinity",
            *spec.scope(),
            makeScopeItemNameToObjectNameToAffinity(spec),
            0);
        spec.affinities()->clear();
        spec.dimension() = "assignment_affinity";
        break;
    }
    solver.addGoal(std::move(spec));
  }

 private:
  static ScopeItemNameToObjectNameToAffinity
  makeScopeItemNameToObjectNameToAffinity(
      const AssignmentAffinitiesSpec& spec) {
    ScopeItemNameToObjectNameToAffinity scopeItemNameToObjectNameToAffinity;
    for (const auto& affinity : *spec.affinities()) {
      scopeItemNameToObjectNameToAffinity[*affinity.scopeItemName()]
                                         [*affinity.objectName()] +=
          *affinity.affinity();
    }
    return scopeItemNameToObjectNameToAffinity;
  }
};

INSTANTIATE_TEST_CASE_P(
    NumThreads,
    AssignmentAffinitiesTest,
    ::testing::Combine(
        testThreadCounts(),
        ::testing::Values(AffinityInput::LIST, AffinityInput::DIMENSION)));

TEST_P(AssignmentAffinitiesTest, SingleAffinitiesGoal) {
  // In this example there are 3 hosts and 4 tasks. We add an assignment
  // affinities goal listing the preferences some tasks have for being placed
  // in different hosts. Since there aren't any constraints nor competing goals,
  // each task can be placed in its most preferred host in an optimal solution.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = threadCount()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // There are 3 hosts and 4 tasks. All tasks are initially placed in host0.
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1", "task2", "task3"}},
          {"host1", {}},
          {"host2", {}},
      });

  // The following table describes the affinities between each pair of task and
  // host possible. When a combination is not explicitly listed, it is assumed
  // to have an affinity of zero.
  //       | task0 | task1 | task2 | task3 |
  // host0 |     1 |    -2 |       |       |
  // host1 |     2 |       |   1.4 |       |
  // host2 |     3 |    -3 |       |       |
  AssignmentAffinitiesSpec assignmentAffinitiesSpec;
  assignmentAffinitiesSpec.scope() = "host";
  assignmentAffinitiesSpec.affinities() = {
      makeAssignmentAffinity("task0", "host0", 1),
      makeAssignmentAffinity("task0", "host1", 2),
      makeAssignmentAffinity("task0", "host2", 3),
      makeAssignmentAffinity("task1", "host0", -2),
      makeAssignmentAffinity("task1", "host2", -3),
      makeAssignmentAffinity("task2", "host1", 1.4),
  };

  addAffinityGoal(*solver, std::move(assignmentAffinitiesSpec));

  solver->addSolver(makeDefaultLocalSearchSolver());

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // The largest affinity of task0 is 3 for host2.
  EXPECT_EQ("host2", assignment["task0"]);

  // task1 doesn't list an affinity for host1, so it is assumed to be zero.
  // Since all other affinities of task1 are negative, zero is the largest.
  EXPECT_EQ("host1", assignment["task1"]);

  // task2 only lists one positive affinity, which is also the largest.
  EXPECT_EQ("host1", assignment["task2"]);

  // Since task3 doesn't list any affinities, it has the same affinity of
  // zero for all hosts. There's no incentive for the solver to move task3
  // outside of its initial host.
  EXPECT_EQ("host0", assignment["task3"]);
}

TEST_P(AssignmentAffinitiesTest, CompetingObjects) {
  // In this example there are 3 hosts and 2 tasks. There is Capacity constraint
  // enforcing that at most 1 task can be placed in each host. The first
  // preference of both tasks is for the same host, but because both tasks can't
  // take the same host, one has to settle for its second preference. In this
  // rebalancer will maximize the overall sum of active affinities.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = threadCount()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // There are 3 hosts and 2 tasks, both initially placed in host0.
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1"}},
          {"host1", {}},
          {"host2", {}},
      });

  // Capacity constraint: at most one task can be placed in each host.
  CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "task_count";

  Limit limit;
  limit.type() = LimitType::ABSOLUTE;
  limit.globalLimit() = 1;

  capacitySpec.limit() = limit;

  solver->addConstraint(capacitySpec);

  // Affinities goal: the first preference of both tasks is for host1 with the
  // same affinity of 10. The second preference of both tasks is host2 but with
  // different affinity values.
  AssignmentAffinitiesSpec assignmentAffinitiesSpec;
  assignmentAffinitiesSpec.scope() = "host";
  assignmentAffinitiesSpec.affinities() = {
      makeAssignmentAffinity("task0", "host1", 10),
      makeAssignmentAffinity("task0", "host2", 5),
      makeAssignmentAffinity("task1", "host1", 10),
      makeAssignmentAffinity("task1", "host2", 7),
  };

  addAffinityGoal(*solver, std::move(assignmentAffinitiesSpec));

  solver->addSolver(makeDefaultLocalSearchSolver());

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // The largest sum of active affinities attainable is 17: place task0 in host1
  // with an affinity of 10, and place task1 in host2 with an affinity of 7.
  EXPECT_EQ("host1", assignment["task0"]);
  EXPECT_EQ("host2", assignment["task1"]);
}

TEST_P(AssignmentAffinitiesTest, ContainerOutOfScope) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = threadCount()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0"}},
          {"host1", {}},
          {"host2", {}},
      });

  solver->addScope(
      "rack",
      std::map<std::string, std::string>{
          {"host1", "rack1"}, {"host2", "rack2"}});

  AssignmentAffinitiesSpec spec;
  spec.scope() = "rack";
  spec.affinities() = {
      makeAssignmentAffinity("task0", "rack1", 1),
  };

  addAffinityGoal(*solver, std::move(spec));

  solver->addSolver(makeDefaultLocalSearchSolver());

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  EXPECT_EQ("host1", assignment["task0"]);
}
