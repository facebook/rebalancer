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
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/moves/SingleRandomStratifiedMoveType.h"
#include "algopt/rebalancer/solver/moves/tests/MoveTestBase.h"

#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class SingleRandomStratifiedMoveTypeTest : public MoveTestBase {
 protected:
  SingleRandomStratifiedMoveTypeTest() : MoveTestBase("object", "container") {}

  folly::coro::Task<std::shared_ptr<const entities::Universe>> setUpUniverse(
      std::optional<PackerMap<std::string, std::vector<std::string>>>
          initialAssignment = std::nullopt) {
    auto assignment = initialAssignment
        ? *initialAssignment
        : PackerMap<std::string, std::vector<std::string>>{
              {"container1", {"object1", "object2"}},
              {"container2", {"object4"}},
              {"container3", {"object5", "object6", "object7", "object3"}},
              {"container4", {"object8"}},
              {"container5", {"object9"}}};
    setInitialAssignment(assignment);

    co_await addScope(
        "region",
        {{"region1", {"container1", "container3"}},
         {"region2", {"container2", "container4"}}});

    co_await addPartition("job", {{"j1", {"object9"}}, {"j2", {"object4"}}});

    co_return buildUniverse();
  }
};

class MockSingleRandomStratifiedMoveType
    : public SingleRandomStratifiedMoveType {
 public:
  explicit MockSingleRandomStratifiedMoveType(
      const interface::LocalSearchSolverSpec& configs)
      : SingleRandomStratifiedMoveType(configs) {
    rng_.seed(std::random_device()());
  }

  explicit MockSingleRandomStratifiedMoveType(
      const interface::LocalSearchSolverSpec& solverConfigs,
      const interface::SingleRandomStratifiedMoveTypeSpec& config)
      : SingleRandomStratifiedMoveType(solverConfigs, config) {
    rng_.seed(std::random_device()());
  }
};

