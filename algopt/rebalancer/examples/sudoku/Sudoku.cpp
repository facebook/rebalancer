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
#include "algopt/rebalancer/examples/common/SudokuProblem.h"

#include <folly/container/irange.h>
#include <folly/init/Init.h>
#include <folly/logging/xlog.h>

#include <exception>
#include <sstream>
#include <string>
#include <vector>

using namespace facebook::rebalancer::interface;
namespace examples = facebook::rebalancer::examples;

DEFINE_string(
    approach,
    examples::kPartitionsApproach.str(),
    ("What approach to use for solving the problem: "
     "partitions and group count constraints on partition groups "
     "or capacity constraints on dimensions"));

static void print_solution(const AssignmentSolution& solution) {
  const auto solved_puzzle = examples::solvedSudokuGrid(solution);

  std::stringstream ss;
  for (const auto row : folly::irange(9)) {
    for (const auto col : folly::irange(9)) {
      ss << solved_puzzle[row][col] << " ";
    }
    ss << '\n';
  }
  XLOG(INFO) << "Final solution: \n" << ss.str();
}

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  try {
    const auto solver = examples::makeSudokuSolver(
        examples::defaultSudokuPuzzle(), FLAGS_approach);

    const auto solution = solver->solve();
    examples::maybeSaveBundle(*solver);

    if (*solution.finalObjective()->value() > 0.01) {
      XLOG(FATAL) << "failed to solve sudoku completely";
    }

    print_solution(solution);
  } catch (const std::exception& e) {
    XLOG(FATAL) << e.what();
  }

  return 0;
}
