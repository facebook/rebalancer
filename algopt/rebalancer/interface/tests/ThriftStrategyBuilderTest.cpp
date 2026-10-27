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

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/interface/ThriftStrategyBuilder.h"
#include "algopt/rebalancer/tests/SolverTestUtils.h"

#include <gtest/gtest.h>

using namespace facebook::rebalancer::interface;
using facebook::rebalancer::SolverT;

TEST(ThriftStrategyBuilderTest, DefaultStrategy) {
  ThriftStrategyBuilder builder;
  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.build(),
      "Expected at least one solver to be added using addSolver()");
}

TEST(ThriftStrategyBuilderTest, LocalSearchSolver) {
  ThriftStrategyBuilder builder;
  LocalSearchSolverSpec spec;
  spec.solveTime() = 300;
  spec.stopAfterMoves() = 10;
  spec.timePerMove() = 30;
  spec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  spec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec()));
  spec.randomSeed() = 42;
  builder.addSolver(spec);
  auto strategy = builder.build();

  ASSERT_EQ(1, strategy.solvers()->size());
  auto& solver = strategy.solvers()[0];

  EXPECT_EQ(SolverT::Type::localSearchSolverSpec, solver.getType());
  auto& localSearch = solver.localSearchSolverSpec().as_const().value();

  EXPECT_EQ(10, *localSearch.stopAfterMoves());
  EXPECT_EQ(30, *localSearch.timePerMove());
  EXPECT_EQ(42, *localSearch.randomSeed());

  const auto& moveTypes = *localSearch.moveTypeList();
  EXPECT_EQ(2, moveTypes.size());
  EXPECT_EQ(MoveTypeSpec::Type::singleMoveTypeSpec, moveTypes.at(0).getType());
  EXPECT_EQ(MoveTypeSpec::Type::swapMoveTypeSpec, moveTypes.at(1).getType());
}

TEST(ThriftStrategyBuilderTest, OptimalSolver) {
  // Arrange
  ThriftStrategyBuilder builder;
  OptimalSolverSpec spec;
  spec.solveTime() = 300;
  spec.skipInitialAssignmentHint() = true;
  spec.xpressLogFile() = "./tmp_log_file.txt";
  spec.suppressLogs() = true;
  spec.solverPackage() = OptimalSolverPackage::XPRESS;
  builder.addSolver(spec);

  // Act
  auto strategy = builder.build();

  // Assert
  ASSERT_EQ(1, strategy.solvers()->size());
  auto& solver = strategy.solvers()[0];

  EXPECT_EQ(SolverT::Type::optimalSolverSpec, solver.getType());
  auto& optimal = solver.optimalSolverSpec().as_const().value();

  EXPECT_EQ(300, *optimal.solveTime());
  EXPECT_TRUE(*optimal.suppressLogs());
  EXPECT_TRUE(*optimal.skipInitialAssignmentHint());
  EXPECT_STREQ("./tmp_log_file.txt", optimal.xpressLogFile()->c_str());

  EXPECT_EQ(OptimalSolverPackage::XPRESS, *optimal.solverPackage());
}

TEST(ArgsStrategyBuilderTest, MultipleSolvers) {
  ThriftStrategyBuilder builder;
  builder.addSolver(LocalSearchSolverSpec());
  builder.addSolver(OptimalSolverSpec());
  builder.addSolver(LocalSearchSolverSpec());

  auto strategy = builder.build();

  auto& solvers = *strategy.solvers();
  ASSERT_EQ(3, solvers.size());
  EXPECT_EQ(SolverT::Type::localSearchSolverSpec, solvers[0].getType());
  EXPECT_EQ(SolverT::Type::optimalSolverSpec, solvers[1].getType());
  EXPECT_EQ(SolverT::Type::localSearchSolverSpec, solvers[2].getType());
}
