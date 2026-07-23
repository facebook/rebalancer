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

#include "algopt/rebalancer/entities/builders/RoutingConfigsBuilder.h"
#include "algopt/rebalancer/entities/RoutingConfig.h"
#include "algopt/rebalancer/solver/expressions/AnyPositive.h"
#include "algopt/rebalancer/solver/expressions/BipartiteSwaps.h"
#include "algopt/rebalancer/solver/expressions/Ceil.h"
#include "algopt/rebalancer/solver/expressions/GroupRoutingRing.h"
#include "algopt/rebalancer/solver/expressions/LinearSum.h"
#include "algopt/rebalancer/solver/expressions/Log.h"
#include "algopt/rebalancer/solver/expressions/Max.h"
#include "algopt/rebalancer/solver/expressions/NthLargest.h"
#include "algopt/rebalancer/solver/expressions/ObjectLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartition.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionMoveLimit.h"
#include "algopt/rebalancer/solver/expressions/ObjectVector.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/Piecewise.h"
#include "algopt/rebalancer/solver/expressions/Power.h"
#include "algopt/rebalancer/solver/expressions/ProductOperation.h"
#include "algopt/rebalancer/solver/expressions/QuotientOperation.h"
#include "algopt/rebalancer/solver/expressions/Rectangle.h"
#include "algopt/rebalancer/solver/expressions/Step.h"
#include "algopt/rebalancer/solver/expressions/SumOverThreshold.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"
#include "algopt/rebalancer/solver/expressions/Variable.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class ChangeAffectionTest : public ExpressionTestsBase {
 protected:
  void SetUp() override {
    constexpr int kNumContainers = 20;
    entities::Map<std::string, std::vector<std::string>> initialAssignment;
    for (const auto i : folly::irange(kNumContainers)) {
      initialAssignment[fmt::format("container{}", i)] = {
          fmt::format("object{}", i)};
    }
    setInitialAssignment(initialAssignment);
  }
};

// Fixture for tests that need custom initial assignments
class ChangeAffectionTestCustom : public ExpressionTestsBase {};

TEST_F(ChangeAffectionTest, AnyPositive) {
  buildUniverse();
  const AnyPositive anyPositive({}, getUniverse(), 1e-3);
  EXPECT_FALSE(anyPositive.getDirectlyAffectedContainers().exists());
  EXPECT_TRUE(
      anyPositive.isAffectedByChange(AffectedByChangeDecisionData(0, 0)) ==
      std::nullopt);
}

TEST_F(ChangeAffectionTest, BipartiteSwaps) {
  buildUniverse();
  const BipartiteSwaps bipartiteSwaps({}, {}, {}, getUniverse());
  EXPECT_TRUE(bipartiteSwaps.getDirectlyAffectedContainers().isEmpty());
  EXPECT_TRUE(
      bipartiteSwaps.isAffectedByChange(AffectedByChangeDecisionData(0, 0))
          ->getType() == AffectedByChangeType::ALL_CHANGES);
}

TEST_F(ChangeAffectionTest, Ceil) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Ceil ceil(const_expr(1, universe));
  EXPECT_FALSE(ceil.getDirectlyAffectedContainers().exists());
  EXPECT_TRUE(
      ceil.isAffectedByChange(AffectedByChangeDecisionData(0, 0)) ==
      std::nullopt);
}

TEST_F(ChangeAffectionTest, LinearSum) {
  buildUniverse();
  const LinearSum sum(getUniverse(), 10, {});
  EXPECT_FALSE(sum.getDirectlyAffectedContainers().exists());
  EXPECT_TRUE(
      sum.isAffectedByChange(AffectedByChangeDecisionData(0, 0)) ==
      std::nullopt);
}

TEST_F(ChangeAffectionTest, Log) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Log log(const_expr(1, universe));
  EXPECT_FALSE(log.getDirectlyAffectedContainers().exists());
  EXPECT_TRUE(
      log.isAffectedByChange(AffectedByChangeDecisionData(0, 0)) ==
      std::nullopt);
}

