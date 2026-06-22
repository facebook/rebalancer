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
#include "algopt/rebalancer/solver/moves/ColocateGroupsMoveType.h"
#include "algopt/rebalancer/solver/moves/tests/MoveTestBase.h"

#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class ColocateGroupsMoveTypeTest : public MoveTestBase {
 protected:
  ColocateGroupsMoveTypeTest() : MoveTestBase("object", "container") {}

  folly::coro::Task<void> setUpUniverse(
      std::optional<entities::Map<std::string, std::vector<std::string>>>
          initialAssignment = std::nullopt) {
    auto assignment = initialAssignment
        ? *initialAssignment
        : entities::Map<std::string, std::vector<std::string>>{
              {"container11", {"object1", "object10"}},
              {"container21", {"object4", "object11"}},
              {"container31", {"object5", "object6", "object7", "object3"}},
              {"container41", {"object8"}},
              {"container51", {"object9", "object2"}},
              //
              {"container12", {}},
              {"container22", {}},
              {"container32", {}},
              {"container42", {}},
              {"container53", {}}};

    setInitialAssignment(assignment);

    co_await addScope(
        "region",
        {
            {"region1",
             {"container11", "container21", "container31", "container41"}},
            {"region2",
             {"container12", "container22", "container32", "container42"}},
            {"region3", {"container53"}},
        });

    co_await addPartition(
        "tenant",
        {{"tenant1", {"object1"}},
         {"tenant2", {"object4"}},
         {"tenant3", {"object7"}},
         {"tenant4", {"object2", "object10", "object11"}}});

    co_await addObjectDimension(
        "traffic_load",
        entities::ObjectDimensionData{
            std::make_unique<const entities::ObjectDimension>(
                entities::ObjectIdToDoubleMap(
                    buildObjectToLoad(),
                    /*defaultValue=*/0.0,
                    /*totalSize=*/getNumObjects()))});

    buildUniverse();
  }

  entities::Map<entities::ObjectId, double> buildObjectToLoad() {
    return {
        {object(1), 1},
        {object(2), 1},
        {object(3), 1},
        {object(4), 2},
        {object(5), 2},
        {object(6), 2},
        {object(7), 3},
        {object(8), 3},
        {object(9), 3},
        {object(10), 10},
        {object(11), 11}};
  }

  std::shared_ptr<ObjectVector> makeLoadObjectVector(
      bool negateValues = false) {
    const auto objectToLoad = buildObjectToLoad();
    if (!negateValues) {
      return makeObjectVector(objectToLoad, getUniversePtr());
    }
    // negate the values
    entities::Map<entities::ObjectId, double> negativeObjectToLoad;
    for (const auto& [object, load] : objectToLoad) {
      negativeObjectToLoad[object] = -load;
    }
    return makeObjectVector(negativeObjectToLoad, getUniversePtr());
  }

  std::shared_ptr<PackerSet<entities::ContainerId>> getContainerSet() {
    // add all containers in region1, all in region2 except container(12), and
    // all in region3
    return std::make_shared<PackerSet<entities::ContainerId>>(
        PackerSet<entities::ContainerId>{
            container(11),
            container(21),
            container(31),
            container(41),
            container(22),
            container(32),
            container(42),
            container(53)});
  }
};

TEST_F(ColocateGroupsMoveTypeTest, Name) {
  auto colocateMoveType = ColocateGroupsMoveType(
      interface::LocalSearchSolverSpec{},
      interface::ColocateGroupsMoveTypeSpec());
  EXPECT_EQ("COLOCATE_GROUPS", colocateMoveType.name());
}

