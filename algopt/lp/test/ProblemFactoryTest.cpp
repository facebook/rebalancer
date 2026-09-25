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

#include "algopt/lp/environment/Environment.h"
#include "algopt/lp/factory/ProblemFactory.h"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace facebook::algopt::lp;

namespace {

// Every case here needs a backend that loads without a license: the two
// fallback cases need one to fall back to, and the third uses one as a loader
// that succeeds. An OSS build may be configured with no solver at all, and
// there is nothing to assert about a fallback such a build cannot perform.
class ProblemFactoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!facebook::algopt::isHiGHSAvailable()) {
      GTEST_SKIP() << "build has no unlicensed solver backend";
    }
  }
};

// Minimizes a single variable bounded below by 3, so a usable Problem solves
// to 3 and a hollow one does not.
double solveBoundedMin(Problem& problem) {
  auto var = problem.makeVar();
  var.setLB(3);
  var.setUB(10);
  problem.setObjective(var);
  problem.solve();
  return var.getValue();
}

[[noreturn]] Problem failToLoad() {
  throw std::runtime_error("simulated license expiration");
}

// Stands in for makeGurobiProblem() on a host whose license has expired.
Problem loadFailingSolver() {
  return detail::loadWithFallback("GUROBI", &failToLoad);
}

} // namespace

// Which backend the chain lands on depends on the build's configured chain and
// on which licenses this host has, so only "not the one that failed" and
// "actually solves" hold everywhere.
TEST_F(ProblemFactoryTest, FallsBackToAUsableBackendWhenLoadThrows) {
  auto problem = loadFailingSolver();

  EXPECT_NE(problem.backendName(), "GUROBI");
  EXPECT_DOUBLE_EQ(solveBoundedMin(problem), 3);
}

TEST_F(ProblemFactoryTest, ReturnsLoadedProblemWhenLoadSucceeds) {
  bool loaded = false;
  auto problem = detail::loadWithFallback("FakeSolver", [&]() {
    loaded = true;
    return ProblemFactory::makeHiGHSProblem();
  });

  EXPECT_TRUE(loaded);
  EXPECT_DOUBLE_EQ(solveBoundedMin(problem), 3);
}

// Both rebalancer and opal wrap the factory this way when simplify is on, and
// both report backendName() to Scuba, so the wrapper must name the backend it
// realized rather than itself.
TEST_F(ProblemFactoryTest, SimplifiedProblemReportsRealizedBackend) {
  auto realized = loadFailingSolver();
  auto problem = ProblemFactory::makeSimplifiedProblem(loadFailingSolver);

  EXPECT_EQ(problem.backendName(), realized.backendName());
}
