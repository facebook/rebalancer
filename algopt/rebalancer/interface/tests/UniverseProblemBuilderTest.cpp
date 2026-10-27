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
#include "algopt/rebalancer/entities/ObjectPartitionRoutingDimension.h"
#include "algopt/rebalancer/interface/tests/UniverseProblemBuilderTestBase.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/interface/UniverseProblemBuilder.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

using namespace std;
using namespace facebook::rebalancer;
using namespace facebook::rebalancer::entities;

namespace facebook::rebalancer::interface::tests {

class UniverseProblemBuilderTest : public UniverseProblemBuilderTestBase {};

INSTANTIATE_TEST_CASE_P(
    NumThreads,
    UniverseProblemBuilderTest,
    ::testing::Values(0, 1, 3, 20)); // 0 to test the case where AsyncConfig is
                                     // not used

TEST_P(UniverseProblemBuilderTest, Basic) {
  const map<string, vector<string>> assignment = {
      {"h1", {"s1", "s2"}}, {"h2", {"s3"}}, {"h3", {}}};
  const map<string, double> shard_cpu = {{"s1", 10}, {"s2", 20}, {"s3", 30}};
  const map<string, double> host_cpu = {{"h1", 100}, {"h2", 200}, {"h3", 300}};
  const map<string, string> rack_scope = {
      {"h1", "r1"}, {"h2", "r1"}, {"h3", "r2"}};
  const map<string, double> rack_shard_count = {{"r1", 10}, {"r2", 10}};
  const map<string, string> part1_map = {
      {"s1", "g1"}, {"s2", "g2"}, {"s3", "g2"}};
  const map<string, vector<string>> part2_vec = {
      {"g1", {"s1", "s2"}}, {"g2", {"s2", "s3"}}, {"g3", {"s1", "s3"}}};
  const map<string, vector<string>> part1_vec = {
      {"g1", {"s1"}}, {"g2", {"s2", "s3"}}};

  auto builder = getBuilder();
  builder.setObjectName("shard");
  builder.setContainerName("host");
  builder.setAssignment(assignment);
  builder.addObjectDimension("cpu", shard_cpu);
  builder.addContainerDimension("cpu", host_cpu);
  builder.addScope("rack", rack_scope);
  builder.addScopeDimension("shard_count", "rack", rack_shard_count);
  builder.addPartition("part1", part1_map);
  builder.addPartition("part2", part2_vec);
  const auto universe = builder.build();

  EXPECT_EQ("shard", universe->getObjectTypeName());
  EXPECT_EQ("host", universe->getContainerTypeName());

  EXPECT_EQ(
      std::set<ContainerId>({
          universe->getContainerId("h1"),
          universe->getContainerId("h2"),
          universe->getContainerId("h3"),
      }),
      toSet<ContainerId>(universe->getContainers().getContainerIds()));
}

TEST_P(UniverseProblemBuilderTest, BasicWithEmptyScope) {
  const map<string, vector<string>> assignment = {
      {"h1", {"s1", "s2"}}, {"h2", {"s3"}}, {"h3", {}}};
  const map<string, double> shard_cpu = {{"s1", 10}, {"s2", 20}, {"s3", 30}};
  const map<string, double> host_cpu = {{"h1", 100}, {"h2", 200}, {"h3", 300}};
  const map<string, std::vector<string>> rack_scope = {
      {"r1", {"h1", "h2"}},
      {"r2", {"h3"}},
      {"r3", {}}, // r3 is empty
  };
  const map<string, double> rack_shard_count = {{"r1", 10}, {"r2", 10}};
  const map<string, string> part1_map = {
      {"s1", "g1"}, {"s2", "g2"}, {"s3", "g2"}};
  const map<string, vector<string>> part2_vec = {
      {"g1", {"s1", "s2"}}, {"g2", {"s2", "s3"}}, {"g3", {"s1", "s3"}}};
  const map<string, vector<string>> part1_vec = {
      {"g1", {"s1"}}, {"g2", {"s2", "s3"}}};

  auto builder = getBuilder();
  builder.setObjectName("shard");
  builder.setContainerName("host");
  builder.setAssignment(assignment);
  builder.addObjectDimension("cpu", shard_cpu);
  builder.addContainerDimension("cpu", host_cpu);
  builder.addScope("rack", rack_scope);
  builder.addScopeDimension("shard_count", "rack", rack_shard_count);
  builder.addPartition("part1", part1_map);
  builder.addPartition("part2", part2_vec);
  const auto universe = builder.build();

  EXPECT_EQ("shard", universe->getObjectTypeName());
  EXPECT_EQ("host", universe->getContainerTypeName());

  EXPECT_EQ(
      std::set<ContainerId>({
          universe->getContainerId("h1"),
          universe->getContainerId("h2"),
          universe->getContainerId("h3"),
      }),
      toSet<ContainerId>(universe->getContainers().getContainerIds()));
}

TEST_P(UniverseProblemBuilderTest, ThrottlingSpec) {
  auto builder = getBuilder();
  builder.setObjectName("task");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0"}}});

  {
    ThrottlingSpec spec;
    spec.name() = "abc";
    spec.scope() = "host";
    spec.dimension() = "task_count";
    spec.definition() = ThrottlingSpecDefinition::IN;
    spec.limit()->globalLimit() = 3;
    spec.filter()->itemsBlacklist() = {"host0"};
    builder.addConstraint(
        spec, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
  }

  const auto universe = builder.build();

  auto constraintId = universe->getConstraintId("abc");
  auto& constraint = universe->getConstraints().getConstraint(constraintId);
  auto& spec = constraint.getSpec();
  EXPECT_EQ(ConstraintSpecs::Type::capacitySpec, spec.getType());

  auto& capacity = spec.capacitySpec().value();
  EXPECT_EQ("abc", *capacity.name());
  EXPECT_EQ("host", *capacity.scope());
  EXPECT_EQ("task_count", *capacity.dimension());
  EXPECT_EQ(CapacitySpecDefinition::NEW, *capacity.definition());
  EXPECT_EQ(3, *capacity.limit()->globalLimit());
  EXPECT_EQ(
      std::vector<std::string>({"host0"}),
      *capacity.filter()->itemsBlacklist());
}

TEST_P(UniverseProblemBuilderTest, Detailed) {
  auto builder = getBuilder();
  // Build the problem.
  builder.setObjectName("task");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task1", "task2"}},
          {"host1", {"task3"}},
          {"host2", {}},
      });
  builder.addScope(
      "rack",
      std::unordered_map<std::string, std::string>{
          {"host0", "rack0"},
          {"host1", "rack0"},
          {"host2", "rack1"},
      });
  builder.addPartition(
      "job",
      std::map<std::string, std::vector<std::string>>({
          {"job0", {"task0", "task1"}},
          {"job1", {"task2", "task3"}},
      }));
  builder.addObjectDimension(
      "cpu",
      folly::F14FastMap<std::string, double>{
          {"task0", 10}, {"task1", 20}, {"task2", 5}},
      5);

  // static dimension with usageMap
  builder.addObjectDimension(
      "memory",
      std::map<std::string, double>(
          {{"task0", 10}, {"task1", 15}, {"task3", 20}}),
      5,
      std::make_optional(
          std::map<std::string, double>(
              {{"host0", 10}, {"host1", 1}, {"host2", 5}})));

  builder.addObjectDimension(
      "network",
      std::map<std::string, std::vector<double>>{
          {"task0", {1.5, 2.5}},
          {"task1", {3.5, 4.5}},
          {"task2", {0, 0}},
      });
  builder.addDynamicObjectDimension(
      "power",
      "rack",
      folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>{
          {"rack0", {{"task0", 10}, {"task1", 20}, {"task2", 42}}},
          {"rack1", {{"task1", 30}}}},
      42);
  builder.addDynamicObjectDimension(
      "disk",
      "rack",
      "job",
      folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>{
          {"rack0", {{"job0", 100}, {"job1", 200}}},
          {"rack1", {{"job1", 300}}}},
      50);
  builder.addContainerDimension(
      "disk",
      std::map<std::string, double>{
          {"host0", 100.0},
          {"host1", 200.0},
      });
  builder.addScopeDimension(
      "network",
      "rack",
      std::unordered_map<std::string, double>{
          {"rack0", 1000.0},
          {"rack1", 2000.0},
      });