CO_TEST_F(ColocateGroupsMoveTypeTest, VerifyMoveSetsBasic) {
  interface::ColocateGroupsMoveTypeSpec spec;
  spec.partitionName() = "tenant";
  spec.colocationScopeName() = "region";

  interface::ColocateGroupsMoveTypeRelatedGroupsInfo relatedGroupsInfo;
  relatedGroupsInfo.relatedGroups() = {"tenant1", "tenant2", "tenant3"};
  spec.relatedGroupsList() = {relatedGroupsInfo};

  auto colocateMoveType =
      ColocateGroupsMoveType(interface::LocalSearchSolverSpec{}, spec);

  co_await setUpUniverse();

  // add an objective to minimize the utilization of region1 containers and
  // some containers in region2
  createProblem(
      {object_lookup(
          makeLoadObjectVector(),
          getContainerSet(),
          getUniversePtr(),
          Assignment(getUniverse().getContainers().getInitialAssignment()))},
      const_expr(0, getUniversePtr()));

  auto bestResult = colocateMoveType.findBestMove(
      getMovesEvaluator(),
      container(11) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // we have one set of related groups. In region2, there are 4 potential
  // destinations they can go to and in region 3 there is one potential
  // destination. So, number of moves evaluated is (4 * 4 * 4 + 1 * 1 * 1) * 3,
  // where we have (4 * 4 * 4 ) move sets w.r.t. region 2, 1 move set w.r.t.
  // region 1, and 3 moves in each move set
  EXPECT_EQ((4 * 4 * 4 + 1 * 1 * 1) * 3, getTotalMovesEvaluated());

  // Note that only container(12) in region2 is not part of the objective. So,
  // we expect all the three objects (object(1), object(4), object(7)) in the
  // best move set to have container(12) as the destination
  const std::vector<Move> expectedMoveSet = {
      {Move{object(1), container(11), container(12)},
       Move{object(4), container(21), container(12)},
       Move{object(7), container(31), container(12)}}};

  REBALANCER_EXPECT_EQ_MOVESETS(expectedMoveSet, bestResult.getMoveSet());
}

CO_TEST_F(ColocateGroupsMoveTypeTest, VerifyMoveSetsWithSampling) {
  interface::ColocateGroupsMoveTypeSpec spec;
  spec.partitionName() = "tenant";
  spec.colocationScopeName() = "region";

  interface::ColocateGroupsMoveTypeRelatedGroupsInfo relatedGroupsInfo;
  relatedGroupsInfo.relatedGroups() = {"tenant1", "tenant2", "tenant3"};
  spec.relatedGroupsList() = {relatedGroupsInfo};

  spec.defaultSampleSize() = 2;

  auto colocateMoveType =
      ColocateGroupsMoveType(interface::LocalSearchSolverSpec{}, spec);

  co_await setUpUniverse();

  // add an objective to minimize the utilization of region1 containers and
  // some containers in region2
  createProblem(
      {object_lookup(
          makeLoadObjectVector(),
          getContainerSet(),
          getUniversePtr(),
          Assignment(getUniverse().getContainers().getInitialAssignment()))},
      const_expr(0, getUniversePtr()));

  auto bestResult = colocateMoveType.findBestMove(
      getMovesEvaluator(),
      container(11) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // we have one set of related groups.  In region2, there are 4 potential
  // destinations they can go to and in region 3 there is one potential
  // destination. However, we have a sample size of 2 as well. So, number of
  // moves evaluated is (2 * 2 * 2 + 1 * 1 * 1)
  // * 3, where we have (2 * 2 * 2 ) move sets w.r.t. region 2, 1 move set
  // w.r.t. region 1, and 3 moves in each move set
  EXPECT_EQ((2 * 2 * 2 + 1 * 1 * 1) * 3, getTotalMovesEvaluated());
}

CO_TEST_F(ColocateGroupsMoveTypeTest, VerifyMoveSetsWithGroupToContainers) {
  interface::ColocateGroupsMoveTypeSpec spec;
  spec.partitionName() = "tenant";
  spec.colocationScopeName() = "region";

  interface::ColocateGroupsMoveTypeRelatedGroupsInfo relatedGroupsInfo;
  relatedGroupsInfo.relatedGroups() = {"tenant1", "tenant2", "tenant3"};
  spec.relatedGroupsList() = {relatedGroupsInfo};
  spec.colocationScopeItemToGroupToContainers() = {{
      "region2",
      {
          {"tenant1", {"container12", "container32"}},
          {"tenant2", {"container22"}},
      },
  }};

  auto colocateMoveType =
      ColocateGroupsMoveType(interface::LocalSearchSolverSpec{}, spec);

  co_await setUpUniverse();

  // add an objective to minimize the utilization of region1 containers and
  // some containers in region2
  createProblem(
      {object_lookup(
          makeLoadObjectVector(),
          getContainerSet(),
          getUniversePtr(),
          Assignment(getUniverse().getContainers().getInitialAssignment()))},
      const_expr(0, getUniversePtr()));

  auto bestResult = colocateMoveType.findBestMove(
      getMovesEvaluator(),
      container(31) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // we have one set of related groups. In region2, tenant1, tenant2,
  // tenant3, respectively, have only 2, 1, 4 potential destinations they
  // can go to. In region 3, there is one potential destination. So, number of
  // moves evaluated is (2 * 1 * 4 + 1 * 1
  // * 1) * 3
  EXPECT_EQ((2 * 1 * 4 + 1 * 1 * 1) * 3, getTotalMovesEvaluated());

  const std::vector<Move> expectedMoveSet = {
      {Move{object(1), container(11), container(12)},
       Move{object(4), container(21), container(22)},
       Move{object(7), container(31), container(12)}}};

  REBALANCER_EXPECT_EQ_MOVESETS(expectedMoveSet, bestResult.getMoveSet());
}

CO_TEST_F(
    ColocateGroupsMoveTypeTest,
    VerifyMoveSetsWithRelatedGroupColocationScopeItems) {
  interface::ColocateGroupsMoveTypeSpec spec;
  spec.partitionName() = "tenant";
  spec.colocationScopeName() = "region";

  interface::ColocateGroupsMoveTypeRelatedGroupsInfo relatedGroupsInfo;
  relatedGroupsInfo.relatedGroups() = {"tenant1", "tenant2", "tenant3"};
  relatedGroupsInfo.destinationScopeItems() = {"region2"};

  spec.relatedGroupsList() = {relatedGroupsInfo};
  spec.colocationScopeItemToGroupToContainers() = {{
      "region2",
      {
          {"tenant1", {"container12", "container32"}},
          {"tenant2", {"container22"}},
      },
  }};

  auto colocateMoveType =
      ColocateGroupsMoveType(interface::LocalSearchSolverSpec{}, spec);

  co_await setUpUniverse();

  // add an objective to minimize the utilization of region1 containers and
  // some containers in region2
  createProblem(
      {object_lookup(
          makeLoadObjectVector(),
          getContainerSet(),
          getUniversePtr(),
          Assignment(getUniverse().getContainers().getInitialAssignment()))},
      const_expr(0, getUniversePtr()));

  auto bestResult = colocateMoveType.findBestMove(
      getMovesEvaluator(),
      container(31) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // we have one set of related groups. In region2, tenant1, tenant2,
  // tenant3, respectively, have only 2, 1, 4 potential destinations they
  // can go to. Note that this set can only specifies region2 in
  // ColocateGroupsMoveTypeRelatedGroupsInfo, so no moves will be tried to
  // region3. So, number of moves evaluated is (2 * 1 * 4) * 3
  EXPECT_EQ((2 * 1 * 4) * 3, getTotalMovesEvaluated());

  const std::vector<Move> expectedMoveSet = {
      {Move{object(1), container(11), container(12)},
       Move{object(4), container(21), container(22)},
       Move{object(7), container(31), container(12)}}};

  REBALANCER_EXPECT_EQ_MOVESETS(expectedMoveSet, bestResult.getMoveSet());
}

CO_TEST_F(
    ColocateGroupsMoveTypeTest,
    VerifyMoveSetsWithHotContainerOutsideColocationScope) {
  interface::ColocateGroupsMoveTypeSpec spec;
  spec.partitionName() = "tenant";
  spec.colocationScopeName() = "region";

  interface::ColocateGroupsMoveTypeRelatedGroupsInfo relatedGroupsInfo;
  relatedGroupsInfo.relatedGroups() = {"tenant1", "tenant2", "tenant3"};

  spec.relatedGroupsList() = {relatedGroupsInfo};

  auto colocateMoveType =
      ColocateGroupsMoveType(interface::LocalSearchSolverSpec{}, spec);

  co_await setUpUniverse(
      entities::Map<std::string, std::vector<std::string>>{
          {"container11", {"object2"}},
          {"container21", {"object11"}},
          {"container31", {"object5", "object6", "object3"}},
          {"container41", {"object8", "object9", "object10"}},
          {"container51", {"object1", "object4", "object7"}},
          {"container12", {}},
          {"container22", {}},
          {"container32", {}},
          {"container42", {}},
          {"container53", {}}});

  // add an objective to minimize the utilization of region1 containers and
  // some containers in region2
  createProblem(
      {object_lookup(
          makeLoadObjectVector(),
          getContainerSet(),
          getUniversePtr(),
          Assignment(getUniverse().getContainers().getInitialAssignment()))},
      const_expr(0, getUniversePtr()));

  auto bestResult = colocateMoveType.findBestMove(
      getMovesEvaluator(),
      container(51) /*hotContainer; outside region scope*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // we have one set of related groups. In region1 and region2, eack group has 4
  // potential destinations they can go to. In region3, there one potential
  // destination. So, number of moves evaluated is (4 * 4 * 4 + 4 * 4 * 4 + 1 *
  // 1 * 1) * 3
  EXPECT_EQ((4 * 4 * 4 + 4 * 4 * 4 + 1 * 1 * 1) * 3, getTotalMovesEvaluated());

  const std::vector<Move> expectedMoveSet = {
      {Move{object(1), container(51), container(12)},
       Move{object(4), container(51), container(12)},
       Move{object(7), container(51), container(12)}}};

  REBALANCER_EXPECT_EQ_MOVESETS(expectedMoveSet, bestResult.getMoveSet());
}

CO_TEST_F(
    ColocateGroupsMoveTypeTest,
    NotAllGroupsHaveAnObjectInSourceContainers) {
  interface::ColocateGroupsMoveTypeSpec spec;
  spec.partitionName() = "tenant";
  spec.colocationScopeName() = "region";

  interface::ColocateGroupsMoveTypeRelatedGroupsInfo relatedGroupsInfo;
  relatedGroupsInfo.relatedGroups() = {"tenant1", "tenant2", "tenant3"};

  spec.relatedGroupsList() = {relatedGroupsInfo};

  auto colocateMoveType =
      ColocateGroupsMoveType(interface::LocalSearchSolverSpec{}, spec);

  co_await setUpUniverse(
      entities::Map<std::string, std::vector<std::string>>{
          {"container11", {"object2", "object4"}},
          {"container21", {"object11", "object10"}},
          {"container31", {"object5", "object6", "object7", "object3"}},
          {"container41", {"object8", "object9"}},
          {"container51", {"object1"}},
          {"container12", {}},
          {"container22", {}},
          {"container32", {}},
          {"container42", {}},
          {"container53", {}}});

  // add an objective to minimize the utilization of region1 containers and
  // some containers in region2
  createProblem(
      {object_lookup(
          makeLoadObjectVector(),
          getContainerSet(),
          getUniversePtr(),
          Assignment(getUniverse().getContainers().getInitialAssignment()))},
      const_expr(0, getUniversePtr()));

  auto bestResult = colocateMoveType.findBestMove(
      getMovesEvaluator(),
      container(41) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // No moves to evaluate since tenant1 does not have an object in source
  // scope item containers (i.e., region1 containers)
  EXPECT_EQ(0, getTotalMovesEvaluated());
}

CO_TEST_F(ColocateGroupsMoveTypeTest, VerifyMoveSetsBasicNoBetterMove) {
  interface::ColocateGroupsMoveTypeSpec spec;
  spec.partitionName() = "tenant";
  spec.colocationScopeName() = "region";

  interface::ColocateGroupsMoveTypeRelatedGroupsInfo relatedGroupsInfo1;
  relatedGroupsInfo1.relatedGroups() = {"tenant1", "tenant2"};
  interface::ColocateGroupsMoveTypeRelatedGroupsInfo relatedGroupsInfo2;
  relatedGroupsInfo2.relatedGroups() = {"tenant3", "tenant4"};
  spec.relatedGroupsList() = {relatedGroupsInfo1, relatedGroupsInfo2};

  auto colocateMoveType =
      ColocateGroupsMoveType(interface::LocalSearchSolverSpec{}, spec);

  co_await setUpUniverse();

  auto allContainers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{
          container(11),
          container(21),
          container(31),
          container(41),
          container(51),
          container(12),
          container(22),
          container(32),
          container(42),
          container(53)});

  // add an objective to minimize the utilization of all containers
  auto objective = object_lookup(
      makeLoadObjectVector(),
      allContainers,
      getUniversePtr(),
      Assignment(getUniverse().getContainers().getInitialAssignment()));
  createProblem({objective}, const_expr(0, getUniversePtr()));

  const auto& precision = getUniverse().getPrecision();

  auto bestResult = colocateMoveType.findBestMove(
      getMovesEvaluator(),
      container(11) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  EXPECT_EQ(false, bestResult.isBetter(precision));

  // we have two set of related groups. In region2, there are 4 potential
  // destinations they can go to and in region 3 there is one potential
  // destination. So, number of moves evaluated is (4 * 4 + 1 * 1) * 2) + (4 * 4
  // + 1 * 1) * 2), one for each set of related groups, where we have (4 * 4)
  // move sets w.r.t. region 2, 1 move set w.r.t. region 1, and 2 moves in each
  // move set
  EXPECT_EQ(
      ((4 * 4 + 1 * 1) * 2) + ((4 * 4 + 1 * 1) * 2), getTotalMovesEvaluated());
}

// Builds a colocation set with a huge candidate space: 4 related groups that
// can each move to all 100 containers of the destination region, i.e.
// 100^4 = 100M candidate move sets. With the time-limit guard, findBestMove
// stops enumerating at ~the time limit and returns quickly; without it,
// enumerating 100M move sets takes many seconds (and gigabytes of memory), so
// the bound below catches a regression that drops the guard.
CO_TEST_F(ColocateGroupsMoveTypeTest, FindBestMoveStopsAtTimeLimit) {
  constexpr int kNumGroups = 4;
  constexpr int kNumDstContainers = 100;

  entities::Map<std::string, std::vector<std::string>> assignment;
  std::vector<std::string> srcContainers;
  std::vector<std::string> dstContainers;
  entities::Map<std::string, std::vector<std::string>> tenantToObjects;
  entities::Map<std::string, double> objectNameToLoad;
  for (int g = 1; g <= kNumGroups; ++g) {
    const auto containerName = "container" + std::to_string(g);
    const auto objectName = "object" + std::to_string(g);
    const auto tenantName = "tenant" + std::to_string(g);
    srcContainers.push_back(containerName);
    assignment[containerName] = {objectName};
    tenantToObjects[tenantName] = {objectName};
    objectNameToLoad[objectName] = static_cast<double>(g);
  }
  for (int d = 0; d < kNumDstContainers; ++d) {
    const auto containerName = "container" + std::to_string(1000 + d);
    dstContainers.push_back(containerName);
    assignment[containerName] = {};
  }

  setInitialAssignment(assignment);
  co_await addScope("region", {{"src", srcContainers}, {"dst", dstContainers}});
  co_await addPartition("tenant", tenantToObjects);
  co_await addObjectDimension(
      "traffic_load", objectNameToLoad, /*defaultValue=*/0.0);
  buildUniverse();

  entities::Map<entities::ObjectId, double> objectToLoad;
  auto containerSet = std::make_shared<PackerSet<entities::ContainerId>>();
  for (int g = 1; g <= kNumGroups; ++g) {
    objectToLoad[object("object" + std::to_string(g))] = static_cast<double>(g);
    containerSet->insert(container("container" + std::to_string(g)));
  }
  for (int d = 0; d < kNumDstContainers; ++d) {
    containerSet->insert(container("container" + std::to_string(1000 + d)));
  }
  createProblem(
      {object_lookup(
          makeObjectVector(objectToLoad, getUniversePtr()),
          containerSet,
          getUniversePtr(),
          Assignment(getUniverse().getContainers().getInitialAssignment()))},
      const_expr(0, getUniversePtr()));

  interface::ColocateGroupsMoveTypeSpec spec;
  spec.partitionName() = "tenant";
  spec.colocationScopeName() = "region";
  interface::ColocateGroupsMoveTypeRelatedGroupsInfo relatedGroupsInfo;
  relatedGroupsInfo.relatedGroups() = {
      "tenant1", "tenant2", "tenant3", "tenant4"};
  spec.relatedGroupsList() = {relatedGroupsInfo};
  auto colocateMoveType =
      ColocateGroupsMoveType(interface::LocalSearchSolverSpec{}, spec);

  constexpr double kTimeLimitSecs = 0.01;
  const algopt::Timer timer(/*autoStart=*/true);
  colocateMoveType.findBestMove(
      getMovesEvaluator(),
      container("container1") /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      kTimeLimitSecs);
  const double elapsedSecs = timer.getSeconds();

  // With the guard this returns at ~kTimeLimitSecs. The bound is intentionally
  // generous (vs. the ~0.01s limit) to tolerate setup overhead and scheduling
  // jitter on loaded CI hosts, while staying well below the many seconds a full
  // enumeration of 100M move sets would take without the guard (~14s observed).
  EXPECT_LT(elapsedSecs, 2.0);
}

} // namespace facebook::rebalancer::packer::tests
