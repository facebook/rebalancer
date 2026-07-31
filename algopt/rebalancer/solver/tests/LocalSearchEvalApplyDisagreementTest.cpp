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

#include "algopt/rebalancer/algopt_common/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/common/log/InMemoryLog.h"
#include "algopt/rebalancer/entities/tests/UniverseBuilderTestUtils.h"
#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/solver/expressions/LinearSum.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/solvers/LocalSearchSolver.h"
#include "algopt/rebalancer/solver/tests/ExprProblemCreation.h"
#include "algopt/rebalancer/solver/utils/ProblemConfigs.h"

#include <gtest/gtest.h>

#include <memory>

namespace facebook::rebalancer::tests {

class LocalSearchEvalApplyDisagreementTest
    : public ::testing::Test,
      public entities::tests::UniverseBuilderTestUtils {
 protected:
  static constexpr double kContribution = 4194400.0;
  static constexpr double kInitialValue = 79.00000000582155;

  std::shared_ptr<const entities::Universe> buildNetZeroUniverse() {
    algopt::common::thrift::PrecisionTolerances tolerances;
    tolerances.absolute() = 1e-12;
    tolerances.relative() = 1e-12;
    universeBuilder_.setPrecision(tolerances);

    setInitialAssignment(
        entities::Map<std::string, std::vector<std::string>>{
            {"container0", {"object0"}}, {"container1", {}}});
    return buildUniverse();
  }

  ExprPtr makeNetZeroObjective(const entities::Universe& universe) {
    const Assignment assignment(
        universe.getContainers().getInitialAssignment());
    // These constants reproduce a production case where different
    // floating-point operation orders make evaluate() see an improvement that
    // disappears on apply().
    const auto source = variable(object(0), container(0), universe, assignment);
    const auto destination =
        variable(object(0), container(1), universe, assignment);
    const auto offset = const_expr(1.0, universe);
    return std::make_shared<LinearSum>(
        universe,
        kInitialValue,
        PackerMap<std::shared_ptr<Expression>, double>{
            {source, kContribution},
            {destination, kContribution},
            {offset, -kContribution}});
  }
};

TEST_F(LocalSearchEvalApplyDisagreementTest, UndoesMoveAndStopsLocalSearch) {
  const auto universe = buildNetZeroUniverse();
  auto logger = std::make_shared<InMemoryLog>();
  ProblemConfigs config;
  config.logger = logger;

  auto problem = packer::tests::createTestProblem(
      universe,
      /*objectiveTuple=*/{makeNetZeroObjective(*universe)},
      /*constraint=*/const_expr(0, *universe),
      /*nonAcceptingContainers=*/{},
      /*config=*/config);

  interface::LocalSearchSolverSpec spec;
  spec.stopAfterMoves() = 10;
  spec.moveTypeList() = {interface::ProblemSolver::makeMoveTypeSpec(
      interface::SingleMoveTypeSpec{})};

  LocalSearchSolver solver(std::move(spec));
  EXPECT_FALSE(solver.solve(*problem));

  const auto& summaries = logger->getSolverSummaries();
  ASSERT_EQ(1, summaries.size());
  const auto& summary = summaries.front();
  EXPECT_EQ(
      interface::EndReason::UNABLE_TO_FIND_IMPROVING_MOVES, summary.endReason);
  ASSERT_TRUE(summary.auxInfo.has_value());
  EXPECT_EQ("candidate move does not improve the objective", *summary.auxInfo);
  ASSERT_TRUE(summary.moveStats.has_value());
  EXPECT_EQ(0, *summary.moveStats->numMoves());

  EXPECT_TRUE(logger->flushMoves().empty());
  EXPECT_EQ(container(0), problem->assignment.getContainer(object(0)));
  EXPECT_DOUBLE_EQ(kInitialValue, problem->objective.getValue().get(0));

  const auto& profiles = logger->getLocalSearchProfiles();
  ASSERT_EQ(1, profiles.size());
  ASSERT_EQ(1, profiles.front().moveTypeEvents()->size());
  const auto& event = profiles.front().moveTypeEvents()->front();
  EXPECT_DOUBLE_EQ(*event.initialValue(), *event.finalValue());
}

} // namespace facebook::rebalancer::tests
