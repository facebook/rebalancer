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

#include "algopt/rebalancer/algopt_common/Utils.h"
#include "algopt/rebalancer/interface/standalone/BackwardCompatabilityUtils.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSolver_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/interface/thrift/ThriftUtils.h"

#include <folly/Portability.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::interface::tests {

class BackwardCompatabilityUtilsTest : public ::testing::Test {
 protected:
  template <typename T>
  void addGoal(T spec, int goalId) {
    auto goalSpec =
        algopt::utils::createThriftUnionByField<GoalSpecs, T>(std::move(spec));

    entities::thrift::Goal goalThrift;
    goalThrift.spec() = std::move(goalSpec);
    goalThrift.weight() = 1.0;
    goalThrift.tupleIndex() = 1;

    entities::thrift::Goals goals;
    (*goals.goals())[goalId] = std::move(goalThrift);

    universeThrift.goals() = std::move(goals);
  }

  template <typename T>
  void addConstraint(T spec, int constraintId) {
    auto constraint =
        algopt::utils::createThriftUnionByField<ConstraintSpecs, T>(
            std::move(spec));

    entities::thrift::Constraint constraintThrift;
    constraintThrift.spec() = std::move(constraint);

    entities::thrift::Constraints constraints;
    (*constraints.constraints())[constraintId] = std::move(constraintThrift);

    universeThrift.constraints() = std::move(constraints);
  }

  const interface::GoalSpecs& getGoalSpec(int id) {
    return *universeThrift.goals()->goals()->at(id).spec();
  }

  size_t getGoalCount() {
    return universeThrift.goals()->goals()->size();
  }

  size_t getConstraintCount() {
    return universeThrift.constraints()->constraints()->size();
  }

  void possiblyModify() {
    AssignmentProblem problem;
    problem.universe() = std::move(universeThrift);
    BackwardCompatabilityUtils::possiblyModify(problem);
    universeThrift = std::move(*problem.universe());
  }

  entities::thrift::Universe universeThrift;
};

class RoutingLatencySpecTest : public BackwardCompatabilityUtilsTest {
 protected:
  static interface::RoutingLatencySpec makeSpec(
      interface::RoutingLatencyMetric metric) {
    interface::RoutingLatencySpec spec;
    spec.scope() = "host";
    spec.metric() = metric;

    return spec;
  }
};

TEST_F(RoutingLatencySpecTest, RoutingLatencySpecMax) {
  // add old configuration for RoutingLatencySpec

  auto spec = makeSpec(interface::RoutingLatencyMetric::MAX);
  addGoal(spec, /*goalId=*/0);
  addConstraint(spec, /*constraintId=*/0);

  // modify universeThrift
  possiblyModify();

  // check that the new configuration is correct
  EXPECT_EQ(1, getGoalCount());
  EXPECT_EQ(1, getConstraintCount());

  auto& changedRoutingLatencyGoalSpec = getGoalSpec(0).get_routingLatencySpec();

  EXPECT_EQ(
      thriftUtils::makeRoutingLatencyMetric(
          interface::RoutingLatencyMetric::PERCENTILE, 100.0),
      *changedRoutingLatencyGoalSpec.latencyMetric());
}

TEST_F(RoutingLatencySpecTest, RoutingLatencySpecP99) {
  // add old configuration for RoutingLatencySpec
  auto spec = makeSpec(interface::RoutingLatencyMetric::P99);
  addGoal(spec, /*goalId=*/0);
  addConstraint(spec, /*constraintId=*/0);

  // modify universeThrift
  possiblyModify();

  // check that the new configuration is correct
  EXPECT_EQ(1, getGoalCount());
  EXPECT_EQ(1, getConstraintCount());

  auto& changedRoutingLatencyGoalSpec = getGoalSpec(0).get_routingLatencySpec();

  EXPECT_EQ(
      thriftUtils::makeRoutingLatencyMetric(
          interface::RoutingLatencyMetric::PERCENTILE, 99.0),
      *changedRoutingLatencyGoalSpec.latencyMetric());
}

TEST_F(BackwardCompatabilityUtilsTest, ExclusiveScopeItemsSpec) {
  // add old configuration for ExclusiveScopeItemsSpec
  interface::ExclusiveScopeItemsSpec spec;
  spec.name() = "test";
  spec.scope() = "host";
  spec.dimension() = "task_count";

  std::vector<std::pair<std::string, std::string>> conflicts = {
      {"host1", "host2"}, {"host2", "host3"}};
  std::vector<interface::ScopeItemPair> pairs;
  for (auto& [scopeItem1, scopeItem2] : conflicts) {
    interface::ScopeItemPair pair;
    pair.scopeItem1() = scopeItem1;
    pair.scopeItem2() = scopeItem2;
    pairs.push_back(pair);
  }
  spec.pairs() = pairs;

  addGoal(spec, /*goalId=*/0);
  addConstraint(spec, /*constraintId=*/0);

  // modify universeThrift
  possiblyModify();

  // check that the new configuration is correct
  EXPECT_EQ(1, getGoalCount());
  EXPECT_EQ(1, getConstraintCount());

  auto& changedExclusiveScopeItemsGoalSpec =
      getGoalSpec(0).get_exclusiveScopeItemsSpec();

  EXPECT_EQ(2, changedExclusiveScopeItemsGoalSpec.conflictInfoList()->size());
}