TEST_F(ChangeAffectionTest, Max) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Max max({const_expr(1, universe)}, universe);
  EXPECT_FALSE(max.getDirectlyAffectedContainers().exists());
  EXPECT_TRUE(
      max.isAffectedByChange(AffectedByChangeDecisionData(0, 0)) ==
      std::nullopt);
}

TEST_F(ChangeAffectionTest, NthLargest) {
  buildUniverse();
  const auto& universe = getUniverse();
  const NthLargest nthLargest({const_expr(1, universe)}, 4, false, universe);
  EXPECT_FALSE(nthLargest.getDirectlyAffectedContainers().exists());
  EXPECT_TRUE(
      nthLargest.isAffectedByChange(AffectedByChangeDecisionData(0, 0)) ==
      std::nullopt);
}

TEST_F(ChangeAffectionTest, ObjectVector) {
  buildUniverse();
  const auto objectVector = makeObjectVector({}, getUniverse());
  EXPECT_FALSE(objectVector->getDirectlyAffectedContainers().exists());
  EXPECT_TRUE(
      objectVector->isAffectedByChange(AffectedByChangeDecisionData(0, 0)) ==
      std::nullopt);
}

TEST_F(ChangeAffectionTest, ObjectLookupObjectsOnly) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  auto allContainersPtr = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{container(10), container(11)});
  constexpr int numTotalObjects = 1000;
  const PackerMap<entities::ObjectId, double> objectValues = {{object(1), 1}};
  auto objectVector =
      makeObjectVector(objectValues, 0, numTotalObjects, universe);
  const ObjectLookup objectLookup(
      objectVector, allContainersPtr, universe, assignment);
  auto& directedAffected =
      objectLookup.getDirectlyAffectedContainers().getNonNullSet();

  EXPECT_EQ(
      std::set<entities::ContainerId>(
          directedAffected.begin(), directedAffected.end()),
      std::set<entities::ContainerId>({container(10), container(11)}));

  const AffectedByChangeDecisionData data(
      numTotalObjects, allContainersPtr->size());
  // Lookup will be indexed by objects because it affects only 0.1% of
  // the objects but all the containers are part of lookup
  EXPECT_TRUE(
      objectLookup.isAffectedByChange(data)->getType() ==
      AffectedByChangeType::OBJECTS_ONLY);

  EXPECT_EQ(
      objectLookup.isAffectedByChange(data)->getObjects(),
      entities::Set<entities::ObjectId>{object(1)});
}

TEST_F(ChangeAffectionTest, ObjectLookupContainerOnly) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  entities::ObjectIdToDoubleMap objectValues(
      /*totalSize=*/getNumObjects(),
      /*defaultValue=*/0.0,
      /*expectedNonDefaultSize=*/4);
  objectValues.emplace(object(1), 1);
  objectValues.emplace(object(2), 2);
  objectValues.emplace(object(3), 3);
  objectValues.emplace(object(4), 4);
  auto allContainersPtr = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{container(10), container(11)});
  auto objectVector = makeObjectVector(objectValues, universe);
  const ObjectLookup objectLookup(
      objectVector, allContainersPtr, universe, assignment);
  auto& directedAffected =
      objectLookup.getDirectlyAffectedContainers().getNonNullSet();

  EXPECT_EQ(
      std::set<entities::ContainerId>(
          directedAffected.begin(), directedAffected.end()),
      std::set<entities::ContainerId>({container(10), container(11)}));

  const AffectedByChangeDecisionData data(
      static_cast<int>(objectValues.nonDefaultSize()),
      allContainersPtr->size());
  // Lookup will be indexed by containers as it affects all objects and all
  // containers of the objects, but all the containers are part of lookup
  EXPECT_TRUE(
      objectLookup.isAffectedByChange(data)->getType() ==
      AffectedByChangeType::CONTAINERS_ONLY);

  auto& containers = objectLookup.isAffectedByChange(data)->getContainers();
  auto containerSet =
      std::set<entities::ContainerId>(containers.begin(), containers.end());
  EXPECT_EQ(
      containerSet,
      std::set<entities::ContainerId>({container(10), container(11)}));
}

