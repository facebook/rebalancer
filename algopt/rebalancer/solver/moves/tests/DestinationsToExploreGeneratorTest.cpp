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
#include "algopt/rebalancer/solver/moves/DestinationsToExploreGenerator.h"
#include "algopt/rebalancer/solver/moves/tests/MoveTestBase.h"

#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

#include <utility>

namespace facebook::rebalancer::packer::tests {

class DestinationsToExploreGeneratorTest : public MoveTestBase {
 protected:
  DestinationsToExploreGeneratorTest() : MoveTestBase("object", "server") {}

  folly::coro::Task<void> setUpProblem(
      const std::vector<int>& nonAcceptingContainerIndices = {}) {
    // the initial assignment does not matter for the tests below
    setInitialAssignment({
        {"server1", {"object1"}},
        {"server2", {"object2"}},
        {"server3", {"object3"}},
        {"server4", {"object4"}},
        {"server5", {}},
    });

    // all containers except container(2) and container(5) are in the same
    // region; container(5) is not in this scope
    co_await addScope(
        "region",
        {{"region1", {"server1", "server3", "server4"}},
         {"region2", {"server2"}}});

    co_await addPartition(
        "job", {{"job1", {"object1", "object3"}}, {"job2", {"object2"}}});

    // Per-object latency that varies by region (lower is better), used to
    // exercise filter. Objects sit in region1 (except object2 in
    // region2) per the initial assignment above.
    co_await addDynamicObjectDimension(
        "latency",
        scopeId("region"),
        {{"region1",
          {{"object1", 10}, {"object2", 10}, {"object3", 5}, {"object4", 3}}},
         {"region2",
          {{"object1", 3}, {"object2", 20}, {"object3", 5}, {"object4", 10}}}},
        /*defaultValue=*/0);

    const auto universe = buildUniverse();

    PackerSet<entities::ContainerId> nonAcceptingContainers;
    for (const int idx : nonAcceptingContainerIndices) {
      nonAcceptingContainers.insert(container(idx));
    }

    createProblem(
        {const_expr(0, *universe)} /*objective*/,
        const_expr(0, *universe) /*constraint*/,
        std::nullopt /*higherPriorityObjConfig*/,
        nonAcceptingContainers);
  }

  static void verifyContainersAreAsExpected(
      const std::set<entities::ContainerId>& expected,
      const std::vector<entities::ContainerId>& result) {
    EXPECT_EQ(expected.size(), result.size());
    EXPECT_EQ(
        expected,
        std::set<entities::ContainerId>(result.begin(), result.end()));
  }

  static interface::MoveToCurrentScopeItemSpec getMoveToCurrentScopeItemSpec() {
    interface::MoveToCurrentScopeItemSpec spec;
    spec.scopeNameForExploringMovesToCurrentScopeItem() = "region";

    return spec;
  }

  static interface::ObjectDimensionBasedFilter makeObjectDimensionBasedFilter(
      const std::string& dimensionName) {
    interface::ObjectDimensionBasedFilter filter;
    filter.dimensionName() = dimensionName;
    return filter;
  }

  static interface::ScopeItemFilter makeScopeItemFilter(
      const interface::ObjectDimensionBasedFilter& dimensionFilter) {
    interface::ScopeItemFilter scopeItemFilter;
    scopeItemFilter.objectDimensionBased() = dimensionFilter;
    return scopeItemFilter;
  }

