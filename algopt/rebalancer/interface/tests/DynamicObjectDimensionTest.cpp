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

using namespace std;
using namespace facebook::rebalancer::interface;

class DynamicObjectDimensionTest : public ::testing::TestWithParam<int> {};

INSTANTIATE_TEST_CASE_P(
    NumThreads,
    DynamicObjectDimensionTest,
    testThreadCounts());

static void testDynamicDimension(
    bool useCompactFormat,
    int executorThreadCount) {
  // The only task has a different cpu requirement depending on the host where
  // it's placed. The task has a different affinity to each host. It is expected
  // for the task to move to the host with the highest affinity where it fits.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = executorThreadCount});

  // Set basic properties.
  solver->setObjectName("task");
  solver->setContainerName("host");
  solver->setAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"host0", {"task0"}}, {"host1", {}}, {"host2", {}}, {"host3", {}}});

  // Each host has 10 units of cpu available.
  solver->addContainerDimension(
      "cpu",
      folly::F14FastMap<std::string, double>{
          {"host0", 10}, {"host1", 10}, {"host2", 10}, {"host3", 10}});

  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{{"task0", "job0"}});

  // The only task requires different amounts of cpu depending on the host.
  if (useCompactFormat) {
    solver->addDynamicObjectDimension(
        "cpu",
        "host",
        "job",
        std::map<std::string, std::map<std::string, double>>{
            {"host0", {{"job0", 10}}},
            {"host1", {{"job0", 6}}},
            {"host2", {{"job0", 8}}},
            {"host3", {{"job0", 12}}}});
  } else {
    solver->addDynamicObjectDimension(
        "cpu",
        "host",
        std::map<std::string, std::map<std::string, double>>{
            {"host0", {{"task0", 10}}},
            {"host1", {{"task0", 6}}},
            {"host2", {{"task0", 8}}},
            {"host3", {{"task0", 12}}}});
  }

  // The only task prefers hosts with higher IDs.
  {
    AssignmentAffinitiesSpec spec;
    spec.scope() = "host";
    spec.affinities() = {
        makeAssignmentAffinity("task0", "host1", 1),
        makeAssignmentAffinity("task0", "host2", 2),
        makeAssignmentAffinity("task0", "host3", 3),
    };
    solver->addGoal(spec);
  }

  // Enforce host cpu capacity.
  {
    CapacitySpec spec;
    spec.scope() = "host";
    spec.dimension() = "cpu";
    solver->addConstraint(spec);
  }

  solver->addSolver(makeDefaultLocalSearchSolver());

  // Generate a solution.
  auto solution = solver->solve();

  if (useCompactFormat) {
    const auto bundle = solver->getBundle();
    const auto groupBackedDynamicDimensions =
        *bundle.problem()->rolloutConfig()->groupBackedDynamicDimensions();

    const facebook::rebalancer::entities::Universe universe(
        *bundle.problem()->universe());
    const auto dimensionId = universe.getDimensionId("cpu");
    const auto scopeId = universe.getScopeId("host");
    const auto scopeItemId = universe.getScopeItemId(scopeId, "host0");
    const auto& dimension = universe.getObjects().getDimension(dimensionId);
    const bool usesGroupBackedStorage =
        dimension.only().values(scopeItemId).asMapOrNull() == nullptr;
    EXPECT_EQ(groupBackedDynamicDimensions, usesGroupBackedStorage);
  }

  // task0 would prefer being placed in host3, but it doesn't fit there. The
  // second preference is host2.
  const map<string, string> expected = {{"task0", "host2"}};
  EXPECT_EQ(expected, toOrderedMap(*solution.assignment()));
}

TEST_P(DynamicObjectDimensionTest, Basic) {
  testDynamicDimension(
      /* useCompactFormat */ false, GetParam());
}

TEST_P(DynamicObjectDimensionTest, CompactFormat) {
  testDynamicDimension(/* useCompactFormat */ true, GetParam());
}
