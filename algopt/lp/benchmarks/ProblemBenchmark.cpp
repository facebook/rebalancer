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

#include "algopt/lp/detail/generic/impl/GenericProblemImpl.h"
#include "algopt/lp/factory/ProblemFactory.h"
#include "algopt/rebalancer/benchmarks/utils/LpInstanceGenerator.h"

#include <folly/Benchmark.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/init/Init.h>

#include <functional>
#include <memory>

using namespace facebook::algopt::lp;
using namespace facebook::algopt::lp::detail;

Problem makeGenericGurobiProblem() {
  return Problem(
      std::make_unique<GenericProblemImpl>(ProblemFactory::makeGurobiProblem));
}

Problem makeGenericXpressProblem() {
  return Problem(
      std::make_unique<GenericProblemImpl>(ProblemFactory::makeGurobiProblem));
}

/** A set cover instance consists of a universe of @param numVars elements,
 * @param numSets sets with @param setSize each. With default values
 **/
Problem buildLargeSetCoverInstance(
    std::function<Problem()> factory,
    int numVars = 1 * 1E6,
    int numSets = 0.2 * 1E6,
    int setSize = 100) {
  auto problem = factory();
  int randomSeed = 1234;
  facebook::LpInstanceGenerator generator(
      problem, facebook::LpInstanceType::ODD_EVEN, randomSeed);
  generator.addHittingSetInstance(numVars, numSets, setSize, PartitionId(1));
  return problem;
}

// build problem benchmark

// Add term bennchmarks
BENCHMARK(BuildGurobiProblem) {
  buildLargeSetCoverInstance(ProblemFactory::makeGurobiProblem);
}

BENCHMARK(BuildXpressProblem) {
  buildLargeSetCoverInstance(ProblemFactory::makeXpressProblem);
}

BENCHMARK(BuildXpressV2Problem) {
  buildLargeSetCoverInstance(ProblemFactory::makeXpressV2Problem);
}

BENCHMARK(BuildGenericProblem) {
  buildLargeSetCoverInstance(makeGenericGurobiProblem);
}

BENCHMARK(BuildGenericProblemRealizedAsGurobi) {
  auto setCoverProblem = buildLargeSetCoverInstance(makeGenericGurobiProblem);
  auto& genericSetCover = (GenericProblemImpl&)setCoverProblem.get();
  auto problem = genericSetCover.getFactory()();
  genericSetCover.realize(problem);
}

BENCHMARK(BuildGenericProblemRealizedAsXpress) {
  auto setCoverProblem = buildLargeSetCoverInstance(makeGenericXpressProblem);
  auto& genericSetCover = (GenericProblemImpl&)setCoverProblem.get();
  auto problem = genericSetCover.getFactory()();
  genericSetCover.realize(problem);
}

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  folly::runBenchmarks();

  return 0;
}