  builder.addContainerDimension(
      "unusable_host", map<string, double>({{"host2", 0}}), /*defaultValue=*/1);
  builder.addScopeDimension(
      "unusable_rack",
      "rack",
      map<string, double>({{"rack1", 0}}),
      /*defaultValue=*/1);

  builder.addSimilarContainers({{"host0"}, {"host1"}});

  const map<string, string> region_scope = {
      {"host2", "rgn1"}, {"host0", "rgn2"}, {"host1", "rgn2"}};
  builder.addScope("region", region_scope);
  DestinationsToExploreOptions destinationsToExploreOptions;
  MoveToCurrentScopeItemSpec moveToCurrentScopeItemSpec;
  moveToCurrentScopeItemSpec.scopeNameForExploringMovesToCurrentScopeItem() =
      "rack";
  destinationsToExploreOptions.set_moveToCurrentScopeItem(
      moveToCurrentScopeItemSpec);

  DestinationsToExploreOptions destinationsToExploreOptions2;
  MoveToScopeItemsSpec moveToScopeItemsSpec;

  interface::ScopeItemList scopeItemList;
  scopeItemList.scopeName() = "host";
  scopeItemList.scopeItems() = {"host1", "host2"};
  moveToScopeItemsSpec.defaultScopeItems() = scopeItemList;

  interface::ScopeItemList scopeItemList2;
  scopeItemList2.scopeName() = "region";
  scopeItemList2.scopeItems() = {"rgn2"};
  moveToScopeItemsSpec.objectToScopeItems() = {{"task1", scopeItemList2}};

  destinationsToExploreOptions2.set_moveToScopeItems(moveToScopeItemsSpec);

  interface::DestinationsToExploreOptions destinationsToExploreOptions3;
  destinationsToExploreOptions3.set_destinationToExploreName(
      "destinationToExploreName");

  builder.addDestinationsToExploreOptions(
      "moveToCurrentScopeItem", destinationsToExploreOptions);
  builder.addDestinationsToExploreOptions(
      "moveToScopeItems", destinationsToExploreOptions2);
  builder.addDestinationsToExploreOptions(
      "destinationToExploreName", destinationsToExploreOptions3);

  {
    auto spec = BalanceSpec();
    spec.name() = {"my_balance_goal"};
    spec.scope() = "host";
    spec.dimension() = "network";
    spec.filter()->itemsBlacklist() = {{"host0"}};

    builder.addGoal(spec, /*weight=*/10.0, /*tuplePos=*/0);
  }

  {
    auto spec = GroupCountSpec();
    spec.scope() = "host";
    spec.dimension() = "cpu";
    spec.partitionName() = "job";

    builder.addGoal(spec, /*weight=*/20.0, /*tuplePos=*/2);
  }

  {
    auto spec = CapacitySpec();
    spec.scope() = "rack";
    spec.dimension() = "network";

    builder.addConstraint(spec, ConstraintPolicy::HARD, 1000, 2000, 2);
  }

