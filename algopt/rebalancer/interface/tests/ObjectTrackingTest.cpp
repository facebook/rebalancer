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

#include <folly/logging/Init.h>
#include <gtest/gtest.h>

using namespace facebook::rebalancer::interface;

class ObjectTrackingTest : public ::testing::TestWithParam<int> {};

INSTANTIATE_TEST_CASE_P(NumThreads, ObjectTrackingTest, testThreadCounts());

static AssignmentSolution solveProblem(
    int threadCount,
    const std::vector<std::string>& objectsToTrack) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = threadCount});
  solver->setObjectName("task");
  solver->setContainerName("host");

  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1"}},
          {"host1", {}},
      });

  solver->addObjectDimension(
      "memory",
      std::unordered_map<std::string, double>{
          {"task0", 10},
          {"task1", 10},
      });

  solver->addContainerDimension(
      "memory",
      std::vector<std::pair<std::string, double>>{
          {"host0", 20},
          {"host1", 20},
      });

  {
    // Enforce memory capacity.
    CapacitySpec spec;
    spec.scope() = "host";
    spec.dimension() = "memory";
    solver->addConstraint(spec);
  }

  {
    // Keep host0 full. The invalid move filter does not model MIN bounds, so
    // moves out of host0 are still evaluated (and tracked).
    CapacitySpec spec;
    spec.scope() = "host";
    spec.dimension() = "memory";
    spec.bound() = CapacitySpecBound::MIN;
    spec.filter()->itemsWhitelist() = {"host0"};
    solver->addConstraint(spec);
  }

  {
    // Both tasks prefer host1.
    AssignmentAffinitiesSpec spec;
    spec.scope() = "host";
    spec.affinities() = {
        makeAssignmentAffinity("task0", "host1", 1.0),
        makeAssignmentAffinity("task1", "host1", 1.0)};
    solver->addGoal(spec);
  }

  {
    // Enable object tracking.
    MoveStatsSpec spec;
    spec.trackObjects() = true;
    spec.trackObjectsWhitelist() = objectsToTrack;
    solver->enableMoveStats(spec);
  }

  solver->addSolver(makeDefaultLocalSearchSolver());
  solver->publishEquivalenceSetInfo();

  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  // None of the tasks were able to move because host0 must stay full.
  EXPECT_EQ("host0", assignment.at("task0"));
  EXPECT_EQ("host0", assignment.at("task1"));

  // There's a single object equivalence class.
  EXPECT_EQ(1, solution.equivalenceSetInfo()->equivalenceSets()->size());

  return solution;
}

TEST_P(ObjectTrackingTest, EquivalentObjectsTrackNone) {
  auto solution = solveProblem(GetParam(), {});
  auto& objectStats = *solution.finalEvaluationSummary()->objectStats();

  EXPECT_EQ(0, objectStats.size());
}

TEST_P(ObjectTrackingTest, EquivalentObjectsTrackFirst) {
  auto solution = solveProblem(GetParam(), {"task0"});
  auto& objectStats = *solution.finalEvaluationSummary()->objectStats();

  EXPECT_EQ(1, objectStats.size());
  EXPECT_TRUE(objectStats.contains("task0"));
}

TEST_P(ObjectTrackingTest, EquivalentObjectsTrackSecond) {
  auto solution = solveProblem(GetParam(), {"task1"});
  auto& objectStats = *solution.finalEvaluationSummary()->objectStats();

  EXPECT_EQ(1, objectStats.size());
  EXPECT_TRUE(objectStats.contains("task1"));
}

TEST_P(ObjectTrackingTest, EquivalentObjectsTrackBoth) {
  auto solution = solveProblem(GetParam(), {"task0", "task1"});
  auto& objectStats = *solution.finalEvaluationSummary()->objectStats();

  EXPECT_EQ(2, objectStats.size());
  EXPECT_TRUE(objectStats.contains("task0"));
  EXPECT_TRUE(objectStats.contains("task1"));
}
