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

#include "algopt/rebalancer/solver/expressions/LinearSum.h"
#include "algopt/rebalancer/solver/expressions/Max.h"
#include "algopt/rebalancer/solver/expressions/ObjectLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartition.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectVector.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/Orchestrator.h"
#include "algopt/rebalancer/solver/expressions/Power.h"
#include "algopt/rebalancer/solver/expressions/SumOverThreshold.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/expressions/Variable.h"
#include "algopt/rebalancer/solver/iterators/StlWrapper.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/Change.h"

#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <vector>

namespace facebook::rebalancer::packer::tests {

namespace {
double
partialApply(Expression& expr, Context& context, const Assignment& assignment) {
  Orchestrator orchestrator;
  orchestrator.init(
      std::vector<Expression*>{&expr},
      AffectedByChangeDecisionData(
          assignment.getObjects().size(), assignment.getContainers().size()));
  orchestrator.apply(context, assignment);
  return *context.apply().get(expr.getId());
}
} // namespace

class ObjectPotentialsTest : public ExpressionTestsBase {};

class MockExpression : public Expression {
 public:
  // Process-lifetime dummy Universe; outlives the mock's non-owning pointer.
  static const entities::Universe& dummyUniverse() {
    static const auto universe = std::make_shared<const entities::Universe>();
    return *universe;
  }

  explicit MockExpression(
      std::vector<ObjectPotential> objectPotentials,
      double value = 0)
      : Expression(dummyUniverse(), value),
        objectPotentials_(std::move(objectPotentials)) {}
  double innerFullApply(
      const TopToBottomEvaluator& /* evaluator */,
      const Assignment& /* assignment */) override {
    return value;
  }

  static bool inner_is_positive(
      Context& /* context */,
      const ChangeSet& /* changes */) {
    throw std::runtime_error("not implemented");
  }

  bool shouldComputeDescendingChildPotentials() const override {
    return false;
  }

  Bounds innerLowerAndUpperBounds(
      Context& /* context */,
      const BoundConstraints& /* bc */) const override {
    return {.lower_bound = value, .upper_bound = value};
  }

  const std::string_view& getType() const override {
    throw std::runtime_error("no type defined for MockExpression");
  }

  AbstractContainer<ObjectPotential> getObjectPotentials(
      bool descending) const override {
    auto objectPotentials = objectPotentials_;
    std::sort(objectPotentials.begin(), objectPotentials.end());
    if (descending) {
      std::reverse(objectPotentials.begin(), objectPotentials.end());
    }
    return makeStlWrapperContainer(std::move(objectPotentials));
  }

 private:
  std::vector<ObjectPotential> objectPotentials_;
};

TEST_F(ObjectPotentialsTest, MockExpression) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1", {"object1", "object2", "object3"}}});
  const auto universe = buildUniverse();
  auto mock = MockExpression(
      {{.objectId = object(1), .potential = 2000},
       {.objectId = object(2), .potential = 1000},
       {.objectId = object(3), .potential = 3000}});
  auto ascending = mock.getObjectPotentials(false);
  EXPECT_EQ(
      std::vector<ObjectPotential>(ascending.begin(), ascending.end()),
      std::vector<ObjectPotential>(
          {{object(2), 1000}, {object(1), 2000}, {object(3), 3000}}));
  auto descending = mock.getObjectPotentials(true);
  EXPECT_EQ(
      std::vector<ObjectPotential>(descending.begin(), descending.end()),
      std::vector<ObjectPotential>(
          {{object(3), 3000}, {object(1), 2000}, {object(2), 1000}}));
}

TEST_F(ObjectPotentialsTest, VariableInactive) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container100", {}}, {"container101", {"object10"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment initialAssignment(
      universe.getContainers().getInitialAssignment());
  Variable variable(object(10), container(100), universe, initialAssignment);
  const Assignment assignment({{container(101), {object(10)}}});
  Context context;
  EXPECT_EQ(0, variable.fullApply(TopToBottomEvaluator(context), assignment));
  auto potentials = variable.getObjectPotentials(true);
  EXPECT_EQ(
      std::vector<ObjectPotential>(),
      std::vector<ObjectPotential>(potentials.begin(), potentials.end()));
}

