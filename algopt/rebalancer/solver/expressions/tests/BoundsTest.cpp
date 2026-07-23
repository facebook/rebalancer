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
#include "algopt/rebalancer/entities/builders/DimensionsBuilder.h"
#include "algopt/rebalancer/solver/expressions/LinearSum.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartition.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/tests/ExprProblemCreation.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class BoundsTest : public ExpressionTestsBase {
 protected:
  void SetUp() override {
    folly::coro::blockingWait(setUpUniverse());
  }

  folly::coro::Task<void> setUpUniverse() {
    constexpr int kObjectCount = 10;
    constexpr int kGroupCount = 10;

    // Set up initial assignment
    // object i is assigned to container0 if i in {1, 5, 7, 9}, else container1
    entities::Map<std::string, std::vector<std::string>> initialAssignment;
    initialAssignment["container0"] = {};
    initialAssignment["container1"] = {};
    for (const auto i : folly::irange(kObjectCount)) {
      const auto objectName = fmt::format("object{}", i);
      if (i == 1 || i == 5 || i == 7 || i == 9) {
        initialAssignment["container0"].push_back(objectName);
      } else {
        initialAssignment["container1"].push_back(objectName);
      }
    }
    universeBuilder_.setInitialAssignment(initialAssignment);

    // object i is in group i, for all i not in {1, 7}. Object 1 is in group 2
    // and object 7 is in group 8
    entities::Map<std::string, std::vector<std::string>> groupToObjects;
    for (const auto i : folly::irange(kGroupCount)) {
      const auto groupName = fmt::format("group{}", i);
      if (i == 1 || i == 7) {
        groupToObjects[groupName] = {};
      } else if (i == 2) {
        groupToObjects[groupName] = {"object1", "object2"};
      } else if (i == 8) {
        groupToObjects[groupName] = {"object7", "object8"};
      } else {
        groupToObjects[groupName] = {fmt::format("object{}", i)};
      }
    }
    co_await addPartition("partition1", groupToObjects);

    // Create object dimension
    // objects 1 and 9 have negative weight; object 7 has weight 3; rest all
    // have weight 1
    constexpr double kDefaultValue = 1.0;
    entities::ObjectIdToDoubleMap objectToWeight(
        kObjectCount, kDefaultValue, /*expectedNonDefaultSize=*/3);
    objectToWeight.emplace(object(1), -2);
    objectToWeight.emplace(object(9), -1);
    objectToWeight.emplace(object(7), 3);

    co_await addObjectDimension(
        "object_weight",
        entities::ObjectDimensionData{
            std::make_unique<const entities::ObjectDimension>(
                std::move(objectToWeight))});

    // Create scope with one scope item containing container0
    co_await addScope(
        "scope1",
        entities::Map<std::string, std::vector<std::string>>{
            {"scopeItem0", {"container0"}}});

    universe_ = buildUniverse();
  }

  folly::coro::Task<ExprPtr> createObjectPartitionLookup(
      ObjectPartitionLookupDefault::Bound boundType) {
    constexpr int kGroupCount = 10;

    const auto partition1Id = partitionId("partition1");
    const auto objectWeightDimensionId = dimensionId("object_weight");
    const auto container0 = container(0);
    const auto scope1Id = scopeId("scope1");
    const auto scopeItem0 = scopeItemId(scope1Id, "scopeItem0");

    // Create group limits (all zeros)
    PackerMap<entities::GroupId, double> groupLimits;
    for (const auto i : folly::irange(kGroupCount)) {
      const auto gId = groupId(partition1Id, fmt::format("group{}", i));
      groupLimits[gId] = 0;
    }

    // Create ObjectPartition
    auto objectPartition = std::make_shared<ObjectPartition>(
        std::make_shared<const PartitionInfo>(*universe_, partition1Id),
        objectWeightDimensionId,
        groupLimits);

    // Create ObjectPartitionLookup
    Assignment assignment(universe_->getContainers().getInitialAssignment());
    auto expression = std::make_shared<ObjectPartitionLookupDefault>(
        objectPartition,
        std::make_shared<PackerSet<entities::ContainerId>>(
            PackerSet<entities::ContainerId>{
                container0}) /* lookupContainers */,
        scope1Id,
        scopeItem0,
        *universe_,
        assignment,
        PackerMap<entities::GroupId, double>({}) /* groupLimitOverrides */,
        PackerSet<entities::ObjectId>(
            {object(1),
             object(5),
             object(7),
             object(9)}) /* initialDuringObjects */,
        std::nullopt,
        ObjectPartitionLookupPenaltyTransform::IDENTITY,
        0 /* groupsAllowed */,
        boundType);

    co_return expression;
  }

  std::shared_ptr<const entities::Universe> universe_;
};

