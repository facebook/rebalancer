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

#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"
#include "algopt/rebalancer/solver/moves/SingleChainFastMoveType.h"
#include "algopt/rebalancer/solver/moves/tests/MoveTestBase.h"

#include <fmt/core.h>
#include <folly/container/irange.h>
#include <folly/coro/BlockingWait.h>
#include <gtest/gtest.h>

#include <limits>

namespace facebook::rebalancer::packer::tests {

class MockSingleChainFastMoveType : public SingleChainFastMoveType {
 public:
  explicit MockSingleChainFastMoveType(
      const interface::LocalSearchSolverSpec& solverConfigs,
      const interface::SingleChainFastMoveTypeSpec& spec)
      : SingleChainFastMoveType(solverConfigs, spec) {}

  MoveResult exploreWithinGroupAndGetBestResult(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      entities::ObjectId hotObject,
      entities::ContainerId coldContainer,
      entities::ContainerId otherContainer,
      const std::string& partitionName,
      MoveStatsAggregator& stats,
      bool greedy) {
    return this->SingleChainFastMoveType::exploreWithinGroupAndGetBestResult(
        evaluator,
        hotContainer,
        hotObject,
        coldContainer,
        otherContainer,
        partitionName,
        stats,
        greedy);
  }

  MoveResult exploreAllAndGetBestResult(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      entities::ObjectId hotObject,
      entities::ContainerId coldContainer,
      entities::ContainerId otherContainer,
      MoveStatsAggregator& stats,
      bool greedy) {
    return this->SingleChainFastMoveType::exploreAllAndGetBestResult(
        evaluator,
        hotContainer,
        hotObject,
        coldContainer,
        otherContainer,
        stats,
        greedy);
  }

  MoveResult generateMoveSetAndGetResult(
      const MovesEvaluator& evaluator,
      entities::ObjectId hotObject,
      entities::ContainerId hotContainer,
      entities::ContainerId coldContainer,
      entities::ObjectId otherObject,
      entities::ContainerId otherContainer,
      MoveStatsAggregator& stats) {
    return this->SingleChainFastMoveType::generateMoveSetAndGetResult(
        evaluator,
        hotObject,
        hotContainer,
        coldContainer,
        otherObject,
        otherContainer,
        stats);
  }
};

class SingleChainFastMoveTypeTest : public MoveTestBase {
 protected:
  const std::string kPartitionName = "tenant";

  SingleChainFastMoveTypeTest() : MoveTestBase("replica", "region") {}

  folly::coro::Task<std::shared_ptr<const entities::Universe>> setUpUniverse() {
    setInitialAssignment({
        {"region1", {"replica1", "replica2", "replica3", "replica4"}},
        {"region2", {}},
        {"region3", {"replica5", "replica6", "replica7"}},
        {"region4", {"replica8"}},
    });

    co_await addPartition(
        kPartitionName,
        {
            {"tenant1", {"replica1", "replica3", "replica5", "replica7"}},
            {"tenant2", {"replica2", "replica4", "replica6", "replica8"}},
        });

    co_return buildUniverse();
  }

  entities::PartitionId partition() const {
    return partitionId(kPartitionName);
  }

