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

#include "algopt/rebalancer/entities/builders/AsyncUniverseBuilder.h"
#include "algopt/rebalancer/entities/GraphEmbeddingUtils.h"
#include "algopt/rebalancer/entities/tests/UniverseBuilderTestUtils.h"
#include "algopt/rebalancer/entities/tests/Utils.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSolver_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"

#include <fmt/core.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

#include <set>

using namespace facebook::rebalancer::interface;

namespace facebook::rebalancer::entities::tests {
using SortedDestinationLatencyMap = facebook::algopt::ValueSortedMap<
    entities::ScopeItemId,
    double,
    entities::CompareScopeItemLatencyPair>;

template <class T>
auto makeSet(std::initializer_list<T> items) {
  return std::set<T>(items);
}

template <class T>
auto makeSet(const auto& items) {
  return std::set<T>(items.begin(), items.end());
}

template <class T>
std::set<T> makeSet(const entities::Set<T>& items) {
  return std::set<T>(items.begin(), items.end());
}

template <typename T = ObjectId>
std::shared_ptr<const Map<T, double>> makeSharedPtrEntityToValueMap(
    Map<T, double>&& map) {
  return std::make_shared<const Map<T, double>>(std::move(map));
}

class UniverseTest : public UniverseBuilderTestUtils, public ::testing::Test {
 public:
  UniverseTest() : UniverseBuilderTestUtils("task", "host") {}

  AsyncUniverseBuilder& builder() {
    return universeBuilder_;
  }
};

TEST_F(UniverseTest, Constructor) {
  // Create IdStoreConfig
  IdStoreConfig idStoreConfig;
  idStoreConfig.objectNames = {"object1", "object2"};
  idStoreConfig.containerNames = {"container1", "container2"};
  idStoreConfig.scopeNames = {"scope1"};
  idStoreConfig.scopeItemNames = {"scopeItem1"};
  idStoreConfig.partitionNames = {"partition1"};
  idStoreConfig.groupNames = {"group1"};
  idStoreConfig.dimensionNames = {"dimension1"};
  idStoreConfig.goalNames = {"goal1"};
  idStoreConfig.constraintNames = {"constraint1"};
  idStoreConfig.routingConfigNames = {};
  const auto object1Id = ObjectId(0);
  const auto object2Id = ObjectId(1);
  const auto container1Id = ContainerId(0);
  const auto container2Id = ContainerId(1);
  const auto scope1Id = ScopeId(0);
  const auto scopeItem1Id = ScopeItemId(0);
  const auto partition1Id = PartitionId(0);
  const auto group1Id = GroupId(0);
  const auto dimension1Id = DimensionId(0);
  const auto goal1Id = GoalId(0);
  const auto constraint1Id = ConstraintId(0);

  idStoreConfig.objectNameToId = {
      {"object1", object1Id}, {"object2", object2Id}};
  idStoreConfig.containerNameToId = {
      {"container1", container1Id}, {"container2", container2Id}};
  idStoreConfig.scopeNameToId = {{"scope1", scope1Id}};
  idStoreConfig.scopeIdToScopeItemNameToId = {
      {scope1Id,
       std::make_shared<const Map<std::string, ScopeItemId>>(
           Map<std::string, ScopeItemId>{{"scopeItem1", scopeItem1Id}})}};
  idStoreConfig.dimensionNameToId = {{"dimension1", dimension1Id}};
  idStoreConfig.partitionNameToId = {{"partition1", partition1Id}};
  idStoreConfig.partitionIdToGroupNameToId = {
      {partition1Id,
       std::make_shared<const Map<std::string, GroupId>>(
           Map<std::string, GroupId>{{"group1", group1Id}})}};
  idStoreConfig.goalNameToId = {{"goal1", goal1Id}};
  idStoreConfig.constraintNameToId = {{"constraint1", constraint1Id}};

  // Create Objects
  auto objects = std::make_unique<Objects>(
      /*numObjects=*/2,
      Map<DimensionId, std::shared_ptr<const ObjectDimension>>());

  // Create Containers
  auto containers =
      std::make_unique<Containers>(Map<ContainerId, std::vector<ObjectId>>{
          {container1Id, {object1Id}}, {container2Id, {object2Id}}});

  // Create Scopes
  Map<ScopeId, std::unique_ptr<Scope>> scopeIdToScope;
  scopeIdToScope.emplace(
      scope1Id,
      std::make_unique<Scope>(
          std::make_shared<const Map<ScopeItemId, Set<ContainerId>>>(
              Map<ScopeItemId, Set<ContainerId>>{
                  {scopeItem1Id, {container1Id, container2Id}}}),
          Map<DimensionId, std::shared_ptr<const ScopeDimension>>{}));
  Scopes scopes(std::move(scopeIdToScope));

  // Create Partitions
  Map<PartitionId, std::shared_ptr<Partition>> partitionIdToPartition;
  partitionIdToPartition.emplace(
      partition1Id,
      std::make_shared<Partition>(
          Map<GroupId, std::vector<ObjectId>>{{group1Id, {object1Id}}}));
  Partitions partitions(std::move(partitionIdToPartition));

  // Create Goals
  GoalSpecs goalSpec;
  goalSpec.capacitySpec() = CapacitySpec();
  auto goal = std::make_shared<Goal>(std::move(goalSpec), 1.0, 0);
  std::map<GoalId, std::shared_ptr<Goal>> goalIdToGoal;
  goalIdToGoal.emplace(goal1Id, std::move(goal));
  Goals goals(std::move(goalIdToGoal));

  // Create Constraints
  ConstraintSpecs constraintSpec;
  constraintSpec.capacitySpec() = CapacitySpec();
  auto constraint = std::make_shared<Constraint>(
      std::move(constraintSpec), ConstraintPolicy::HARD, 1.0, 2.0, 0);
  std::map<ConstraintId, std::shared_ptr<Constraint>> constraintIdToConstraint;
  constraintIdToConstraint.emplace(constraint1Id, std::move(constraint));
  Constraints constraints(std::move(constraintIdToConstraint));

  // Create UniverseConfig
  UniverseConfig config;
  config.idStore = IdStore(std::move(idStoreConfig));
  config.objectTypeName = "task";
  config.containerTypeName = "host";
  config.objects = std::move(objects);
  config.containers = std::move(containers);
  config.scopes = std::move(scopes);
  config.partitions = std::move(partitions);
  config.goals = std::move(goals);
  config.constraints = std::move(constraints);
  config.moveObjectsOnce = true;
  config.stableOptimization = false;
  config.similarContainerIds = {{container1Id, container2Id}};
  config.descendingHotnessContainers = {container1Id};
  config.objectOrderingDimensionId = dimension1Id;

  const Universe universe(std::move(config));

  // Verify IdStore
  EXPECT_EQ("task", universe.getObjectTypeName());
  EXPECT_EQ("host", universe.getContainerTypeName());
  EXPECT_EQ(object1Id, universe.getObjectId("object1"));
  EXPECT_EQ(container1Id, universe.getContainerId("container1"));
  EXPECT_EQ(scope1Id, universe.getScopeId("scope1"));
  EXPECT_EQ(partition1Id, universe.getPartitionId("partition1"));
  EXPECT_EQ(goal1Id, universe.getGoalId("goal1"));
  EXPECT_EQ(constraint1Id, universe.getConstraintId("constraint1"));

  // Verify Objects
  EXPECT_EQ(
      makeSet({object1Id, object2Id}),
      makeSet<ObjectId>(universe.getObjects().getObjectIds()));

  // Verify Containers
  EXPECT_EQ(
      makeSet({container1Id, container2Id}),
      makeSet<ContainerId>(universe.getContainers().getContainerIds()));
  EXPECT_EQ(
      makeSet({object1Id}),
      makeSet<ObjectId>(
          universe.getContainers().getInitialObjectIds(container1Id)));

  // Verify Scopes
  EXPECT_EQ(makeSet({scope1Id}), makeSet<ScopeId>(universe.getScopeIds()));
  EXPECT_EQ(
      makeSet({scopeItem1Id}),
      makeSet<ScopeItemId>(universe.getScope(scope1Id).getScopeItemIds()));

  // Verify Partitions
  EXPECT_EQ(
      makeSet({partition1Id}),
      makeSet<PartitionId>(universe.getPartitionIds()));
  EXPECT_EQ(
      std::vector<ObjectId>({object1Id}),
      universe.getPartition(partition1Id).getObjectIds(group1Id));

  // Verify Goals
  EXPECT_EQ(
      makeSet({goal1Id}), makeSet<GoalId>(universe.getGoals().getGoalIds()));
  EXPECT_EQ(1.0, universe.getGoals().getGoal(goal1Id).getWeight());

  // Verify Constraints
  EXPECT_EQ(
      makeSet({constraint1Id}),
      makeSet<ConstraintId>(universe.getConstraints().getConstraintIds()));
  EXPECT_EQ(
      ConstraintPolicy::HARD,
      universe.getConstraints().getConstraint(constraint1Id).getPolicy());

  // Verify other config fields
  EXPECT_TRUE(universe.getMoveObjectsOnce());
  EXPECT_FALSE(universe.getStableOptimization());
  EXPECT_EQ(
      std::vector<std::vector<ContainerId>>({{container1Id, container2Id}}),
      universe.getSimilarContainers());
  EXPECT_EQ(
      std::vector<ContainerId>({container1Id}),
      universe.getDescendingHotnessContainers());
  EXPECT_EQ(dimension1Id, universe.getObjectOrderingDimensionId());
}

CO_TEST_F(UniverseTest, Universe) {
  // Set initial assignment.
  setInitialAssignment(
      Map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task1", "task2"}},
          {"host1", {"task3"}},
          {"host2", {}},
      });