TEST_F(ObjectPotentialsTest, VariableActive) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container100", {"object10"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment initialAssignment(
      universe.getContainers().getInitialAssignment());
  Variable variable(object(10), container(100), universe, initialAssignment);
  const Assignment assignment({{container(100), {object(10)}}});
  Context context;
  EXPECT_EQ(1, variable.fullApply(TopToBottomEvaluator(context), assignment));
  auto potentials = variable.getObjectPotentials(true);
  EXPECT_EQ(
      std::vector<ObjectPotential>({{object(10), 1}}),
      std::vector<ObjectPotential>(potentials.begin(), potentials.end()));
}

TEST_F(ObjectPotentialsTest, ObjectLookup) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container100", {"object10"}},
          {"container101", {"object11"}},
          {"container102", {"object12"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment initialAssignment(
      universe.getContainers().getInitialAssignment());
  entities::ObjectIdToDoubleMap objectValues(
      /*totalSize=*/13, /*defaultValue=*/0.0, /*expectedNonDefaultSize=*/3);
  objectValues.emplace(object(10), 1000);
  objectValues.emplace(object(11), 2000);
  objectValues.emplace(object(12), 4000);
  auto objectVector = makeObjectVector(objectValues, universe);
  ObjectLookup objectLookup(
      objectVector,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(100), container(101)}),
      universe,
      initialAssignment);

  {
    const Assignment assignment(
        {{container(100), {object(10)}},
         {container(101), {object(11)}},
         {container(102), {object(12)}}});
    Context context;
    EXPECT_EQ(
        3000,
        objectLookup.fullApply(TopToBottomEvaluator(context), assignment));
    auto potentialsAscending = objectLookup.getObjectPotentials(false);
    EXPECT_EQ(
        std::vector<ObjectPotential>({{object(10), 1000}, {object(11), 2000}}),
        std::vector<ObjectPotential>(
            potentialsAscending.begin(), potentialsAscending.end()));
    auto potentialsDescending = objectLookup.getObjectPotentials(true);
    EXPECT_EQ(
        std::vector<ObjectPotential>({{object(11), 2000}, {object(10), 1000}}),
        std::vector<ObjectPotential>(
            potentialsDescending.begin(), potentialsDescending.end()));
  }

  {
    const Assignment assignment(
        {{container(100), {object(12)}},
         {container(101), {object(11)}},
         {container(102), {object(10)}}});
    ChangeSet changes;
    changes.insert(Change(object(10), container(100), -1));
    changes.insert(Change(object(10), container(102), 1));
    changes.insert(Change(object(12), container(102), -1));
    changes.insert(Change(object(12), container(100), 1));
    Context context;
    context.changes() = changes;
    EXPECT_EQ(6000, partialApply(objectLookup, context, assignment));
    auto potentialsAscending = objectLookup.getObjectPotentials(false);
    EXPECT_EQ(
        std::vector<ObjectPotential>({{object(11), 2000}, {object(12), 4000}}),
        std::vector<ObjectPotential>(
            potentialsAscending.begin(), potentialsAscending.end()));
    auto potentialsDescending = objectLookup.getObjectPotentials(true);
    EXPECT_EQ(
        std::vector<ObjectPotential>({{object(12), 4000}, {object(11), 2000}}),
        std::vector<ObjectPotential>(
            potentialsDescending.begin(), potentialsDescending.end()));
  }
}

