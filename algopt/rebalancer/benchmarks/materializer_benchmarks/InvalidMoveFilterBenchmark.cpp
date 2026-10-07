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

#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/interface/ProblemSolverFactory.h"

#include <fmt/core.h>
#include <folly/Benchmark.h>
#include <folly/container/irange.h>
#include <folly/init/Init.h>

#include <map>
#include <string>
#include <vector>

using facebook::rebalancer::interface::ProblemSolver;
using facebook::rebalancer::interface::ProblemSolverFactory;
namespace interface = facebook::rebalancer::interface;

namespace {

constexpr double kUnboundedLimit = 1e9;

struct Inputs {
  std::map<std::string, std::vector<std::string>> containerToObjects;
  std::map<std::string, double> objectToValue;
  std::map<std::string, std::string> containerToScopeItem;
  std::map<std::string, double> zeroLimitScopeItems;
};

Inputs makeInputs(
    int numScopeItems,
    int objectsPerScopeItem,
    int pctZeroObjects,
    int numEmptyScopeItems = 0) {
  Inputs in;
  int objectIdx = 0;
  for (const auto i : folly::irange(numScopeItems)) {
    const auto host = fmt::format("host{}", i);
    const auto scopeItem = fmt::format("rack{}", i);
    in.containerToScopeItem[host] = scopeItem;
    in.zeroLimitScopeItems[scopeItem] = 0.0;
    std::vector<std::string> objects;
    if (i < numEmptyScopeItems) {
      in.containerToObjects[host] = std::move(objects);
      continue;
    }
    objects.reserve(objectsPerScopeItem);
    for ([[maybe_unused]] const auto j : folly::irange(objectsPerScopeItem)) {
      const auto object = fmt::format("task{}", objectIdx);
      // A non-zero value only for the non-zero fraction; the rest default to 0
      // (omitted), keeping the dimension sparse when pctZeroObjects is high.
      if (objectIdx % 100 >= pctZeroObjects) {
        in.objectToValue[object] = 1.0;
      }
      objects.push_back(object);
      ++objectIdx;
    }
    in.containerToObjects[host] = std::move(objects);
  }
  return in;
}

interface::CapacitySpec makeCapacitySpec(
    const std::map<std::string, double>& zeroLimitScopeItems,
    interface::CapacitySpecDefinition definition) {
  interface::CapacitySpec spec;
  spec.name() = "zero_capacity";
  spec.scope() = "rack";
  spec.dimension() = "d";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = definition;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = kUnboundedLimit;
  spec.limit()->scopeItemLimits() = zeroLimitScopeItems;
  return spec;
}

void runImpl(
    int benchmarkIters,
    int numScopeItems,
    int objectsPerScopeItem,
    int pctZeroObjects,
    int numEmptyScopeItems,
    interface::CapacitySpecDefinition definition) {
  Inputs inputs;
  BENCHMARK_SUSPEND {
    inputs = makeInputs(
        numScopeItems, objectsPerScopeItem, pctZeroObjects, numEmptyScopeItems);
  }

  for ([[maybe_unused]] const auto _ : folly::irange(benchmarkIters)) {
    auto solver = ProblemSolverFactory::makeProblemSolver(
        "rebalancer", "invalid_move_filter_benchmark");
    solver->setObjectName("task");
    solver->setContainerName("host");
    solver->setAssignment(inputs.containerToObjects);
    solver->addObjectDimension("d", inputs.objectToValue, /*defaultValue=*/0.0);
    solver->addScope("rack", inputs.containerToScopeItem);
    solver->addConstraint(
        makeCapacitySpec(inputs.zeroLimitScopeItems, definition));
    solver->enableInvalidMoveFilter(true);

    interface::LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(interface::SingleFastMoveTypeSpec()));
    solverSpec.solveTime() = 1;
    solver->addSolver(solverSpec);

    const auto solution = solver->solve();
    folly::doNotOptimizeAway(solution);
  }
}

void run(
    int benchmarkIters,
    int numScopeItems,
    int objectsPerScopeItem,
    int pctZeroObjects) {
  runImpl(
      benchmarkIters,
      numScopeItems,
      objectsPerScopeItem,
      pctZeroObjects,
      /*numEmptyScopeItems=*/0,
      interface::CapacitySpecDefinition::DURING);
}

void runAfter(
    int benchmarkIters,
    int numScopeItems,
    int objectsPerOccupiedScopeItem,
    int pctZeroObjects) {
  runImpl(
      benchmarkIters,
      numScopeItems,
      objectsPerOccupiedScopeItem,
      pctZeroObjects,
      /*numEmptyScopeItems=*/numScopeItems / 2,
      interface::CapacitySpecDefinition::AFTER);
}

} // namespace

BENCHMARK_NAMED_PARAM(
    run,
    capacity_1500si_6Mobj,
    /*numScopeItems=*/1500,
    /*objectsPerScopeItem=*/4000,
    /*pctZeroObjects=*/50)

BENCHMARK_NAMED_PARAM(
    runAfter,
    capacity_1500si_6Mobj_750empty,
    /*numScopeItems=*/1500,
    /*objectsPerOccupiedScopeItem=*/8000,
    /*pctZeroObjects=*/50)

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