  const auto task0 = object("task0");
  const auto task1 = object("task1");
  const auto task2 = object("task2");
  const auto task3 = object("task3");

  const auto host0 = container("host0");
  const auto host1 = container("host1");
  const auto host2 = container("host2");

  // Create scopes.
  co_await addScope(
      "rack", {{"rack0", {"host0", "host1"}}, {"rack1", {"host2"}}});
  const auto rack = scopeId("rack");
  const auto rack0 = scopeItemId(rack, "rack0");
  const auto rack1 = scopeItemId(rack, "rack1");

  // Create partitions.
  co_await addPartition(
      "job", {{"job0", {"task0", "task1"}}, {"job1", {"task2", "task3"}}});
  const auto job = partitionId("job");
  const auto job0 = groupId(job, "job0");
  const auto job1 = groupId(job, "job1");

  // Create dimensions.
  co_await addObjectDimension("cpu", {{"task0", 0.75}, {"task1", 2.25}}, 1.5);

  const auto totalObjects = universeBuilder_.getObjects()->numObjects;

  const auto network = builder().makeObjectDimensionId("network");
  std::vector<ObjectIdToDoubleMap> networkMaps;
  networkMaps.emplace_back(
      Map<ObjectId, double>{{task0, 0.5}, {task3, 0.1}}, 1.0, totalObjects);
  networkMaps.emplace_back(
      Map<ObjectId, double>{{task0, 1.5}, {task3, 0.2}}, 2.0, totalObjects);
  co_await builder().addObjectDimension(
      network,
      ObjectDimensionData{
          .dimension =
              std::make_shared<const ObjectDimension>(std::move(networkMaps))});

  co_await addScopeDimension(
      "network", rack, {{"rack0", 10.0}, {"rack1", 20.0}}, 1.0);

  co_await addDynamicObjectDimension(
      "power",
      rack,
      {{"rack0", {{"task0", 10}, {"task1", 30}}}, {"rack1", {{"task0", 20}}}},
      1);

  // memory dimension is not used hence universe wont't register it.
  co_await addObjectDimension("memory", Map<std::string, double>{}, 0.0);

  // Create goals.
  {
    GoalSpecs spec;
    spec.capacitySpec() = CapacitySpec();
    co_await addGoal("goal0", std::move(spec), 10.0, 0);
  }

  {
    GoalSpecs spec;
    spec.balanceSpec() = BalanceSpec();
    co_await addGoal("goal1", std::move(spec), 5.0, 2);
  }

  // Create constraints.
  {
    ConstraintSpecs spec;
    spec.capacitySpec() = CapacitySpec();
    co_await addConstraint(
        "constraint0", std::move(spec), ConstraintPolicy::HARD, 50.0, 500.0, 2);
  }

  // Set descending hotness containers.
  builder().setDescendingHotnessContainers({host2, host1});

  MovesInProgressSpec movesInProgressSpec;
  movesInProgressSpec.name() = "testSpec";
  std::vector<MoveInProgress> moves;
  {
    MoveInProgress move;
    move.objName() = "task0";
    move.toContainer() = "host2";
    moves.push_back(move);
  }
  {
    MoveInProgress move;
    move.objName() = "task1";
    move.toContainer() = "host1";
    moves.push_back(move);
  }
  movesInProgressSpec.moves() = std::move(moves);

  ConstraintSpecs spec;
  spec.movesInProgressSpec() = movesInProgressSpec;
  co_await addConstraint(
      "constraint1", std::move(spec), ConstraintPolicy::HARD, 50.0, 500.0, 0);

  const std::vector<std::vector<ContainerId>> similarContainers = {
      {host0}, {host1}};
  builder().setSimilarContainers(similarContainers);

  // Test setRoutingConfig
  auto latencyTable =
      std::make_shared<Map<ScopeItemId, SortedDestinationLatencyMap>>();
  auto groupToRoutingRings =
      entities::Map<entities::GroupId, std::vector<entities::RoutingRing>>();
  groupToRoutingRings[entities::GroupId(123)] = {RoutingRing(
      entities::ScopeItemId(246),
      0.5,
      std::make_optional(
          std::vector<std::vector<entities::ScopeItemId>>{
              {entities::ScopeItemId(789)}}))};
  auto defaultOriginToDestinationScopeItemSetsPtr =
      std::make_shared<entities::Map<
          entities::ScopeItemId,
          std::vector<std::vector<entities::ScopeItemId>>>>();
  const auto host = scopeId("host");
  co_await addRoutingConfig(
      "routingConfig",
      RoutingConfigData{
          .routingConfig = std::make_shared<RoutingConfig>(
              groupToRoutingRings,
              latencyTable,
              host,
              job,
              defaultOriginToDestinationScopeItemSetsPtr)});

