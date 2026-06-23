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

#include "fmt/core.h"
#include <folly/container/irange.h>
#include <folly/init/Init.h>
#include <folly/logging/xlog.h>
#include <folly/String.h>

#include <bitset>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace facebook::rebalancer::interface;

constexpr folly::StringPiece kDimensionsApproach{"dimensions"};
constexpr folly::StringPiece kPartitionsApproach{"partitions"};

DEFINE_string(
    approach,
    kPartitionsApproach.str(),
    ("What approach to use for solving the problem: "
     "partitions and group count constraints on partition groups "
     "or capacity constraints on dimensions"));

static void add_initial_assignment(
    ProblemSolver& solver,
    const std::vector<std::string>& puzzle) {
  // compute initial state to set for rebalancer
  std::map<std::string, std::vector<std::string>> initial_board;
  // also compute which numbers should not be moving
  std::vector<std::string> avoid_moving;

  // keep track of pre-assigned numbers
  std::bitset<10> numbers;

  // used to store all non-assigned numbers
  initial_board["dummy_square"] = {};
  for (const auto row : folly::irange(9)) {
    numbers.reset();
    for (const auto col : folly::irange(9)) {
      const auto& square = fmt::format("square_{}_{}", row, col);
      initial_board[square] = {};
      auto value = puzzle[row].at(col);
      if (value == '.') {
        continue;
      }
      int given_number = value - '0';
      XLOG_IF(FATAL, given_number < 1 || given_number > 9)
          << fmt::format("given value: {} is not a valid digit", value);
      const auto& number_label =
          fmt::format("number_{}_in_row_{}", given_number, row);
      initial_board[square].emplace_back(number_label);
      avoid_moving.emplace_back(number_label);
      numbers.set(given_number);
    }
    // all numbers which are not assigned to squares are assigned to a special
    // dummy square
    for (const auto number : folly::irange(1, 10)) {
      if (numbers.test(number)) { // already assigned, so continue
        continue;
      }
      initial_board["dummy_square"].emplace_back(
          fmt::format("number_{}_in_row_{}", number, row));
    }
  }

  // set initial assignment
  solver.setAssignment(initial_board);
  // avoid moving assigned squares
  AvoidMovingSpec avoidMovingSpecs;
  avoidMovingSpecs.objects() = avoid_moving;
  solver.addConstraint(avoidMovingSpecs);

  // completely free the dummy square
  ToFreeSpec toFreeSpec;
  toFreeSpec.containers() = {"dummy_square"};
  solver.addConstraint(toFreeSpec);
}

static void add_scopes(ProblemSolver& solver) {
  // define scopes: rows, columns and 3x3 boxes
  std::map<std::string, std::string> rows, columns, boxes;
  for (const auto row : folly::irange(9)) {
    for (const auto col : folly::irange(9)) {
      auto square_label = fmt::format("square_{}_{}", row, col);
      rows[square_label] = fmt::format("row_{}", row);
      columns[square_label] = fmt::format("col_{}", col);
      boxes[square_label] = fmt::format("box_{}_{}", row / 3, col / 3);
    }
  }
  solver.addScope("rows", rows);
  solver.addScope("columns", columns);
  solver.addScope("boxes", boxes);
}

static void use_partition_groups(ProblemSolver& solver) {
  // add partition group: all 'numbers' with same mathematical (numerical)
  // value belong to one partition group (part of the 'same_numbers' partition)
  std::map<std::string, std::string> same_numbers;
  for (const auto number : folly::irange(1, 10)) {
    for (const auto row : folly::irange(9)) {
      same_numbers[fmt::format("number_{}_in_row_{}", number, row)] =
          fmt::format("number_{}", number);
    }
  }
  solver.addPartition("same_numbers", std::move(same_numbers));

  // with in any scope item each partition group can have at most a count of 1
  constexpr std::array<std::string_view, 3> scopes = {
      "rows", "columns", "boxes"};
  for (const auto& scope : scopes) {
    GroupCountSpec groupCountSpec;
    groupCountSpec.scope() = scope;
    groupCountSpec.partitionName() = "same_numbers";

    Limit limit = Limit();
    limit.type() = LimitType::ABSOLUTE;
    limit.globalLimit() = 1;
    groupCountSpec.limit() = limit;

    solver.addConstraint(groupCountSpec);
  }
}

