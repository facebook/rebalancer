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

#include "algopt/rebalancer/solver/expressions/AnyPositive.h"
#include "algopt/rebalancer/solver/expressions/BipartiteSwaps.h"
#include "algopt/rebalancer/solver/expressions/Ceil.h"
#include "algopt/rebalancer/solver/expressions/Log.h"
#include "algopt/rebalancer/solver/expressions/NthLargest.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionMoveLimit.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/ProductOperation.h"
#include "algopt/rebalancer/solver/expressions/QuotientOperation.h"
#include "algopt/rebalancer/solver/expressions/Square.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"

#include <fmt/format.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace facebook::rebalancer::packer::tests {

class PropertiesTest : public ExpressionTestsBase {
 protected:
  void SetUp() override {
    // Set up default universe with 20 containers/objects
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
class PropertiesTestCustom : public ExpressionTestsBase {};

TEST_F(PropertiesTest, AnyPositive) {
  buildUniverse();
  const auto& universe = getUniverse();
  const AnyPositive anyPositive({}, universe, 1e-3);
  ASSERT_EQ("AnyPositive", anyPositive.getType());

  auto properties = anyPositive.getProperties();
  ASSERT_EQ(1, properties.properties()->size());
  ASSERT_EQ(
      1e-3,
      *properties.properties()
           ->at("feasibility_tolerance")
           .valueDouble()
           ->value());
}

TEST_F(PropertiesTest, Product) {
  buildUniverse();
  const auto& universe = getUniverse();
  const ProductOperation product(
      const_expr(1, universe), const_expr(1, universe));
  ASSERT_EQ("Product", product.getType());

  auto properties = product.getProperties();
  ASSERT_EQ(1, properties.properties()->size());
  ASSERT_EQ(
      "PRODUCT", *properties.properties()->at("type").valueString()->value());
}

TEST_F(PropertiesTest, Quotient) {
  buildUniverse();
  const auto& universe = getUniverse();
  const QuotientOperation quotient(
      const_expr(1, universe), const_expr(1, universe));
  ASSERT_EQ("Quotient", quotient.getType());

  auto properties = quotient.getProperties();
  ASSERT_EQ(1, properties.properties()->size());
  ASSERT_EQ(
      "QUOTIENT", *properties.properties()->at("type").valueString()->value());
}

TEST_F(PropertiesTest, BipartiteSwaps) {
  buildUniverse();
  const auto& universe = getUniverse();
  const BipartiteSwaps swaps(
      {}, {container(1), container(2)}, {container(3)}, universe);
  ASSERT_EQ("BipartiteSwaps", swaps.getType());

  auto properties = swaps.getProperties();
  ASSERT_EQ(2, properties.properties()->size());
  // Container list order may vary by platform due to hash map iteration
  auto leftSubset = *properties.properties()
                         ->at("left_subset")
                         .valueContainerIdList()
                         ->value();
  std::sort(leftSubset.begin(), leftSubset.end());
  auto expectedLeft =
      std::vector<int>({container(1).asInt(), container(2).asInt()});
  std::sort(expectedLeft.begin(), expectedLeft.end());
  ASSERT_EQ(expectedLeft, leftSubset);
  ASSERT_EQ(
      std::vector<int>({container(3).asInt()}),
      *properties.properties()
           ->at("right_subset")
           .valueContainerIdList()
           ->value());
  EXPECT_TRUE(swaps.references(container(1)));
  EXPECT_FALSE(swaps.references(object(1)));
}

TEST_F(PropertiesTest, LinearSum) {
  buildUniverse();
  const auto& universe = getUniverse();
  auto sum = const_expr(42, universe);
  ASSERT_EQ("LinearSum", sum->getType());

  auto properties = sum->getProperties();
  ASSERT_EQ(1, properties.properties()->size());
  ASSERT_EQ(
      42, *properties.properties()->at("constant").valueDouble()->value());
}

TEST_F(PropertiesTest, NthLargest) {
  buildUniverse();
  const auto& universe = getUniverse();
  const NthLargest nth({const_expr(1, universe)}, 4, false, universe);
  ASSERT_EQ("NthLargest", nth.getType());

  auto properties = nth.getProperties();
  ASSERT_EQ(2, properties.properties()->size());
  ASSERT_EQ(4, *properties.properties()->at("n").valueInt()->value());
  ASSERT_EQ(false, *properties.properties()->at("unique").valueBool()->value());
}

TEST_F(PropertiesTest, ObjectLookup) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  auto lookup = object_lookup(
      makeObjectVector(PackerMap<entities::ObjectId, double>(), universe),
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(3), container(4)}),
      assignment);
  ASSERT_EQ("ObjectLookup", lookup->getType());

  auto properties = lookup->getProperties();
  ASSERT_EQ(1, properties.properties()->size());
  // Container list order may vary by platform due to hash map iteration
  auto containers = *properties.properties()
                         ->at("containers")
                         .valueContainerIdList()
                         ->value();
  std::sort(containers.begin(), containers.end());
  auto expectedContainers =
      std::vector<int>({container(3).asInt(), container(4).asInt()});
  std::sort(expectedContainers.begin(), expectedContainers.end());
  ASSERT_EQ(expectedContainers, containers);
  EXPECT_TRUE(lookup->references(container(3)));
  EXPECT_FALSE(lookup->references(object(3)));
}