CO_TEST_F(BoundsTest, ObjectPartitionLookupBasicMax) {
  const auto partitionLookup = co_await createObjectPartitionLookup(
      ObjectPartitionLookupDefault::Bound::MAX);

  Context context;
  const auto [lb, ub] = partitionLookup->lowerAndUpperBounds(context);
  // expected lowerBound is penalty because of all negative weighted objects
  // and all positive weighted initialDuringObjects = 1 (because of object 5)
  // + 3 (because of object {7}) = 4;
  EXPECT_NEAR(lb, 4.0, 1e-8);

  // expected upperBound is penalty because of all the positive weighted
  // objects and negative weighted initialDuringObjects = 5
  // (because of objects in {0, 3, 4, 5, 6}) + 0 (because of objects {1, 2} in
  // group 2) + 4 (because of objects {7, 8} in
  // group 8)
  EXPECT_NEAR(ub, 9.0, 1e-8);
}

CO_TEST_F(BoundsTest, ObjectPartitionLookupBasicMin) {
  const auto partitionLookup = co_await createObjectPartitionLookup(
      ObjectPartitionLookupDefault::Bound::MIN);

  Context context;
  const auto [lb, ub] = partitionLookup->lowerAndUpperBounds(context);
  // expected lowerBound is penalty because of all positive weighted objects
  // and all negative weighted initialDuringObjects = 1 (because of objects
  // {1,2} in group2) + 1 (because of object {9}) = 2;
  EXPECT_NEAR(lb, 2.0, 1e-8);

  // expected upperBound is penalty because of all the negative weighted
  // objects and positive weighted initialDuringObjects = 2 (because of
  // objects {1,2} in group2) + 1 (because of object {9}) = 3;
  EXPECT_NEAR(ub, 3.0, 1e-8);
}

CO_TEST_F(BoundsTest, SerialComputing) {
  PackerMap<std::shared_ptr<Expression>, double> exprToCoef;
  for (const auto _ : folly::irange(100)) {
    auto partitionLookup = co_await createObjectPartitionLookup(
        ObjectPartitionLookupDefault::Bound::MIN);
    exprToCoef.emplace(std::move(partitionLookup), 1);
  }

  auto p = createTestProblem(
      universe_,
      {std::make_shared<LinearSum>(*universe_, 0, exprToCoef)},
      std::make_shared<LinearSum>(*universe_, 0));

  Context context;
  auto [lb, ub] =
      p.get()->objective.getFirstObjective().get()->lowerAndUpperBounds(
          context);
  EXPECT_EQ(lb, 200);
  EXPECT_EQ(ub, 300);
}

CO_TEST_F(BoundsTest, ParallelComputing) {
  PackerMap<std::shared_ptr<Expression>, double> exprToCoef;
  for (const auto _ : folly::irange(100)) {
    auto partitionLookup = co_await createObjectPartitionLookup(
        ObjectPartitionLookupDefault::Bound::MIN);
    exprToCoef.emplace(std::move(partitionLookup), 1);
  }

  auto p = createTestProblem(
      universe_,
      {std::make_shared<LinearSum>(*universe_, 0, exprToCoef)},
      std::make_shared<LinearSum>(*universe_, 0),
      {},
      {},
      /*performInitialFullApply=*/true,
      /* enableParallelizedBoundsComputing=*/true);

  Context context;
  auto [lb, ub] =
      p.get()->objective.getFirstObjective().get()->lowerAndUpperBounds(
          context);
  EXPECT_EQ(lb, 200);
  EXPECT_EQ(ub, 300);
}
} // namespace facebook::rebalancer::packer::tests