  {
    auto spec = GroupCountSpec();
    spec.scope() = "host";
    spec.dimension() = "network";
    spec.partitionName() = "job";

    builder.addConstraint(
        spec, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
  }

  {
    auto spec = AvoidMovingSpec();
    spec.name() = "avoid_moving";
    spec.objects() = {"task0", "task2"};

    builder.addConstraint(
        spec, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
  }

  {
    auto move = MoveInProgress();
    move.objName() = "task0";
    move.toContainer() = "host1";

    auto spec = MovesInProgressSpec();
    spec.name() = "moves_in_progress";
    spec.moves() = {move};

    builder.addConstraint(
        spec, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
  }

  {
    auto spec = MoveGroupSpec();
    spec.name() = "move_group";
    spec.partitionName() = "job";
    builder.addConstraint(
        spec, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
  }

  {
    auto spec = ToFreeSpec();
    spec.containers() = {"host0"};
    builder.addConstraint(spec, std::nullopt, std::nullopt, std::nullopt, 1);
  }

  builder.enableStableAsMuchAsPossible();
  builder.enableRestrictMovingObjectOnlyOnce();

  facebook::algopt::common::thrift::PrecisionTolerances tolerances;
  tolerances.absolute() = 1e-6;
  tolerances.relative() = 1e-9;
  builder.setPrecision(tolerances);

  const auto universe = builder.build();

  // Test type names.
  EXPECT_EQ("task", universe->getObjectTypeName());
  EXPECT_EQ("host", universe->getContainerTypeName());

  // Test objects.
  EXPECT_EQ(
      toSet<ObjectId>({
          universe->getObjectId("task0"),
          universe->getObjectId("task1"),
          universe->getObjectId("task2"),
          universe->getObjectId("task3"),
      }),
      toSet<ObjectId>(universe->getObjects().getObjectIds()));

  // Test containers.
  EXPECT_EQ(
      toSet<ContainerId>({
          universe->getContainerId("host0"),
          universe->getContainerId("host1"),
          universe->getContainerId("host2"),
      }),
      toSet<ContainerId>(universe->getContainers().getContainerIds()));

  EXPECT_EQ(
      toSet({
          universe->getObjectId("task0"),
          universe->getObjectId("task1"),
          universe->getObjectId("task2"),
      }),
      toSet<ObjectId>(universe->getContainers().getInitialObjectIds(
          universe->getContainerId("host0"))));
  EXPECT_EQ(
      toSet({
          universe->getObjectId("task3"),
      }),
      toSet<ObjectId>(universe->getContainers().getInitialObjectIds(
          universe->getContainerId("host1"))));
  EXPECT_EQ(
      toSet<ObjectId>({}),
      toSet<ObjectId>(universe->getContainers().getInitialObjectIds(
          universe->getContainerId("host2"))));

  // Test scopes.
  EXPECT_EQ(
      toSet({
          universe->getScopeId("host"),
          universe->getScopeId("rack"),
          universe->getScopeId("region"),
      }),
      toSet<ScopeId>(universe->getScopeIds()));

  auto hostId = universe->getScopeId("host");
  EXPECT_EQ(
      toSet({universe->getContainerId("host0")}),
      toSet<ContainerId>(universe->getScope(hostId).getContainerIds(
          universe->getScopeItemId(hostId, "host0"))));
  EXPECT_EQ(
      toSet({universe->getContainerId("host1")}),
      toSet<ContainerId>(universe->getScope(hostId).getContainerIds(
          universe->getScopeItemId(hostId, "host1"))));
  EXPECT_EQ(
      toSet({universe->getContainerId("host2")}),
      toSet<ContainerId>(universe->getScope(hostId).getContainerIds(
          universe->getScopeItemId(hostId, "host2"))));

  auto rackId = universe->getScopeId("rack");
  EXPECT_EQ(
      toSet({
          universe->getContainerId("host0"),
          universe->getContainerId("host1"),
      }),
      toSet<ContainerId>(universe->getScope(rackId).getContainerIds(
          universe->getScopeItemId(rackId, "rack0"))));
  EXPECT_EQ(
      toSet({
          universe->getContainerId("host2"),
      }),
      toSet<ContainerId>(universe->getScope(rackId).getContainerIds(
          universe->getScopeItemId(rackId, "rack1"))));

  // Test partitions.
  EXPECT_EQ(
      toSet({universe->getPartitionId("job")}),
      toSet<PartitionId>(universe->getPartitionIds()));

  auto jobId = universe->getPartitionId("job");
  EXPECT_EQ(
      toSet({
          universe->getObjectId("task0"),
          universe->getObjectId("task1"),
      }),
      toSet<ObjectId>(universe->getPartition(jobId).getObjectIds(
          universe->getGroupId(jobId, "job0"))));
  EXPECT_EQ(
      toSet({
          universe->getObjectId("task2"),
          universe->getObjectId("task3"),
      }),
      toSet<ObjectId>(universe->getPartition(jobId).getObjectIds(
          universe->getGroupId(jobId, "job1"))));

  // Test object dimensions.
  auto cpuId = universe->getDimensionId("cpu");
  EXPECT_EQ(1, universe->getObjects().getDimension(cpuId).size());
  EXPECT_EQ(
      10.0,
      universe->getObjects().getDimension(cpuId).at(0).getValue(
          universe->getObjectId("task0")));
  EXPECT_EQ(
      20.0,
      universe->getObjects().getDimension(cpuId).at(0).getValue(
          universe->getObjectId("task1")));
  EXPECT_EQ(
      5.0,
      universe->getObjects().getDimension(cpuId).at(0).getValue(
          universe->getObjectId("task2")));
  EXPECT_EQ(
      5.0,
      universe->getObjects().getDimension(cpuId).at(0).getValue(
          universe->getObjectId("task3")));

  auto networkId = universe->getDimensionId("network");
  EXPECT_EQ(2, universe->getObjects().getDimension(networkId).size());
  EXPECT_EQ(
      1.5,
      universe->getObjects().getDimension(networkId).at(0).getValue(
          universe->getObjectId("task0")));
  EXPECT_EQ(
      3.5,
      universe->getObjects().getDimension(networkId).at(0).getValue(
          universe->getObjectId("task1")));
  EXPECT_EQ(
      0.0,
      universe->getObjects().getDimension(networkId).at(0).getValue(
          universe->getObjectId("task2")));
  EXPECT_EQ(
      0.0,
      universe->getObjects().getDimension(networkId).at(0).getValue(
          universe->getObjectId("task3")));
  EXPECT_EQ(
      2.5,
      universe->getObjects().getDimension(networkId).at(1).getValue(
          universe->getObjectId("task0")));
  EXPECT_EQ(
      4.5,
      universe->getObjects().getDimension(networkId).at(1).getValue(
          universe->getObjectId("task1")));
  EXPECT_EQ(
      0.0,
      universe->getObjects().getDimension(networkId).at(1).getValue(
          universe->getObjectId("task2")));
  EXPECT_EQ(
      0.0,
      universe->getObjects().getDimension(networkId).at(1).getValue(
          universe->getObjectId("task3")));

  auto powerId = universe->getDimensionId("power");
  EXPECT_EQ(1, universe->getObjects().getDimension(powerId).size());
  EXPECT_EQ(
      10,
      universe->getObjects().getDimension(powerId).at(0).getValue(
          universe->getObjectId("task0"),
          universe->getScopeItemId(rackId, "rack0")));
  EXPECT_EQ(
      42,
      universe->getObjects().getDimension(powerId).at(0).getValue(
          universe->getObjectId("task0"),
          universe->getScopeItemId(rackId, "rack1")));
  EXPECT_EQ(
      20,
      universe->getObjects().getDimension(powerId).at(0).getValue(
          universe->getObjectId("task1"),
          universe->getScopeItemId(rackId, "rack0")));
  EXPECT_EQ(
      42,
      universe->getObjects().getDimension(powerId).at(0).getValue(
          universe->getObjectId("task2"),
          universe->getScopeItemId(rackId, "rack0")));
  EXPECT_EQ(
      30,
      universe->getObjects().getDimension(powerId).at(0).getValue(
          universe->getObjectId("task1"),
          universe->getScopeItemId(rackId, "rack1")));

  auto diskId = universe->getDimensionId("disk");
  EXPECT_EQ(1, universe->getObjects().getDimension(diskId).size());
  EXPECT_EQ(
      100,
      universe->getObjects().getDimension(diskId).at(0).getValue(
          universe->getObjectId("task0"),
          universe->getScopeItemId(rackId, "rack0")));
  EXPECT_EQ(
      100,
      universe->getObjects().getDimension(diskId).at(0).getValue(
          universe->getObjectId("task1"),
          universe->getScopeItemId(rackId, "rack0")));
  EXPECT_EQ(
      200,
      universe->getObjects().getDimension(diskId).at(0).getValue(
          universe->getObjectId("task2"),
          universe->getScopeItemId(rackId, "rack0")));
  EXPECT_EQ(
      200,
      universe->getObjects().getDimension(diskId).at(0).getValue(
          universe->getObjectId("task3"),
          universe->getScopeItemId(rackId, "rack0")));
  EXPECT_EQ(
      50,
      universe->getObjects().getDimension(diskId).at(0).getValue(
          universe->getObjectId("task0"),
          universe->getScopeItemId(rackId, "rack1")));
  EXPECT_EQ(
      50,
      universe->getObjects().getDimension(diskId).at(0).getValue(
          universe->getObjectId("task1"),
          universe->getScopeItemId(rackId, "rack1")));
  EXPECT_EQ(
      300,
      universe->getObjects().getDimension(diskId).at(0).getValue(
          universe->getObjectId("task2"),
          universe->getScopeItemId(rackId, "rack1")));
  EXPECT_EQ(
      300,
      universe->getObjects().getDimension(diskId).at(0).getValue(
          universe->getObjectId("task3"),
          universe->getScopeItemId(rackId, "rack1")));

  // Test static object dimension when usageMap is given
  auto memoryDimensionId = universe->getDimensionId("memory");
  EXPECT_EQ(1, universe->getObjects().getDimension(memoryDimensionId).size());
  // task1 and task2 are scaled by values corresponding to host0 in the usageMap
  EXPECT_NEAR(
      151 / 30.3,
      universe->getObjects()
          .getDimension(memoryDimensionId)
          .at(0)
          .getValue(universe->getObjectId("task1")),
      1e-8);

  EXPECT_NEAR(
      51 / 30.3,
      universe->getObjects()
          .getDimension(memoryDimensionId)
          .at(0)
          .getValue(universe->getObjectId("task2")),
      1e-8);
  EXPECT_NEAR(
      101 / 30.3,
      universe->getObjects()
          .getDimension(memoryDimensionId)
          .at(0)
          .getValue(universe->getObjectId("task0")),
      1e-8);
  EXPECT_NEAR(
      20.2 / 20.2,
      universe->getObjects()
          .getDimension(memoryDimensionId)
          .at(0)
          .getValue(universe->getObjectId("task3")),
      1e-8);

  // Test scope dimensions.
  EXPECT_EQ(
      100.0,
      universe->getScope(hostId).getDimension(diskId).getValue(
          universe->getScopeItemId(hostId, "host0")));
  EXPECT_EQ(
      200.0,
      universe->getScope(hostId).getDimension(diskId).getValue(
          universe->getScopeItemId(hostId, "host1")));
  EXPECT_EQ(
      1.0,
      universe->getScope(hostId).getDimension(diskId).getValue(
          universe->getScopeItemId(hostId, "host2")));

  EXPECT_EQ(
      1000.0,
      universe->getScope(rackId).getDimension(networkId).getValue(
          universe->getScopeItemId(rackId, "rack0")));
  EXPECT_EQ(
      2000.0,
      universe->getScope(rackId).getDimension(networkId).getValue(
          universe->getScopeItemId(rackId, "rack1")));

  EXPECT_EQ(
      1.0,
      universe->getScope(rackId).getDimension(diskId).getValue(
          universe->getScopeItemId(rackId, "rack0")));
  EXPECT_EQ(
      1.0,
      universe->getScope(rackId).getDimension(diskId).getValue(
          universe->getScopeItemId(rackId, "rack1")));

  auto unusableHost = universe->getDimensionId("unusable_host");
  auto unusableRack = universe->getDimensionId("unusable_rack");
  // Test unusable_host dimension
  EXPECT_EQ(
      0.0,
      universe->getScope(hostId)
          .getDimension(unusableHost)
          .getValue(universe->getScopeItemId(hostId, "host2")));
  EXPECT_EQ(
      1.0,
      universe->getScope(hostId)
          .getDimension(unusableHost)
          .getValue(universe->getScopeItemId(hostId, "host0")));
  EXPECT_EQ(
      1.0,
      universe->getScope(hostId)
          .getDimension(unusableHost)
          .getValue(universe->getScopeItemId(hostId, "host1")));

  // Test unusable_rack dimension
  EXPECT_EQ(
      0.0,
      universe->getScope(rackId)
          .getDimension(unusableRack)
          .getValue(universe->getScopeItemId(rackId, "rack1")));
  EXPECT_EQ(
      1.0,
      universe->getScope(rackId)
          .getDimension(unusableRack)
          .getValue(universe->getScopeItemId(rackId, "rack0")));

  // Test goals.
  auto goal0 = universe->getGoalId("my_balance_goal");
  auto goal1 = universe->getGoalId("goal_1");
  EXPECT_EQ(
      std::vector<GoalId>({goal0, goal1}),
      toVec<entities::GoalId>(universe->getGoals().getGoalIds()));

  EXPECT_EQ(3, universe->getGoals().getTupleSize());
  EXPECT_EQ(std::vector<GoalId>({goal0}), universe->getGoals().getGoalIds(0));
  EXPECT_EQ(std::vector<GoalId>({}), universe->getGoals().getGoalIds(1));
  EXPECT_EQ(std::vector<GoalId>({goal1}), universe->getGoals().getGoalIds(2));

  EXPECT_EQ(
      GoalSpecs::Type::balanceSpec,
      universe->getGoals().getGoal(goal0).getSpec().getType());
  EXPECT_EQ(10.0, universe->getGoals().getGoal(goal0).getWeight());
  EXPECT_EQ(0, universe->getGoals().getGoal(goal0).getTupleIndex());

  // Test constraints.
  auto avoidMoving = universe->getConstraintId("avoid_moving");
  auto movesInProgress = universe->getConstraintId("moves_in_progress");
  auto moveGroup = universe->getConstraintId("move_group");
  auto constraint0 = universe->getConstraintId("constraint_0");
  auto constraint1 = universe->getConstraintId("constraint_1");
  auto toFree = universe->getConstraintId("constraint_5");
  EXPECT_EQ(
      std::vector<ConstraintId>({
          constraint0,
          constraint1,
          avoidMoving,
          movesInProgress,
          moveGroup,
          toFree,
      }),
      toVec<entities::ConstraintId>(
          universe->getConstraints().getConstraintIds()));

  {
    auto& constraint = universe->getConstraints().getConstraint(avoidMoving);
    EXPECT_EQ(
        ConstraintSpecs::Type::avoidMovingSpec, constraint.getSpec().getType());
    EXPECT_EQ(
        "avoid_moving", *constraint.getSpec().avoidMovingSpec().value().name());
    EXPECT_EQ(
        toSet<std::string>(std::vector<std::string>({"task0", "task2"})),
        toSet<std::string>(
            *constraint.getSpec().avoidMovingSpec().value().objects()));

    EXPECT_EQ(ConstraintPolicy::DEFAULT, constraint.getPolicy());
    EXPECT_EQ(100, constraint.getInvalidCost());
    EXPECT_EQ(10000, constraint.getInvalidState());
    EXPECT_EQ(0, constraint.getTupleIndex());
  }

  {
    auto& constraint =
        universe->getConstraints().getConstraint(movesInProgress);
    EXPECT_EQ(
        ConstraintSpecs::Type::movesInProgressSpec,
        constraint.getSpec().getType());
    EXPECT_EQ(
        "moves_in_progress",
        *constraint.getSpec().movesInProgressSpec().value().name());

    auto& moves = *constraint.getSpec().movesInProgressSpec()->moves();
    EXPECT_EQ(1, moves.size());
    EXPECT_EQ("task0", *moves.at(0).objName());
    EXPECT_EQ("host1", *moves.at(0).toContainer());

    EXPECT_EQ(ConstraintPolicy::DEFAULT, constraint.getPolicy());
    EXPECT_EQ(100, constraint.getInvalidCost());
    EXPECT_EQ(10000, constraint.getInvalidState());
    EXPECT_EQ(0, constraint.getTupleIndex());
  }

  {
    auto& constraint = universe->getConstraints().getConstraint(moveGroup);
    EXPECT_EQ(
        ConstraintSpecs::Type::moveGroupSpec, constraint.getSpec().getType());
    EXPECT_EQ(
        "move_group", *constraint.getSpec().moveGroupSpec().value().name());
    EXPECT_EQ(
        "job", *constraint.getSpec().moveGroupSpec().value().partitionName());

    EXPECT_EQ(ConstraintPolicy::DEFAULT, constraint.getPolicy());
    EXPECT_EQ(100, constraint.getInvalidCost());
    EXPECT_EQ(10000, constraint.getInvalidState());
    EXPECT_EQ(0, constraint.getTupleIndex());
  }

  {
    auto& constraint = universe->getConstraints().getConstraint(toFree);
    EXPECT_EQ(
        ConstraintSpecs::Type::toFreeSpec, constraint.getSpec().getType());
    EXPECT_EQ(
        std::vector<std::string>{"host0"},
        *constraint.getSpec().toFreeSpec().value().containers());

    EXPECT_EQ(ConstraintPolicy::DEFAULT, constraint.getPolicy());
    EXPECT_EQ(100, constraint.getInvalidCost());
    EXPECT_EQ(10000, constraint.getInvalidState());
    EXPECT_EQ(1, constraint.getTupleIndex());
  }

  EXPECT_EQ(
      ConstraintSpecs::Type::capacitySpec,
      universe->getConstraints()
          .getConstraint(constraint0)
          .getSpec()
          .getType());
  EXPECT_EQ(
      ConstraintPolicy::HARD,
      universe->getConstraints().getConstraint(constraint0).getPolicy());
  EXPECT_EQ(
      1000,
      universe->getConstraints().getConstraint(constraint0).getInvalidCost());
  EXPECT_EQ(
      2000,
      universe->getConstraints().getConstraint(constraint0).getInvalidState());
  EXPECT_EQ(
      2, universe->getConstraints().getConstraint(constraint0).getTupleIndex());

  EXPECT_EQ(
      ConstraintSpecs::Type::groupCountSpec,
      universe->getConstraints()
          .getConstraint(constraint1)
          .getSpec()
          .getType());
  EXPECT_EQ(
      ConstraintPolicy::DEFAULT,
      universe->getConstraints().getConstraint(constraint1).getPolicy());
  EXPECT_EQ(
      100,
      universe->getConstraints().getConstraint(constraint1).getInvalidCost());
  EXPECT_EQ(
      10000,
      universe->getConstraints().getConstraint(constraint1).getInvalidState());
  EXPECT_EQ(
      0, universe->getConstraints().getConstraint(constraint1).getTupleIndex());

  // Test stable optimization.
  EXPECT_TRUE(universe->getStableOptimization());

  EXPECT_TRUE(universe->getMoveObjectsOnce());

  auto res = universe->getSimilarContainers();
  const std::vector<std::vector<ContainerId>> expected = {
      {universe->getContainerId("host0")}, {universe->getContainerId("host1")}};
  EXPECT_EQ(expected, res);

  // Test destinationsToExploreOptions
  auto expectedDestinationsToExplore =
      universe->getDestinationsToExploreOptions("moveToCurrentScopeItem");
  auto expectedDestinationsToExplore2 =
      universe->getDestinationsToExploreOptions("moveToScopeItems");
  auto expectedDestinationsToExplore3 =
      universe->getDestinationsToExploreOptions("destinationToExploreName");

  EXPECT_EQ(
      "rack",
      expectedDestinationsToExplore.moveToCurrentScopeItem()
          .as_const()
          .value()
          .scopeNameForExploringMovesToCurrentScopeItem());
  EXPECT_EQ(
      "host",
      expectedDestinationsToExplore2.moveToScopeItems()
          .as_const()
          .value()
          .defaultScopeItems()
          ->scopeName());
  EXPECT_EQ(
      "host1",
      expectedDestinationsToExplore2.moveToScopeItems()
          .as_const()
          .value()
          .defaultScopeItems()
          ->scopeItems()
          ->at(0));
  EXPECT_EQ(
      "host2",
      expectedDestinationsToExplore2.moveToScopeItems()
          .as_const()
          .value()
          .defaultScopeItems()
          ->scopeItems()
          ->at(1));
  EXPECT_EQ(
      "region",
      expectedDestinationsToExplore2.moveToScopeItems()
          .as_const()
          .value()
          .objectToScopeItems()
          ->at("task1")
          .scopeName());
  EXPECT_EQ(
      "rgn2",
      expectedDestinationsToExplore2.moveToScopeItems()
          .as_const()
          .value()
          .objectToScopeItems()
          ->at("task1")
          .scopeItems()
          ->at(0));
  EXPECT_EQ(
      "destinationToExploreName",
      expectedDestinationsToExplore3.destinationToExploreName()
          .as_const()
          .value());

  EXPECT_EQ(1e-6, universe->getPrecision().getTolerances().absolute());
  EXPECT_EQ(1e-9, universe->getPrecision().getTolerances().relative());
}

TEST_P(UniverseProblemBuilderTest, InvalidSetDefaultAfterAddConstraint) {
  double invalidCost = 1;
  double invalidState = 2;
  auto builder = getBuilder();
  builder.setObjectName("task");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{});

  auto spec = ThrottlingSpec();
  spec.scope() = "host";
  spec.dimension() = "cpu";

  builder.addConstraint(
      spec, std::nullopt, invalidCost, invalidState, std::nullopt);

  ConstraintParams params;
  params.invalidCost() = 0;
  params.invalidState() = 0;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.setDefaultConstraintParams(params),
      "Calling setDefaultConstraintParams after a addConstraint call leads to undesirable behavior as the parameters in setDefaultConstraintParams only affect future addConstraint calls.");
}

TEST_P(UniverseProblemBuilderTest, InvalidSetConstraintAfterAddConstraint) {
  double invalidCost = 1;
  double invalidState = 2;
  auto builder = getBuilder();
  builder.setObjectName("task");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{});

  auto spec = ThrottlingSpec();
  spec.scope() = "host";
  spec.dimension() = "cpu";

  builder.addConstraint(
      spec, std::nullopt, invalidCost, invalidState, std::nullopt);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.setConstraintPolicy(ConstraintPolicy::DEFAULT),
      "Calling setConstraintPolicy after a addConstraint call leads to undesirable behavior as the policy in setConstraintPolicy only affects future addConstraint calls.");
}