static void use_dimensions(ProblemSolver& solver) {
  // declare dimensions "dimension_{n}", iff number n it has value 1.
  for (const auto number : folly::irange(1, 10)) {
    std::map<std::string, double> dimensions;
    for (const auto row : folly::irange(9)) {
      dimensions[fmt::format("number_{}_in_row_{}", number, row)] = 1;
    }
    solver.addObjectDimension(fmt::format("dimension_{}", number), dimensions);
  }

  // only one object can have a non-zero dimension value in each scope
  constexpr std::array<std::string_view, 3> scopes = {
      "rows", "columns", "boxes"};
  for (const auto& scope : scopes) {
    for (const auto number : folly::irange(1, 10)) {
      CapacitySpec capacitySpec;
      capacitySpec.scope() = scope;
      capacitySpec.dimension() = fmt::format("dimension_{}", number);

      auto limit = Limit();
      limit.globalLimit() = 1;
      capacitySpec.limit() = limit;

      solver.addConstraint(capacitySpec);
    }
  }
}

static void print_solution(const AssignmentSolution& solution) {
  std::vector<std::vector<int>> solved_puzzle(9, std::vector<int>(9));
  std::vector<std::string_view> square_parts(5), number_parts(5);
  for (const auto& [number_label, square] : *solution.assignment()) {
    number_parts.clear();
    folly::split('_', number_label, number_parts);
    square_parts.clear();
    folly::split('_', square, square_parts);
    solved_puzzle[folly::to<int>(square_parts[1])]
                 [folly::to<int>(square_parts[2])] =
                     folly::to<int>(number_parts[1]);
  }

  std::stringstream ss;
  for (const auto row : folly::irange(9)) {
    for (const auto col : folly::irange(9)) {
      ss << solved_puzzle[row][col] << " ";
    }
    ss << std::endl;
  }
  XLOG(INFO) << "Final solution: \n" << ss.str();
}

static void solve_sudoku(
    const std::vector<std::string>& puzzle,
    const std::string& approach) {
  auto solver =
      ProblemSolverFactory::makeProblemSolver("rebalancer", "examples");
  // This is used to define the default dimension (has value 1 for all
  // objects)
  solver->setObjectName("number");
  // This is used to define a default scope (that has all containers)
  solver->setContainerName("square");

  add_initial_assignment(*solver, puzzle);
  add_scopes(*solver);

  if (approach == kPartitionsApproach) {
    use_partition_groups(*solver);
  } else if (approach == kDimensionsApproach) {
    use_dimensions(*solver);
  } else {
    XLOG(FATAL) << fmt::format(
        "{} must be either {} or {}",
        approach,
        kPartitionsApproach,
        kDimensionsApproach);
  }

  // each square should limit to have exactly one number
  CapacitySpec capacitySpec;
  capacitySpec.scope() = "square";
  capacitySpec.dimension() = "number_count";
  capacitySpec.limit()->globalLimit() = 1;

  solver->addConstraint(capacitySpec);

  OptimalSolverSpec solver_spec;
  solver_spec.solverPackage() = OptimalSolverPackage::HIGHS;
  solver->addSolver(solver_spec);

  auto solution = solver->solve();
  facebook::rebalancer::examples::maybeSaveBundle(*solver);

  if (*solution.finalObjective()->value() > 0.01) {
    XLOG(FATAL) << "failed to solve sudoku completely";
  }

  print_solution(solution);
}

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  const std::vector<std::string> puzzle = {
      "85...24..",
      "72......9",
      "..4......",
      "...1.7..2",
      "3.5...9..",
      ".4.......",
      "....8..7.",
      ".17......",
      "....36.4.",
  };
  solve_sudoku(puzzle, FLAGS_approach);

  return 0;
}