  // Test destinationsToExploreOptionsName
  const auto destinationsToExploreOptionsName1 = "DTEOID1";
  interface::DestinationsToExploreOptions destinationToExplore1;
  interface::MoveToCurrentScopeItemSpec moveToCurrentScopeItemSpec1;
  moveToCurrentScopeItemSpec1.scopeNameForExploringMovesToCurrentScopeItem() =
      "region";
  destinationToExplore1.set_moveToCurrentScopeItem(moveToCurrentScopeItemSpec1);
  addDestinationsToExploreOptions(
      destinationsToExploreOptionsName1, destinationToExplore1);

  const auto destinationsToExploreOptionsName2 = "DTEOID2";
  interface::DestinationsToExploreOptions destinationToExplore2;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec2;
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "host";
  defaultScopeItems.scopeItems() = {"host1", "host0"};
  moveToScopeItemsSpec2.defaultScopeItems() = defaultScopeItems;

  interface::ScopeItemList scopeItemList1;
  scopeItemList1.scopeName() = "rack";
  scopeItemList1.scopeItems() = {"rack1", "rack0"};
  moveToScopeItemsSpec2.objectToScopeItems()->emplace("task0", scopeItemList1);
  destinationToExplore2.set_moveToScopeItems(moveToScopeItemsSpec2);
  addDestinationsToExploreOptions(
      destinationsToExploreOptionsName2, destinationToExplore2);

  const auto destinationsToExploreOptionsName3 = "DTEOID3";
  interface::DestinationsToExploreOptions destinationToExplore3;
  destinationToExplore3.set_destinationToExploreName(
      "destinationToExploreName");
  addDestinationsToExploreOptions(
      destinationsToExploreOptionsName3, destinationToExplore3);

  interface::DestinationsToExploreOptions destinationToExplore4;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec4;
  interface::ScopeItemList defaultScopeItems4;
  defaultScopeItems4.scopeName() = "host";
  defaultScopeItems4.scopeItems() = {"host1", "host0"};
  moveToScopeItemsSpec4.defaultScopeItems() = defaultScopeItems4;

  interface::ScopeItemList scopeItemList4;
  scopeItemList4.scopeName() = "rack";
  scopeItemList4.scopeItems() = {"rack1", "rack0"};

  moveToScopeItemsSpec4.scopeItemsPerGroups()->groupToScopeItemList()->emplace(
      "j1", scopeItemList4);
  moveToScopeItemsSpec4.scopeItemsPerGroups()->partitionName() = "job";

  destinationToExplore4.set_moveToScopeItems(moveToScopeItemsSpec4);
  const auto destinationsToExploreOptionsName4 = "DTEOID4";
  addDestinationsToExploreOptions(
      destinationsToExploreOptionsName4, destinationToExplore4);

  // Test PrecisionTolerance
  algopt::common::thrift::PrecisionTolerances tolerances;
  tolerances.absolute() = 1e-5;
  tolerances.relative() = 1e-8;
  builder().setPrecision(tolerances);

  const auto universePtr = co_await builder().build();
  const auto& universe = *universePtr;

  const auto cpu = universe.getDimensionId("cpu");
  const auto memory = universe.getDimensionId("memory");
  const auto power = universe.getDimensionId("power");
  const auto goal0 = universe.getGoalId("goal0");
  const auto goal1 = universe.getGoalId("goal1");
  const auto constraint0 = universe.getConstraintId("constraint0");
  const auto constraint1 = universe.getConstraintId("constraint1");
  const auto routingConfigId = universe.getRoutingConfigId("routingConfig");

  // Test object and container type names.
  EXPECT_EQ("task", universe.getObjectTypeName());
  EXPECT_EQ("host", universe.getContainerTypeName());

  // Test entity ID getters.
  EXPECT_EQ(task0, universe.getObjectId("task0"));
  EXPECT_EQ(task1, universe.getObjectId("task1"));
  EXPECT_EQ(task2, universe.getObjectId("task2"));
  EXPECT_EQ(task3, universe.getObjectId("task3"));

  EXPECT_EQ(host0, universe.getContainerId("host0"));
  EXPECT_EQ(host1, universe.getContainerId("host1"));
  EXPECT_EQ(host2, universe.getContainerId("host2"));

  EXPECT_EQ(rack, universe.getScopeId("rack"));

  EXPECT_EQ(rack0, universe.getScopeItemId(rack, "rack0"));
  EXPECT_EQ(rack1, universe.getScopeItemId(rack, "rack1"));

  EXPECT_TRUE(universe.existsPartition("job"));
  EXPECT_FALSE(universe.existsPartition("fake_job"));
  EXPECT_EQ(job, universe.getPartitionId("job"));

  EXPECT_EQ(job0, universe.getGroupId(job, "job0"));
  EXPECT_EQ(job1, universe.getGroupId(job, "job1"));

  EXPECT_EQ(cpu, universe.getDimensionId("cpu"));
  EXPECT_EQ(network, universe.getDimensionId("network"));
  EXPECT_EQ(memory, universe.getDimensionId("memory"));

  EXPECT_EQ(goal0, universe.getGoalId("goal0"));
  EXPECT_EQ(goal1, universe.getGoalId("goal1"));

  auto defaultDimension = universe.getDimensionId(
      fmt::format("{}_count", universe.getObjectTypeName()));
  const auto& objects = universe.getObjects();
  EXPECT_EQ(
      makeSet({defaultDimension, cpu, network, power, memory}),
      makeSet<DimensionId>(objects.getDimensionIds()));
  EXPECT_EQ(
      makeSet({network}),
      makeSet<DimensionId>(universe.getScope(rack).getDimensionIds()));

  // Test entity name getters.
  EXPECT_EQ("task0", universe.getEntityName(task0));
  EXPECT_EQ("task1", universe.getEntityName(task1));
  EXPECT_EQ("task2", universe.getEntityName(task2));
  EXPECT_EQ("task3", universe.getEntityName(task3));
  EXPECT_EQ("host", universe.getEntityName(host));
  EXPECT_EQ("host0", universe.getEntityName(host0));
  EXPECT_EQ("host1", universe.getEntityName(host1));
  EXPECT_EQ("host2", universe.getEntityName(host2));
  EXPECT_EQ("rack", universe.getEntityName(rack));
  EXPECT_EQ("rack0", universe.getEntityName(rack0));
  EXPECT_EQ("rack1", universe.getEntityName(rack1));
  EXPECT_EQ("job", universe.getEntityName(job));
  EXPECT_EQ("job0", universe.getEntityName(job0));
  EXPECT_EQ("job1", universe.getEntityName(job1));
  EXPECT_EQ("cpu", universe.getEntityName(cpu));
  EXPECT_EQ("network", universe.getEntityName(network));
  EXPECT_EQ("memory", universe.getEntityName(memory));
  EXPECT_EQ("goal0", universe.getEntityName(goal0));
  EXPECT_EQ("goal1", universe.getEntityName(goal1));

