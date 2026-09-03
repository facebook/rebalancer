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

#pragma once

#include "algopt/rebalancer/interface/ProblemSolver.h"

#include <folly/Range.h>

#include <memory>
#include <string>
#include <vector>

namespace facebook::rebalancer::examples {

inline constexpr folly::StringPiece kDimensionsApproach{"dimensions"};
inline constexpr folly::StringPiece kPartitionsApproach{"partitions"};

// The 9x9 puzzle solved by //algopt/rebalancer/examples/sudoku:sudoku_cpp.
const std::vector<std::string>& defaultSudokuPuzzle();

// Builds the Sudoku assignment problem, ready to solve(). `puzzle` must be 9
// rows of 9 characters, each '1'-'9' or '.'; `approach` must be
// kPartitionsApproach or kDimensionsApproach. Throws std::invalid_argument
// otherwise.
std::unique_ptr<interface::ProblemSolver> makeSudokuSolver(
    const std::vector<std::string>& puzzle,
    folly::StringPiece approach);

// Decodes a solved assignment into the 9x9 grid of digits it represents,
// indexed [row][col]. Throws std::invalid_argument if any number is still
// parked on the dummy square, which means the puzzle was not solved, or if a
// label does not carry the row, column and digit this decoder expects.
std::vector<std::vector<int>> solvedSudokuGrid(
    const interface::AssignmentSolution& solution);

} // namespace facebook::rebalancer::examples
