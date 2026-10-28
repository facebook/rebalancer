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

// Supplies REBALANCER_USE_XPRESS before the conditional test registration.
#include "algopt/lp/environment/Environment.h" // NOLINT(facebook-unused-include-check)

#include <gtest/gtest.h>

#ifdef REBALANCER_USE_XPRESS

#include "algopt/lp/factory/ProblemFactory.h"
#include "algopt/lp/lp.h"

#include <folly/container/irange.h>

namespace facebook::algopt::lp {
namespace {

void solveWithCallback(Problem& problem) {
  const auto x = problem.makeBoolVar("");
  const auto y = problem.makeBoolVar("");
  problem.newConstraint(x + y >= 1.0);
  problem.setObjective(x + y);
  problem.setCallback(
      [](const auto&) { return ProblemCallbackAction::CONTINUE; });
  problem.solve(false);
  EXPECT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_NEAR(x.getValue() + y.getValue(), 1.0, 1e-6);
}

TEST(XpressBackendLifetimeTest, RepeatedTyped) {
  for (const auto i : folly::irange(8)) {
    SCOPED_TRACE(i);
    auto typed = ProblemFactory::makeXpressV2Problem();
    solveWithCallback(typed);
  }
}

TEST(XpressBackendLifetimeTest, BclThenTyped) {
  {
    auto bcl = ProblemFactory::makeXpressProblem();
    solveWithCallback(bcl);
  }
  auto typed = ProblemFactory::makeXpressV2Problem();
  solveWithCallback(typed);
}

TEST(XpressBackendLifetimeTest, TypedThenBcl) {
  {
    auto typed = ProblemFactory::makeXpressV2Problem();
    solveWithCallback(typed);
  }
  auto bcl = ProblemFactory::makeXpressProblem();
  solveWithCallback(bcl);
}

TEST(XpressBackendLifetimeTest, BclOutlivesTyped) {
  auto bcl = ProblemFactory::makeXpressProblem();
  {
    auto typed = ProblemFactory::makeXpressV2Problem();
    solveWithCallback(bcl);
    solveWithCallback(typed);
  }
  solveWithCallback(bcl);
}

TEST(XpressBackendLifetimeTest, TypedOutlivesBcl) {
  auto typed = ProblemFactory::makeXpressV2Problem();
  {
    auto bcl = ProblemFactory::makeXpressProblem();
    solveWithCallback(bcl);
  }
  solveWithCallback(typed);
}

} // namespace
} // namespace facebook::algopt::lp

#endif // REBALANCER_USE_XPRESS