TEST_F(PropertiesTest, ObjectVector) {
  buildUniverse();
  const auto& universe = getUniverse();
  auto vector = makeObjectVector({{object(3), 1.3}}, universe);
  ASSERT_EQ("ObjectVector", vector->getType());

  auto properties = vector->getProperties();
  ASSERT_EQ(2, properties.properties()->size());
  ASSERT_EQ(
      0, properties.properties()->at("default_value").valueDouble()->value());
  const std::map<int, double> expected({{object(3).asInt(), 1.3}});
  ASSERT_EQ(
      expected,
      *properties.properties()
           ->at("object_values")
           .valueObjectIdDoubleMap()
           ->value());
  EXPECT_TRUE(vector->references(object(3)));
  EXPECT_FALSE(vector->references(object(2)));
  EXPECT_FALSE(vector->references(container(2)));

  auto nonzeroDefaultVector =
      makeObjectVector({{object(3), 0}}, 1, universe.getNumObjects(), universe);
  EXPECT_TRUE(nonzeroDefaultVector->references(object(2)));
  EXPECT_TRUE(nonzeroDefaultVector->references(object(3)));
}

TEST_F(PropertiesTest, Piecewise) {
  buildUniverse();
  const auto& universe = getUniverse();
  auto pw = piecewise({{0.0, 10.0}, {10.0, 5.0}}, const_expr(2.5, universe));
  ASSERT_EQ("Piecewise", pw->getType());

  auto properties = pw->getProperties();
  ASSERT_EQ(1, properties.properties()->size());
  auto& points =
      *properties.properties()->at("points").valuePoint2dList()->value();
  ASSERT_EQ(2, points.size());
  ASSERT_EQ(0.0, *points.at(0).x());
  ASSERT_EQ(10.0, *points.at(0).y());
  ASSERT_EQ(10.0, *points.at(1).x());
  ASSERT_EQ(5.0, *points.at(1).y());
}

TEST_F(PropertiesTest, TransformPower) {
  buildUniverse();
  const auto& universe = getUniverse();
  auto transform = power(const_expr(1.0, universe), 2.0);
  ASSERT_EQ("Power", transform->getType());

  auto properties = transform->getProperties();
  ASSERT_EQ(1, properties.properties()->size());
  ASSERT_EQ(
      2.0, *properties.properties()->at("exponent").valueDouble()->value());
}

TEST_F(PropertiesTest, TransformRectangle) {
  buildUniverse();
  const auto& universe = getUniverse();
  auto transform = rectangle(const_expr(1.0, universe), 2.0, 3.0);
  ASSERT_EQ("Rectangle", transform->getType());

  auto properties = transform->getProperties();
  ASSERT_EQ(2, properties.properties()->size());
  ASSERT_EQ(
      2.0, *properties.properties()->at("lower_bound").valueDouble()->value());
  ASSERT_EQ(
      3.0, *properties.properties()->at("upper_bound").valueDouble()->value());
}