  // Test objects.
  EXPECT_EQ(
      makeSet({task0, task1, task2, task3}),
      makeSet<ObjectId>(objects.getObjectIds()));

  // Test containers.
  EXPECT_EQ(
      makeSet({host0, host1, host2}),
      makeSet<ContainerId>(universe.getContainers().getContainerIds()));

  EXPECT_EQ(
      makeSet({task0, task1, task2}),
      makeSet<ObjectId>(universe.getContainers().getInitialObjectIds(host0)));
  EXPECT_EQ(
      makeSet({task3}),
      makeSet<ObjectId>(universe.getContainers().getInitialObjectIds(host1)));
  EXPECT_EQ(
      makeSet<ObjectId>({}),
      makeSet<ObjectId>(universe.getContainers().getInitialObjectIds(host2)));

  // Test scopes.
  EXPECT_EQ(makeSet({host, rack}), makeSet<ScopeId>(universe.getScopeIds()));

  EXPECT_EQ(
      makeSet({rack0, rack1}),
      makeSet<ScopeItemId>(universe.getScope(rack).getScopeItemIds()));

  EXPECT_EQ(
      makeSet({host0, host1}),
      makeSet(universe.getScope(rack).getContainerIds(rack0)));
  EXPECT_EQ(
      makeSet({host2}),
      makeSet(universe.getScope(rack).getContainerIds(rack1)));

  // Test partitions.
  EXPECT_TRUE(universe.existsPartition("job"));
  EXPECT_EQ(makeSet({job}), makeSet<PartitionId>(universe.getPartitionIds()));

  EXPECT_EQ(
      makeSet({task0, task1}),
      makeSet<ObjectId>(universe.getPartition(job).getObjectIds(job0)));
  EXPECT_EQ(
      makeSet({task2, task3}),
      makeSet<ObjectId>(universe.getPartition(job).getObjectIds(job1)));

  // Test object dimensions.
  EXPECT_EQ(1, objects.getDimension(cpu).size());

  EXPECT_EQ(0.75, objects.getDimension(cpu).at(0).getValue(task0));
  EXPECT_EQ(2.25, objects.getDimension(cpu).at(0).getValue(task1));
  EXPECT_EQ(1.5, objects.getDimension(cpu).at(0).getValue(task2));
  EXPECT_EQ(1.5, objects.getDimension(cpu).at(0).getValue(task3));

  EXPECT_EQ(2, objects.getDimension(network).size());

  EXPECT_EQ(0.5, objects.getDimension(network).at(0).getValue(task0));
  EXPECT_EQ(1.0, objects.getDimension(network).at(0).getValue(task1));
  EXPECT_EQ(1.0, objects.getDimension(network).at(0).getValue(task2));
  EXPECT_EQ(0.1, objects.getDimension(network).at(0).getValue(task3));

  EXPECT_EQ(1.5, objects.getDimension(network).at(1).getValue(task0));
  EXPECT_EQ(2.0, objects.getDimension(network).at(1).getValue(task1));
  EXPECT_EQ(2.0, objects.getDimension(network).at(1).getValue(task2));
  EXPECT_EQ(0.2, objects.getDimension(network).at(1).getValue(task3));

  EXPECT_EQ(1, objects.getDimension(memory).size());

  EXPECT_EQ(0.0, objects.getDimension(memory).at(0).getValue(task0));
  EXPECT_EQ(0.0, objects.getDimension(memory).at(0).getValue(task1));
  EXPECT_EQ(0.0, objects.getDimension(memory).at(0).getValue(task2));
  EXPECT_EQ(0.0, objects.getDimension(memory).at(0).getValue(task3));

  auto taskCount = universe.getDimensionId("task_count");
  EXPECT_EQ(1, objects.getDimension(taskCount).size());

  EXPECT_EQ(1.0, objects.getDimension(taskCount).at(0).getValue(task0));
  EXPECT_EQ(1.0, objects.getDimension(taskCount).at(0).getValue(task1));
  EXPECT_EQ(1.0, objects.getDimension(taskCount).at(0).getValue(task2));
  EXPECT_EQ(1.0, objects.getDimension(taskCount).at(0).getValue(task3));

  EXPECT_EQ(1, objects.getDimension(power).size());

  EXPECT_EQ(10.0, objects.getDimension(power).at(0).getValue(task0, rack0));
  EXPECT_EQ(20.0, objects.getDimension(power).at(0).getValue(task0, rack1));
  EXPECT_EQ(30.0, objects.getDimension(power).at(0).getValue(task1, rack0));
  EXPECT_EQ(1.0, objects.getDimension(power).at(0).getValue(task1, rack1));

  // Test scope dimensions.
  EXPECT_EQ(
      10.0, universe.getScope(rack).getDimension(network).getValue(rack0));
  EXPECT_EQ(
      20.0, universe.getScope(rack).getDimension(network).getValue(rack1));

  EXPECT_EQ(1.0, universe.getScope(rack).getDimension(memory).getValue(rack0));
  EXPECT_EQ(1.0, universe.getScope(rack).getDimension(memory).getValue(rack1));

  // Test goals.
  EXPECT_EQ(
      makeSet({goal0, goal1}),
      makeSet<GoalId>(universe.getGoals().getGoalIds()));

  EXPECT_EQ(3, universe.getGoals().getTupleSize());

  EXPECT_EQ(
      makeSet({goal0}), makeSet<GoalId>(universe.getGoals().getGoalIds(0)));
  EXPECT_EQ(
      makeSet<GoalId>({}), makeSet<GoalId>(universe.getGoals().getGoalIds(1)));
  EXPECT_EQ(
      makeSet({goal1}), makeSet<GoalId>(universe.getGoals().getGoalIds(2)));

  EXPECT_EQ(
      GoalSpecs::Type::capacitySpec,
      universe.getGoals().getGoal(goal0).getSpec().getType());
  EXPECT_EQ(10.0, universe.getGoals().getGoal(goal0).getWeight());
  EXPECT_EQ(0, universe.getGoals().getGoal(goal0).getTupleIndex());

  EXPECT_EQ(
      GoalSpecs::Type::balanceSpec,
      universe.getGoals().getGoal(goal1).getSpec().getType());
  EXPECT_EQ(5.0, universe.getGoals().getGoal(goal1).getWeight());
  EXPECT_EQ(2, universe.getGoals().getGoal(goal1).getTupleIndex());

  // Test constraints.
  EXPECT_EQ(
      makeSet({constraint0, constraint1}),
      makeSet<ConstraintId>(universe.getConstraints().getConstraintIds()));

