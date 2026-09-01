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

#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/solver/solvers/LocalSearchStageSolver.h"

#include <gtest/gtest.h>

using namespace facebook::rebalancer::interface;

namespace facebook::rebalancer::tests {

namespace {

// Solver level (LocalSearchStageSolverSpec) sets batching/128.
// Stage 0 sets nothing at stage level, so it must take the solver level value.
// Stage 1 sets sliding-window at stage level, which must override it.
LocalSearchStageSolverSpec makeTwoStageSpec() {
  LocalSearchStageSolverSpec spec;

  BatchingExecutionConfig batchingConfig;
  batchingConfig.batchSize() = 128;
  ParallelExecutionConfig solverLevelConfig;
  solverLevelConfig.set_batching(std::move(batchingConfig));
  spec.parallelExecutionConfig() = solverLevelConfig;

  spec.stageSpecs()->emplace_back();

  ParallelExecutionConfig stageLevelConfig;
  stageLevelConfig.set_slidingWindow(SlidingWindowExecutionConfig{});
  spec.stageSpecs()->emplace_back();
  spec.stageSpecs()->back().solverSpec()->parallelExecutionConfig() =
      stageLevelConfig;

  return spec;
}

std::optional<ParallelExecutionConfig> getStageConfig(
    const LocalSearchStageSolver& solver,
    size_t stageId) {
  return solver.getConfigs()
      .stageSpecs()
      ->at(stageId)
      .solverSpec()
      ->parallelExecutionConfig()
      .to_optional();
}

} // namespace

// The overrides have to be applied by the constructor. solve() builds the move
// types from the stage specs, and a move type keeps a copy of
// parallelExecutionConfig, so overriding any later has no effect.
TEST(LocalSearchStageSolverTest, StageWithoutConfigTakesSolverLevelConfig) {
  const LocalSearchStageSolver solver(makeTwoStageSpec());

  const auto config = getStageConfig(solver, 0);
  ASSERT_TRUE(config.has_value());
  ASSERT_EQ(config->getType(), ParallelExecutionConfig::Type::batching);
  EXPECT_EQ(*config->get_batching().batchSize(), 128);
}

TEST(LocalSearchStageSolverTest, StageLevelConfigOverridesSolverLevelConfig) {
  const LocalSearchStageSolver solver(makeTwoStageSpec());

  const auto config = getStageConfig(solver, 1);
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->getType(), ParallelExecutionConfig::Type::slidingWindow);
}

} // namespace facebook::rebalancer::tests