  // A spec whose region-scoped defaultScopeItems is filtered by the given
  // predicate.
  static interface::MoveToScopeItemsSpec makeMoveToScopeItemsSpec(
      const interface::ObjectDimensionBasedFilter& filter) {
    interface::ScopeItemList defaultScopeItems;
    defaultScopeItems.scopeName() = "region";
    defaultScopeItems.filter() = makeScopeItemFilter(filter);

    interface::MoveToScopeItemsSpec spec;
    spec.defaultScopeItems() = std::move(defaultScopeItems);
    return spec;
  }
};

CO_TEST_F(
    DestinationsToExploreGeneratorTest,
    getAcceptingDestinationsTestWithoutNonAccepting) {
  co_await setUpProblem();
  auto& problem = getProblem();
  auto& destinationsGenerator = problem.getDestinationsGenerator();
  auto moveToCurrentScopeItemSpec = getMoveToCurrentScopeItemSpec();

  {
    auto& containers =
        destinationsGenerator
            .getAcceptingDestinations(moveToCurrentScopeItemSpec, container(1))
            .at(0)
            .get();

    const std::set<entities::ContainerId> expectedContainers = {
        container(1), container(3), container(4)};

    EXPECT_EQ(containers.size(), 3);
    EXPECT_EQ(
        expectedContainers,
        std::set<entities::ContainerId>(containers.begin(), containers.end()));
  }
  {
    auto& containers =
        destinationsGenerator
            .getAcceptingDestinations(moveToCurrentScopeItemSpec, container(2))
            .at(0)
            .get();
    EXPECT_EQ(containers.size(), 1);
  }

  {
    // container(5) is not part of "region" scope, so we expect to explore all
    // destinations
    auto containers = destinationsGenerator.getAcceptingDestinations(
        moveToCurrentScopeItemSpec, container(5));
    EXPECT_EQ(containers.size(), 2);
    verifyContainersAreAsExpected(
        std::set<entities::ContainerId>(
            {container(1), container(3), container(4)}),
        containers.at(0).get());

    verifyContainersAreAsExpected(
        std::set<entities::ContainerId>({container(2)}),
        containers.at(1).get());
  }
}

CO_TEST_F(
    DestinationsToExploreGeneratorTest,
    getAcceptingDestinationsTestWithNonAccepting) {
  co_await setUpProblem(/*nonAcceptingContainerIndices=*/{3});
  auto& problem = getProblem();
  auto& destinationsGenerator = problem.getDestinationsGenerator();
  auto moveToCurrentScopeItemSpec = getMoveToCurrentScopeItemSpec();

  auto& containers =
      destinationsGenerator
          .getAcceptingDestinations(moveToCurrentScopeItemSpec, container(1))
          .at(0)
          .get();

  const std::set<entities::ContainerId> expectedContainers = {
      container(1), container(4)};
  EXPECT_EQ(containers.size(), 2);
  EXPECT_EQ(
      expectedContainers,
      std::set<entities::ContainerId>(containers.begin(), containers.end()));
}

CO_TEST_F(
    DestinationsToExploreGeneratorTest,
    getAcceptingDestinationsWithMoveToScopeItemsSpecAllDefault) {
  co_await setUpProblem({3});
  auto& problem = getProblem();
  auto& destinationsGenerator = problem.getDestinationsGenerator();

  // since no list of scopeItems is specified, all scopeItems in region are
  // considered
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "region";

  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;

  auto object1Destinations = destinationsGenerator.getAcceptingDestinations(
      moveToScopeItemsSpec, container(1), object(1));
  auto object2Destinations = destinationsGenerator.getAcceptingDestinations(
      moveToScopeItemsSpec, container(2), object(2));

  EXPECT_EQ(object1Destinations.size(), 2);
  EXPECT_EQ(object2Destinations.size(), 2);

  {
    auto& object1FirstContainerSet = object1Destinations.at(0).get();
    const std::set<entities::ContainerId> expectedContainers = {
        container(1), container(4)};
    verifyContainersAreAsExpected(expectedContainers, object1FirstContainerSet);
  }

  {
    auto& object1SecondContainerSet = object1Destinations.at(1).get();
    const std::set<entities::ContainerId> expectedContainers = {container(2)};
    verifyContainersAreAsExpected(
        expectedContainers, object1SecondContainerSet);
  }

  {
    auto& object2FirstContainerSet = object2Destinations.at(0).get();
    const std::set<entities::ContainerId> expectedContainers = {
        container(1), container(4)};
    verifyContainersAreAsExpected(expectedContainers, object2FirstContainerSet);
  }

  {
    auto& object2SecondContainerSet = object2Destinations.at(1).get();
    const std::set<entities::ContainerId> expectedContainers = {container(2)};
    verifyContainersAreAsExpected(
        expectedContainers, object2SecondContainerSet);
  }
}

CO_TEST_F(
    DestinationsToExploreGeneratorTest,
    getAcceptingDestinationsWithMoveToScopeItemsSpecDefault) {
  co_await setUpProblem(/*nonAcceptingContainerIndices=*/{3});
  auto& problem = getProblem();
  auto& destinationsGenerator = problem.getDestinationsGenerator();

  // by default try to all scope items in region
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "region";

  // object2 wants only destinations in region2
  interface::ScopeItemList scopeItemList;
  scopeItemList.scopeName() = "region";
  scopeItemList.scopeItems() = {"region2"};

  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;
  moveToScopeItemsSpec.objectToScopeItems() = {{"object2", scopeItemList}};

  auto object1Destinations = destinationsGenerator.getAcceptingDestinations(
      moveToScopeItemsSpec, container(1), object(1));
  auto object2Destinations = destinationsGenerator.getAcceptingDestinations(
      moveToScopeItemsSpec, container(2), object(2));

  EXPECT_EQ(object1Destinations.size(), 2);
  EXPECT_EQ(object2Destinations.size(), 1);

  {
    auto& object1FirstContainerSet = object1Destinations.at(0).get();
    const std::set<entities::ContainerId> expectedContainers = {
        container(1), container(4)};
    verifyContainersAreAsExpected(expectedContainers, object1FirstContainerSet);
  }

  {
    auto& object1SecondContainerSet = object1Destinations.at(1).get();
    const std::set<entities::ContainerId> expectedContainers = {container(2)};
    verifyContainersAreAsExpected(
        expectedContainers, object1SecondContainerSet);
  }

  {
    auto& object2FirstContainerSet = object2Destinations.at(0).get();
    const std::set<entities::ContainerId> expectedContainers = {container(2)};
    verifyContainersAreAsExpected(expectedContainers, object2FirstContainerSet);
  }
}

CO_TEST_F(
    DestinationsToExploreGeneratorTest,
    getAcceptingDestinationsWithMoveToScopeItemsSpecDifferentScopes) {
  co_await setUpProblem(/*nonAcceptingContainerIndices=*/{3});
  auto& problem = getProblem();
  auto& destinationsGenerator = problem.getDestinationsGenerator();

  // object1 wants only container(1)
  interface::ScopeItemList scopeItemList1;
  scopeItemList1.scopeName() = "server";
  scopeItemList1.scopeItems() = {"server1"};

  // object2 wants only destinations in region2
  interface::ScopeItemList scopeItemList2;
  scopeItemList2.scopeName() = "region";
  scopeItemList2.scopeItems() = {"region2"};

  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  moveToScopeItemsSpec.objectToScopeItems() = {
      {"object1", scopeItemList1}, {"object2", scopeItemList2}};

  auto object1Destinations = destinationsGenerator.getAcceptingDestinations(
      moveToScopeItemsSpec, container(1), object(1));
  auto object2Destinations = destinationsGenerator.getAcceptingDestinations(
      moveToScopeItemsSpec, container(2), object(2));

  EXPECT_EQ(object1Destinations.size(), 1);
  EXPECT_EQ(object2Destinations.size(), 1);

  {
    auto& object1FirstContainerSet = object1Destinations.at(0).get();
    const std::set<entities::ContainerId> expectedContainers = {container(1)};
    verifyContainersAreAsExpected(expectedContainers, object1FirstContainerSet);
  }

  {
    auto& object2FirstContainerSet = object2Destinations.at(0).get();
    const std::set<entities::ContainerId> expectedContainers = {container(2)};
    verifyContainersAreAsExpected(expectedContainers, object2FirstContainerSet);
  }
}

CO_TEST_F(
    DestinationsToExploreGeneratorTest,
    getAcceptingDestinationsWithMoveToScopeItemsSpecSomeEmpty) {
  co_await setUpProblem(/*nonAcceptingContainerIndices=*/{3});
  auto& problem = getProblem();
  auto& destinationsGenerator = problem.getDestinationsGenerator();

  // object1 wants no destinations
  interface::ScopeItemList scopeItemList1;
  scopeItemList1.scopeName() = "server";
  scopeItemList1.scopeItems() = {};

  // object2 wants only destinations in region2
  interface::ScopeItemList scopeItemList2;
  scopeItemList2.scopeName() = "region";
  scopeItemList2.scopeItems() = {"region2"};

  // object3 wants only container(3), but that is non-accepting
  interface::ScopeItemList scopeItemList3;
  scopeItemList3.scopeName() = "server";
  scopeItemList3.scopeItems() = {"server3"};

  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  moveToScopeItemsSpec.objectToScopeItems() = {
      {"object1", scopeItemList1},
      {"object2", scopeItemList2},
      {"object3", scopeItemList3}};

  auto object1Destinations = destinationsGenerator.getAcceptingDestinations(
      moveToScopeItemsSpec, container(1), object(1));
  auto object2Destinations = destinationsGenerator.getAcceptingDestinations(
      moveToScopeItemsSpec, container(2), object(2));
  auto object3Destinations = destinationsGenerator.getAcceptingDestinations(
      moveToScopeItemsSpec, container(3), object(3));

  EXPECT_EQ(object1Destinations.size(), 0);
  EXPECT_EQ(object2Destinations.size(), 1);
  EXPECT_EQ(object3Destinations.size(), 1);

  {
    auto& object2FirstContainerSet = object2Destinations.at(0).get();
    const std::set<entities::ContainerId> expectedContainers = {container(2)};
    verifyContainersAreAsExpected(expectedContainers, object2FirstContainerSet);
  }

  {
    auto& object3FirstContainerSet = object3Destinations.at(0).get();
    const std::set<entities::ContainerId> expectedContainers = {};
    verifyContainersAreAsExpected(expectedContainers, object3FirstContainerSet);
  }
}

CO_TEST_F(DestinationsToExploreGeneratorTest, groupToScopeItems) {
  co_await setUpProblem(/*nonAcceptingContainerIndices=*/{3});
  auto& problem = getProblem();
  auto& destinationsGenerator = problem.getDestinationsGenerator();

  // object1 and 3 wants destinations in region1
  interface::ScopeItemList scopeItemList1;
  scopeItemList1.scopeName() = "region";
  scopeItemList1.scopeItems() = {"region1"};

  // object2 wants only destinations in region2
  interface::ScopeItemList scopeItemList2;
  scopeItemList2.scopeName() = "region";
  scopeItemList2.scopeItems() = {"region2"};

  interface::GroupToScopeItemList groupToScopeItems;
  groupToScopeItems.groupToScopeItemList() = {
      {"job1", scopeItemList1}, {"job2", scopeItemList2}};
  groupToScopeItems.partitionName() = "job";

  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  moveToScopeItemsSpec.scopeItemsPerGroups() = groupToScopeItems;

  auto object1Destinations = destinationsGenerator.getAcceptingDestinations(
      moveToScopeItemsSpec, container(1), object(1));
  auto object2Destinations = destinationsGenerator.getAcceptingDestinations(
      moveToScopeItemsSpec, container(2), object(2));
  auto object3Destinations = destinationsGenerator.getAcceptingDestinations(
      moveToScopeItemsSpec, container(3), object(3));

  EXPECT_EQ(object1Destinations.size(), 1);
  EXPECT_EQ(object2Destinations.size(), 1);
  EXPECT_EQ(object3Destinations.size(), 1);

  {
    auto& object1FirstContainerSet = object1Destinations.at(0).get();
    const std::set<entities::ContainerId> expectedContainers = {
        container(1), container(4)};
    verifyContainersAreAsExpected(expectedContainers, object1FirstContainerSet);
  }

  {
    auto& object2FirstContainerSet = object2Destinations.at(0).get();
    const std::set<entities::ContainerId> expectedContainers = {container(2)};
    verifyContainersAreAsExpected(expectedContainers, object2FirstContainerSet);
  }

  {
    auto& object3FirstContainerSet = object3Destinations.at(0).get();
    const std::set<entities::ContainerId> expectedContainers = {
        container(1), container(4)};
    verifyContainersAreAsExpected(expectedContainers, object3FirstContainerSet);
  }
}

CO_TEST_F(
    DestinationsToExploreGeneratorTest,
    objectToScopeItemHighPriorityThanGroupToScopeItems) {
  co_await setUpProblem(/*nonAcceptingContainerIndices=*/{3});
  auto& problem = getProblem();
  auto& destinationsGenerator = problem.getDestinationsGenerator();

  // object1 and 3 wants destinations in region1
  interface::ScopeItemList scopeItemList1;
  scopeItemList1.scopeName() = "region";
  scopeItemList1.scopeItems() = {"region1"};

  // object2 wants only destinations in region2
  interface::ScopeItemList scopeItemList2;
  scopeItemList2.scopeName() = "region";
  scopeItemList2.scopeItems() = {"region2"};

  // object3 does not want any destinations
  interface::ScopeItemList scopeItemList3;
  scopeItemList3.scopeName() = "region";
  scopeItemList3.scopeItems() = {};

  interface::GroupToScopeItemList groupToScopeItems;
  groupToScopeItems.groupToScopeItemList() = {
      {"job1", scopeItemList1}, {"job2", scopeItemList2}};
  groupToScopeItems.partitionName() = "job";

  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  moveToScopeItemsSpec.scopeItemsPerGroups() = groupToScopeItems;
  moveToScopeItemsSpec.objectToScopeItems() = {{"object3", scopeItemList3}};

  auto object1Destinations = destinationsGenerator.getAcceptingDestinations(
      moveToScopeItemsSpec, container(1), object(1));
  auto object2Destinations = destinationsGenerator.getAcceptingDestinations(
      moveToScopeItemsSpec, container(2), object(2));
  auto object3Destinations = destinationsGenerator.getAcceptingDestinations(
      moveToScopeItemsSpec, container(3), object(3));

  EXPECT_EQ(object1Destinations.size(), 1);
  EXPECT_EQ(object2Destinations.size(), 1);
  EXPECT_EQ(object3Destinations.size(), 0);

  {
    auto& object1FirstContainerSet = object1Destinations.at(0).get();
    const std::set<entities::ContainerId> expectedContainers = {
        container(1), container(4)};
    verifyContainersAreAsExpected(expectedContainers, object1FirstContainerSet);
  }

  {
    auto& object2FirstContainerSet = object2Destinations.at(0).get();
    const std::set<entities::ContainerId> expectedContainers = {container(2)};
    verifyContainersAreAsExpected(expectedContainers, object2FirstContainerSet);
  }
}

CO_TEST_F(
    DestinationsToExploreGeneratorTest,
    getAcceptingDestinationsWithSpecOnly) {
  co_await setUpProblem(/*nonAcceptingContainerIndices=*/{3});
  auto& problem = getProblem();
  auto& destinationsGenerator = problem.getDestinationsGenerator();

  // Create a MoveToScopeItemsSpec with empty objectToScopeItems
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "region";

  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;
  // objectToScopeItems is empty by default

  auto containerDestinations = destinationsGenerator.getAcceptingDestinations(
      moveToScopeItemsSpec, container(1));

  // Should return destinations for all scope items in the region
  EXPECT_EQ(containerDestinations.size(), 2);

  {
    auto& firstContainerSet = containerDestinations.at(0).get();
    const std::set<entities::ContainerId> expectedContainers = {
        container(1), container(4)};
    verifyContainersAreAsExpected(expectedContainers, firstContainerSet);
  }

  {
    auto& secondContainerSet = containerDestinations.at(1).get();
    const std::set<entities::ContainerId> expectedContainers = {container(2)};
    verifyContainersAreAsExpected(expectedContainers, secondContainerSet);
  }
}

CO_TEST_F(
    DestinationsToExploreGeneratorTest,
    getAcceptingDestinationsWithContainerIdOnlyThrowsWhenObjectToScopeItemsNotEmpty) {
  co_await setUpProblem(/*nonAcceptingContainerIndices=*/{3});
  auto& problem = getProblem();
  auto& destinationsGenerator = problem.getDestinationsGenerator();

  // Create a MoveToScopeItemsSpec with NON-empty objectToScopeItems
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "region";

  interface::ScopeItemList objectScopeItems;
  objectScopeItems.scopeName() = "region";
  objectScopeItems.scopeItems() = {"region1"};

  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;
  moveToScopeItemsSpec.objectToScopeItems() = {{"object1", objectScopeItems}};

  // throws because objectToScopeItems is not empty
  REBALANCER_EXPECT_RUNTIME_ERROR(
      destinationsGenerator.getAcceptingDestinations(
          moveToScopeItemsSpec, container(1)),
      "this function requires that objectToScopeItems is empty");
}

CO_TEST_F(
    DestinationsToExploreGeneratorTest,
    filterStrictlyBetterAndIncludeEqual) {
  co_await setUpProblem();
  auto& problem = getProblem();
  auto& destinationsGenerator = problem.getDestinationsGenerator();

  // Lower latency is better by default.
  auto spec =
      makeMoveToScopeItemsSpec(makeObjectDimensionBasedFilter("latency"));

  // object1 sits in region1 (latency 10); only region2 (3) is strictly cheaper.
  auto object1Destinations = destinationsGenerator.getAcceptingDestinations(
      spec, container(1), object(1));
  EXPECT_EQ(object1Destinations.size(), 1);
  verifyContainersAreAsExpected(
      std::set<entities::ContainerId>({container(2)}),
      object1Destinations.at(0).get());

  // object2 sits in region2 (latency 20); only region1 (10) is strictly
  // cheaper.
  auto object2Destinations = destinationsGenerator.getAcceptingDestinations(
      spec, container(2), object(2));
  EXPECT_EQ(object2Destinations.size(), 1);
  verifyContainersAreAsExpected(
      std::set<entities::ContainerId>(
          {container(1), container(3), container(4)}),
      object2Destinations.at(0).get());

  // object3 sits in region1 (latency 5) and region2 is equal (5); nothing is
  // strictly cheaper, but includeEqual explores both equal-latency regions.
  auto object3Destinations = destinationsGenerator.getAcceptingDestinations(
      spec, container(3), object(3));
  EXPECT_EQ(object3Destinations.size(), 0);

  auto equalFilter = makeObjectDimensionBasedFilter("latency");
  equalFilter.includeEqual() = true;
  auto object3WithEqual = destinationsGenerator.getAcceptingDestinations(
      makeMoveToScopeItemsSpec(equalFilter), container(3), object(3));
  EXPECT_EQ(object3WithEqual.size(), 2);
}

CO_TEST_F(DestinationsToExploreGeneratorTest, filterPreferHigher) {
  co_await setUpProblem();
  auto& problem = getProblem();
  auto& destinationsGenerator = problem.getDestinationsGenerator();

  auto filter = makeObjectDimensionBasedFilter("latency");
  filter.preferLower() = false; // higher is better
  auto spec = makeMoveToScopeItemsSpec(filter);

  // object4 sits in region1 (latency 3); only region2 (10) is strictly higher.
  auto object4Destinations = destinationsGenerator.getAcceptingDestinations(
      spec, container(4), object(4));
  EXPECT_EQ(object4Destinations.size(), 1);
  verifyContainersAreAsExpected(
      std::set<entities::ContainerId>({container(2)}),
      object4Destinations.at(0).get());
}

CO_TEST_F(
    DestinationsToExploreGeneratorTest,
    filterRestrictsExplicitScopeItems) {
  co_await setUpProblem();
  auto& problem = getProblem();
  auto& destinationsGenerator = problem.getDestinationsGenerator();

  // object2 sits in region2 (latency 20); region1 (latency 10) is cheaper.
  // Restricting the list to region2 leaves nothing cheaper to explore. Were
  // the list ignored, filtering would fall back to the whole scope and
  // surface region1, so an empty result proves the explicit list is honored.
  interface::ScopeItemList currentRegionOnly;
  currentRegionOnly.scopeName() = "region";
  currentRegionOnly.scopeItems() = {"region2"};
  currentRegionOnly.filter() =
      makeScopeItemFilter(makeObjectDimensionBasedFilter("latency"));

  interface::MoveToScopeItemsSpec spec;
  spec.objectToScopeItems() = {{"object2", std::move(currentRegionOnly)}};

  auto destinations = destinationsGenerator.getAcceptingDestinations(
      spec, container(2), object(2));
  EXPECT_EQ(destinations.size(), 0);
}

CO_TEST_F(
    DestinationsToExploreGeneratorTest,
    filterObjectNotInScopeExploresAll) {
  co_await setUpProblem();
  auto& problem = getProblem();
  auto& destinationsGenerator = problem.getDestinationsGenerator();

  auto spec =
      makeMoveToScopeItemsSpec(makeObjectDimensionBasedFilter("latency"));

  // container(5) is not in the region scope, so there is no current value to
  // beat and all candidate regions are explored.
  auto destinations = destinationsGenerator.getAcceptingDestinations(
      spec, container(5), object(1));
  EXPECT_EQ(destinations.size(), 2);
}

CO_TEST_F(DestinationsToExploreGeneratorTest, filterWithGroupToScopeItemList) {
  co_await setUpProblem();
  auto& problem = getProblem();
  auto& destinationsGenerator = problem.getDestinationsGenerator();

  // The group's scope item list carries the filter, so the group path must
  // thread the moving object through to apply it.
  interface::ScopeItemList filteredRegions;
  filteredRegions.scopeName() = "region";
  filteredRegions.filter() =
      makeScopeItemFilter(makeObjectDimensionBasedFilter("latency"));

  interface::GroupToScopeItemList groupToScopeItems;
  groupToScopeItems.groupToScopeItemList() = {
      {"job1", filteredRegions}, {"job2", std::move(filteredRegions)}};
  groupToScopeItems.partitionName() = "job";

  interface::MoveToScopeItemsSpec spec;
  spec.scopeItemsPerGroups() = std::move(groupToScopeItems);

  // object1 (job1) sits in region1 (latency 10); region2 (3) is strictly
  // cheaper, so the group's filter keeps only region2.
  auto object1Destinations = destinationsGenerator.getAcceptingDestinations(
      spec, container(1), object(1));
  EXPECT_EQ(object1Destinations.size(), 1);
  verifyContainersAreAsExpected(
      std::set<entities::ContainerId>({container(2)}),
      object1Destinations.at(0).get());

  // object3 shares job1's spec but sits in region1 (latency 5) with region2
  // equal (5); filtering is per moving object, so nothing is strictly cheaper.
  auto object3Destinations = destinationsGenerator.getAcceptingDestinations(
      spec, container(3), object(3));
  EXPECT_EQ(object3Destinations.size(), 0);

  // object2 (job2) sits in region2 (latency 20); region1 (10) is strictly
  // cheaper.
  auto object2Destinations = destinationsGenerator.getAcceptingDestinations(
      spec, container(2), object(2));
  EXPECT_EQ(object2Destinations.size(), 1);
  verifyContainersAreAsExpected(
      std::set<entities::ContainerId>(
          {container(1), container(3), container(4)}),
      object2Destinations.at(0).get());
}

} // namespace facebook::rebalancer::packer::tests