TEST_P(UniverseProblemBuilderTest, ValidSetDefaultBeforeAddConstraint) {
  double invalidCost = 1;
  double invalidState = 2;
  auto builder = getBuilder();
  builder.setObjectName("task");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{});

  auto spec = ThrottlingSpec();
  spec.scope() = "host";
  spec.dimension() = "cpu";

  ConstraintParams params;
  params.invalidCost() = 0;
  params.invalidState() = 0;

  builder.setDefaultConstraintParams(params);
  builder.addConstraint(
      spec, std::nullopt, invalidCost, invalidState, std::nullopt);

  const auto universe = builder.build();
  EXPECT_EQ(universe->getConstraints().getConstraintIds().size(), 1);
}

TEST_P(UniverseProblemBuilderTest, ValidSetConstraintBeforeAddConstraint) {
  double invalidCost = 1;
  double invalidState = 2;
  auto builder = getBuilder();
  builder.setObjectName("task");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{});

  auto spec = ThrottlingSpec();
  spec.scope() = "host";
  spec.dimension() = "cpu";

  builder.setConstraintPolicy(ConstraintPolicy::DEFAULT);
  builder.addConstraint(
      spec, std::nullopt, invalidCost, invalidState, std::nullopt);

  const auto universe = builder.build();
  EXPECT_EQ(universe->getConstraints().getConstraintIds().size(), 1);
}

