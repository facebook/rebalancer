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

#include "algopt/rebalancer/interface/ProblemSolverFactory.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"

#include <folly/Benchmark.h>
#include <folly/container/irange.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/init/Init.h>

#include <map>
#include <string>

using namespace folly;
using namespace std;
using namespace facebook::rebalancer::interface;

enum SolverType {
  LP_BUILD_ONLY,
};

static std::shared_ptr<folly::CPUThreadPoolExecutor> get_serial_executor() {
  return std::make_shared<folly::CPUThreadPoolExecutor>(
      1, std::make_shared<folly::NamedThreadFactory>("CPUThreadPool"));
}

static void addBuildOnlyOptimalSolver(ProblemSolver& solver) {
  // only build the MIP model
  OptimalSolverSpec spec;
  spec.skipMipSolveForTesting() = true;
  solver.addSolver(spec);
  return;
}

/** Constructs an instance where most of the containers are frozen, unless
 * frozen containers are optimized away by rebalancer, building this model would
 * take forever */
static void runBalanceWithFrozenContainers(
    int objectCount,
    int containerCount,
    int frozenContainerCount) {
  if (frozenContainerCount > containerCount) {
    throw std::runtime_error(
        "number of frozen containers cannot be greater than total number of containers");
  }

  auto executor = get_serial_executor();
  auto solver =
      ProblemSolverFactory::makeProblemSolver(executor, "rebalancer", "tests");
  solver->setObjectName("task");
  solver->setContainerName("host");

  // Evenly distribute tasks across hosts
  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto taskId : folly::irange(objectCount)) {
    int hostId = taskId * containerCount / objectCount;
    initialAssignment[fmt::format("host{}", hostId)].push_back(
        fmt::format("task{}", taskId));
  }
  solver->setAssignment(initialAssignment);

  // Give each task a different cpu dimension, so they are all different in the
  // eyes of the equivalence sets optimization.
  std::map<std::string, double> taskCpu;
  for (const auto taskId : folly::irange(objectCount)) {
    taskCpu[fmt::format("task{}", taskId)] = taskId * 1e-5;
  }
  solver->addObjectDimension("cpu", taskCpu);

  std::map<std::string, double> hostCpu;
  for (const auto hostId : folly::irange(containerCount)) {
    hostCpu[fmt::format("host{}", hostId)] = 10.0;
  }
  solver->addContainerDimension("cpu", hostCpu);

  // Prevent frozen containers from getting any new objects.
  {
    NonAcceptingSpec spec;
    spec.scope() = "host";
    for (const auto hostId : folly::irange(frozenContainerCount)) {
      spec.items()->push_back(fmt::format("host{}", hostId));
    }
    solver->addConstraint(spec);
  }

  // Prevent objects initially in frozen containers from moving.
  {
    AvoidMovingSpec spec;
    for (const auto hostId : folly::irange(frozenContainerCount)) {
      for (auto& taskName :
           initialAssignment.at(fmt::format("host{}", hostId))) {
        spec.objects()->push_back(taskName);
      }
    }
    solver->addConstraint(spec);
  }

  // Optimize for cpu balance.
  {
    BalanceSpec spec;
    spec.scope() = "host";
    spec.dimension() = "cpu";
    spec.formula() = BalanceSpecFormula::MAX;
    solver->addGoal(spec);
  }

  addBuildOnlyOptimalSolver(*solver);
  auto solution = solver->solve();
}

/** Balance with IDEAL formula uses qudratic expressions of util expressions.
 * Depending on how we model qudratic expressions internally, the build can take
 * a long time. For example, if we have util = x_11 + x_21 + ... + x_1000_1, and
 * we compute util^2 directly as util * util, we will have a lot of terms in
 * objective In contrast, if we add a constraint util == temp, and use temp^2,
 * we only have one extra term, See D37491233 for a prod instance where this
 * happened.
 */
static void runQuadraticModelBuild(int numObjects) {
  auto solver = ProblemSolverFactory::makeProblemSolver("rebalancer", "tests");

  solver->setObjectName("task");
  solver->setContainerName("host");

  // Initially, all tasks are placed in host0, and host1 is empty.
  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(numObjects)) {
    initialAssignment["host0"].push_back(fmt::format("task{}", i));
  }
  initialAssignment["host1"] = {};
  solver->setAssignment(initialAssignment);

  std::map<std::string, double> taskCpu;
  int totalCpuRequirement = 0;
  for (const auto i : folly::irange(numObjects)) {
    // make each oject distinct by setting a different requirement
    taskCpu[fmt::format("task{}", i)] = i;
    totalCpuRequirement += i;
  }
  solver->addObjectDimension("cpu", std::move(taskCpu));

  const std::map<std::string, double> hostCpu = {
      {"host0", totalCpuRequirement}, {"host1", totalCpuRequirement}};
  solver->addContainerDimension("cpu", hostCpu);

  BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  // Only IDEAL balance uses power(util, 2)
  spec.formula() = BalanceSpecFormula::IDEAL;
  solver->addGoal(std::move(spec));

  addBuildOnlyOptimalSolver(*solver);
  auto solution = solver->solve();
}

BENCHMARK(FrozenContainers_100000_10000_9998) {
  runBalanceWithFrozenContainers(100000, 10000, 9998);
}

BENCHMARK(QuadraticModel_1000) {
  runQuadraticModelBuild(1000);
}

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  runBenchmarks();

  return 0;
}
