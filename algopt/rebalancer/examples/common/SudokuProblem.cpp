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

#include "algopt/rebalancer/examples/common/SudokuProblem.h"

#include "algopt/rebalancer/interface/ProblemSolverFactory.h"

#include "fmt/core.h"
#include <folly/container/irange.h>
#include <folly/Conv.h>
#include <folly/String.h>

#include <array>
#include <bitset>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <string_view>

namespace facebook::rebalancer::examples {

namespace {

using namespace facebook::rebalancer::interface;

constexpr std::string_view kDummySquare = "dummy_square";

constexpr int kGridSize = 9;

constexpr std::array<std::string_view, 3> kScopes = {
    "rows",
    "columns",
    "boxes"};

int labelField(
    const std::vector<std::string_view>& parts,
    const std::size_t index,
    const std::string_view label,
    const int minValue,
    const int maxValue) {
  if (parts.size() <= index) {
    throw std::invalid_argument(
        fmt::format("label {} has no field {}", label, index));
  }
  const auto value = folly::tryTo<int>(parts[index]);
  if (!value.hasValue() || *value < minValue || *value > maxValue) {
    throw std::invalid_argument(
        fmt::format(
            "field {} of label {} is not an integer in [{}, {}]",
            index,
            label,
            minValue,
            maxValue));
  }
  return *value;
}

void addInitialAssignment(
    ProblemSolver& solver,
    const std::vector<std::string>& puzzle) {
  // compute initial state to set for rebalancer
  std::map<std::string, std::vector<std::string>> initialBoard;
  // also compute which numbers should not be moving
  std::vector<std::string> avoidMoving;

  // keep track of pre-assigned numbers
  std::bitset<kGridSize + 1> numbers;

  // used to store all non-assigned numbers
  initialBoard[std::string(kDummySquare)] = {};
  for (const auto row : folly::irange(kGridSize)) {
    numbers.reset();
    for (const auto col : folly::irange(kGridSize)) {
      const auto& square = fmt::format("square_{}_{}", row, col);
      initialBoard[square] = {};
      auto value = puzzle[row].at(col);
      if (value == '.') {
        continue;
      }
      const int givenNumber = value - '0';
      if (givenNumber < 1 || givenNumber > kGridSize) {
        throw std::invalid_argument(
            fmt::format("given value: {} is not a valid digit", value));
      }
      const auto& numberLabel =
          fmt::format("number_{}_in_row_{}", givenNumber, row);
      initialBoard[square].emplace_back(numberLabel);
      avoidMoving.emplace_back(numberLabel);
      numbers.set(givenNumber);
    }
    // all numbers which are not assigned to squares are assigned to a special
    // dummy square
    for (const auto number : folly::irange(1, kGridSize + 1)) {
      if (numbers.test(number)) { // already assigned, so continue
        continue;
      }
      initialBoard[std::string(kDummySquare)].emplace_back(
          fmt::format("number_{}_in_row_{}", number, row));
    }
  }

  // set initial assignment
  solver.setAssignment(initialBoard);
  // avoid moving assigned squares
  AvoidMovingSpec avoidMovingSpecs;
  avoidMovingSpecs.objects() = avoidMoving;
  solver.addConstraint(avoidMovingSpecs);

  // completely free the dummy square
  ToFreeSpec toFreeSpec;
  toFreeSpec.containers() = {std::string(kDummySquare)};
  solver.addConstraint(toFreeSpec);
}

void addScopes(ProblemSolver& solver) {
  // define scopes: rows, columns and 3x3 boxes
  std::map<std::string, std::string> rows, columns, boxes;
  for (const auto row : folly::irange(kGridSize)) {
    for (const auto col : folly::irange(kGridSize)) {
      auto squareLabel = fmt::format("square_{}_{}", row, col);
      rows[squareLabel] = fmt::format("row_{}", row);
      columns[squareLabel] = fmt::format("col_{}", col);
      boxes[squareLabel] = fmt::format("box_{}_{}", row / 3, col / 3);
    }
  }
  solver.addScope("rows", rows);
  solver.addScope("columns", columns);
  solver.addScope("boxes", boxes);
}

void usePartitionGroups(ProblemSolver& solver) {
  // add partition group: all 'numbers' with same mathematical (numerical)
  // value belong to one partition group (part of the 'same_numbers' partition)
  std::map<std::string, std::string> sameNumbers;
  for (const auto number : folly::irange(1, kGridSize + 1)) {
    for (const auto row : folly::irange(kGridSize)) {
      sameNumbers[fmt::format("number_{}_in_row_{}", number, row)] =
          fmt::format("number_{}", number);
    }
  }
  solver.addPartition("same_numbers", std::move(sameNumbers));

  // with in any scope item each partition group can have at most a count of 1
  for (const auto& scope : kScopes) {
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

void useDimensions(ProblemSolver& solver) {
  // declare dimensions "dimension_{n}", iff number n it has value 1.
  for (const auto number : folly::irange(1, kGridSize + 1)) {
    std::map<std::string, double> dimensions;
    for (const auto row : folly::irange(kGridSize)) {
      dimensions[fmt::format("number_{}_in_row_{}", number, row)] = 1;
    }
    solver.addObjectDimension(fmt::format("dimension_{}", number), dimensions);
  }

  // only one object can have a non-zero dimension value in each scope
  for (const auto& scope : kScopes) {
    for (const auto number : folly::irange(1, kGridSize + 1)) {
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

} // namespace

const std::vector<std::string>& defaultSudokuPuzzle() {
  static const std::vector<std::string> puzzle = {
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
  return puzzle;
}

std::unique_ptr<interface::ProblemSolver> makeSudokuSolver(
    const std::vector<std::string>& puzzle,
    folly::StringPiece approach) {
  if (puzzle.size() != kGridSize) {
    throw std::invalid_argument(
        fmt::format(
            "puzzle must have {} rows, got {}", kGridSize, puzzle.size()));
  }
  for (const auto& row : puzzle) {
    if (row.size() != kGridSize) {
      throw std::invalid_argument(
          fmt::format(
              "puzzle row '{}' must have {} characters", row, kGridSize));
    }
  }

  auto solver = interface::ProblemSolverFactory::makeProblemSolver(
      "rebalancer", "examples");
  // This is used to define the default dimension (has value 1 for all
  // objects)
  solver->setObjectName("number");
  // This is used to define a default scope (that has all containers)
  solver->setContainerName("square");

  addInitialAssignment(*solver, puzzle);
  addScopes(*solver);

  if (approach == kPartitionsApproach) {
    usePartitionGroups(*solver);
  } else if (approach == kDimensionsApproach) {
    useDimensions(*solver);
  } else {
    throw std::invalid_argument(
        fmt::format(
            "{} must be either {} or {}",
            approach,
            kPartitionsApproach,
            kDimensionsApproach));
  }

  // each square should limit to have exactly one number
  interface::CapacitySpec capacitySpec;
  capacitySpec.scope() = "square";
  capacitySpec.dimension() = "number_count";
  capacitySpec.limit()->globalLimit() = 1;

  solver->addConstraint(capacitySpec);

  interface::OptimalSolverSpec solverSpec;
  solverSpec.solverPackage() = interface::OptimalSolverPackage::HIGHS;
  solver->addSolver(solverSpec);

  return solver;
}

std::vector<std::vector<int>> solvedSudokuGrid(
    const interface::AssignmentSolution& solution) {
  std::vector<std::vector<int>> grid(kGridSize, std::vector<int>(kGridSize, 0));
  std::vector<std::string_view> numberParts, squareParts;
  for (const auto& [numberLabel, square] : *solution.assignment()) {
    if (square == kDummySquare) {
      throw std::invalid_argument(
          fmt::format(
              "{} is still on {}: puzzle unsolved", numberLabel, square));
    }
    numberParts.clear();
    folly::split('_', numberLabel, numberParts);
    squareParts.clear();
    folly::split('_', square, squareParts);
    const auto row = labelField(squareParts, 1, square, 0, kGridSize - 1);
    const auto col = labelField(squareParts, 2, square, 0, kGridSize - 1);
    grid[row][col] = labelField(numberParts, 1, numberLabel, 1, kGridSize);
  }
  return grid;
}

} // namespace facebook::rebalancer::examples
