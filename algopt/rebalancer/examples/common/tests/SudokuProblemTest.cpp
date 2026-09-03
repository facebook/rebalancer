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
#include "algopt/rebalancer/tests/SolverTestUtils.h"

#include <folly/container/irange.h>
#include <gtest/gtest.h>

#include <initializer_list>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer::examples {

namespace {

std::vector<std::vector<int>> solveDefaultPuzzle(folly::StringPiece approach) {
  const auto solver = makeSudokuSolver(defaultSudokuPuzzle(), approach);
  solver->disableLogging();
  solver->setRunId("sudoku_problem_test");
  const auto solution = solver->solve();
  EXPECT_LE(*solution.finalObjective()->value(), 0.01);
  return solvedSudokuGrid(solution);
}

void expectIsSolutionOfDefaultPuzzle(
    const std::vector<std::vector<int>>& grid) {
  const std::set<int> kAllDigits = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  const auto& puzzle = defaultSudokuPuzzle();

  for (const auto index : folly::irange(9)) {
    std::set<int> rowDigits, columnDigits, boxDigits;
    for (const auto offset : folly::irange(9)) {
      rowDigits.insert(grid[index][offset]);
      columnDigits.insert(grid[offset][index]);
      boxDigits.insert(
          grid[3 * (index / 3) + offset / 3][3 * (index % 3) + offset % 3]);

      const char given = puzzle[index].at(offset);
      if (given != '.') {
        EXPECT_EQ(grid[index][offset], given - '0')
            << "given at (" << index << ", " << offset << ") was not preserved";
      }
    }
    EXPECT_EQ(rowDigits, kAllDigits) << "row " << index;
    EXPECT_EQ(columnDigits, kAllDigits) << "column " << index;
    EXPECT_EQ(boxDigits, kAllDigits)
        << "box (" << index / 3 << ", " << index % 3 << ")";
  }
}

interface::AssignmentSolution solutionAssigning(
    std::initializer_list<std::pair<const std::string, std::string>> entries) {
  interface::AssignmentSolution solution;
  solution.assignment() = entries;
  return solution;
}

interface::AssignmentSolution solutionAssigning(
    const std::string& numberLabel,
    const std::string& square) {
  return solutionAssigning({{numberLabel, square}});
}

} // namespace

TEST(SudokuProblemTest, PartitionsApproachSolvesTheDefaultPuzzle) {
  REBALANCER_SKIP_IF_NO_HIGHS();
  expectIsSolutionOfDefaultPuzzle(solveDefaultPuzzle(kPartitionsApproach));
}

TEST(SudokuProblemTest, DimensionsApproachSolvesTheDefaultPuzzle) {
  REBALANCER_SKIP_IF_NO_HIGHS();
  expectIsSolutionOfDefaultPuzzle(solveDefaultPuzzle(kDimensionsApproach));
}

TEST(SudokuProblemTest, RejectsUnknownApproach) {
  EXPECT_THROW(
      makeSudokuSolver(defaultSudokuPuzzle(), "backtracking"),
      std::invalid_argument);
}

TEST(SudokuProblemTest, RejectsPuzzleWithWrongRowCount) {
  const std::vector<std::string> tooFewRows = {"85...24.."};
  EXPECT_THROW(
      makeSudokuSolver(tooFewRows, kPartitionsApproach), std::invalid_argument);
}

TEST(SudokuProblemTest, RejectsPuzzleRowWithWrongLength) {
  std::vector<std::string> shortRow = defaultSudokuPuzzle();
  shortRow.back().pop_back();
  EXPECT_THROW(
      makeSudokuSolver(shortRow, kPartitionsApproach), std::invalid_argument);
}

TEST(SudokuProblemTest, RejectsPuzzleWithZero) {
  std::vector<std::string> zeroDigit = defaultSudokuPuzzle();
  zeroDigit.front().front() = '0';
  EXPECT_THROW(
      makeSudokuSolver(zeroDigit, kPartitionsApproach), std::invalid_argument);
}

TEST(SudokuProblemTest, RejectsPuzzleWithNonDigitCharacter) {
  std::vector<std::string> badDigit = defaultSudokuPuzzle();
  badDigit.front().front() = 'x';
  EXPECT_THROW(
      makeSudokuSolver(badDigit, kPartitionsApproach), std::invalid_argument);
}

TEST(SudokuProblemTest, RejectsUnsolvedAssignment) {
  EXPECT_THROW(
      solvedSudokuGrid(solutionAssigning("number_5_in_row_0", "dummy_square")),
      std::invalid_argument);
}

TEST(SudokuProblemTest, RejectsSquareLabelMissingItsColumn) {
  EXPECT_THROW(
      solvedSudokuGrid(solutionAssigning("number_5_in_row_0", "square_0")),
      std::invalid_argument);
}

TEST(SudokuProblemTest, RejectsSquareLabelOneRowPastTheGrid) {
  EXPECT_THROW(
      solvedSudokuGrid(solutionAssigning("number_5_in_row_0", "square_9_0")),
      std::invalid_argument);
}

TEST(SudokuProblemTest, RejectsSquareLabelOneColumnPastTheGrid) {
  EXPECT_THROW(
      solvedSudokuGrid(solutionAssigning("number_5_in_row_0", "square_0_9")),
      std::invalid_argument);
}

TEST(SudokuProblemTest, RejectsSquareLabelWithANonNumericIndex) {
  EXPECT_THROW(
      solvedSudokuGrid(solutionAssigning("number_5_in_row_0", "square_x_0")),
      std::invalid_argument);
}

TEST(SudokuProblemTest, RejectsNumberLabelWithoutADigit) {
  EXPECT_THROW(
      solvedSudokuGrid(solutionAssigning("number", "square_0_0")),
      std::invalid_argument);
}

TEST(SudokuProblemTest, RejectsNumberLabelWithANonNumericDigit) {
  EXPECT_THROW(
      solvedSudokuGrid(solutionAssigning("number_x_in_row_0", "square_0_0")),
      std::invalid_argument);
}

TEST(SudokuProblemTest, RejectsNumberLabelBelowOne) {
  EXPECT_THROW(
      solvedSudokuGrid(solutionAssigning("number_0_in_row_0", "square_0_0")),
      std::invalid_argument);
}

TEST(SudokuProblemTest, RejectsNumberLabelAboveNine) {
  EXPECT_THROW(
      solvedSudokuGrid(solutionAssigning("number_10_in_row_0", "square_0_0")),
      std::invalid_argument);
}

TEST(SudokuProblemTest, DecodesTheCornersOfTheGrid) {
  const auto grid = solvedSudokuGrid(solutionAssigning(
      {{"number_1_in_row_0", "square_0_0"},
       {"number_9_in_row_8", "square_8_8"}}));
  EXPECT_EQ(grid.size(), 9);
  EXPECT_EQ(grid.back().size(), 9);
  EXPECT_EQ(grid[0][0], 1);
  EXPECT_EQ(grid[8][8], 9);
  EXPECT_EQ(grid[4][4], 0);
}

} // namespace facebook::rebalancer::examples