  EXPECT_EQ(
      ConstraintSpecs::Type::capacitySpec,
      universe.getConstraints().getConstraint(constraint0).getSpec().getType());
  EXPECT_EQ(
      ConstraintPolicy::HARD,
      universe.getConstraints().getConstraint(constraint0).getPolicy());
  EXPECT_EQ(
      50.0,
      universe.getConstraints().getConstraint(constraint0).getInvalidCost());
  EXPECT_EQ(
      500.0,
      universe.getConstraints().getConstraint(constraint0).getInvalidState());
  EXPECT_EQ(
      2, universe.getConstraints().getConstraint(constraint0).getTupleIndex());

  const std::vector<std::vector<ContainerId>> expected = {{host0}, {host1}};
  EXPECT_EQ(expected, universe.getSimilarContainers());

  EXPECT_EQ(
      1,
      universe.getRoutingConfig(routingConfigId)
          .getGroupToRoutingRings()
          .size());

  EXPECT_EQ(
      entities::ScopeItemId(246),
      universe.getRoutingConfig(routingConfigId)
          .getGroupToRoutingRings()
          .at(entities::GroupId(123))
          .at(0)
          .getOriginScopeItem());

  EXPECT_EQ(
      0.5,
      universe.getRoutingConfig(routingConfigId)
          .getGroupToRoutingRings()
          .at(entities::GroupId(123))
          .at(0)
          .getOriginTraffic());

  EXPECT_EQ(
      entities::ScopeItemId(789),
      universe.getRoutingConfig(routingConfigId)
          .getGroupToRoutingRings()
          .at(entities::GroupId(123))
          .at(0)
          .getDestinationScopeItemSets()
          .value()
          .at(0)
          .at(0));

  auto expectedRoutingConfig = facebook::rebalancer::entities::RoutingConfig(
      groupToRoutingRings,
      latencyTable,
      host,
      job,
      defaultOriginToDestinationScopeItemSetsPtr);
  EXPECT_EQ(
      *expectedRoutingConfig.getLatencyTablePtr(),
      *universe.getRoutingConfig(routingConfigId).getLatencyTablePtr());

  // Test destinationsToExploreOptions.
  auto expectedDestinationsToExploreOptions =
      universe.getDestinationsToExploreOptions(
          destinationsToExploreOptionsName1);

  EXPECT_EQ(
      interface::DestinationsToExploreOptions::Type::moveToCurrentScopeItem,
      expectedDestinationsToExploreOptions.getType());

  EXPECT_EQ(
      "region",
      expectedDestinationsToExploreOptions.moveToCurrentScopeItem()
          .as_const()
          .value()
          .scopeNameForExploringMovesToCurrentScopeItem());

  auto expectedDestinationsToExploreOptions2 =
      universe.getDestinationsToExploreOptions(
          destinationsToExploreOptionsName2);

  EXPECT_EQ(
      interface::DestinationsToExploreOptions::Type::moveToScopeItems,
      expectedDestinationsToExploreOptions2.getType());

  auto expectedMoveToScopeItems =
      expectedDestinationsToExploreOptions2.moveToScopeItems()
          .as_const()
          .value();

  auto expectedDefaultScopeItems = expectedMoveToScopeItems.defaultScopeItems();

  EXPECT_EQ("host", expectedDefaultScopeItems->scopeName());
  EXPECT_EQ(2, expectedDefaultScopeItems->scopeItems()->size());
  EXPECT_EQ("host0", expectedDefaultScopeItems->scopeItems()->at(1));
  EXPECT_EQ("host1", expectedDefaultScopeItems->scopeItems()->at(0));

  EXPECT_EQ(1, expectedMoveToScopeItems.objectToScopeItems()->size());
  EXPECT_TRUE(expectedMoveToScopeItems.objectToScopeItems()->contains("task0"));

  auto expectedScopeItemLists =
      expectedMoveToScopeItems.objectToScopeItems()->at("task0");

  EXPECT_EQ("rack", expectedScopeItemLists.scopeName());

  EXPECT_EQ("rack1", expectedScopeItemLists.scopeItems()->at(0));
  EXPECT_EQ("rack0", expectedScopeItemLists.scopeItems()->at(1));

  auto expectedDestinationsToExploreOptions3 =
      universe.getDestinationsToExploreOptions(
          destinationsToExploreOptionsName3);

  EXPECT_EQ(
      interface::DestinationsToExploreOptions::Type::destinationToExploreName,
      expectedDestinationsToExploreOptions3.getType());

  EXPECT_EQ(
      "destinationToExploreName",
      expectedDestinationsToExploreOptions3.destinationToExploreName()
          .as_const()
          .value());

  auto expectedDestinationsToExploreOptions4 =
      universe.getDestinationsToExploreOptions(
          destinationsToExploreOptionsName4);

  EXPECT_EQ(
      interface::DestinationsToExploreOptions::Type::moveToScopeItems,
      expectedDestinationsToExploreOptions4.getType());

  auto expectedMoveToScopeItems4 =
      expectedDestinationsToExploreOptions4.moveToScopeItems()
          .as_const()
          .value();

  auto expectedDefaultScopeItems4 =
      expectedMoveToScopeItems4.defaultScopeItems();

  EXPECT_EQ("host", expectedDefaultScopeItems4->scopeName());
  EXPECT_EQ(2, expectedDefaultScopeItems4->scopeItems()->size());
  EXPECT_EQ("host0", expectedDefaultScopeItems4->scopeItems()->at(1));
  EXPECT_EQ("host1", expectedDefaultScopeItems4->scopeItems()->at(0));

  EXPECT_EQ(0, expectedMoveToScopeItems4.objectToScopeItems()->size());

  EXPECT_EQ(
      1,
      expectedMoveToScopeItems4.scopeItemsPerGroups()
          ->groupToScopeItemList()
          ->size());
  EXPECT_EQ(
      "job", expectedMoveToScopeItems4.scopeItemsPerGroups()->partitionName());

  auto expectedScopeItemLists4 =
      expectedMoveToScopeItems4.scopeItemsPerGroups()
          ->groupToScopeItemList()
          ->at("j1");

  EXPECT_EQ("rack", expectedScopeItemLists4.scopeName());

  EXPECT_EQ("rack1", expectedScopeItemLists4.scopeItems()->at(0));
  EXPECT_EQ("rack0", expectedScopeItemLists4.scopeItems()->at(1));

  // Test descending hotness containers.
  EXPECT_EQ(
      std::vector<ContainerId>({host2, host1}),
      universe.getDescendingHotnessContainers());

  // Test PrecisionTolerance.
  EXPECT_EQ(1e-5, universe.getPrecision().getTolerances().absolute());
  EXPECT_EQ(1e-8, universe.getPrecision().getTolerances().relative());
}