TEST_F(ObjectPotentialsTest, LinearSum) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1",
           {"object1",
            "object2",
            "object3",
            "object4",
            "object5",
            "object6",
            "object7"}}});
  buildUniverse();
  auto child1 = std::make_shared<MockExpression>(std::vector<ObjectPotential>(
      {{.objectId = object(1), .potential = 1000},
       {.objectId = object(2), .potential = 2500}}));
  auto child2 = std::make_shared<MockExpression>(std::vector<ObjectPotential>(
      {{.objectId = object(3), .potential = 600},
       {.objectId = object(4), .potential = 2000}}));
  auto child3 = std::make_shared<MockExpression>(std::vector<ObjectPotential>(
      {{.objectId = object(5), .potential = 5000}}));
  auto child4 = std::make_shared<MockExpression>(std::vector<ObjectPotential>(
      {{.objectId = object(6), .potential = -500},
       {.objectId = object(7), .potential = 500}}));
  const LinearSum sum(
      getUniverse(), 42, {{child1, 1}, {child2, 2}, {child3, 0}, {child4, -3}});
  auto potentials = sum.getObjectPotentials(true);
  const std::vector<ObjectPotential> expected = {
      {.objectId = object(4), .potential = 4000},
      {.objectId = object(2), .potential = 2500},
      {.objectId = object(6), .potential = 1500},
      {.objectId = object(3), .potential = 1200},
      {.objectId = object(1), .potential = 1000},
      {.objectId = object(7), .potential = -1500}};
  EXPECT_EQ(
      expected,
      std::vector<ObjectPotential>(potentials.begin(), potentials.end()));
}

TEST_F(ObjectPotentialsTest, SumOverThreshold) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1",
           {"object1", "object2", "object3", "object4", "object5"}}});
  buildUniverse();
  auto threshold =
      std::make_shared<MockExpression>(std::vector<ObjectPotential>(), 50);
  auto child1 = std::make_shared<MockExpression>(
      std::vector<ObjectPotential>(
          {{.objectId = object(1), .potential = 10},
           {.objectId = object(2), .potential = 20},
           {.objectId = object(3), .potential = 30}}),
      75);
  auto child2 = std::make_shared<MockExpression>(
      std::vector<ObjectPotential>(
          {{.objectId = object(4), .potential = 10},
           {.objectId = object(5), .potential = 20}}),
      45);
  SumOverThreshold sot(threshold, {child1, child2}, false);
  Context context;
  const Assignment assignment;
  sot.fullApply(TopToBottomEvaluator(context), assignment);
  auto potentials = sot.getObjectPotentials(true);
  const std::vector<ObjectPotential> expected = {
      {.objectId = object(3), .potential = 25},
      {.objectId = object(2), .potential = 20},
      {.objectId = object(1), .potential = 10}};
  EXPECT_EQ(
      expected,
      std::vector<ObjectPotential>(potentials.begin(), potentials.end()));
}

TEST_F(ObjectPotentialsTest, SumOverThresholdWithSquares) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1", {"object1", "object2", "object3", "object4"}}});
  buildUniverse();
  auto threshold =
      std::make_shared<MockExpression>(std::vector<ObjectPotential>(), 10);
  auto child1 = std::make_shared<MockExpression>(
      std::vector<ObjectPotential>(
          {{.objectId = object(1), .potential = 1},
           {.objectId = object(2), .potential = 2}}),
      15);
  auto child2 = std::make_shared<MockExpression>(
      std::vector<ObjectPotential>(
          {{.objectId = object(3), .potential = 1},
           {.objectId = object(4), .potential = 2}}),
      14);
  SumOverThreshold sot(threshold, {child1, child2}, true);
  Context context;
  const Assignment assignment;
  sot.fullApply(TopToBottomEvaluator(context), assignment);
  auto potentials = sot.getObjectPotentials(true);
  const std::vector<ObjectPotential> expected = {
      {.objectId = object(2), .potential = 16},
      {.objectId = object(4), .potential = 12},
      {.objectId = object(1), .potential = 9},
      {.objectId = object(3), .potential = 7}};
  EXPECT_EQ(
      expected,
      std::vector<ObjectPotential>(potentials.begin(), potentials.end()));
}

TEST_F(ObjectPotentialsTest, TransformPower) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1", {"object1", "object2"}}});
  buildUniverse();
  auto child = std::make_shared<MockExpression>(
      std::vector<ObjectPotential>(
          {{.objectId = object(1), .potential = 1},
           {.objectId = object(2), .potential = 2}}),
      10);
  Power power(child, 2);
  Context context;
  const Assignment assignment;
  power.fullApply(TopToBottomEvaluator(context), assignment);
  auto potentials = power.getObjectPotentials(true);
  const std::vector<ObjectPotential> expected = {
      {.objectId = object(2), .potential = 36},
      {.objectId = object(1), .potential = 19}};
  EXPECT_EQ(
      expected,
      std::vector<ObjectPotential>(potentials.begin(), potentials.end()));
}

