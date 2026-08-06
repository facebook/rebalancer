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

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::interface::tests {

class MaximizeFreeCapacityUnitsTest
    : public ::testing::TestWithParam<std::tuple<int, SolverAlgoType>> {
 protected:
  void SetUp() override {
    switch (std::get<1>(GetParam())) {
      case SolverAlgoType::LOCALSEARCH:
        break;
      case SolverAlgoType::OPTIMAL:
        REBALANCER_SKIP_IF_NO_MIP_SOLVER();
        break;
    }
  }
};

INSTANTIATE_TEST_CASE_P(
    Solvers,
    MaximizeFreeCapacityUnitsTest,
    ::testing::Combine(
        testThreadCounts(),
        ::testing::Values(
            SolverAlgoType::LOCALSEARCH,
            SolverAlgoType::OPTIMAL)));

TEST_P(MaximizeFreeCapacityUnitsTest, MaximizesAvailableUnits) {
  constexpr auto kContainerCapacity = 16;
  constexpr auto kUnitSize = 8;
  constexpr auto kHost0InitialTasks = 10;
  constexpr auto kTotalTasks = 16;

  const auto [threadCount, solverType] = GetParam();
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = threadCount});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // host0, host1, and host2 start with utilizations 10, 6, and 0,
  // respectively, leaving 3 available units of size 8. Rebalancing
  // utilization to multiples of 8 increases the total to 4.
  std::vector<std::pair<std::string, std::vector<std::string>>> assignment = {
      {"host0", {}}, {"host1", {}}, {"host2", {}}};
  for (const auto i : folly::irange(kHost0InitialTasks)) {
    assignment[0].second.push_back(fmt::format("task{}", i));
  }
  for (const auto i : folly::irange(kHost0InitialTasks, kTotalTasks)) {
    assignment[1].second.push_back(fmt::format("task{}", i));
  }
  solver->setAssignment(assignment);
  solver->addObjectDimension(
      "cpu",
      /*objectToValue=*/std::map<std::string, double>{},
      /*defaultValue=*/1);
  solver->addContainerDimension(
      "cpu",
      /*containerToValue=*/std::map<std::string, double>{},
      /*defaultValue=*/kContainerCapacity);

  auto spec = MaximizeFreeCapacityUnitsSpec{};
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.unitSize()->type() = LimitType::ABSOLUTE;
  spec.unitSize()->globalLimit() = kUnitSize;
  solver->addGoal(spec);
  switch (solverType) {
    case SolverAlgoType::LOCALSEARCH: {
      auto solverSpec = LocalSearchSolverSpec{};
      solverSpec.moveTypeList() = {
          ProblemSolver::makeMoveTypeSpec(SingleFastMoveTypeSpec{})};
      solver->addSolver(solverSpec);
      break;
    }
    case SolverAlgoType::OPTIMAL:
      solver->addSolver(facebook::algopt::makeAvailableOptimalSolverSpec());
      break;
  }

  const auto solution = solver->solve();
  std::map<std::string, int> hostUtilization;
  for (const auto& [_, host] : *solution.assignment()) {
    ++hostUtilization[host];
  }
  auto availableUnits = 0;
  for (const auto& host : {"host0", "host1", "host2"}) {
    availableUnits += (kContainerCapacity - hostUtilization[host]) / kUnitSize;
  }
  EXPECT_EQ(4, availableUnits);
}

} // namespace facebook::rebalancer::interface::tests