CO_TEST_F(SingleRandomStratifiedMoveTypeTest, VerifyMoveSetBasic) {
  interface::LocalSearchSolverSpec solverSpec;
  solverSpec.stratifiedSampleSize() = 2;

  auto singleRandomMoveType = MockSingleRandomStratifiedMoveType(solverSpec);

  const auto universe = co_await setUpUniverse();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{
          container(1), container(3), container(4)});
  createProblem(
      /*objectiveTuple=*/{object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(universe->getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  auto bestResult = singleRandomMoveType.findBestMove(
      getMovesEvaluator(),
      container(4) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // container(4) has only one object, object(8) and sample size is 2.
  // Therefore, we expect 2 moves in total
  EXPECT_EQ(2, getTotalMovesEvaluated());

  // object(8) has the highest dimension value and should be moved out of
  // container(4). The specific destination depends on which containers are
  // sampled, which varies by platform due to hash map iteration order.
  EXPECT_EQ(1, bestResult.getMoveSet().size());
  const auto& move = *bestResult.getMoveSet().begin();
  EXPECT_EQ(object(8), move.getObject());
  EXPECT_EQ(container(4), move.getSourceContainer());
  EXPECT_NE(container(4), move.getDestinationContainer());
}

CO_TEST_F(SingleRandomStratifiedMoveTypeTest, VerifyMoveSetBasic2) {
  interface::LocalSearchSolverSpec solverSpec;
  solverSpec.stratifiedSampleSize() = 3;

  auto singleRandomMoveType = MockSingleRandomStratifiedMoveType(solverSpec);

  const auto universe = co_await setUpUniverse(
      PackerMap<std::string, std::vector<std::string>>{
          {"container1", {"object1", "object2"}},
          {"container2", {"object4"}},
          {"container3", {"object5", "object6", "object7", "object3"}},
          {"container4", {"object8"}},
          {"container5", {"object9"}},
      });

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{container(1)});

  createProblem(
      /*objectiveTuple=*/{object_lookup(
          makeAllUnequalObjectVector(9, /*negateAllValues=*/true),
          containers,
          Assignment(universe->getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  auto bestResult = singleRandomMoveType.findBestMove(
      getMovesEvaluator(),
      container(1) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // container(1) has two objects, sample size is 3, and no move will improve
  // the objective since all object values are negative
  EXPECT_EQ(2 * 3, getTotalMovesEvaluated());
}

static interface::SingleRandomStratifiedMoveTypeSpec makeInRegionSpec() {
  interface::MoveToCurrentScopeItemSpec moveToCurrentScopeItemSpec;
  moveToCurrentScopeItemSpec.scopeNameForExploringMovesToCurrentScopeItem() =
      "region";
  interface::DestinationsToExploreOptions destinationsToExplore;
  destinationsToExplore.set_moveToCurrentScopeItem() =
      moveToCurrentScopeItemSpec;

  interface::SingleRandomStratifiedMoveTypeSpec singleRandomSpec;
  singleRandomSpec.destinationsToExplore() = destinationsToExplore;

  interface::SampleSize sampleSize;
  sampleSize.defaultSampleSize() = 3;
  singleRandomSpec.stratifiedSampleSize() = std::move(sampleSize);

  return singleRandomSpec;
}

CO_TEST_F(
    SingleRandomStratifiedMoveTypeTest,
    VerifyMoveEvalsWithExploreInRegion) {
  auto singleRandomMoveType = MockSingleRandomStratifiedMoveType(
      interface::LocalSearchSolverSpec{}, makeInRegionSpec());

  const auto universe = co_await setUpUniverse();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{container(4)});
  createProblem(
      /*objectiveTuple=*/{object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(universe->getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  auto bestResult = singleRandomMoveType.findBestMove(
      getMovesEvaluator(),
      container(4) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // container(4) has one object, but we only want to explore in-region
  // destinations. Hence although the sample size is 3, we explore only 1 move
  EXPECT_EQ(1, getTotalMovesEvaluated());

  const std::vector<Move> expectedMoveSet = {
      {Move{object(8), container(4), container(2)}}};
  REBALANCER_EXPECT_EQ_MOVESETS(expectedMoveSet, bestResult.getMoveSet());
}

CO_TEST_F(
    SingleRandomStratifiedMoveTypeTest,
    VerifyDefaultMinObjectsToExplore) {
  auto singleRandomMoveType = MockSingleRandomStratifiedMoveType(
      interface::LocalSearchSolverSpec{}, makeInRegionSpec());

  const auto universe = co_await setUpUniverse();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{container(3)});
  createProblem(
      /*objectiveTuple=*/{object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(universe->getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  singleRandomMoveType.findBestMove(
      getMovesEvaluator(),
      container(3) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // container(3) holds four objects and shares "region1" with container(1)
  // only, so exploring one object costs exactly one move evaluation. Moving any
  // object out of container(3) improves the objective, so with the default of 1
  // the search stops after the first object.
  EXPECT_EQ(1, getTotalMovesEvaluated());
}

CO_TEST_F(SingleRandomStratifiedMoveTypeTest, VerifyBiggerMinObjectsToExplore) {
  auto singleRandomSpec = makeInRegionSpec();
  singleRandomSpec.minObjectsToExplore() = 3;

  auto singleRandomMoveType = MockSingleRandomStratifiedMoveType(
      interface::LocalSearchSolverSpec{}, singleRandomSpec);

  const auto universe = co_await setUpUniverse();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{container(3)});
  createProblem(
      /*objectiveTuple=*/{object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(universe->getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  singleRandomMoveType.findBestMove(
      getMovesEvaluator(),
      container(3) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // Same problem as VerifyDefaultMinObjectsToExplore, but the spec now asks for
  // three objects to be fully explored before an improving move is accepted, so
  // the search does not stop at the first one.
  EXPECT_EQ(3, getTotalMovesEvaluated());
}

CO_TEST_F(
    SingleRandomStratifiedMoveTypeTest,
    VerifyMoveEvalsWithInRegionButHotContainerNotPartofScope) {
  interface::MoveToCurrentScopeItemSpec moveToCurrentScopeItemSpec;
  moveToCurrentScopeItemSpec.scopeNameForExploringMovesToCurrentScopeItem() =
      "region";
  interface::DestinationsToExploreOptions destinationsToExplore;
  destinationsToExplore.set_moveToCurrentScopeItem() =
      moveToCurrentScopeItemSpec;
  interface::SingleRandomStratifiedMoveTypeSpec singleRandomSpec;
  singleRandomSpec.destinationsToExplore() = destinationsToExplore;

  interface::SampleSize sampleSize;
  sampleSize.defaultSampleSize() = 2;
  sampleSize.objectToSampleSize() = {{"object9", 4}};
  singleRandomSpec.stratifiedSampleSize() = std::move(sampleSize);

  auto singleRandomMoveType = MockSingleRandomStratifiedMoveType(
      interface::LocalSearchSolverSpec{}, singleRandomSpec);

  const auto universe = co_await setUpUniverse();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{
          container(5), container(1), container(2), container(3)});
  createProblem(
      /*objectiveTuple=*/{object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(universe->getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  auto bestResult = singleRandomMoveType.findBestMove(
      getMovesEvaluator(),
      container(5) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // container(5) has one object (object9) and we only want to explore
  // in-region destinations. However, container(5) is not part of "region"
  // scope, so we expect to explore four destinations (sample size is four for
  // object9)
  EXPECT_EQ(4, getTotalMovesEvaluated());

  const std::vector<Move> expectedMoveSet = {
      {Move{object(9), container(5), container(4)}}};
  REBALANCER_EXPECT_EQ_MOVESETS(expectedMoveSet, bestResult.getMoveSet());
}

CO_TEST_F(
    SingleRandomStratifiedMoveTypeTest,
    VerifyMoveEvalsWithMoveToScopeItems) {
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
  interface::SingleRandomStratifiedMoveTypeSpec singleRandomSpec;
  singleRandomSpec.destinationsToExplore() = destinationsToExplore;

  interface::SampleSize sampleSize;
  sampleSize.defaultSampleSize() = 0;
  sampleSize.objectToSampleSize() = {
      {"object4", 100}, {"object5", 1}, {"object8", 10}, {"object9", 6}};
  singleRandomSpec.stratifiedSampleSize() = std::move(sampleSize);

  auto singleRandomMoveType = MockSingleRandomStratifiedMoveType(
      interface::LocalSearchSolverSpec{}, singleRandomSpec);

  const auto universe = co_await setUpUniverse();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{
          container(5), container(1), container(2), container(3)});
  createProblem(
      /*objectiveTuple=*/{object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(universe->getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  {
    auto bestResult = singleRandomMoveType.findBestMove(
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
    auto bestResult = singleRandomMoveType.findBestMove(
        getMovesEvaluator(),
        container(2) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // container(2) has one object ("object4") and that object wants to
    // explore destinations in region1
    EXPECT_EQ(2, getTotalMovesEvaluated());
  }

  {
    auto bestResult = singleRandomMoveType.findBestMove(
        getMovesEvaluator(),
        container(4) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // container(4) has one object ("object8") and that object wants to
    // explore only moves to container2 and container4. Since container4 is
    // hot, we only expect 1 evaluation
    EXPECT_EQ(1, getTotalMovesEvaluated());
  }

  {
    auto bestResult = singleRandomMoveType.findBestMove(
        getMovesEvaluator(),
        container(4) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // container(4) has one object ("object8") and that object wants to
    // explore only moves to container2 and container4. Since container4 is
    // hot, we only expect 1 evaluation
    EXPECT_EQ(1, getTotalMovesEvaluated());
  }

  {
    auto bestResult = singleRandomMoveType.findBestMove(
        getMovesEvaluator(),
        container(1) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // all objects in container(1) have sampleSize of 1 (since that is the
    // default)
    EXPECT_EQ(0, getTotalMovesEvaluated());
  }

  {
    auto bestResult = singleRandomMoveType.findBestMove(
        getMovesEvaluator(),
        container(3) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // except object(5) which has a sample size of 1, all other objects in
    // container(3) have sampleSize of 0
    EXPECT_EQ(1, getTotalMovesEvaluated());
  }
}

CO_TEST_F(
    SingleRandomStratifiedMoveTypeTest,
    VerifyMoveEvalsWithMoveToScopeItemsInUniverse) {
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

  addDestinationsToExploreOptions("myHint", destinationsToExplore);

  interface::DestinationsToExploreOptions destinationName;
  destinationName.set_destinationToExploreName("myHint");

  interface::SingleRandomStratifiedMoveTypeSpec singleRandomSpec;
  singleRandomSpec.destinationsToExplore() = destinationName;

  interface::SampleSize sampleSize;
  sampleSize.defaultSampleSize() = 0;
  sampleSize.objectToSampleSize() = {
      {"object4", 100}, {"object5", 1}, {"object8", 10}, {"object9", 6}};
  singleRandomSpec.stratifiedSampleSize() = std::move(sampleSize);

  auto singleRandomMoveType = MockSingleRandomStratifiedMoveType(
      interface::LocalSearchSolverSpec{}, singleRandomSpec);

  const auto universe = co_await setUpUniverse();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{
          container(5), container(1), container(2), container(3)});
  createProblem(
      /*objectiveTuple=*/{object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(universe->getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  {
    auto bestResult = singleRandomMoveType.findBestMove(
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
    auto bestResult = singleRandomMoveType.findBestMove(
        getMovesEvaluator(),
        container(2) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // container(2) has one object ("object4") and that object wants to
    // explore destinations in region1
    EXPECT_EQ(2, getTotalMovesEvaluated());
  }

  {
    auto bestResult = singleRandomMoveType.findBestMove(
        getMovesEvaluator(),
        container(4) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // container(4) has one object ("object8") and that object wants to
    // explore only moves to container2 and container4. Since container4 is
    // hot, we only expect 1 evaluation
    EXPECT_EQ(1, getTotalMovesEvaluated());
  }

  {
    auto bestResult = singleRandomMoveType.findBestMove(
        getMovesEvaluator(),
        container(4) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // container(4) has one object ("object8") and that object wants to
    // explore only moves to container2 and container4. Since container4 is
    // hot, we only expect 1 evaluation
    EXPECT_EQ(1, getTotalMovesEvaluated());
  }

  {
    auto bestResult = singleRandomMoveType.findBestMove(
        getMovesEvaluator(),
        container(1) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // all objects in container(1) have sampleSize of 1 (since that is the
    // default)
    EXPECT_EQ(0, getTotalMovesEvaluated());
  }

  {
    auto bestResult = singleRandomMoveType.findBestMove(
        getMovesEvaluator(),
        container(3) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // except object(5) which has a sample size of 1, all other objects in
    // container(3) have sampleSize of 0
    EXPECT_EQ(1, getTotalMovesEvaluated());
  }
}

CO_TEST_F(
    SingleRandomStratifiedMoveTypeTest,
    VerifyMoveToCurrentScopeItemSpecInUniverse) {
  interface::MoveToCurrentScopeItemSpec moveToCurrentScopeItemSpec;
  moveToCurrentScopeItemSpec.scopeNameForExploringMovesToCurrentScopeItem() =
      "region";
  interface::DestinationsToExploreOptions destinationsToExplore;
  destinationsToExplore.set_moveToCurrentScopeItem() =
      moveToCurrentScopeItemSpec;

  addDestinationsToExploreOptions("myHint", destinationsToExplore);

  interface::DestinationsToExploreOptions destinationName;
  destinationName.set_destinationToExploreName("myHint");

  interface::SingleRandomStratifiedMoveTypeSpec singleRandomSpec;
  singleRandomSpec.destinationsToExplore() = destinationName;

  interface::SampleSize sampleSize;
  sampleSize.defaultSampleSize() = 3;
  singleRandomSpec.stratifiedSampleSize() = std::move(sampleSize);

  auto singleRandomMoveType = MockSingleRandomStratifiedMoveType(
      interface::LocalSearchSolverSpec{}, singleRandomSpec);

  const auto universe = co_await setUpUniverse();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{container(4)});
  createProblem(
      /*objectiveTuple=*/{object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(universe->getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  auto bestResult = singleRandomMoveType.findBestMove(
      getMovesEvaluator(),
      container(4) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // container(4) has one object, but we only want to explore in-region
  // destinations. Hence although the sample size is 3, we explore only 1 move
  EXPECT_EQ(1, getTotalMovesEvaluated());

  const std::vector<Move> expectedMoveSet = {
      {Move{object(8), container(4), container(2)}}};
  REBALANCER_EXPECT_EQ_MOVESETS(expectedMoveSet, bestResult.getMoveSet());
}

CO_TEST_F(
    SingleRandomStratifiedMoveTypeTest,
    VerifyDestinationToExploreOptionNotInUniverse) {
  interface::SingleRandomStratifiedMoveTypeSpec singleRandomSpec;

  interface::SampleSize sampleSize;
  sampleSize.defaultSampleSize() = 3;
  singleRandomSpec.stratifiedSampleSize() = std::move(sampleSize);

  auto singleRandomMoveType = MockSingleRandomStratifiedMoveType(
      interface::LocalSearchSolverSpec{}, singleRandomSpec);

  const auto universe = co_await setUpUniverse();
  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{container(4)});
  createProblem(
      /*objectiveTuple=*/{object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(universe->getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      singleRandomMoveType.findBestMove(
          getMovesEvaluator(),
          container(4) /*hotContainer*/,
          getMoveStatsAggregator(),
          getEmptySearchHints(),
          std::numeric_limits<double>::max() /*timelimit*/),
      "DestinationToExploreOptions is empty; you need to specify one of the options");
}

CO_TEST_F(SingleRandomStratifiedMoveTypeTest, VerifyGroupToScopeItemLists) {
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

  addDestinationsToExploreOptions("myHint", destinationsToExplore);

  interface::DestinationsToExploreOptions destinationName;
  destinationName.set_destinationToExploreName("myHint");

  interface::SingleRandomStratifiedMoveTypeSpec singleRandomSpec;
  singleRandomSpec.destinationsToExplore() = destinationName;

  interface::SampleSize sampleSize;
  sampleSize.defaultSampleSize() = 0;
  sampleSize.objectToSampleSize() = {
      {"object4", 100}, {"object5", 1}, {"object8", 10}, {"object9", 6}};
  singleRandomSpec.stratifiedSampleSize() = std::move(sampleSize);

  auto singleRandomMoveType = MockSingleRandomStratifiedMoveType(
      interface::LocalSearchSolverSpec{}, singleRandomSpec);

  const auto universe = co_await setUpUniverse();
  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{
          container(5), container(1), container(2), container(3)});
  createProblem(
      /*objectiveTuple=*/{object_lookup(
          makeAllUnequalObjectVector(9),
          containers,
          Assignment(getUniverse().getContainers().getInitialAssignment()))},
      /*constraint=*/const_expr(0, *universe));

  {
    auto bestResult = singleRandomMoveType.findBestMove(
        getMovesEvaluator(),
        container(5) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // container(5) has one object and partitionToGroupToScopeItems will make
    // object(9) only have 2 moves
    EXPECT_EQ(2, getTotalMovesEvaluated());

    const std::vector<Move> expectedMoveSet = {
        {Move{object(9), container(5), container(4)}}};
    REBALANCER_EXPECT_EQ_MOVESETS(expectedMoveSet, bestResult.getMoveSet());
  }

  {
    auto bestResult = singleRandomMoveType.findBestMove(
        getMovesEvaluator(),
        container(2) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // container(2) has one object ("object4") and that object wants to
    // explore destinations in region1, this will override the groupToScopeItems
    // destinations
    EXPECT_EQ(2, getTotalMovesEvaluated());
  }

  {
    auto bestResult = singleRandomMoveType.findBestMove(
        getMovesEvaluator(),
        container(4) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // container(4) has one object ("object8") and that object wants to
    // explore only moves to container2 and container4. Since container4 is
    // hot, we only expect 1 evaluation
    EXPECT_EQ(1, getTotalMovesEvaluated());
  }

  {
    auto bestResult = singleRandomMoveType.findBestMove(
        getMovesEvaluator(),
        container(4) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // container(4) has one object ("object8") and that object wants to
    // explore only moves to container2 and container4. Since container4 is
    // hot, we only expect 1 evaluation
    EXPECT_EQ(1, getTotalMovesEvaluated());
  }

  {
    auto bestResult = singleRandomMoveType.findBestMove(
        getMovesEvaluator(),
        container(1) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // all objects in container(1) have sampleSize of 1 (since that is the
    // default)
    EXPECT_EQ(0, getTotalMovesEvaluated());
  }

  {
    auto bestResult = singleRandomMoveType.findBestMove(
        getMovesEvaluator(),
        container(3) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);

    // except object(5) which has a sample size of 1, all other objects in
    // container(3) have sampleSize of 0
    EXPECT_EQ(1, getTotalMovesEvaluated());
  }
}

} // namespace facebook::rebalancer::packer::tests