CO_TEST_F(ChangeAffectionTestCustom, ObjectPartition) {
  setInitialAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"container1", {"object1"}}});

  const auto objectCountDimensionId = dimensionId("object_count");
  co_await addPartition("partition1", {});

  buildUniverse();

  const ObjectPartition objectPartition(
      std::make_shared<const PartitionInfo>(
          getUniverse(), partitionId("partition1")),
      objectCountDimensionId,
      entities::Map<entities::GroupId, double>{});
  const AffectedByChangeDecisionData data(1, 1);
  EXPECT_FALSE(objectPartition.getDirectlyAffectedContainers().exists());
  EXPECT_TRUE(objectPartition.isAffectedByChange(data) == std::nullopt);
}

CO_TEST_F(ChangeAffectionTestCustom, ObjectPartitionLookup) {
  setInitialAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"container1", {"object1"}},
          {"container100", {}},
          {"container321", {}}});

  const auto objectCountDimensionId = dimensionId("object_count");
  co_await addPartition("partition1", {});

  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  const auto containerScopeId = scopeId("container");
  const auto container1ScopeItemId =
      scopeItemId(containerScopeId, "container1");

  auto partition = std::make_shared<ObjectPartition>(
      std::make_shared<const PartitionInfo>(
          universe, partitionId("partition1")),
      objectCountDimensionId,
      entities::Map<entities::GroupId, double>{});

  const ObjectPartitionLookup objectPartitionLookup(
      partition,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(100), container(321)}),
      containerScopeId,
      container1ScopeItemId,
      universe,
      assignment);
  auto& directedAffected =
      objectPartitionLookup.getDirectlyAffectedContainers().getNonNullSet();
  EXPECT_EQ(
      std::set<entities::ContainerId>(
          directedAffected.begin(), directedAffected.end()),
      std::set<entities::ContainerId>({container(100), container(321)}));

  constexpr int numTotalObjects = 1;
  constexpr int numTotalContainers = 3;
  const AffectedByChangeDecisionData data(numTotalObjects, numTotalContainers);
  EXPECT_TRUE(
      objectPartitionLookup.isAffectedByChange(data)->getType() ==
      AffectedByChangeType::CONTAINERS_ONLY);

  auto& containers =
      objectPartitionLookup.isAffectedByChange(data)->getContainers();
  auto containerSet =
      std::set<entities::ContainerId>(containers.begin(), containers.end());
  EXPECT_EQ(
      containerSet,
      std::set<entities::ContainerId>({container(100), container(321)}));
}

CO_TEST_F(ChangeAffectionTestCustom, ObjectPartitionMoveLimit) {
  setInitialAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"container1", {"object1"}}, {"container2", {}}, {"container3", {}}});

  const auto objectCountDimensionId = dimensionId("object_count");
  co_await addPartition("partition1", {});

  buildUniverse();
  const auto& universe = getUniverse();
  const auto partition1Id = partitionId("partition1");

  {
    const ObjectPartitionMoveLimit objectPartitionMoveLimit(
        universe, {}, partition1Id, objectCountDimensionId, {}, {}, {});
    EXPECT_TRUE(objectPartitionMoveLimit.getDirectlyAffectedContainers()
                    .getNonNullSet()
                    .empty());
    EXPECT_TRUE(
        objectPartitionMoveLimit
            .isAffectedByChange(AffectedByChangeDecisionData(0, 0))
            ->getType() == AffectedByChangeType::ALL_CHANGES);
  }

  {
    auto initialAssignment = Assignment(
        {{container(1), {object(1)}}, {container(2), {}}, {container(3), {}}});

    const ObjectPartitionMoveLimit objectPartitionMoveLimit(
        universe,
        initialAssignment,
        partition1Id,
        objectCountDimensionId,
        {},
        {},
        {});

    EXPECT_TRUE(
        objectPartitionMoveLimit
            .isAffectedByChange(AffectedByChangeDecisionData(1, 3))
            ->getType() == AffectedByChangeType::ALL_CHANGES);
  }

  {
    auto initialAssignment = Assignment(
        {{container(1), {object(1)}}, {container(2), {}}, {container(3), {}}});

    const ObjectPartitionMoveLimit objectPartitionMoveLimit(
        universe,
        initialAssignment,
        partition1Id,
        objectCountDimensionId,
        {},
        {container(2)},
        {container(2)});

    auto& directlyAffectedSet =
        objectPartitionMoveLimit.getDirectlyAffectedContainers()
            .getNonNullSet();
    auto actualDirectlyAffectedSet = std::set<entities::ContainerId>(
        directlyAffectedSet.begin(), directlyAffectedSet.end());
    auto expectedDirectlyAffectedSet =
        std::set<entities::ContainerId>({container(1), container(3)});
    EXPECT_EQ(expectedDirectlyAffectedSet, actualDirectlyAffectedSet);

    EXPECT_TRUE(
        objectPartitionMoveLimit
            .isAffectedByChange(AffectedByChangeDecisionData(1, 3))
            ->getType() == AffectedByChangeType::CONTAINERS_ONLY);
  }
}

