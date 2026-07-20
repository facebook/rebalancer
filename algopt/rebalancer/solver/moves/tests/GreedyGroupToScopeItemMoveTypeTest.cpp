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

#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/moves/GreedyGroupToScopeItemMoveType.h"
#include "algopt/rebalancer/solver/moves/tests/MoveTestBase.h"

#include <folly/container/irange.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace facebook::rebalancer::packer::tests {

class GreedyGroupToScopeItemMoveTypeTest : public MoveTestBase {
 protected:
  GreedyGroupToScopeItemMoveTypeTest() : MoveTestBase("object", "container") {}
};

CO_TEST_F(GreedyGroupToScopeItemMoveTypeTest, SamplingIsDeterministic) {
  // Several scope items are explored in parallel; per-destination seeding makes
  // the chosen move identical on every run and race-free (checked under
  // dev-tsan).
  constexpr int kSampleRuns = 100;
  setInitialAssignment(
      {{"container0", {"object0", "object1"}},
       {"container1", {}},
       {"container2", {}},
       {"container3", {}},
       {"container4", {}},
       {"container5", {}},
       {"container6", {}}});
  co_await addScope(
      "assignable",
      {{"item0", {"container1", "container2"}},
       {"item1", {"container3", "container4"}},
       {"item2", {"container5", "container6"}}});
  co_await addPartition("job", {{"j1", {"object0", "object1"}}});
  const auto universe = buildUniverse();

  createProblem(
      /*objectiveTuple=*/{const_expr(0, *universe)},
      /*constraint=*/const_expr(0, *universe));

  const auto sampleGroupMove = [&] {
    interface::GreedyGroupToScopeItemMoveTypeSpec spec;
    spec.groupMovesPartition() = "job";
    spec.scopeItemMovesScope() = "assignable";
    spec.nSampleSetsToExplore() = 1;
    return GreedyGroupToScopeItemMoveType(
               interface::LocalSearchSolverSpec{}, spec, getProblem())
        .findBestMove(
            getMovesEvaluator(),
            /*hotContainer=*/container(0),
            getMoveStatsAggregator(),
            getEmptySearchHints(),
            std::numeric_limits<double>::max())
        .getMoveSet();
  };

  const auto expected = sampleGroupMove();
  const std::vector<Move> expectedMoves(expected.begin(), expected.end());
  for ([[maybe_unused]] const auto i : folly::irange(kSampleRuns)) {
    REBALANCER_EXPECT_EQ_MOVESETS(expectedMoves, sampleGroupMove());
  }
}

CO_TEST_F(
    GreedyGroupToScopeItemMoveTypeTest,
    ResolvesPerGroupDestinationsWithFallbackAndSizeFilter) {
  // One spec drives three groups: one restricted to a specific item, one left
  // unrestricted, and one restricted to an item that is too small for it.
  setInitialAssignment({
      {"start1", {"object0"}},
      {"start2", {"object1"}},
      {"start3", {"object2", "object3"}},
      {"c1", {}},
      {"c2", {}},
      {"c3", {}},
      {"c4a", {}},
      {"c4b", {}},
  });
  co_await addScope(
      "assignable",
      {{"item1", {"c1"}},
       {"item2", {"c2"}},
       {"small", {"c3"}},
       {"big", {"c4a", "c4b"}}});
  co_await addPartition(
      "job",
      {{"j1", {"object0"}},
       {"j2", {"object1"}},
       {"j3", {"object2", "object3"}}});
  const auto universe = buildUniverse();

  createProblem(
      /*objectiveTuple=*/{const_expr(0, *universe)},
      /*constraint=*/const_expr(0, *universe));

  const auto restrictToItem = [](const std::string& scopeItem) {
    interface::ScopeItemList list;
    list.scopeName() = "assignable";
    list.scopeItems() = {scopeItem};
    return list;
  };

  interface::GroupToScopeItemList scopeItemsPerGroups;
  scopeItemsPerGroups.partitionName() = "job";
  scopeItemsPerGroups.groupToScopeItemList() = {
      {"j1", restrictToItem("item1")}, {"j3", restrictToItem("small")}};

  interface::MoveToScopeItemsSpec moveToScopeItems;
  moveToScopeItems.defaultScopeItems() = restrictToItem("item2");
  moveToScopeItems.scopeItemsPerGroups() = scopeItemsPerGroups;

  interface::DestinationsToExploreOptions destinationsToExplore;
  destinationsToExplore.moveToScopeItems() = moveToScopeItems;

  interface::GreedyGroupToScopeItemMoveTypeSpec spec;
  spec.groupMovesPartition() = "job";
  spec.nSampleSetsToExplore() = 1;
  spec.destinationsToExplore() = destinationsToExplore;

  GreedyGroupToScopeItemMoveType moveType(
      interface::LocalSearchSolverSpec{}, spec, getProblem());
  const auto getMoveSet = [&](entities::ContainerId hotContainer) {
    return moveType
        .findBestMove(
            getMovesEvaluator(),
            hotContainer,
            getMoveStatsAggregator(),
            getEmptySearchHints(),
            std::numeric_limits<double>::max())
        .getMoveSet();
  };

  // j1 is restricted to item1, so it moves there and not to the default item2.
  const std::vector<Move> expectedJ1 = {
      Move{object("object0"), container("start1"), container("c1")}};
  REBALANCER_EXPECT_EQ_MOVESETS(expectedJ1, getMoveSet(container("start1")));

  // j2 is not restricted, so it uses the default item2.
  const std::vector<Move> expectedJ2 = {
      Move{object("object1"), container("start2"), container("c2")}};
  REBALANCER_EXPECT_EQ_MOVESETS(expectedJ2, getMoveSet(container("start2")));

  // j3's allowed item has fewer containers than the group has objects. Every
  // object needs its own container, so the item can't hold the group and j3
  // can't move.
  const std::vector<Move> noMove = {};
  REBALANCER_EXPECT_EQ_MOVESETS(noMove, getMoveSet(container("start3")));
}

} // namespace facebook::rebalancer::packer::tests
