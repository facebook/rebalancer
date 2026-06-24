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

#include "algopt/rebalancer/algopt_common/Utils.h"
#include "algopt/rebalancer/examples/common/BundleOutput.h"
#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/interface/ProblemSolverFactory.h"

#include "fmt/core.h"
#include <folly/container/irange.h>
#include <folly/init/Init.h>

#include <functional>
#include <map>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace facebook::rebalancer::interface;

DEFINE_bool(
    local_search,
    false,
    "Will use local search solver instead of optimal solver if set");

DEFINE_int32(board_size, 8, "Side length of the chess board");

DEFINE_int32(max_solve_time, 600, "Max solve time in seconds");

DEFINE_bool(do_not_print_solution, false, "Whether to skip printing the board");

static void print_board(
    std::stringstream& ss,
    size_t board_size,
    const std::function<bool(int, int)>& is_queen) {
  ss << "\n";
  for (const auto _ : folly::irange(board_size)) {
    ss << "---+";
  }
  ss << "\n";
  for (const auto row : folly::irange(board_size)) {
    for (const auto col : folly::irange(board_size)) {
      ss << (is_queen(row, col) ? " Q |" : "   |");
    }
    ss << "\n";
    for (const auto _ : folly::irange(board_size)) {
      ss << "---+";
    }
    ss << "\n";
  }
}

static int solve_eightqueens(bool use_optimal_solver) {
  auto solver = ProblemSolverFactory::makeProblemSolver("rebalancer", "tests");
  // Name things and define objects and containers in the initial assignment
  solver->setObjectName("queen");
  solver->setContainerName("square");

  // Iniitally place all queens on the first row of the chess board.
  std::map<std::pair<int, int>, int> initial_assignment;
  for (const auto i : folly::irange(FLAGS_board_size)) {
    initial_assignment[{0, i}] = i;
  }

  std::map<std::string, std::vector<std::string>> initial_board;
  for (const auto row : folly::irange(FLAGS_board_size)) {
    for (const auto col : folly::irange(FLAGS_board_size)) {
      auto square = fmt::format("s_{}_{}", row, col);
      initial_board[square] = {};
      auto it = initial_assignment.find({row, col});
      if (it != initial_assignment.end()) {
        initial_board[square].emplace_back(fmt::format("q_{}", it->second));
      }
    }
  }
  solver->setAssignment(initial_board);

  if (!FLAGS_do_not_print_solution) {
    std::stringstream ss;
    print_board(ss, FLAGS_board_size, [&](int row, int col) {
      return initial_assignment.contains({row, col});
    });
    XLOG(INFO) << "Initial assignment";
    XLOG(INFO) << ss.str();
  }

  // define scopes
  std::map<std::string, std::string> chess_rows, chess_columns, chess_diag_lr,
      chess_diag_rl;

  for (const auto row : folly::irange(FLAGS_board_size)) {
    for (const auto col : folly::irange(FLAGS_board_size)) {
      chess_rows[fmt::format("s_{}_{}", row, col)] = fmt::format("row_{}", row);
      chess_columns[fmt::format("s_{}_{}", row, col)] =
          fmt::format("column_{}", col);
      chess_diag_lr[fmt::format("s_{}_{}", row, col)] =
          fmt::format("diag_lr_{}", row - col + FLAGS_board_size);
      chess_diag_rl[fmt::format("s_{}_{}", row, col)] =
          fmt::format("diag_rl_{}", row + col);
    }
  }

  solver->addScope("chess_rows", chess_rows);
  solver->addScope("chess_columns", chess_columns);
  solver->addScope("chess_diag_lr", chess_diag_lr);
  solver->addScope("chess_diag_rl", chess_diag_rl);

  // define partition groups
  std::map<std::string, std::string> queens_group;
  for (const auto i : folly::irange(FLAGS_board_size)) {
    queens_group[fmt::format("q_{}", i)] = "queens";
  }

  const std::string partition_name = "queens_group";
  solver->addPartition(partition_name, queens_group);

  // add constraints
  constexpr std::array<std::string_view, 4> scopes = {
      "chess_rows", "chess_columns", "chess_diag_lr", "chess_diag_rl"};
  for (auto& scope : scopes) {
    GroupCountSpec groupCountSpec;
    groupCountSpec.scope() = scope;
    groupCountSpec.partitionName() = partition_name;
    groupCountSpec.definition() = GroupCountSpecDefinition::AFTER;

    Limit limit = Limit();
    limit.type() = LimitType::ABSOLUTE;
    limit.globalLimit() = 1;
    groupCountSpec.limit() = limit;

    solver->addConstraint(groupCountSpec);
  }

  // use either LocalSearchSolver or Optimal solver
  if (use_optimal_solver) {
    OptimalSolverSpec spec;
    spec.solverPackage() = OptimalSolverPackage::HIGHS;
    if (FLAGS_max_solve_time == 0) {
      spec.skipMipSolveForTesting() = true;
    } else {
      spec.solveTime() = FLAGS_max_solve_time;
    }
    solver->addSolver(spec);
  } else {
    LocalSearchSolverSpec spec;
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec()));
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(TripleLoopMoveTypeSpec()));
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(KLSearchMoveTypeSpec()));
    spec.solveTime() = FLAGS_max_solve_time;
    solver->addSolver(spec);
  }

  auto solution = solver->solve();
  facebook::rebalancer::examples::maybeSaveBundle(*solver);

  if (!FLAGS_do_not_print_solution) {
    std::stringstream ss;
    const auto selected = *solution.assignment() | std::views::values |
        facebook::algopt::utils::to<std::set>;
    print_board(ss, FLAGS_board_size, [&](int row, int col) {
      return selected.contains(fmt::format("s_{}_{}", row, col));
    });
    XLOG(INFO) << "Final assignment";
    XLOG(INFO) << ss.str();
  }

  if (*solution.finalObjective()->value() > 0.01) {
    XLOG(FATAL) << "failed to solve completely";
  }
  return 0;
}

// See tutorial at:
// https://www.internalfb.com/intern/wiki/ReBalancer/API/Tutorial/
int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  solve_eightqueens(!FLAGS_local_search);

  return 0;
}
