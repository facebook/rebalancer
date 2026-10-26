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

#include "algopt/rebalancer/solver/solvers/ChainSolver.h"

#include <folly/logging/xlog.h>

#include <algorithm>

using namespace std;

namespace facebook::rebalancer {

ChainSolver::ChainSolver(vector<unique_ptr<Solver>> solvers)
    : solvers_(std::move(solvers)) {}

bool ChainSolver::solve(Problem& p, Profile profile) {
  XLOG(INFO) << "Initial objective: " << p.objective.getValue().toString();
  int count = 0;
  for (auto& solver : solvers_) {
    solver->solve(p, profile);

    auto objectiveStr = (solvers_.size() == 1)
        ? fmt::format("Final objective: {}", p.objective.getValue().toString())
        : fmt::format(
              "Objective after solve {}: {}",
              ++count,
              p.objective.getValue().toString());
    XLOG(INFO) << objectiveStr;
  }
  return true;
}

/*
 * Always use constraint policies defined by the first solver.
 * This way users can tune and control which policy gets materialized.
 * For greedy, the expressions are smooth and for optimal,
 * the expressions use  LP.
 */
bool ChainSolver::needs_continuous_expressions() {
  if (!solvers_.size()) {
    throw std::runtime_error("No solver found in ChainSolver");
  }
  return solvers_[0]->needs_continuous_expressions();
}

bool ChainSolver::needsInvalidMoveFilter() const {
  return std::any_of(solvers_.begin(), solvers_.end(), [](const auto& solver) {
    return solver->needsInvalidMoveFilter();
  });
}

} // namespace facebook::rebalancer