TEST_P(UniverseProblemBuilderTest, ValidSetDefaultBeforeSetConstraint) {
  double invalidCost = 1;
  double invalidState = 2;
  auto builder = getBuilder();
  builder.setObjectName("task");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{});

  auto spec = ThrottlingSpec();
  spec.scope() = "host";
  spec.dimension() = "cpu";

  ConstraintParams params;
  params.invalidCost() = 0;
  params.invalidState() = 0;

  builder.setDefaultConstraintParams(params);
  builder.setConstraintPolicy(ConstraintPolicy::DEFAULT);
  builder.addConstraint(
      spec, std::nullopt, invalidCost, invalidState, std::nullopt);

  const auto universe = builder.build();
  EXPECT_EQ(universe->getConstraints().getConstraintIds().size(), 1);
}

TEST_P(UniverseProblemBuilderTest, ValidSetConstraintBeforeSetDefault) {
  double invalidCost = 1;
  double invalidState = 2;
  auto builder = getBuilder();
  builder.setObjectName("task");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{});

  auto spec = ThrottlingSpec();
  spec.scope() = "host";
  spec.dimension() = "cpu";

  ConstraintParams params;
  params.invalidCost() = 0;
  params.invalidState() = 0;

  builder.setConstraintPolicy(ConstraintPolicy::DEFAULT);
  builder.setDefaultConstraintParams(params);
  builder.addConstraint(
      spec, std::nullopt, invalidCost, invalidState, std::nullopt);

  const auto universe = builder.build();
  EXPECT_EQ(universe->getConstraints().getConstraintIds().size(), 1);
}

TEST_P(UniverseProblemBuilderTest, GroupCountSpecEmptyDimension) {
  auto builder = getBuilder();
  builder.setObjectName("shard");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{});

  GroupCountSpec spec;
  spec.name() = "abc";
  spec.scope() = "host";
  builder.addConstraint(
      spec, std::nullopt, std::nullopt, std::nullopt, std::nullopt);

  const auto universe = builder.build();
  auto constraintId = universe->getConstraintId("abc");
  auto& constraint = universe->getConstraints().getConstraint(constraintId);

  EXPECT_EQ(
      "shard_count",
      *constraint.getSpec().groupCountSpec().value().dimension());
}

TEST_P(UniverseProblemBuilderTest, AvoidMovingSpecName) {
  auto builder = getBuilder();
  builder.setObjectName("shard");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{});

  {
    AvoidMovingSpec spec;
    spec.objects() = {"shard_0"};
    builder.addConstraint(
        spec, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
  }

  {
    AvoidMovingSpec spec;
    spec.name() = "abc";
    spec.objects() = {"shard_1"};
    builder.addConstraint(
        spec, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
  }

  const auto universe = builder.build();

  const auto constraintIds = toVec<entities::ConstraintId>(
      universe->getConstraints().getConstraintIds());
  EXPECT_EQ(2, constraintIds.size());

  auto constraintId0 = constraintIds.at(0);
  EXPECT_EQ("constraint_0", universe->getEntityName(constraintId0));

  auto constraintId1 = constraintIds.at(1);
  EXPECT_EQ("abc", universe->getEntityName(constraintId1));
}

TEST_P(UniverseProblemBuilderTest, DescendingHotnessContainers) {
  auto builder = getBuilder();
  builder.setObjectName("shard");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"h1", {"s1"}}, {"h2", {"s2"}}, {"h3", {}}});

  builder.overrideContainerHotnessRanking({"h3", "h1"});

  const auto universe = builder.build();

  auto& containerIds = universe->getDescendingHotnessContainers();
  EXPECT_EQ(2, containerIds.size());
  EXPECT_EQ("h3", universe->getEntityName(containerIds.at(0)));
  EXPECT_EQ("h1", universe->getEntityName(containerIds.at(1)));
}

TEST_P(UniverseProblemBuilderTest, ScaleByUsageBasic) {
  auto builder = getBuilder();
  builder.setObjectName("shard");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"shard0", "shard1"}}});
  builder.addObjectDimension(
      "memory",
      std::map<std::string, double>({{"shard0", 10}, {"shard1", 15}}),
      5.0,
      std::make_optional(std::map<std::string, double>({{"host0", 10}})));
  const auto universe = builder.build();

  auto memoryDimensionId = universe->getDimensionId("memory");
  // shard0 and shard1 are scaled by values corresponding to host0 in the
  // scaleByUsageMap
  const double offset = 25.0 / 100.0 / 2;
  const double total = 25.0 + offset * 2;
  EXPECT_NEAR(
      (10.0 / total) * (10.0 + offset),
      universe->getObjects()
          .getDimension(memoryDimensionId)
          .at(0)
          .getValue(universe->getObjectId("shard0")),
      1e-8);

  EXPECT_NEAR(
      (10.0 / total) * (15.0 + offset),
      universe->getObjects()
          .getDimension(memoryDimensionId)
          .at(0)
          .getValue(universe->getObjectId("shard1")),
      1e-8);
}