CO_TEST_F(UniverseTest, StableOptimization) {
  // Default.
  {
    AsyncUniverseBuilder builder;
    builder.setObjectTypeName("task");
    builder.setContainerTypeName("host");
    builder.setInitialAssignment(
        Map<std::string, std::vector<std::string>>{
            {"host0", {"task0"}},
        });

    const auto universePtr = co_await builder.build();
    EXPECT_FALSE(universePtr->getStableOptimization());
  }

  {
    AsyncUniverseBuilder builder;
    builder.setObjectTypeName("task");
    builder.setContainerTypeName("host");
    builder.setInitialAssignment(
        Map<std::string, std::vector<std::string>>{
            {"host0", {"task0"}},
        });
    builder.setStableOptimization(true);

    const auto universePtr = co_await builder.build();
    EXPECT_TRUE(universePtr->getStableOptimization());
  }

  {
    AsyncUniverseBuilder builder;
    builder.setObjectTypeName("task");
    builder.setContainerTypeName("host");
    builder.setInitialAssignment(
        Map<std::string, std::vector<std::string>>{{"host0", {"task0"}}});
    builder.setStableOptimization(false);

    const auto universePtr = co_await builder.build();
    EXPECT_FALSE(universePtr->getStableOptimization());
  }
}

CO_TEST_F(UniverseTest, MoveObjectsOnce) {
  // Default.
  {
    AsyncUniverseBuilder builder;
    builder.setObjectTypeName("task");
    builder.setContainerTypeName("host");
    builder.setInitialAssignment(
        Map<std::string, std::vector<std::string>>{{"host0", {"task0"}}});

    const auto universePtr = co_await builder.build();
    EXPECT_FALSE(universePtr->getMoveObjectsOnce());
  }

  {
    AsyncUniverseBuilder builder;
    builder.setObjectTypeName("task");
    builder.setContainerTypeName("host");
    builder.setInitialAssignment(
        Map<std::string, std::vector<std::string>>{{"host0", {"task0"}}});
    builder.setMoveObjectsOnce(true);

    const auto universePtr = co_await builder.build();
    EXPECT_TRUE(universePtr->getMoveObjectsOnce());
  }

  {
    AsyncUniverseBuilder builder;
    builder.setObjectTypeName("task");
    builder.setContainerTypeName("host");
    builder.setInitialAssignment(
        Map<std::string, std::vector<std::string>>{{"host0", {"task0"}}});
    builder.setMoveObjectsOnce(false);

    const auto universePtr = co_await builder.build();
    EXPECT_FALSE(universePtr->getMoveObjectsOnce());
  }
}

CO_TEST_F(UniverseTest, ToThrift) {
  AsyncUniverseBuilder builder;

  builder.setObjectTypeName("task");
  builder.setContainerTypeName("host");

  const Map<std::string, std::vector<std::string>> assignment{
      {"container0", {"object0"}},
  };
  builder.setInitialAssignment(assignment);

  const auto containers = builder.getContainers();
  const auto container0 = containers->getId("container0");

  const auto scope0 = builder.makeScopeId("scope0");
  const Map<std::string, std::vector<std::string>> scope0Data{
      {"scopeItem0", {"container0"}},
  };
  co_await builder.addScope(scope0, scope0Data);

  const auto partition0 = builder.makePartitionId("partition0");
  const Map<std::string, std::vector<std::string>> partition0Data{
      {"group0", {"object0"}},
  };
  co_await builder.addPartition(partition0, partition0Data);

  const auto goal0 = builder.makeGoalId("goal0");
  co_await builder.addGoal(
      goal0, GoalData{.goal = std::make_unique<Goal>(GoalSpecs(), 10.0, 3)});

  const auto constraint0 = builder.makeConstraintId("constraint0");
  co_await builder.addConstraint(
      constraint0,
      ConstraintData{
          .constraint = std::make_unique<Constraint>(
              ConstraintSpecs(), ConstraintPolicy::SOFT, 20.0, 30.0, 2)});

  builder.setStableOptimization(true);
  builder.setMoveObjectsOnce(false);
  builder.setSimilarContainers({{container0}});
  builder.setDescendingHotnessContainers({container0});

  const auto totalObjects = builder.getObjects()->numObjects;
  const auto dimension0 = builder.makeObjectDimensionId("dimension0");
  co_await builder.addObjectDimension(
      dimension0,
      ObjectDimensionData{
          .dimension =
              std::make_shared<const ObjectDimension>(ObjectIdToDoubleMap(
                  totalObjects,
                  /*defaultValue=*/1.0,
                  /*expectedNonDefaultSize=*/0))});
  builder.setObjectOrderingDimensionId(dimension0);

  algopt::common::thrift::PrecisionTolerances tolerances;
  tolerances.absolute() = 1e-5;
  tolerances.relative() = 1e-8;
  builder.setPrecision(tolerances);

  auto latencyTable =
      std::make_shared<Map<ScopeItemId, SortedDestinationLatencyMap>>();
  auto groupToRoutingRings = Map<GroupId, std::vector<entities::RoutingRing>>();
  auto defaultOriginToDestinationScopeItemSetsPtr =
      std::make_shared<entities::Map<
          entities::ScopeItemId,
          std::vector<std::vector<entities::ScopeItemId>>>>();
  const auto routingConfigId = builder.makeRoutingConfigId("routingConfig");
  co_await builder.addRoutingConfig(
      routingConfigId,
      RoutingConfigData{
          .routingConfig = std::make_shared<RoutingConfig>(
              groupToRoutingRings,
              latencyTable,
              scope0,
              partition0,
              defaultOriginToDestinationScopeItemSetsPtr)});

  const auto destinationsToExploreOptionsName1 = "DTEOID1";
  interface::DestinationsToExploreOptions destinationsToExploreOptions1;
  interface::MoveToCurrentScopeItemSpec moveToCurrentScopeItemSpec1;
  moveToCurrentScopeItemSpec1.scopeNameForExploringMovesToCurrentScopeItem() =
      "region";
  destinationsToExploreOptions1.set_moveToCurrentScopeItem(
      moveToCurrentScopeItemSpec1);
  builder.addDestinationsToExploreOptions(
      destinationsToExploreOptionsName1, destinationsToExploreOptions1);

  const auto destinationsToExploreOptionsName2 = "DTEOID2";
  interface::DestinationsToExploreOptions destinationsToExploreOptions2;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec2;
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "host";
  defaultScopeItems.scopeItems() = {"host1", "host0"};
  moveToScopeItemsSpec2.defaultScopeItems() = defaultScopeItems;

  interface::ScopeItemList scopeItemList1;
  scopeItemList1.scopeName() = "rack";
  scopeItemList1.scopeItems() = {"rack1", "rack0"};
  moveToScopeItemsSpec2.objectToScopeItems()->emplace("task0", scopeItemList1);
  destinationsToExploreOptions2.set_moveToScopeItems(moveToScopeItemsSpec2);
  builder.addDestinationsToExploreOptions(
      destinationsToExploreOptionsName2, destinationsToExploreOptions2);

  const auto destinationsToExploreOptionsName3 = "DTEOID3";
  interface::DestinationsToExploreOptions destinationsToExploreOptions3;
  destinationsToExploreOptions3.set_destinationToExploreName(
      "destinationToExploreName");
  builder.addDestinationsToExploreOptions(
      destinationsToExploreOptionsName3, destinationsToExploreOptions3);

  const auto universePtr = co_await builder.build();
  const auto& universe = *universePtr;

  const auto object0 = universe.getObjectId("object0");

  auto thrift = universe.toThrift();

  EXPECT_EQ(1, thrift.idStore()->objectNames()->size());
  EXPECT_EQ(1, thrift.idStore()->containerNames()->size());
  EXPECT_EQ("task", *thrift.objectTypeName());
  EXPECT_EQ("host", *thrift.containerTypeName());
  EXPECT_EQ(1, *thrift.objects()->numObjects());
  const std::map<int, std::vector<int>> expectedInitialAssignment(
      {{container0.asInt(), {object0.asInt()}}});
  EXPECT_EQ(
      expectedInitialAssignment,
      toStdMap(*thrift.containers()->initialAssignment()));
  EXPECT_TRUE(thrift.similarContainerIds());
  EXPECT_EQ(
      std::vector<std::vector<int>>({{container0.asInt()}}),
      *thrift.similarContainerIds());
  EXPECT_EQ(2, thrift.scopes()->scopes()->size());
  EXPECT_EQ(1, thrift.partitions()->partitions()->size());
  EXPECT_EQ(1, thrift.goals()->goals()->size());
  EXPECT_EQ(1, thrift.constraints()->constraints()->size());
  EXPECT_TRUE(*thrift.stableOptimization());
  EXPECT_FALSE(*thrift.moveObjectsOnce());
  EXPECT_EQ(
      std::vector<int>({container0.asInt()}),
      *thrift.descendingHotnessContainers());
  EXPECT_TRUE(thrift.objectOrderingDimensionId());
  EXPECT_EQ(dimension0.asInt(), *thrift.objectOrderingDimensionId());
  // only checking that the defined routing config is added to
  // thrift.routingConfigIdToRoutingConfig()
  EXPECT_TRUE(thrift.routingConfigIdToRoutingConfig()->contains(
      routingConfigId.asInt()));

  EXPECT_TRUE(thrift.idToDestinationToExploreOptions()->contains(
      destinationsToExploreOptionsName1));
  EXPECT_EQ(
      destinationsToExploreOptions1,
      thrift.idToDestinationToExploreOptions()->at(
          destinationsToExploreOptionsName1));

  EXPECT_TRUE(thrift.idToDestinationToExploreOptions()->contains(
      destinationsToExploreOptionsName2));
  EXPECT_EQ(
      destinationsToExploreOptions2,
      thrift.idToDestinationToExploreOptions()->at(
          destinationsToExploreOptionsName2));

  EXPECT_TRUE(thrift.idToDestinationToExploreOptions()->contains(
      destinationsToExploreOptionsName3));
  EXPECT_EQ(
      destinationsToExploreOptions3,
      thrift.idToDestinationToExploreOptions()->at(
          destinationsToExploreOptionsName3));
  EXPECT_EQ(tolerances.absolute(), *thrift.precisionTolerances()->absolute());
  EXPECT_EQ(tolerances.relative(), *thrift.precisionTolerances()->relative());
}