TEST_F(ChangeAffectionTest, Piecewise) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Piecewise pw({{0.0, 10.0}, {10.0, 5.0}}, const_expr(2.5, universe));
  EXPECT_FALSE(pw.getDirectlyAffectedContainers().exists());
  EXPECT_TRUE(
      pw.isAffectedByChange(AffectedByChangeDecisionData(0, 0)) ==
      std::nullopt);
}

TEST_F(ChangeAffectionTest, Power) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Power power(const_expr(1, universe), 2);
  EXPECT_FALSE(power.getDirectlyAffectedContainers().exists());
  EXPECT_TRUE(
      power.isAffectedByChange(AffectedByChangeDecisionData(0, 0)) ==
      std::nullopt);
}

TEST_F(ChangeAffectionTest, ProductOperation) {
  buildUniverse();
  const auto& universe = getUniverse();
  const ProductOperation product(
      const_expr(1, universe), const_expr(1, universe));
  EXPECT_FALSE(product.getDirectlyAffectedContainers().exists());
  EXPECT_TRUE(
      product.isAffectedByChange(AffectedByChangeDecisionData(0, 0)) ==
      std::nullopt);
}

TEST_F(ChangeAffectionTest, QuotientOperation) {
  buildUniverse();
  const auto& universe = getUniverse();
  const QuotientOperation quotient(
      const_expr(1, universe), const_expr(1, universe));
  EXPECT_FALSE(quotient.getDirectlyAffectedContainers().exists());
  EXPECT_TRUE(
      quotient.isAffectedByChange(AffectedByChangeDecisionData(0, 0)) ==
      std::nullopt);
}

TEST_F(ChangeAffectionTest, Rectangle) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Rectangle rectangle(const_expr(1, universe), 2, 5);
  EXPECT_FALSE(rectangle.getDirectlyAffectedContainers().exists());
  EXPECT_TRUE(
      rectangle.isAffectedByChange(AffectedByChangeDecisionData(0, 0)) ==
      std::nullopt);
}

TEST_F(ChangeAffectionTest, Step) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Step step(const_expr(1, universe));
  EXPECT_FALSE(step.getDirectlyAffectedContainers().exists());
  EXPECT_TRUE(
      step.isAffectedByChange(AffectedByChangeDecisionData(0, 0)) ==
      std::nullopt);
}

TEST_F(ChangeAffectionTest, SumOverThreshold) {
  buildUniverse();
  const auto& universe = getUniverse();
  const SumOverThreshold sot(const_expr(1, universe), {}, false);
  EXPECT_FALSE(sot.getDirectlyAffectedContainers().exists());
  EXPECT_TRUE(
      sot.isAffectedByChange(AffectedByChangeDecisionData(0, 0)) ==
      std::nullopt);
}

TEST_F(ChangeAffectionTest, Swaps) {
  buildUniverse();
  const Swaps swaps({}, getUniverse());
  EXPECT_TRUE(swaps.getDirectlyAffectedContainers().isEmpty());
  EXPECT_TRUE(
      swaps.isAffectedByChange(AffectedByChangeDecisionData(0, 0))->getType() ==
      AffectedByChangeType::ALL_CHANGES);
}