TEST_F(BackwardCompatabilityUtilsTest, MinimizeContainersSpecMaxFreeLimit) {
  // Old configuration: the deprecated maxFreeLimit field is set directly.
  interface::MinimizeContainersSpec spec;
  spec.name() = "test";
  spec.scope() = "host";
  spec.dimension() = "task_count";
  FOLLY_PUSH_WARNING
  FOLLY_GNU_DISABLE_WARNING("-Wdeprecated-declarations")
  // NOLINTNEXTLINE(facebook-hte-Deprecated)
  spec.maxFreeLimit() = 5;
  FOLLY_POP_WARNING

  addGoal(spec, /*goalId=*/0);

  possiblyModify();

  ASSERT_EQ(1, getGoalCount());
  const auto& migratedSpec = getGoalSpec(0).get_minimizeContainersSpec();

  // maxFreeLimit is migrated into the target union...
  ASSERT_TRUE(migratedSpec.target().has_value());
  EXPECT_EQ(
      interface::MinimizeContainersTarget::Type::maxFreeLimit,
      migratedSpec.target()->getType());
  EXPECT_EQ(5, migratedSpec.target()->get_maxFreeLimit());

  // ...and the deprecated field is cleared so it cannot be read again.
  FOLLY_PUSH_WARNING
  FOLLY_GNU_DISABLE_WARNING("-Wdeprecated-declarations")
  // NOLINTNEXTLINE(facebook-hte-Deprecated)
  EXPECT_FALSE(migratedSpec.maxFreeLimit().has_value());
  FOLLY_POP_WARNING
}

// ---------------------------------------------------------------------------
// densifyEntityIds tests
// ---------------------------------------------------------------------------

class DensifyEntityIdsTest : public BackwardCompatabilityUtilsTest {
 protected:
  entities::thrift::IdStore& idStore() {
    return *universeThrift.idStore();
  }

  // Sets the deprecated 'names' field, which marks the bundle as old-format.
  void setOldNames(std::vector<std::string> names) {
    FOLLY_PUSH_WARNING
    FOLLY_GNU_DISABLE_WARNING("-Wdeprecated-declarations")
    // NOLINTNEXTLINE(facebook-hte-Deprecated)
    idStore().names() = std::move(names);
    FOLLY_POP_WARNING
  }

  bool oldNamesCleared() {
    FOLLY_PUSH_WARNING
    FOLLY_GNU_DISABLE_WARNING("-Wdeprecated-declarations")
    // NOLINTNEXTLINE(facebook-hte-Deprecated)
    return idStore().names()->empty();
    FOLLY_POP_WARNING
  }
};

TEST_F(DensifyEntityIdsTest, NoOpWhenNamesIsEmpty) {
  // Bundle is already in the new format: names is empty, per-type fields
  // populated.
  idStore().objectIds() = {0};
  idStore().objectNames() = {"obj0"};

  possiblyModify();

  EXPECT_EQ(std::vector<int32_t>({0}), *idStore().objectIds());
  EXPECT_EQ(std::vector<std::string>({"obj0"}), *idStore().objectNames());
}

TEST_F(DensifyEntityIdsTest, FlatIdListsBecomeDenseAndKeepInsertionOrder) {
  setOldNames({"placeholder0", "placeholder1", "obj_a", "obj_b", "ctr_x"});
  // Sparse old global ids: object 2 = "obj_a", object 3 = "obj_b", container 4
  // = "ctr_x".
  idStore().objectIds() = {2, 3};
  idStore().containerIds() = {4};

  possiblyModify();

  EXPECT_TRUE(oldNamesCleared());
  // Flat lists preserve insertion order, so old [2, 3] -> new [0, 1].
  EXPECT_EQ(std::vector<int32_t>({0, 1}), *idStore().objectIds());
  EXPECT_EQ(std::vector<int32_t>({0}), *idStore().containerIds());
  EXPECT_EQ(
      std::vector<std::string>({"obj_a", "obj_b"}), *idStore().objectNames());
  EXPECT_EQ(std::vector<std::string>({"ctr_x"}), *idStore().containerNames());
}

