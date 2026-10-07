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
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/SingleFastMoveType.h"
#include "algopt/rebalancer/solver/moves/tests/MoveTestBase.h"
#include "algopt/rebalancer/solver/tests/ExprProblemCreation.h"

#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class SingleFastMoveTypeTest : public MoveTestBase {
 protected:
  SingleFastMoveTypeTest() : MoveTestBase("object", "container") {}

  folly::coro::Task<std::shared_ptr<const entities::Universe>> setUpUniverse(
      std::optional<entities::Map<std::string, std::vector<std::string>>>
          initialAssignment = std::nullopt) {
    const static auto defaultInitialAssignment =
        entities::Map<std::string, std::vector<std::string>>{
            {"container1", {"object1", "object2"}},
            {"container2", {"object4"}},
            {"container3", {"object5", "object6", "object7", "object3"}},
            {"container4", {"object8"}},
            {"container5", {"object9"}}};

    const auto& assignment =
        initialAssignment ? *initialAssignment : defaultInitialAssignment;

    setInitialAssignment(assignment);

    co_await addScope(
        "region",
        {{"region1", {"container1", "container3"}},
         {"region2", {"container2", "container4"}}});

    co_await addPartition("job", {{"j1", {"object9"}}, {"j2", {"object4"}}});

    co_return buildUniverse();
  }
};