TEST_F(UniverseTest, FromThrift) {
  entities::thrift::Universe thrift;
  thrift.idStore()->objectNames() = {"object0"};
  thrift.idStore()->containerNames() = {"container0"};
  thrift.idStore()->scopeNames() = {"scope0"};
  thrift.idStore()->scopeItemNames() = {};
  thrift.idStore()->partitionNames() = {"partition0"};
  thrift.idStore()->groupNames() = {"group0"};
  thrift.idStore()->dimensionNames() = {"dimension0"};
  thrift.idStore()->goalNames() = {"goal0"};
  thrift.idStore()->constraintNames() = {"constraint0"};
  thrift.idStore()->routingConfigNames() = {"routingConfig0"};
  thrift.idStore()->objectIds() = {0};
  thrift.idStore()->containerIds() = {0};
  thrift.idStore()->scopeItemIds() = {};
  thrift.idStore()->groupIds() = {};
  thrift.idStore()->dimensionIds() = {0};
  thrift.idStore()->goalIds() = {0};
  thrift.idStore()->constraintIds() = {0};
  thrift.idStore()->routingConfigIds() = {0};
  thrift.objectTypeName() = "task";
  thrift.containerTypeName() = "host";
  thrift.objects()->numObjects() = 1;
  thrift.containers()->initialAssignment() = {{0, {0}}};
  thrift.similarContainerIds() = {{0}};
  thrift.scopes()->scopes() = {{0, entities::thrift::Scope()}};
  thrift.partitions()->partitions() = {{0, entities::thrift::Partition()}};
  thrift.goals()->goals() = {{0, entities::thrift::Goal()}};
  thrift.constraints()->constraints() = {{0, entities::thrift::Constraint()}};
  thrift.stableOptimization() = false;
  thrift.moveObjectsOnce() = true;
  thrift.descendingHotnessContainers() = {0};
  thrift.objectOrderingDimensionId() = 0;

  algopt::common::thrift::PrecisionTolerances tolerances;
  tolerances.absolute() = 1e-5;
  tolerances.relative() = 1e-8;
  thrift.precisionTolerances() = tolerances;

  thrift::RoutingConfig routingConfig;
  routingConfig.groupToRoutingRingsEntities() = {
      {0, thrift::GroupRoutingRings()}};
  routingConfig.scopeId() = 0;
  routingConfig.partitionId() = 0;
  thrift.routingConfigIdToRoutingConfig() = {{0, routingConfig}};

  interface::DestinationsToExploreOptions destinationsToExploreOptions;
  interface::MoveToCurrentScopeItemSpec moveToCurrentScopeItemSpec;
  moveToCurrentScopeItemSpec.scopeNameForExploringMovesToCurrentScopeItem() =
      "region";
  destinationsToExploreOptions.set_moveToCurrentScopeItem(
      moveToCurrentScopeItemSpec);

  interface::DestinationsToExploreOptions destinationsToExploreOptions2;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "host";
  defaultScopeItems.scopeItems() = {"host1", "host0"};
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;

  interface::ScopeItemList scopeItemList;
  scopeItemList.scopeName() = "rack";
  scopeItemList.scopeItems() = {"rack1", "rack0"};
  moveToScopeItemsSpec.objectToScopeItems()->emplace("task0", scopeItemList);
  destinationsToExploreOptions2.set_moveToScopeItems(moveToScopeItemsSpec);

  interface::DestinationsToExploreOptions destinationsToExploreOptions3;
  destinationsToExploreOptions3.set_destinationToExploreName(
      "destinationToExploreName");

  thrift.idToDestinationToExploreOptions() = {
      {"destinationId1", destinationsToExploreOptions},
      {"destinationId2", destinationsToExploreOptions2},
      {"destinationId3", destinationsToExploreOptions3}};

  auto universe = Universe(thrift);

  EXPECT_EQ("object0", universe.getEntityName(ObjectId(0)));
  EXPECT_EQ("task", universe.getObjectTypeName());
  EXPECT_EQ("host", universe.getContainerTypeName());
  EXPECT_EQ(1, universe.getNumObjects());
  EXPECT_EQ(1, universe.getContainers().getContainerIds().size());
  EXPECT_EQ(
      std::vector<std::vector<ContainerId>>({{ContainerId(0)}}),
      universe.getSimilarContainers());
  EXPECT_EQ(1, universe.getScopeIds().size());
  EXPECT_EQ(1, universe.getPartitionIds().size());
  EXPECT_EQ(1, universe.getGoals().getGoalIds().size());
  EXPECT_EQ(1, universe.getConstraints().getConstraintIds().size());
  EXPECT_FALSE(universe.getStableOptimization());
  EXPECT_TRUE(universe.getMoveObjectsOnce());
  EXPECT_EQ(
      std::vector<ContainerId>({ContainerId(0)}),
      universe.getDescendingHotnessContainers());
  EXPECT_EQ(DimensionId(0), universe.getObjectOrderingDimensionId());
  // only checking that the 'thrift.routingConfigIdToRoutingConfig' is parsed
  // correctly
  EXPECT_TRUE(universe.getRoutingConfig(RoutingConfigId(0))
                  .getGroupToRoutingRings()
                  .contains(GroupId(0)));

  EXPECT_EQ(
      "region",
      universe.getDestinationsToExploreOptions("destinationId1")
          .moveToCurrentScopeItem()
          .value()
          .scopeNameForExploringMovesToCurrentScopeItem());

  EXPECT_EQ(
      "host",
      universe.getDestinationsToExploreOptions("destinationId2")
          .moveToScopeItems()
          .value()
          .defaultScopeItems()
          ->scopeName());

  EXPECT_EQ(
      "host1",
      universe.getDestinationsToExploreOptions("destinationId2")
          .moveToScopeItems()
          .value()
          .defaultScopeItems()
          ->scopeItems()
          ->at(0));

  EXPECT_EQ(
      "host0",
      universe.getDestinationsToExploreOptions("destinationId2")
          .moveToScopeItems()
          .value()
          .defaultScopeItems()
          ->scopeItems()
          ->at(1));

  EXPECT_EQ(
      "rack",
      universe.getDestinationsToExploreOptions("destinationId2")
          .moveToScopeItems()
          .value()
          .objectToScopeItems()
          ->at("task0")
          .scopeName());

  EXPECT_EQ(
      "rack1",
      universe.getDestinationsToExploreOptions("destinationId2")
          .moveToScopeItems()
          .value()
          .objectToScopeItems()
          ->at("task0")
          .scopeItems()
          ->at(0));

  EXPECT_EQ(
      "destinationToExploreName",
      universe.getDestinationsToExploreOptions("destinationId3")
          .destinationToExploreName()
          .value());

  EXPECT_EQ(1e-5, universe.getPrecision().getTolerances().absolute());
  EXPECT_EQ(1e-8, universe.getPrecision().getTolerances().relative());
}