TEST_F(DensifyEntityIdsTest, ContainerReferencesGetRemappedConsistently) {
  setOldNames({"obj0", "ctr_a", "ctr_b"});
  idStore().objectIds() = {0};
  // Old container ids 2 then 1 (out-of-order — exercises remapping).
  // The new ids follow this order: container 2 -> 0, container 1 -> 1.
  idStore().containerIds() = {2, 1};

  // Old: container 2 holds object 0; container 1 is empty.
  (*universeThrift.containers()->initialAssignment())[2] = {0};
  (*universeThrift.containers()->initialAssignment())[1] = {};

  possiblyModify();

  // Names follow the canonical (containerIds) order: index 0 is old id 2, etc.
  EXPECT_EQ(
      std::vector<std::string>({"ctr_b", "ctr_a"}),
      *idStore().containerNames());
  // Assignment keys are the new ids.
  const auto& assignment = *universeThrift.containers()->initialAssignment();
  ASSERT_EQ(2, assignment.size());
  EXPECT_EQ(std::vector<int32_t>({0}), assignment.at(0));
  EXPECT_EQ(std::vector<int32_t>({}), assignment.at(1));
}

TEST_F(DensifyEntityIdsTest, NestedMapEntityTypesAreDensified) {
  // scopeItemIds: keys define scope ids, values define scope item ids.
  setOldNames({"scope_a", "item_x", "item_y", "scope_b", "item_z"});
  (*idStore().scopeItemIds())[0] = {1, 2}; // scope 0 has items 1, 2
  (*idStore().scopeItemIds())[3] = {4}; // scope 3 has item 4

  possiblyModify();

  EXPECT_TRUE(oldNamesCleared());
  // 2 scopes, 3 scope items.
  EXPECT_EQ(2, idStore().scopeNames()->size());
  EXPECT_EQ(3, idStore().scopeItemNames()->size());

  // Names are present (order is unspecified since F14 iteration is
  // unspecified).
  const std::set<std::string> scopeNames(
      idStore().scopeNames()->begin(), idStore().scopeNames()->end());
  EXPECT_EQ(std::set<std::string>({"scope_a", "scope_b"}), scopeNames);

  const std::set<std::string> itemNames(
      idStore().scopeItemNames()->begin(), idStore().scopeItemNames()->end());
  EXPECT_EQ(std::set<std::string>({"item_x", "item_y", "item_z"}), itemNames);

  // Densified scope/scopeItem ids are in [0, N).
  std::set<int> scopeKeys;
  std::set<int> itemValues;
  for (const auto& [scopeId, items] : *idStore().scopeItemIds()) {
    scopeKeys.insert(scopeId);
    for (const auto itemId : items) {
      itemValues.insert(itemId);
    }
  }
  EXPECT_EQ(std::set<int>({0, 1}), scopeKeys);
  EXPECT_EQ(std::set<int>({0, 1, 2}), itemValues);
}

TEST_F(DensifyEntityIdsTest, DynamicDimensionValuesPopulateScopedValues) {
  setOldNames({"unused0", "unused1", "obj_a", "obj_b", "scope", "item", "dim"});
  idStore().objectIds() = {2, 3};
  (*idStore().scopeItemIds())[4] = {5};
  idStore().dimensionIds() = {6};

  entities::thrift::ObjectDynamicDimension dyn;
  dyn.scopeId() = 4;
  dyn.defaultValue() = 20;
  dyn.values() = {{5, {{2, 10}, {3, 20}}}};

  entities::thrift::ObjectScalarDimension scalar;
  scalar.objectDynamicDimension() = std::move(dyn);

  entities::thrift::ObjectDimension dimension;
  dimension.scalarDimensions() = {std::move(scalar)};
  dimension.isDynamic() = true;
  (*universeThrift.objects()->dimensions())[6] = std::move(dimension);

  possiblyModify();

  const auto& dimensionThrift = universeThrift.objects()->dimensions()->at(0);
  const auto& remappedDyn =
      dimensionThrift.scalarDimensions()->at(0).get_objectDynamicDimension();
  EXPECT_EQ(0, *remappedDyn.scopeId());
  EXPECT_TRUE(remappedDyn.values()->empty());
  const auto& scopedValues = *remappedDyn.scopedValues();
  ASSERT_TRUE(scopedValues.contains(0));
  const auto& objectValues = scopedValues.at(0);
  ASSERT_EQ(
      entities::thrift::ObjectValues::Type::objectValues,
      objectValues.getType());
  EXPECT_EQ(10, objectValues.objectValues()->at(0));
  EXPECT_EQ(20, objectValues.objectValues()->at(1));
}

} // namespace facebook::rebalancer::interface::tests