CO_TEST_F(SingleFastMoveTypeTest, VerifyMoveSetBasic) {
  const interface::SingleFastMoveTypeSpec singleFastSpec;
  auto singleFastMoveType =
      SingleFastMoveType(interface::LocalSearchSolverSpec{}, singleFastSpec);

  const auto universe = co_await setUpUniverse();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{
          container(1), container(3), container(4)});

  createProblem(
      /*objectiveTuple=*/
      {object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(universe->getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  auto bestResult = singleFastMoveType.findBestMove(
      getMovesEvaluator(),
      container(3) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // container(3) has four objects and there are four other containers to try
  // moving the objects to, But moving any one of the objects to container (2)
  // or container(5) will improve the objective so we only expect 4 moves in
  // total.
  EXPECT_EQ(4, getTotalMovesEvaluated());
}

CO_TEST_F(SingleFastMoveTypeTest, VerifyMoveSetBasicBiggerMinHotObject) {
  interface::SingleFastMoveTypeSpec singleFastSpec;
  singleFastSpec.minHotObjects() = 3;

  auto singleFastMoveType =
      SingleFastMoveType(interface::LocalSearchSolverSpec{}, singleFastSpec);

  const auto universe = co_await setUpUniverse();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{
          container(1), container(3), container(4)});

  createProblem(
      /*objectiveTuple=*/
      {object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(universe->getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  auto bestResult = singleFastMoveType.findBestMove(
      getMovesEvaluator(),
      container(3) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // container(3) has four objects and there are four other containers to try
  // moving the objects to, But moving any one of the objects to container (2)
  // or container(5) will improve the objective so we only expect 12 moves in
  // total because we will try at least 3 objects.
  EXPECT_EQ(12, getTotalMovesEvaluated());
}

CO_TEST_F(SingleFastMoveTypeTest, VerifyMoveSetBasic2) {
  const interface::SingleFastMoveTypeSpec singleFastSpec;
  auto singleFastMoveType =
      SingleFastMoveType(interface::LocalSearchSolverSpec{}, singleFastSpec);

  const auto universe = co_await setUpUniverse();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{
          container(1),
          container(2),
          container(3),
          container(4),
          container(5)});

  createProblem(
      /*objectiveTuple=*/
      {object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(universe->getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  const auto bestResult = singleFastMoveType.findBestMove(
      getMovesEvaluator(),
      container(3) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // container(3) has four objects and there are four other containers to try
  // moving the objects to, But no move will improve the objective so all
  // objects will be evaluated and we expect 16 moves in total.
  EXPECT_EQ(16, getTotalMovesEvaluated());
}

CO_TEST_F(SingleFastMoveTypeTest, VerifyMoveEvalsWithExploreInRegion) {
  interface::MoveToCurrentScopeItemSpec moveToCurrentScopeItemSpec;
  moveToCurrentScopeItemSpec.scopeNameForExploringMovesToCurrentScopeItem() =
      "region";
  interface::DestinationsToExploreOptions destinationsToExplore;
  destinationsToExplore.set_moveToCurrentScopeItem() =
      moveToCurrentScopeItemSpec;
  interface::SingleFastMoveTypeSpec singleFastSpec;
  singleFastSpec.destinationsToExplore() = destinationsToExplore;

  auto singleFastMoveType =
      SingleFastMoveType(interface::LocalSearchSolverSpec{}, singleFastSpec);

  const auto universe = co_await setUpUniverse();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{container(3)});

  createProblem(
      /*objectiveTuple=*/
      {object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(getUniverse().getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  auto bestResult = singleFastMoveType.findBestMove(
      getMovesEvaluator(),
      /*hotContainer=*/container(3),
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      /*timeLimit=*/std::numeric_limits<double>::max());

  // container(3) has four objects and there is only one other container,
  // container(1), in the same region. Additionally, moving any object will
  // improve the objective so only 1 move will be evaluated.
  EXPECT_EQ(1, getTotalMovesEvaluated());
}

CO_TEST_F(
    SingleFastMoveTypeTest,
    VerifyMoveEvalsWithinRegionButHotContainerNotPartOfScope) {
  interface::MoveToCurrentScopeItemSpec moveToCurrentScopeItemSpec;
  moveToCurrentScopeItemSpec.scopeNameForExploringMovesToCurrentScopeItem() =
      "region";
  interface::DestinationsToExploreOptions destinationsToExplore;
  destinationsToExplore.set_moveToCurrentScopeItem() =
      moveToCurrentScopeItemSpec;
  interface::SingleFastMoveTypeSpec singleFastSpec;
  singleFastSpec.destinationsToExplore() = destinationsToExplore;

  auto singleFastMoveType =
      SingleFastMoveType(interface::LocalSearchSolverSpec{}, singleFastSpec);

  const auto universe = co_await setUpUniverse();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{
          container(1), container(2), container(3)});

  createProblem(
      /*objectiveTuple=*/
      {object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(universe->getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  auto bestResult = singleFastMoveType.findBestMove(
      getMovesEvaluator(),
      container(5) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // container(5) has one object, object(9), and we only want to explore
  // in-region destinations. But container(5) is not part of any region so all
  // other containers will be explored. Thus, we expect 4 moves to be evaluated.
  // Only the move to container(4) will improve the objective.
  EXPECT_EQ(4, getTotalMovesEvaluated());

  const std::vector<Move> expectedMoveSet = {
      {Move{object(9), container(5), container(4)}}};
  REBALANCER_EXPECT_EQ_MOVESETS(expectedMoveSet, bestResult.getMoveSet());
}

CO_TEST_F(SingleFastMoveTypeTest, VerifyMoveEvalsWithMoveToScopeItems) {
  // by default look at all containers as destinations
  interface::ScopeItemList defaultScopeItemList;
  defaultScopeItemList.scopeName() = "container";

  interface::ScopeItemList scopeItemList1;
  scopeItemList1.scopeName() = "region";
  scopeItemList1.scopeItems() = {"region1"};

  interface::ScopeItemList scopeItemList2;
  scopeItemList2.scopeName() = "container";
  scopeItemList2.scopeItems() = {"container2", "container4"};

  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItemList;
  moveToScopeItemsSpec.objectToScopeItems() = {
      {"object4", scopeItemList1}, {"object8", scopeItemList2}};

  interface::DestinationsToExploreOptions destinationsToExplore;
  destinationsToExplore.set_moveToScopeItems() = moveToScopeItemsSpec;
  interface::SingleFastMoveTypeSpec singleFastSpec;
  singleFastSpec.destinationsToExplore() = destinationsToExplore;

  auto singleFastMoveType =
      SingleFastMoveType(interface::LocalSearchSolverSpec{}, singleFastSpec);

  const auto universe = co_await setUpUniverse();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{
          container(1), container(2), container(3), container(5)});

  createProblem(
      /*objectiveTuple=*/
      {object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(universe->getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  {
    auto bestResult = singleFastMoveType.findBestMove(
        getMovesEvaluator(),
        container(5) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // container(5) has one object and by default we expect all destinations
    // to be explored
    EXPECT_EQ(4, getTotalMovesEvaluated());

    const std::vector<Move> expectedMoveSet = {
        {Move{object(9), container(5), container(4)}}};
    REBALANCER_EXPECT_EQ_MOVESETS(expectedMoveSet, bestResult.getMoveSet());
  }

  {
    auto bestResult = singleFastMoveType.findBestMove(
        getMovesEvaluator(),
        /*hotContainer=*/container(2),
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        /*timeLimit=*/std::numeric_limits<double>::max());

    // container(2) has one object, object(4), which is in region1 so both
    // destinations in region1 will be evaluated.
    EXPECT_EQ(2, getTotalMovesEvaluated());
  }

  {
    auto bestResult = singleFastMoveType.findBestMove(
        getMovesEvaluator(),
        container(4) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // container(4) has one object, object(8), which wants to explore moves to
    // container(2) and container(4). Since container(4) is hot, we only expect
    // 1 evaluation.
    EXPECT_EQ(1, getTotalMovesEvaluated());
  }
}

CO_TEST_F(SingleFastMoveTypeTest, VerifyMoveEvalsGroupToScopeItemLists) {
  // by default look at all containers as destinations
  interface::ScopeItemList defaultScopeItemList;
  defaultScopeItemList.scopeName() = "container";

  interface::ScopeItemList scopeItemList1;
  scopeItemList1.scopeName() = "region";
  scopeItemList1.scopeItems() = {"region1"};

  interface::ScopeItemList scopeItemList2;
  scopeItemList2.scopeName() = "container";
  scopeItemList2.scopeItems() = {"container2", "container4"};

  interface::ScopeItemList scopeItemList3;
  scopeItemList3.scopeName() = "container";
  scopeItemList3.scopeItems() = {"container1", "container4"};

  interface::ScopeItemList scopeItemList4;
  scopeItemList4.scopeName() = "container";
  scopeItemList4.scopeItems() = {"container2"};

  interface::GroupToScopeItemList groupToScopeItemList;
  groupToScopeItemList.groupToScopeItemList()->emplace("j1", scopeItemList3);
  groupToScopeItemList.groupToScopeItemList()->emplace("j2", scopeItemList4);
  groupToScopeItemList.partitionName() = "job";

  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItemList;
  moveToScopeItemsSpec.objectToScopeItems() = {
      {"object4", scopeItemList1}, {"object8", scopeItemList2}};
  moveToScopeItemsSpec.scopeItemsPerGroups() = groupToScopeItemList;

  interface::DestinationsToExploreOptions destinationsToExplore;
  destinationsToExplore.set_moveToScopeItems() = moveToScopeItemsSpec;
  interface::SingleFastMoveTypeSpec singleFastSpec;
  singleFastSpec.destinationsToExplore() = destinationsToExplore;

  auto singleFastMoveType =
      SingleFastMoveType(interface::LocalSearchSolverSpec{}, singleFastSpec);

  const auto universe = co_await setUpUniverse();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{
          container(1), container(2), container(3), container(5)});

  createProblem(
      /*objectiveTuple=*/
      {object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(getUniverse().getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, getUniverse()));

  {
    auto bestResult = singleFastMoveType.findBestMove(
        getMovesEvaluator(),
        /*hotContainer=*/container(5),
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // container(5) has one object, object(9) which belongs to the j1 group.
    // Thus only moves to container(1) and container(4) will be evaluated.
    EXPECT_EQ(2, getTotalMovesEvaluated());

    const std::vector<Move> expectedMoveSet = {
        {Move{object(9), container(5), container(4)}}};
    REBALANCER_EXPECT_EQ_MOVESETS(expectedMoveSet, bestResult.getMoveSet());
  }

  {
    auto bestResult = singleFastMoveType.findBestMove(
        getMovesEvaluator(),
        /*hotContainer=*/container(2),
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        /*timeLimit=*/std::numeric_limits<double>::max());

    // container(2) has one object, object(4), which belongs to the j2 group.
    // However it also wants to explore containers in region1 which will
    // override the groupToScopeItems destinations from j2 resulting in 2 moves
    // being evaluated.
    EXPECT_EQ(2, getTotalMovesEvaluated());
  }
}

CO_TEST_F(SingleFastMoveTypeTest, FilterReducesEvaluatedMoves) {
  const auto universe = co_await setUpUniverse();
  const auto numObjects = universe->getObjects().getObjectIds().size();
  const auto numContainers = universe->getContainers().getContainerIds().size();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{
          container(1),
          container(2),
          container(3),
          container(4),
          container(5)});
  auto objectiveTuple = std::vector<ExprPtr>{object_lookup(
      makeAllUnequalObjectVector(9),
      containers,
      Assignment(universe->getContainers().getInitialAssignment()))};

  // Without filter: all destinations evaluated
  auto problemNoFilter =
      createTestProblem(universe, objectiveTuple, const_expr(0, *universe));
  const MovesEvaluator evalNoFilter(*problemNoFilter, 0, 1, "stage");
  MoveStatsAggregator statsNoFilter(universe->getPrecision());
  auto resultNoFilter = SingleFastMoveType(
                            interface::LocalSearchSolverSpec{},
                            interface::SingleFastMoveTypeSpec{})
                            .findBestMove(
                                evalNoFilter,
                                container(3),
                                statsNoFilter,
                                getEmptySearchHints(),
                                std::numeric_limits<double>::max());
  const auto evalsNoFilter = statsNoFilter.getGlobalStats().getTotalMoves();

  // With filter: block all objects in container3 from container1
  auto filter = std::make_unique<InvalidMoveFilter>(numObjects, numContainers);
  filter->markInvalid(
      {object(3), object(5), object(6), object(7)}, container(1));
  auto problemWithFilter = createTestProblem(
      universe,
      objectiveTuple,
      const_expr(0, *universe),
      /*nonAcceptingContainers=*/{},
      /*config=*/{},
      /*performInitialFullApply=*/true,
      /*enableParallelizedBoundsComputing=*/false,
      std::move(filter));
  const MovesEvaluator evalWithFilter(*problemWithFilter, 0, 1, "stage");
  MoveStatsAggregator statsWithFilter(universe->getPrecision());
  auto resultWithFilter = SingleFastMoveType(
                              interface::LocalSearchSolverSpec{},
                              interface::SingleFastMoveTypeSpec{})
                              .findBestMove(
                                  evalWithFilter,
                                  container(3),
                                  statsWithFilter,
                                  getEmptySearchHints(),
                                  std::numeric_limits<double>::max());

  // 4 objects blocked from container1, so exactly 4 fewer evaluations.
  EXPECT_EQ(
      statsWithFilter.getGlobalStats().getTotalMoves(), evalsNoFilter - 4);
  // Filter skips only guaranteed-invalid moves, so the best move is the same.
  EXPECT_EQ(
      resultNoFilter.getMoveSet().size(), resultWithFilter.getMoveSet().size());
  for (size_t i = 0; i < resultNoFilter.getMoveSet().size(); ++i) {
    EXPECT_EQ(
        resultNoFilter.getMoveSet().at(i), resultWithFilter.getMoveSet().at(i));
  }
}

} // namespace facebook::rebalancer::packer::tests