TEST_F(ChangeAffectionTest, Variable) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  const Variable variable(object(2), container(5), universe, assignment);

  auto affectedChangeInfo =
      variable.isAffectedByChange(AffectedByChangeDecisionData(1, 1));
  EXPECT_TRUE(
      affectedChangeInfo->getType() ==
      AffectedByChangeType::ALL_GIVEN_CONTAINER_OBJECT_PAIRS);

  auto& affectedContainer = affectedChangeInfo->getContainers();
  auto& actualObjectSet = affectedChangeInfo->getObjects();
  EXPECT_EQ(
      affectedContainer, PackerSet<entities::ContainerId>({container(5)}));
  EXPECT_EQ(actualObjectSet, entities::Set<entities::ObjectId>{object(2)});
}

class GroupRoutingRingAffectedTest : public ExpressionTestsBase {
 protected:
  folly::coro::Task<std::shared_ptr<GroupRoutingRing>> createGroupRoutingRing(
      const int numObjects,
      const int numContainers) {
    std::vector<std::string> objects;
    std::vector<std::string> containers;
    objects.reserve(numObjects);
    containers.reserve(numContainers);
    for (const auto i : folly::irange(0, numObjects)) {
      objects.push_back(fmt::format("object{}", i));
    }

    for (const auto i : folly::irange(0, numContainers)) {
      containers.push_back(fmt::format("container{}", i));
    }

    PackerMap<std::string, std::vector<std::string>> initialAssignment;
    for (const auto i : folly::irange(0, numContainers)) {
      initialAssignment[containers[i]] = {};
    }
    for (const auto i : folly::irange(0, numObjects)) {
      initialAssignment[containers[i % numContainers]].push_back(objects[i]);
    }

    setInitialAssignment(initialAssignment);

    co_await addPartition("partition1", {{"group1", objects}});

    const auto containerScopeId = scopeId("container");
    const auto& scopeData =
        co_await universeBuilder_.getScope(containerScopeId);
    const auto scopeItems =
        std::views::keys(*scopeData->scopeItemIdToContainerIds);

    using SortedDestinationLatencyMap = facebook::algopt::ValueSortedMap<
        entities::ScopeItemId,
        double,
        entities::CompareScopeItemLatencyPair>;

    const auto latencyTablePtr = std::make_shared<
        entities::Map<entities::ScopeItemId, SortedDestinationLatencyMap>>();
    // Create simple latency table with all origin→destination pairs as 0.0
    // (actual values don't matter for isAffectedByChange tests)
    for (const auto& origin : scopeItems) {
      SortedDestinationLatencyMap destMap;
      for (const auto& dest : scopeItems) {
        destMap.assign(dest, 0.0);
      }
      latencyTablePtr->emplace(origin, std::move(destMap));
    }

    std::vector<entities::RoutingRing> groupRoutingRings;
    std::vector<std::vector<entities::ScopeItemId>> destinationSets;
    destinationSets.reserve(scopeItems.size());
    for (const auto& scopeItem : scopeItems) {
      destinationSets.push_back({scopeItem});
    }

    const entities::RoutingRing groupRoutingRing1(
        *scopeItems.begin(), 100.0, std::make_optional(destinationSets));
    groupRoutingRings.push_back(groupRoutingRing1);

    const auto partition1Id = partitionId("partition1");
    const auto group1Id = groupId(partition1Id, "group1");
    auto groupToRoutingRings =
        entities::Map<entities::GroupId, std::vector<entities::RoutingRing>>();
    groupToRoutingRings.emplace(group1Id, groupRoutingRings);

    auto routingConfig = std::make_shared<entities::RoutingConfig>(
        groupToRoutingRings,
        latencyTablePtr,
        containerScopeId,
        partition1Id,
        nullptr);

    co_await addRoutingConfig(
        "routing_config",
        entities::RoutingConfigData{std::move(routingConfig)});

    buildUniverse();
    const auto& universe = getUniverse();
    const auto assignment =
        Assignment(universe.getContainers().getInitialAssignment());

    co_return std::make_shared<GroupRoutingRing>(
        routingConfigId("routing_config"), group1Id, universe, assignment);
  }
};