TEST_F(ObjectPotentialsTest, Max) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1", {"object1", "object2", "object3", "object4"}}});
  buildUniverse();
  auto children = std::vector<std::shared_ptr<Expression>>(
      {std::make_shared<MockExpression>(
           std::vector<ObjectPotential>(
               {{.objectId = object(1), .potential = 1},
                {.objectId = object(2), .potential = 2}}),
           10),
       std::make_shared<MockExpression>(
           std::vector<ObjectPotential>(
               {{.objectId = object(2), .potential = 3},
                {.objectId = object(3), .potential = 1}}),
           10),
       std::make_shared<MockExpression>(
           std::vector<ObjectPotential>(
               {{.objectId = object(2), .potential = 4},
                {.objectId = object(4), .potential = 8}}),
           9)});
  Max max(children, getUniverse());
  Context context;
  const Assignment assignment;
  max.fullApply(TopToBottomEvaluator(context), assignment);
  auto potentials = max.getObjectPotentials(true);
  auto result =
      std::vector<ObjectPotential>(potentials.begin(), potentials.end());
  ASSERT_EQ(3, result.size());
  // First element (highest potential) is deterministic
  EXPECT_EQ(object(2), result[0].objectId);
  EXPECT_EQ(3.0, result[0].potential);
  // Objects 1 and 3 both have potential 1.0; order may vary by platform
  EXPECT_EQ(1.0, result[1].potential);
  EXPECT_EQ(1.0, result[2].potential);
  std::set<entities::ObjectId> tiedObjects{
      result[1].objectId, result[2].objectId};
  EXPECT_EQ((std::set<entities::ObjectId>{object(1), object(3)}), tiedObjects);
}

CO_TEST_F(ObjectPotentialsTest, ObjectPartitionLookup) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {}},
          {"container1",
           {"object1", "object2", "object3", "object4", "object5", "object6"}},
          {"container2", {}}});

  co_await addScope("scope1", {{"scopeItem0", {"container0"}}});

  co_await addPartition(
      "partition1",
      {{"group1", {"object1", "object2", "object3"}},
       {"group2", {"object4", "object5", "object6"}}});

  buildUniverse();
  const auto& universe = getUniverse();
  const auto scope1Id = scopeId("scope1");
  const auto scopeItem0Id = scopeItemId(scope1Id, "scopeItem0");
  const auto partition1Id = partitionId("partition1");
  const auto objectCountDimId = dimensionId("object_count");
  const auto group1 = universe.getGroupId(partition1Id, "group1");
  const auto group2 = universe.getGroupId(partition1Id, "group2");

  auto assignment = Assignment(universe.getContainers().getInitialAssignment());

  auto partition = std::make_shared<ObjectPartition>(
      std::make_shared<const PartitionInfo>(universe, partition1Id),
      objectCountDimId,
      PackerMap<entities::GroupId, double>({{group1, 2}, {group2, 3}}));

  ObjectPartitionLookup lookup(
      partition,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(1)}),
      scope1Id,
      scopeItem0Id,
      universe,
      assignment);
  Context context;
  assignment = Assignment(
      {{container(1), {object(1), object(2), object(3), object(4), object(5)}},
       {container(2), {object(6)}}});
  EXPECT_EQ(1, lookup.fullApply(TopToBottomEvaluator(context), assignment));
  auto potentials = lookup.getObjectPotentials(true);
  auto result =
      std::vector<ObjectPotential>(potentials.begin(), potentials.end());
  // All 3 objects have potential 1.0; order may vary by platform
  EXPECT_EQ(3, result.size());
  std::set<entities::ObjectId> objectIds;
  for (const auto& p : result) {
    EXPECT_EQ(1.0, p.potential);
    objectIds.insert(p.objectId);
  }
  EXPECT_EQ(
      (std::set<entities::ObjectId>{object(1), object(2), object(3)}),
      objectIds);
}
} // namespace facebook::rebalancer::packer::tests