TEST_F(PropertiesTest, Variable) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  auto var = variable(object(3), container(4), universe, assignment);
  ASSERT_EQ("Variable", var->getType());

  auto properties = var->getProperties();
  ASSERT_EQ(2, properties.properties()->size());
  ASSERT_EQ(
      object(3).asInt(),
      *properties.properties()->at("object").valueObjectId()->value());
  ASSERT_EQ(
      container(4).asInt(),
      *properties.properties()->at("container").valueContainerId()->value());
  EXPECT_TRUE(var->references(object(3)));
  EXPECT_TRUE(var->references(container(4)));
  EXPECT_FALSE(var->references(object(4)));
  EXPECT_FALSE(var->references(container(3)));
}

TEST_F(PropertiesTest, Ceil) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  auto ceil = Ceil(variable(object(2), container(3), universe, assignment));
  ASSERT_EQ("Ceil", ceil.getType());
}

TEST_F(PropertiesTest, Log) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  auto log = Log(200 * variable(object(2), container(3), universe, assignment));
  ASSERT_EQ(log.getType(), "Log");
}

CO_TEST_F(PropertiesTestCustom, ObjectPartition) {
  setInitialAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"container1", {"object1", "object2"}}});

  const auto objectCountDimensionId = dimensionId("object_count");
  co_await addPartition("partition1", {{"group1", {"object1"}}});

  const auto universe = buildUniverse();
  auto objPartition = object_partition(
      std::make_shared<const PartitionInfo>(
          *universe, partitionId("partition1")),
      objectCountDimensionId,
      {});

  EXPECT_EQ("ObjectPartition", objPartition->getType());
  EXPECT_TRUE(objPartition->references(objectId("object1")));
  EXPECT_FALSE(objPartition->references(objectId("object2")));
}

CO_TEST_F(PropertiesTestCustom, ObjectPartitionLookup) {
  setInitialAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"container1", {"object1"}}});

  const auto objectCountDimensionId = dimensionId("object_count");
  co_await addPartition("partition1", {});

  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  const auto containerScopeId = scopeId("container");
  const auto container1ScopeItemId =
      scopeItemId(containerScopeId, "container1");

  auto objPartitionLookup = object_partition_lookup(
      object_partition(
          std::make_shared<const PartitionInfo>(
              universe, partitionId("partition1")),
          objectCountDimensionId,
          {}),
      std::make_shared<PackerSet<entities::ContainerId>>(),
      containerScopeId,
      container1ScopeItemId,
      assignment);
  EXPECT_EQ("ObjectPartitionLookup", objPartitionLookup->getType());
}

CO_TEST_F(PropertiesTestCustom, ObjectPartitionMoveLimit) {
  setInitialAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"container1", {"object1"}}});

  const auto objectCountDimensionId = dimensionId("object_count");
  co_await addPartition("partition1", {});

  buildUniverse();
  const auto& universe = getUniverse();

  auto moveLimit = ObjectPartitionMoveLimit(
      universe,
      {},
      partitionId("partition1"),
      objectCountDimensionId,
      {},
      {},
      {});
  EXPECT_EQ("ObjectPartitionMoveLimit", moveLimit.getType());
}

TEST_F(PropertiesTest, Square) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  auto square = Square(variable(object(3), container(4), universe, assignment));
  ASSERT_EQ("Square", square.getType());
}

TEST_F(PropertiesTest, StableStayed) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  auto vec =
      makeObjectVector(PackerMap<entities::ObjectId, double>{}, universe);
  auto stayed = stable_stayed(
      vec,
      vec,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(0), container(1)}),
      universe,
      assignment);
  ASSERT_EQ("StableStayed", stayed->getType());
}

TEST_F(PropertiesTest, Step) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  auto stepExpr = step(variable(object(2), container(3), universe, assignment));
  ASSERT_EQ("Step", stepExpr->getType());
}

TEST_F(PropertiesTest, SumOverThreshold) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  auto overThreshold = sum_over_threshold(
      const_expr(0.6, universe),
      {variable(object(0), container(1), universe, assignment) + 0.2},
      false);
  ASSERT_EQ("SumOverThreshold", overThreshold->getType());
}

} // namespace facebook::rebalancer::packer::tests