CO_TEST_F(GroupRoutingRingAffectedTest, AllChanges) {
  constexpr int numObjects = 2;
  constexpr int numContainers = 2;

  const auto groupRoutingRing = co_await createGroupRoutingRing(
      /*numObjects=*/numObjects, /*numContainers=*/numContainers);

  EXPECT_FALSE(groupRoutingRing->getDirectlyAffectedContainers().isEmpty());

  const AffectedByChangeDecisionData data(
      /*numTotalObjects=*/numObjects, /*numTotalContainers=*/numContainers);
  const auto affectedChangeInfo = groupRoutingRing->isAffectedByChange(data);

  EXPECT_EQ(affectedChangeInfo->getType(), AffectedByChangeType::ALL_CHANGES);
}

CO_TEST_F(GroupRoutingRingAffectedTest, IndexedByObjects) {
  // Setup: 10 objects in group, 2 containers in routing scope
  // Test: All 2 containers affected, but only 10 of 100 total objects
  // Expected: OBJECTS_ONLY
  constexpr int numGroupObjects = 10;
  constexpr int numContainers = 2;
  constexpr int numTotalObjects = 100;

  const auto groupRoutingRing = co_await createGroupRoutingRing(
      /*numObjects=*/numGroupObjects, /*numContainers=*/numContainers);

  EXPECT_FALSE(groupRoutingRing->getDirectlyAffectedContainers().isEmpty());

  const AffectedByChangeDecisionData data(
      /*numTotalObjects=*/numTotalObjects,
      /*numTotalContainers=*/numContainers);
  const auto affectedChangeInfo = groupRoutingRing->isAffectedByChange(data);

  EXPECT_EQ(affectedChangeInfo->getType(), AffectedByChangeType::OBJECTS_ONLY);
  const auto& affectedObjects = affectedChangeInfo->getObjects();
  EXPECT_EQ(affectedObjects.size(), numGroupObjects);
}

CO_TEST_F(GroupRoutingRingAffectedTest, IndexedByContainers) {
  // Setup: 2 objects in group, 10 containers in routing scope
  // Test: All 2 objects affected, but only 10 of 100 total containers
  // Expected: CONTAINERS_ONLY
  constexpr int numObjects = 2;
  constexpr int numRoutingContainers = 10;
  constexpr int numTotalContainers = 100;

  const auto groupRoutingRing = co_await createGroupRoutingRing(
      /*numObjects=*/numObjects, /*numContainers=*/numRoutingContainers);

  EXPECT_FALSE(groupRoutingRing->getDirectlyAffectedContainers().isEmpty());

  const AffectedByChangeDecisionData data(
      /*numTotalObjects=*/numObjects,
      /*numTotalContainers=*/numTotalContainers);
  const auto affectedChangeInfo = groupRoutingRing->isAffectedByChange(data);

  EXPECT_EQ(
      affectedChangeInfo->getType(), AffectedByChangeType::CONTAINERS_ONLY);
  EXPECT_EQ(affectedChangeInfo->getContainers().size(), numRoutingContainers);
}

CO_TEST_F(GroupRoutingRingAffectedTest, IndexedByPairs) {
  // Setup: 3 objects in group, 3 containers in routing scope
  // Test: 3 of 4 objects affected, 3 of 4 containers affected
  // Expected: ALL_GIVEN_CONTAINER_OBJECT_PAIRS
  constexpr int numGroupObjects = 3;
  constexpr int numRoutingContainers = 3;
  constexpr int numTotalObjects = 4;
  constexpr int numTotalContainers = 4;

  const auto groupRoutingRing = co_await createGroupRoutingRing(
      /*numObjects=*/numGroupObjects, /*numContainers=*/numRoutingContainers);
  EXPECT_FALSE(groupRoutingRing->getDirectlyAffectedContainers().isEmpty());

  const AffectedByChangeDecisionData data(
      /*numTotalObjects=*/numTotalObjects,
      /*numTotalContainers=*/numTotalContainers);
  const auto affectedChangeInfo = groupRoutingRing->isAffectedByChange(data);

  EXPECT_EQ(
      affectedChangeInfo->getType(),
      AffectedByChangeType::ALL_GIVEN_CONTAINER_OBJECT_PAIRS);
  EXPECT_EQ(affectedChangeInfo->getContainers().size(), numRoutingContainers);
  EXPECT_EQ(affectedChangeInfo->getObjects().size(), numGroupObjects);
}

} // namespace facebook::rebalancer::packer::tests
