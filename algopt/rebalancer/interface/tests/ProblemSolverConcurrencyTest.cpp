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
#include "algopt/rebalancer/interface/tests/ProblemSolverTest.h"
#include "algopt/rebalancer/tests/SolverTestUtils.h"

#include <folly/container/irange.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/futures/Future.h>
#include <gtest/gtest.h>

using namespace facebook::algopt;
using namespace facebook::rebalancer::interface;
using namespace folly;
using namespace std;

static shared_ptr<CPUThreadPoolExecutor> makeThreadPool(int threadCount) {
  return make_shared<CPUThreadPoolExecutor>(
      threadCount, make_shared<NamedThreadFactory>("CPUThreadPool"));
}

static void runConcurrentTest(
    bool optimal,
    OptimalSolverPackage package = OptimalSolverPackage::XPRESS) {
  auto testPool = makeThreadPool(20);
  auto solverPool = makeThreadPool(10);
  for (const auto _ : folly::irange(40)) {
    via(testPool.get(), [solverPool, optimal, package]() {
      auto solver = ProblemSolverFactory::makeProblemSolver(
          solverPool, "rebalancer", "tests");
      runBasicTest(*solver, optimal, false, package);
    });
  }
  testPool->join();
}

TEST(ProblemSolverConcurrencyTest, LocalSearch) {
  runConcurrentTest(false);
}

TEST(ProblemSolverConcurrencyTest, OptimalGurobi) {
  REBALANCER_SKIP_IF_NO_GUROBI();
  runConcurrentTest(true, OptimalSolverPackage::GUROBI);
}

TEST(ProblemSolverConcurrencyTest, OptimalXpress) {
  REBALANCER_SKIP_IF_NO_XPRESS();
  runConcurrentTest(true, OptimalSolverPackage::XPRESS);
}

TEST(ProblemSolverConcurrencyTest, OptimalHighs) {
  REBALANCER_SKIP_IF_NO_HIGHS();
  runConcurrentTest(true, OptimalSolverPackage::HIGHS);
}