  ExprPtr getLookupExprOn(PackerSet<entities::ContainerId> containers) {
    auto objectValues = PackerMap<entities::ObjectId, double>();
    for (const auto id : folly::irange(1, 9)) {
      objectValues.emplace(object(id), -id);
    }
    auto objectVector = makeObjectVector(objectValues, getUniverse());
    const Assignment assignment(
        getUniverse().getContainers().getInitialAssignment());
    return std::make_shared<ObjectLookup>(
        objectVector,
        std::make_shared<PackerSet<entities::ContainerId>>(containers),
        getUniverse(),
        assignment);
  }
};

CO_TEST_F(SingleChainFastMoveTypeTest, TestMoveSet) {
  auto mockSingleChainFastMoveType = MockSingleChainFastMoveType(
      interface::LocalSearchSolverSpec{},
      interface::SingleChainFastMoveTypeSpec());

  const auto universe = co_await setUpUniverse();

  // objective and constraint are not relevant when directly testing
  // for the moves evaluated in 'generateMoveSetAndGetResult'
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  auto moveResult = mockSingleChainFastMoveType.generateMoveSetAndGetResult(
      getMovesEvaluator(),
      object(1) /*hotObject*/,
      container(1) /*hotContainer*/,
      container(2) /*coldContainer*/,
      object(5) /*otherObject*/,
      container(3) /*otherContainer*/,
      getMoveStatsAggregator());

  // we expect 1 moveSets to be evaluated which has 2 moves.
  EXPECT_EQ(2, getTotalMovesEvaluated());

  const std::vector<Move> expectedMoveSet = {
      Move{object(5), container(3), container(1)},
      Move{object(1), container(1), container(2)},
  };
  REBALANCER_EXPECT_EQ_MOVESETS(expectedMoveSet, moveResult.getMoveSet());
}

CO_TEST_F(
    SingleChainFastMoveTypeTest,
    ExploreMovesOnlyWithinGroupInOtherContainer) {
  interface::SingleChainFastMoveTypeSpec singleChainFastSpec;
  singleChainFastSpec.partitionNameToExploreFastChainsWithinObjectGroup() =
      kPartitionName;

  auto mockSingleChainFastMoveType = MockSingleChainFastMoveType(
      interface::LocalSearchSolverSpec{}, singleChainFastSpec);

  const auto universe = co_await setUpUniverse();
  // lookup expression ensures that the no two objects are equivalent
  createProblem({getLookupExprOn({container(1)})}, const_expr(0, *universe));

  mockSingleChainFastMoveType.exploreWithinGroupAndGetBestResult(
      getMovesEvaluator(),
      container(1) /*hotContainer*/,
      object(1) /*hotObject*/,
      container(2) /*coldContainer*/,
      container(3) /*otherContainer*/,
      kPartitionName,
      getMoveStatsAggregator(),
      true /*greedy*/);

  // we expect a single moveSet to be evaluated (because the first evaluated
  // move is already an improvement), with 2 total moves.
  EXPECT_EQ(2, getTotalMovesEvaluated());
}

CO_TEST_F(
    SingleChainFastMoveTypeTest,
    ExploreMovesWithAllObjectsInOtherContainer) {
  auto mockSingleChainFastMoveType = MockSingleChainFastMoveType(
      interface::LocalSearchSolverSpec{},
      interface::SingleChainFastMoveTypeSpec());

  const auto universe = co_await setUpUniverse();
  // lookup expression ensures that the no two objects are equivalent
  createProblem({getLookupExprOn({container(1)})}, const_expr(0, *universe));

  mockSingleChainFastMoveType.exploreAllAndGetBestResult(
      getMovesEvaluator(),
      container(1) /*hotContainer*/,
      object(1) /*hotObject*/,
      container(2) /*coldContainer*/,
      container(3) /*otherContainer*/,
      getMoveStatsAggregator(),
      true /*greedy*/);

  // we expect a single moveSet to be evaluated (because otherContainer
  // container(3) has 3 non-equivalent objects, but the first one is already an
  // improvement), with 2 total moves.
  EXPECT_EQ(2, getTotalMovesEvaluated());
}

CO_TEST_F(
    SingleChainFastMoveTypeTest,
    IgnoreEquivalentObjectsWhenExploringWithinGroup) {
  interface::SingleChainFastMoveTypeSpec singleChainFastSpec;
  singleChainFastSpec.partitionNameToExploreFastChainsWithinObjectGroup() =
      kPartitionName;

  auto mockSingleChainFastMoveType = MockSingleChainFastMoveType(
      interface::LocalSearchSolverSpec{}, singleChainFastSpec);

  const auto universe = co_await setUpUniverse();
  // since objective and constraint are const_expr(0, universe), all
  // objects will be equivalent
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  mockSingleChainFastMoveType.exploreWithinGroupAndGetBestResult(
      getMovesEvaluator(),
      container(1) /*hotContainer*/,
      object(1) /*hotObject*/,
      container(2) /*coldContainer*/,
      container(3) /*otherContainer*/,
      kPartitionName,
      getMoveStatsAggregator(),
      true /*greedy*/);

  EXPECT_EQ(2, getTotalMovesEvaluated());
}

CO_TEST_F(
    SingleChainFastMoveTypeTest,
    IgnoreEquivalentObjectsWhenExploringAll) {
  auto mockSingleChainFastMoveType = MockSingleChainFastMoveType(
      interface::LocalSearchSolverSpec{},
      interface::SingleChainFastMoveTypeSpec());

  const auto universe = co_await setUpUniverse();
  // since objective and constraint are const_expr(0, universe), all
  // objects will be equivalent
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  mockSingleChainFastMoveType.exploreAllAndGetBestResult(
      getMovesEvaluator(),
      container(1) /*hotContainer*/,
      object(1) /*hotObject*/,
      container(2) /*coldContainer*/,
      container(3) /*otherContainer*/,
      getMoveStatsAggregator(),
      true /*greedy*/);

  EXPECT_EQ(2, getTotalMovesEvaluated());
}

CO_TEST_F(SingleChainFastMoveTypeTest, TestTotalNumberOfEvaluations) {
  auto mockSingleChainFastMoveType = MockSingleChainFastMoveType(
      interface::LocalSearchSolverSpec{},
      interface::SingleChainFastMoveTypeSpec());

  const auto universe = co_await setUpUniverse();
  // lookup expression ensures that the no two objects are equivalent
  createProblem({getLookupExprOn({container(1)})}, const_expr(0, *universe));

  mockSingleChainFastMoveType.exploreFromAllSingleMoves(
      getMovesEvaluator(),
      container(1) /*hotContainer*/,
      object(1) /*hotObject*/,
      container(2) /*coldContainer*/,
      getMoveStatsAggregator());

  // we expect 4 moveSet evaluations, 3 with container(3) as the
  // 'otherContainer' and 1 with container(4) as the 'otherContainer'.
  EXPECT_EQ(8, getTotalMovesEvaluated());
}

// specialFastColdContainer should send the hot object only to that cold
// container, so setting it should evaluate fewer moves. The const-0 objective
// makes all objects equal, so no move improves (nothing is skipped early) and
// each container counts as one object. A chain is 2 moves: the hot object goes
// to a cold container, and one object from another container moves into the hot
// container.
CO_TEST_F(
    SingleChainFastMoveTypeTest,
    FindBestMoveHonorsSpecialFastColdContainer) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  const auto movesEvaluated =
      [&](const interface::SingleChainFastMoveTypeSpec& spec) {
        MockSingleChainFastMoveType(interface::LocalSearchSolverSpec{}, spec)
            .findBestMove(
                getMovesEvaluator(),
                container(1) /*hotContainer*/,
                getMoveStatsAggregator(),
                getEmptySearchHints(),
                std::numeric_limits<double>::max() /*timeLimit*/);
        return getTotalMovesEvaluated();
      };

  interface::SingleChainFastMoveTypeSpec restricted;
  restricted.specialFastColdContainer() = "region2";

  // Cold containers {region2, region3, region4}; region2 is empty:
  //   into region2: from region3 and region4 = 2 chains = 4 moves
  //   into region3: from region4             = 1 chain  = 2 moves
  //   into region4: from region3             = 1 chain  = 2 moves
  EXPECT_EQ(8, movesEvaluated(interface::SingleChainFastMoveTypeSpec{}));

  // Restricting to region2 keeps only the first line: 4 moves.
  EXPECT_EQ(4, movesEvaluated(restricted));
}

TEST_F(SingleChainFastMoveTypeTest, Name) {
  auto mockSingleChainFastMoveType = MockSingleChainFastMoveType(
      interface::LocalSearchSolverSpec{},
      interface::SingleChainFastMoveTypeSpec());
  EXPECT_EQ(mockSingleChainFastMoveType.name(), "SINGLE_CHAIN_FAST");
}

} // namespace facebook::rebalancer::packer::tests