CO_TEST_F(UniverseTest, BuildGraphVertexCounts) {
  setInitialAssignment(
      Map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task1"}},
          {"host1", {"task2"}},
      });

  co_await addScope("rack", {{"rack0", {"host0", "host1"}}});

  co_await addPartition(
      "job", {{"job0", {"task0", "task1"}}, {"job1", {"task2"}}});

  co_await addObjectDimension("cpu", {{"task0", 0.5}, {"task1", 1.0}}, 0.0);

  auto universe = buildUniverse();
  const auto graph = universe->buildGraph();

  // Vertex count = objects + containers + scope items (from all scopes) +
  // groups. UniverseBuilder creates a built-in container scope with 1 scope
  // item per container in addition to the explicit "rack" scope.
  const size_t expectedVertices = universe->getObjects().getNumObjects() +
      universe->getContainers().getInitialAssignment().size();
  size_t totalScopeItems = 0;
  for (const auto scopeId : universe->getScopeIds()) {
    totalScopeItems += universe->getScope(scopeId).getScopeItemIds().size();
  }
  size_t totalGroups = 0;
  for (const auto partitionId : universe->getPartitionIds()) {
    totalGroups += universe->getPartition(partitionId).getGroupIds().size();
  }
  EXPECT_EQ(
      expectedVertices + totalScopeItems + totalGroups,
      graph.vertices()->size());

  size_t objectCount = 0, containerCount = 0, scopeItemCount = 0,
         groupCount = 0;
  for (const auto& v : *graph.vertices()) {
    switch (*v.type()) {
      case thrift::VertexType::OBJECT:
        ++objectCount;
        break;
      case thrift::VertexType::CONTAINER:
        ++containerCount;
        break;
      case thrift::VertexType::SCOPE_ITEM:
        ++scopeItemCount;
        break;
      case thrift::VertexType::GROUP:
        ++groupCount;
        break;
    }
  }
  EXPECT_EQ(universe->getObjects().getNumObjects(), objectCount);
  EXPECT_EQ(
      universe->getContainers().getInitialAssignment().size(), containerCount);
  EXPECT_EQ(totalScopeItems, scopeItemCount);
  EXPECT_EQ(totalGroups, groupCount);
}

CO_TEST_F(UniverseTest, BuildGraphEdgeCounts) {
  setInitialAssignment(
      Map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task1"}},
          {"host1", {"task2"}},
      });

  co_await addScope("rack", {{"rack0", {"host0", "host1"}}});

  co_await addPartition(
      "job", {{"job0", {"task0", "task1"}}, {"job1", {"task2"}}});

  auto universe = buildUniverse();
  const auto graph = universe->buildGraph();

  size_t assignmentEdges = 0, scopeEdges = 0, partitionEdges = 0;
  for (const auto& edge : *graph.edges()) {
    const auto srcType = *graph.vertices()->at(*edge.sourceIndex()).type();
    const auto tgtType = *graph.vertices()->at(*edge.targetIndex()).type();
    if (srcType == thrift::VertexType::OBJECT &&
        tgtType == thrift::VertexType::CONTAINER) {
      ++assignmentEdges;
    } else if (
        srcType == thrift::VertexType::CONTAINER &&
        tgtType == thrift::VertexType::SCOPE_ITEM) {
      ++scopeEdges;
    } else if (
        srcType == thrift::VertexType::OBJECT &&
        tgtType == thrift::VertexType::GROUP) {
      ++partitionEdges;
    }
  }

  // Every object has exactly one assignment edge.
  EXPECT_EQ(universe->getObjects().getNumObjects(), assignmentEdges);

  // Every (container, scope item) membership produces one edge.
  size_t expectedScopeEdges = 0;
  for (const auto scopeId : universe->getScopeIds()) {
    const auto& scope = universe->getScope(scopeId);
    for (const auto scopeItemId : scope.getScopeItemIds()) {
      expectedScopeEdges += scope.getContainerIds(scopeItemId).size();
    }
  }
  EXPECT_EQ(expectedScopeEdges, scopeEdges);

  // Every (object, group) membership produces one edge.
  size_t expectedPartitionEdges = 0;
  for (const auto partitionId : universe->getPartitionIds()) {
    const auto& partition = universe->getPartition(partitionId);
    for (const auto& [objectId, groupIds] : partition.getObjectIdToGroupIds()) {
      expectedPartitionEdges += groupIds.size();
    }
  }
  EXPECT_EQ(expectedPartitionEdges, partitionEdges);
}

} // namespace facebook::rebalancer::entities::tests
