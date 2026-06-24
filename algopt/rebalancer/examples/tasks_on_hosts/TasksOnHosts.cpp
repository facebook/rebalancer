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

#include "algopt/rebalancer/examples/common/BundleOutput.h"
#include "algopt/rebalancer/interface/ProblemSolverFactory.h"

#include <glog/logging.h>

#include "fmt/core.h"
#include <folly/container/irange.h>
#include <folly/init/Init.h>

using namespace facebook::rebalancer::interface;

constexpr int objectCount = 12;

static void prettyPrint(
    const folly::F14FastMap<std::string, std::string>& objectsToContainer) {
  std::map<std::string, std::vector<std::string>> containerToObjects;
  for (auto& [object, container] : objectsToContainer) {
    containerToObjects[container].push_back(object);
  }
  for (auto& [container, objects] : containerToObjects) {
    fmt::print("{}:\n", container);
    for (auto& object : objects) {
      fmt::print("\t{}\n", object);
    }
  }
}

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  // Instantiate the solver
  auto solver =
      ProblemSolverFactory::makeProblemSolver("rebalancer", "examples");

  // Basic properties
  solver->setObjectName("task");
  solver->setContainerName("host");

  // Set up the initial assignment
  std::vector<std::string> tasks;
  tasks.reserve(objectCount);
  for (const auto i : folly::irange(objectCount)) {
    tasks.push_back(fmt::format("task{}", i));
  }

  const std::map<std::string, std::vector<std::string>> initial = {
      {"host0", {tasks}}, {"host1", {}}, {"host2", {}}, {"host3", {}}};
  solver->setAssignment(initial);

  // Add dimension: task memory
  // Pass in empty map with default value 1, i.e. every task requires 1 memory
  // unit
  solver->addObjectDimension("memory", std::map<std::string, double>(), 1);

  // Add dimension: host memory
  const std::map<std::string, double> hostMemory = {
      {"host0", 10}, {"host1", 20}, {"host2", 10}, {"host3", 10}};
  solver->addContainerDimension("memory", hostMemory);

  // Add goal: balance hosts on memory
  BalanceSpec goal;
  goal.name() = "balance-hosts";
  goal.scope() = "host";
  goal.dimension() = "memory";
  goal.formula() = BalanceSpecFormula::LEGACY;
  goal.fixAverageToInitial() = true;
  solver->addGoal(goal);

  // Add constraint: make host0 empty
  ToFreeSpec freeHost0;
  freeHost0.name() = "make_host0-empty";
  freeHost0.containers() = std::vector<std::string>({"host0"});
  solver->addConstraint(freeHost0);

  // Add scope: rack
  const std::map<std::string, std::string> hostToRack = {
      {"host0", "rack0"},
      {"host1", "rack0"},
      {"host2", "rack1"},
      {"host3", "rack1"}};
  solver->addScope("rack", hostToRack);

  // Add partition: job
  std::map<std::string, std::vector<std::string>> jobToTasks;
  for (const auto i : folly::irange(objectCount)) {
    jobToTasks[fmt::format("job{}", i / 2)].push_back(fmt::format("task{}", i));
  }

  solver->addPartition("job", jobToTasks);

  // Add constraint:
  // limit 1 to the maximum number of tasks of the same job in a rack
  GroupCountSpec jobTaskDistribution;
  jobTaskDistribution.name() = "job-rack-fault-tolerance";
  jobTaskDistribution.scope() = "rack";
  jobTaskDistribution.partitionName() = "job";
  jobTaskDistribution.bound() = GroupCountSpecBound::MAX;
  jobTaskDistribution.limit()->globalLimit() = 1;
  solver->addConstraint(jobTaskDistribution);

  OptimalSolverSpec spec;
  spec.solverPackage() = OptimalSolverPackage::HIGHS;
  solver->addSolver(spec);

  // Generate a solution and print it
  auto solution = solver->solve();
  facebook::rebalancer::examples::maybeSaveBundle(*solver);
  prettyPrint(*solution.assignment());

  // Assert that the distribution is as expected
  std::map<std::string, double> containerOccupancy;
  for (auto& [object, container] : *solution.assignment()) {
    if (container == "host0") {
      containerOccupancy["host0"] += 1;
    } else if (container == "host1") {
      containerOccupancy["host1"] += 1;
    } else if (container == "host2") {
      containerOccupancy["host2"] += 1;
    } else if (container == "host3") {
      containerOccupancy["host3"] += 1;
    }
  }

  CHECK_EQ(containerOccupancy["host0"], 0);
  CHECK_EQ(containerOccupancy["host1"], 6);
  CHECK_EQ(containerOccupancy["host2"], 3);
  CHECK_EQ(containerOccupancy["host3"], 3);
}