TEST_P(UniverseProblemBuilderTest, ScaleByUsageNoErrorForEmptyContainer) {
  auto builder = getBuilder();
  builder.setObjectName("shard");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"shard0", "shard1"}}, {"host1", {}}});
  builder.addObjectDimension(
      "memory",
      std::map<std::string, double>({{"shard0", 10}, {"shard1", 15}}),
      5.0,
      std::make_optional(std::map<std::string, double>({{"host0", 10}})));
  const auto universe = builder.build();

  auto memoryDimensionId = universe->getDimensionId("memory");
  // this is the same example as ScaleByUsageBasic, but where there is  an extra
  // empty host which has no usage value. This should behave exactly as in the
  // example above.

  // shard0 and shard1 are scaled by values corresponding to host0 in the
  // scaleByUsageMap
  const double offset = 25.0 / 100.0 / 2;
  const double total = 25.0 + offset * 2;
  EXPECT_NEAR(
      (10.0 / total) * (10.0 + offset),
      universe->getObjects()
          .getDimension(memoryDimensionId)
          .at(0)
          .getValue(universe->getObjectId("shard0")),
      1e-8);

  EXPECT_NEAR(
      (10.0 / total) * (15.0 + offset),
      universe->getObjects()
          .getDimension(memoryDimensionId)
          .at(0)
          .getValue(universe->getObjectId("shard1")),
      1e-8);
}

TEST_P(
    UniverseProblemBuilderTest,
    ScaleByUsageErrorWhenNonEmptyContainerHasNoValue) {
  auto builder = getBuilder();
  builder.setObjectName("shard");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"shard0", "shard1"}}, {"host1", {"shard2"}}});

  const auto addObjectDimensionAndBuild = [&]() {
    builder.addObjectDimension(
        "memory",
        std::map<std::string, double>({{"shard0", 10}, {"shard1", 20}}),
        5.0,
        std::make_optional(std::map<std::string, double>({{"host0", 100}})));

    return builder.build();
  };

  REBALANCER_EXPECT_RUNTIME_ERROR_MATCHES(
      addObjectDimensionAndBuild(),
      "container .* is non-empty, but it does not have an associated dimension value");
}

TEST_P(UniverseProblemBuilderTest, AddObjectPartitionRoutingDimension) {
  auto builder = getBuilder();
  builder.setObjectName("task");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task1"}}});

  const std::map<std::string, std::string> taskToJob = {
      {"task0", "job0"}, {"task1", "job1"}};
  builder.addPartition("job", taskToJob);

  builder.addRoutingConfig("routingConfig", "host", "job", {}, {}, {});

  builder.addObjectPartitionRoutingDimension(
      "load",
      "job",
      "routingConfig",
      // job1 (5) matches defaultValue and must be dropped from the stored map.
      std::unordered_map<std::string, double>({{"job0", 10}, {"job1", 5}}),
      5.0,
      // job0 (2) matches defaultStaticValue and must be dropped.
      std::unordered_map<std::string, double>({{"job0", 2}, {"job1", 4}}),
      2.0);
  const auto universe = builder.build();

  auto jobId = universe->getPartitionId("job");
  auto routingDimensionId = universe->getDimensionId("load");
  auto& routingDim = dynamic_cast<const ObjectPartitionRoutingDimension&>(
      universe->getObjects().getDimension(routingDimensionId).only());

  // Round-trip back to thrift to confirm the default-valued entries were
  // dropped from groupIdToValue and groupIdToStaticValue.
  const auto routingThrift = routingDim.toThrift()
                                 .objectPartitionRoutingDimension()
                                 .as_const()
                                 .value();
  EXPECT_EQ(1, routingThrift.groupIdToValue()->size());
  EXPECT_EQ(1, routingThrift.groupIdToStaticValue()->size());

  EXPECT_NEAR(
      10.0, routingDim.getValue(universe->getGroupId(jobId, "job0")), 1e-8);

  // returns default value (also where the explicit job1==5 was dropped)
  EXPECT_NEAR(
      5.0, routingDim.getValue(universe->getGroupId(jobId, "job1")), 1e-8);

  EXPECT_NEAR(
      4.0,
      routingDim.getStaticValue(universe->getGroupId(jobId, "job1")),
      1e-8);

  // returns default static value (also where the explicit job0==2 was dropped)
  EXPECT_NEAR(
      2.0,
      routingDim.getStaticValue(universe->getGroupId(jobId, "job0")),
      1e-8);
}

TEST_P(
    UniverseProblemBuilderTest,
    ErrorWhenRoutingDimensionPartitionUnequalToRoutingConfigPartition) {
  auto builder = getBuilder();
  builder.setObjectName("task");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task1"}}});

  const std::map<std::string, std::string> taskToJob = {
      {"task0", "job0"}, {"task1", "job1"}};
  builder.addPartition("job", taskToJob);
  builder.addPartition("jobCopy", taskToJob);

  builder.addRoutingConfig("routingConfig", "host", "job", {}, {}, {});

  REBALANCER_EXPECT_RUNTIME_ERROR(
      builder.addObjectPartitionRoutingDimension(
          "load",
          "jobCopy",
          "routingConfig",
          std::unordered_map<std::string, double>({{"job0", 10}}),
          5.0),
      "Routing config and objectPartitionRoutingDimension are defined on different partitions");
}

TEST_P(
    UniverseProblemBuilderTest,
    ErrorWhenDynamicDimensionBasedOnGroupWithJointPartition) {
  auto builder = getBuilder();
  builder.setObjectName("task");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0"}},
          {"host1", {"task1"}},
      });
  builder.addScope(
      "rack",
      std::unordered_map<std::string, std::string>{
          {"host0", "rack0"},
          {"host1", "rack1"},
      });
  builder.addPartition(
      "job",
      std::map<std::string, std::vector<std::string>>({
          {"job0", {"task0", "task1"}},
          {"job1", {"task0"}},
      }));

  const auto addDynamicDimensionAndBuild = [&]() {
    builder.addDynamicObjectDimension(
        "disk",
        "rack",
        "job",
        folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>{
            {"rack0", {{"job0", 1}}}, {"rack1", {{"job1", 2}}}},
        50);
    return builder.build();
  };

  REBALANCER_EXPECT_RUNTIME_ERROR(
      addDynamicDimensionAndBuild(),
      "Group based dynamic dimension is only supported for disjoint partition.");
}

TEST_P(
    UniverseProblemBuilderTest,
    ErrorWhenMultiDimensionalObjectDimensionWithDifferentVectorSizes) {
  auto builder = getBuilder();
  builder.setObjectName("task");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0"}},
          {"host1", {"task1", "task2", "task3"}},
      });

  const auto addObjectDimensionAndBuild = [&]() {
    builder.addObjectDimension(
        "network",
        std::map<std::string, std::vector<double>>{
            {"task0", {1.5, 2.5}},
            {"task1", {3.5, 4.5}},
            {"task2", {3.5}},
            {"task3", {3.5, 4.5}},
        });
    return builder.build();
  };

  REBALANCER_EXPECT_RUNTIME_ERROR(
      addObjectDimensionAndBuild(),
      "All vectors in objectToValues must have the same size, but found that vector w.r.t. object 'task2' has size 1 while vector w.r.t. 'task0' has size 2");
}

TEST_P(
    UniverseProblemBuilderTest,
    OkForMultiDimensionalObjectDimensionToBeEmpty) {
  auto builder = getBuilder();
  builder.setObjectName("task");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0"}},
          {"host1", {"task1", "task2"}},
      });
  builder.addObjectDimension(
      "network",
      std::map<std::string, std::vector<double>>{},
      /*defaultValue=*/1);

  // in this case, the dimension is empty, we expect all the values to be the
  // default value (1)
  const auto universe = builder.build();
  auto& objects = universe->getObjects();
  auto networkDimensionId = universe->getDimensionId("network");
  auto& networkDim = objects.getDimension(networkDimensionId);
  EXPECT_EQ(1, networkDim.size());
  for (auto& object : {"task0", "task1", "task2"}) {
    EXPECT_EQ(1, networkDim.at(0).getValue(universe->getObjectId(object)));
  }
}
TEST_P(UniverseProblemBuilderTest, BuildTwice) {
  auto builder = getBuilder();
  builder.setObjectName("task");
  builder.setContainerName("host");
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task1"}},
          {"host1", {"task2"}},
          {"host2", {}},
      });
  builder.addObjectDimension(
      "cpu", std::map<std::string, double>{{"task0", 10}, {"task1", 20}}, 5);
  builder.addScope(
      "rack",
      std::unordered_map<std::string, std::string>{
          {"host0", "rack0"}, {"host1", "rack0"}, {"host2", "rack1"}});
  builder.addPartition(
      "job",
      std::map<std::string, std::vector<std::string>>{
          {"job0", {"task0", "task1"}}, {"job1", {"task2"}}});

  auto verifyBuild1Contents = [](const auto& u) {
    EXPECT_EQ("task", u->getObjectTypeName());
    EXPECT_EQ("host", u->getContainerTypeName());

    EXPECT_EQ(
        toSet<ObjectId>({
            u->getObjectId("task0"),
            u->getObjectId("task1"),
            u->getObjectId("task2"),
        }),
        toSet<ObjectId>(u->getObjects().getObjectIds()));

    EXPECT_EQ(
        toSet<ContainerId>({
            u->getContainerId("host0"),
            u->getContainerId("host1"),
            u->getContainerId("host2"),
        }),
        toSet<ContainerId>(u->getContainers().getContainerIds()));

    const auto cpuId = u->getDimensionId("cpu");
    EXPECT_EQ(
        10.0,
        u->getObjects().getDimension(cpuId).at(0).getValue(
            u->getObjectId("task0")));
    EXPECT_EQ(
        20.0,
        u->getObjects().getDimension(cpuId).at(0).getValue(
            u->getObjectId("task1")));
    EXPECT_EQ(
        5.0,
        u->getObjects().getDimension(cpuId).at(0).getValue(
            u->getObjectId("task2")));

    const auto rackId = u->getScopeId("rack");
    EXPECT_EQ(
        toSet({u->getContainerId("host0"), u->getContainerId("host1")}),
        toSet<ContainerId>(u->getScope(rackId).getContainerIds(
            u->getScopeItemId(rackId, "rack0"))));
    EXPECT_EQ(
        toSet({u->getContainerId("host2")}),
        toSet<ContainerId>(u->getScope(rackId).getContainerIds(
            u->getScopeItemId(rackId, "rack1"))));

    const auto jobId = u->getPartitionId("job");
    EXPECT_EQ(
        toSet({u->getObjectId("task0"), u->getObjectId("task1")}),
        toSet<ObjectId>(
            u->getPartition(jobId).getObjectIds(u->getGroupId(jobId, "job0"))));
    EXPECT_EQ(
        toSet({u->getObjectId("task2")}),
        toSet<ObjectId>(
            u->getPartition(jobId).getObjectIds(u->getGroupId(jobId, "job1"))));
  };

  // Build once
  const auto universe1 = builder.build();

  // Verify everything for the first build
  verifyBuild1Contents(universe1);

  EXPECT_EQ(
      toSet({universe1->getObjectId("task0"), universe1->getObjectId("task1")}),
      toSet<ObjectId>(universe1->getContainers().getInitialObjectIds(
          universe1->getContainerId("host0"))));
  EXPECT_EQ(
      toSet({universe1->getObjectId("task2")}),
      toSet<ObjectId>(universe1->getContainers().getInitialObjectIds(
          universe1->getContainerId("host1"))));
  EXPECT_EQ(
      toSet<ObjectId>({}),
      toSet<ObjectId>(universe1->getContainers().getInitialObjectIds(
          universe1->getContainerId("host2"))));

  EXPECT_EQ(
      toSet({universe1->getScopeId("host"), universe1->getScopeId("rack")}),
      toSet<ScopeId>(universe1->getScopeIds()));
  EXPECT_EQ(
      toSet({universe1->getPartitionId("job")}),
      toSet<PartitionId>(universe1->getPartitionIds()));

  // Prepare to build again
  builder.setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task1"}},
          {"host1", {}},
          {"host2", {"task2"}},
      });

  builder.addScope(
      "region",
      std::unordered_map<std::string, std::string>{
          {"host0", "rgn0"}, {"host1", "rgn0"}, {"host2", "rgn1"}});

  builder.addScope(
      "datacenter",
      std::map<std::string, std::vector<std::string>>{
          {"dc0", {"host0", "host1"}}, {"dc1", {"host2"}}});

  builder.addPartition(
      "service",
      std::map<std::string, std::string>{
          {"task0", "svc0"}, {"task1", "svc0"}, {"task2", "svc1"}});

  builder.addPartition(
      "team",
      std::map<std::string, std::vector<std::string>>{
          {"team0", {"task0"}}, {"team1", {"task1", "task2"}}});

  builder.addObjectDimension(
      "memory",
      std::map<std::string, double>{{"task0", 100}, {"task1", 200}},
      50);

  builder.addObjectDimension(
      "network",
      std::map<std::string, std::vector<double>>{
          {"task0", {1.5, 2.5}}, {"task1", {3.5, 4.5}}});

  builder.addContainerDimension(
      "disk_cap",
      std::map<std::string, double>{{"host0", 1000.0}, {"host1", 2000.0}});

  builder.addScopeDimension(
      "rack_cap",
      "rack",
      std::unordered_map<std::string, double>{{"rack0", 10}, {"rack1", 20}});

  builder.addDynamicObjectDimension(
      "power",
      "rack",
      folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>{
          {"rack0", {{"task0", 10}, {"task1", 20}}}, {"rack1", {}}},
      0);

  builder.addDynamicObjectDimension(
      "dynamic_disk",
      "rack",
      "job",
      folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>{
          {"rack0", {{"job0", 100}}}, {"rack1", {{"job1", 200}}}},
      50);

  builder.addSimilarContainers({{"host0"}, {"host1"}});

  builder.addRoutingConfig("routingConfig", "host", "job", {}, {}, {});

  builder.addObjectPartitionRoutingDimension(
      "load",
      "job",
      "routingConfig",
      std::unordered_map<std::string, double>{{"job0", 10}},
      5.0);

  {
    auto spec = BalanceSpec();
    spec.name() = "balance_goal";
    spec.scope() = "host";
    spec.dimension() = "cpu";
    builder.addGoal(spec, 10.0, 0);
  }

  {
    auto spec = CapacitySpec();
    spec.scope() = "host";
    spec.dimension() = "cpu";
    builder.addConstraint(spec, ConstraintPolicy::HARD, 1000.0, 2000.0, 0);
  }

  {
    DestinationsToExploreOptions opts;
    MoveToCurrentScopeItemSpec moveSpec;
    moveSpec.scopeNameForExploringMovesToCurrentScopeItem() = "rack";
    opts.set_moveToCurrentScopeItem(moveSpec);
    builder.addDestinationsToExploreOptions("dteOpts", opts);
  }

  // Build again
  const auto universe2 = builder.build();

  // Verify everything from the first build is still present
  verifyBuild1Contents(universe2);

  // Verify everything for the second build
  EXPECT_EQ(
      toSet({universe2->getObjectId("task0"), universe2->getObjectId("task1")}),
      toSet<ObjectId>(universe2->getContainers().getInitialObjectIds(
          universe2->getContainerId("host0"))));
  EXPECT_EQ(
      toSet<ObjectId>({}),
      toSet<ObjectId>(universe2->getContainers().getInitialObjectIds(
          universe2->getContainerId("host1"))));
  EXPECT_EQ(
      toSet({universe2->getObjectId("task2")}),
      toSet<ObjectId>(universe2->getContainers().getInitialObjectIds(
          universe2->getContainerId("host2"))));

  EXPECT_EQ(
      toSet({
          universe2->getScopeId("host"),
          universe2->getScopeId("rack"),
          universe2->getScopeId("region"),
          universe2->getScopeId("datacenter"),
      }),
      toSet<ScopeId>(universe2->getScopeIds()));

  const auto regionId = universe2->getScopeId("region");
  EXPECT_EQ(
      toSet(
          {universe2->getContainerId("host0"),
           universe2->getContainerId("host1")}),
      toSet<ContainerId>(universe2->getScope(regionId).getContainerIds(
          universe2->getScopeItemId(regionId, "rgn0"))));
  EXPECT_EQ(
      toSet({universe2->getContainerId("host2")}),
      toSet<ContainerId>(universe2->getScope(regionId).getContainerIds(
          universe2->getScopeItemId(regionId, "rgn1"))));

  const auto dcId = universe2->getScopeId("datacenter");
  EXPECT_EQ(
      toSet(
          {universe2->getContainerId("host0"),
           universe2->getContainerId("host1")}),
      toSet<ContainerId>(universe2->getScope(dcId).getContainerIds(
          universe2->getScopeItemId(dcId, "dc0"))));
  EXPECT_EQ(
      toSet({universe2->getContainerId("host2")}),
      toSet<ContainerId>(universe2->getScope(dcId).getContainerIds(
          universe2->getScopeItemId(dcId, "dc1"))));

  EXPECT_EQ(
      toSet({
          universe2->getPartitionId("job"),
          universe2->getPartitionId("service"),
          universe2->getPartitionId("team"),
      }),
      toSet<PartitionId>(universe2->getPartitionIds()));

  const auto svcId = universe2->getPartitionId("service");
  EXPECT_EQ(
      toSet({universe2->getObjectId("task0"), universe2->getObjectId("task1")}),
      toSet<ObjectId>(universe2->getPartition(svcId).getObjectIds(
          universe2->getGroupId(svcId, "svc0"))));
  EXPECT_EQ(
      toSet({universe2->getObjectId("task2")}),
      toSet<ObjectId>(universe2->getPartition(svcId).getObjectIds(
          universe2->getGroupId(svcId, "svc1"))));

  const auto teamPId = universe2->getPartitionId("team");
  EXPECT_EQ(
      toSet({universe2->getObjectId("task0")}),
      toSet<ObjectId>(universe2->getPartition(teamPId).getObjectIds(
          universe2->getGroupId(teamPId, "team0"))));
  EXPECT_EQ(
      toSet({universe2->getObjectId("task1"), universe2->getObjectId("task2")}),
      toSet<ObjectId>(universe2->getPartition(teamPId).getObjectIds(
          universe2->getGroupId(teamPId, "team1"))));

  const auto memId = universe2->getDimensionId("memory");
  EXPECT_EQ(
      100.0,
      universe2->getObjects().getDimension(memId).at(0).getValue(
          universe2->getObjectId("task0")));
  EXPECT_EQ(
      200.0,
      universe2->getObjects().getDimension(memId).at(0).getValue(
          universe2->getObjectId("task1")));
  EXPECT_EQ(
      50.0,
      universe2->getObjects().getDimension(memId).at(0).getValue(
          universe2->getObjectId("task2")));

  const auto networkId = universe2->getDimensionId("network");
  EXPECT_EQ(2, universe2->getObjects().getDimension(networkId).size());
  EXPECT_EQ(
      1.5,
      universe2->getObjects().getDimension(networkId).at(0).getValue(
          universe2->getObjectId("task0")));
  EXPECT_EQ(
      3.5,
      universe2->getObjects().getDimension(networkId).at(0).getValue(
          universe2->getObjectId("task1")));
  EXPECT_EQ(
      0.0,
      universe2->getObjects().getDimension(networkId).at(0).getValue(
          universe2->getObjectId("task2")));
  EXPECT_EQ(
      2.5,
      universe2->getObjects().getDimension(networkId).at(1).getValue(
          universe2->getObjectId("task0")));
  EXPECT_EQ(
      4.5,
      universe2->getObjects().getDimension(networkId).at(1).getValue(
          universe2->getObjectId("task1")));
  EXPECT_EQ(
      0.0,
      universe2->getObjects().getDimension(networkId).at(1).getValue(
          universe2->getObjectId("task2")));

  const auto hostScopeId = universe2->getScopeId("host");
  const auto diskCapId = universe2->getDimensionId("disk_cap");
  EXPECT_EQ(
      1000.0,
      universe2->getScope(hostScopeId)
          .getDimension(diskCapId)
          .getValue(universe2->getScopeItemId(hostScopeId, "host0")));
  EXPECT_EQ(
      2000.0,
      universe2->getScope(hostScopeId)
          .getDimension(diskCapId)
          .getValue(universe2->getScopeItemId(hostScopeId, "host1")));

  const auto rackId = universe2->getScopeId("rack");
  const auto rackCapId = universe2->getDimensionId("rack_cap");
  EXPECT_EQ(
      10.0,
      universe2->getScope(rackId).getDimension(rackCapId).getValue(
          universe2->getScopeItemId(rackId, "rack0")));
  EXPECT_EQ(
      20.0,
      universe2->getScope(rackId).getDimension(rackCapId).getValue(
          universe2->getScopeItemId(rackId, "rack1")));

  const auto powerId = universe2->getDimensionId("power");
  EXPECT_EQ(
      10.0,
      universe2->getObjects().getDimension(powerId).at(0).getValue(
          universe2->getObjectId("task0"),
          universe2->getScopeItemId(rackId, "rack0")));
  EXPECT_EQ(
      20.0,
      universe2->getObjects().getDimension(powerId).at(0).getValue(
          universe2->getObjectId("task1"),
          universe2->getScopeItemId(rackId, "rack0")));
  EXPECT_EQ(
      0.0,
      universe2->getObjects().getDimension(powerId).at(0).getValue(
          universe2->getObjectId("task2"),
          universe2->getScopeItemId(rackId, "rack0")));

  const auto dynDiskId = universe2->getDimensionId("dynamic_disk");
  EXPECT_EQ(
      100.0,
      universe2->getObjects().getDimension(dynDiskId).at(0).getValue(
          universe2->getObjectId("task0"),
          universe2->getScopeItemId(rackId, "rack0")));
  EXPECT_EQ(
      50.0,
      universe2->getObjects().getDimension(dynDiskId).at(0).getValue(
          universe2->getObjectId("task2"),
          universe2->getScopeItemId(rackId, "rack0")));
  EXPECT_EQ(
      200.0,
      universe2->getObjects().getDimension(dynDiskId).at(0).getValue(
          universe2->getObjectId("task2"),
          universe2->getScopeItemId(rackId, "rack1")));

  const std::vector<std::vector<ContainerId>> expectedSimilar = {
      {universe2->getContainerId("host0")},
      {universe2->getContainerId("host1")}};
  EXPECT_EQ(expectedSimilar, universe2->getSimilarContainers());

  const auto jobPId = universe2->getPartitionId("job");
  const auto loadId = universe2->getDimensionId("load");
  const auto& routingDim = dynamic_cast<const ObjectPartitionRoutingDimension&>(
      universe2->getObjects().getDimension(loadId).only());
  EXPECT_NEAR(
      10.0, routingDim.getValue(universe2->getGroupId(jobPId, "job0")), 1e-8);
  EXPECT_NEAR(
      5.0, routingDim.getValue(universe2->getGroupId(jobPId, "job1")), 1e-8);

  const auto goalId = universe2->getGoalId("balance_goal");
  EXPECT_EQ(
      GoalSpecs::Type::balanceSpec,
      universe2->getGoals().getGoal(goalId).getSpec().getType());
  EXPECT_EQ(10.0, universe2->getGoals().getGoal(goalId).getWeight());
  EXPECT_EQ(0, universe2->getGoals().getGoal(goalId).getTupleIndex());

  const auto cId = universe2->getConstraintId("constraint_0");
  EXPECT_EQ(
      ConstraintSpecs::Type::capacitySpec,
      universe2->getConstraints().getConstraint(cId).getSpec().getType());
  EXPECT_EQ(
      ConstraintPolicy::HARD,
      universe2->getConstraints().getConstraint(cId).getPolicy());
  EXPECT_EQ(
      1000.0, universe2->getConstraints().getConstraint(cId).getInvalidCost());
  EXPECT_EQ(
      2000.0, universe2->getConstraints().getConstraint(cId).getInvalidState());

  const auto dteOpts = universe2->getDestinationsToExploreOptions("dteOpts");
  EXPECT_EQ(
      "rack",
      dteOpts.moveToCurrentScopeItem()
          .value()
          .scopeNameForExploringMovesToCurrentScopeItem());
}
// TODO: add more exhaustive tests

} // namespace facebook::rebalancer::interface::tests
