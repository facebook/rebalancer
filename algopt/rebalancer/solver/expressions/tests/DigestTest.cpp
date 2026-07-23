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

#include "algopt/rebalancer/entities/builders/DimensionsBuilder.h"
#include "algopt/rebalancer/solver/expressions/AnyPositive.h"
#include "algopt/rebalancer/solver/expressions/BipartiteSwaps.h"
#include "algopt/rebalancer/solver/expressions/Ceil.h"
#include "algopt/rebalancer/solver/expressions/LinearSum.h"
#include "algopt/rebalancer/solver/expressions/Log.h"
#include "algopt/rebalancer/solver/expressions/Max.h"
#include "algopt/rebalancer/solver/expressions/NthLargest.h"
#include "algopt/rebalancer/solver/expressions/ObjectLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartition.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectVector.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/Piecewise.h"
#include "algopt/rebalancer/solver/expressions/Power.h"
#include "algopt/rebalancer/solver/expressions/ProductOperation.h"
#include "algopt/rebalancer/solver/expressions/QuotientOperation.h"
#include "algopt/rebalancer/solver/expressions/Rectangle.h"
#include "algopt/rebalancer/solver/expressions/Square.h"
#include "algopt/rebalancer/solver/expressions/Step.h"
#include "algopt/rebalancer/solver/expressions/SumOverThreshold.h"
#include "algopt/rebalancer/solver/expressions/Swaps.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"
#include "algopt/rebalancer/solver/expressions/Variable.h"
#include "algopt/rebalancer/solver/tests/ExprProblemCreation.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>

namespace facebook::rebalancer::packer::tests {

// Sorts lines of a digest string to make comparisons independent of hash map
// iteration order. Keeps the first line (root) in place, normalizes └─ to ├─
// so that the last-child marker doesn't affect sort order or equality after
// reordering, then sorts child lines.
static std::string sortDigestLines(const std::string& digest) {
  std::vector<std::string> lines;
  std::istringstream iss(digest);
  std::string line;
  while (std::getline(iss, line)) {
    // Normalize last-child marker so sorted order doesn't affect equality.
    const std::string lastChild = "└─";
    const std::string midChild = "├─";
    const auto pos = line.find(lastChild);
    if (pos != std::string::npos) {
      line.replace(pos, lastChild.size(), midChild);
    }
    lines.push_back(line);
  }
  if (lines.size() > 1) {
    std::sort(lines.begin() + 1, lines.end());
  }
  std::string result;
  for (const auto& l : lines) {
    result += l + "\n";
  }
  return result;
}

// Sorts comma-separated entries in a single-line digest to make comparisons
// independent of hash map iteration order.
static std::string sortDigestItems(const std::string& digest) {
  std::vector<std::string> items;
  std::istringstream iss(digest);
  std::string item;
  while (std::getline(iss, item, ',')) {
    if (!item.empty() && item.front() == ' ') {
      item = item.substr(1);
    }
    items.push_back(item);
  }
  std::sort(items.begin(), items.end());
  std::string result;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i > 0) {
      result += ", ";
    }
    result += items[i];
  }
  return result;
}

class DigestTest : public ExpressionTestsBase {};

TEST_F(DigestTest, Variable) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  auto eVariable =
      std::make_shared<Variable>(object(0), container(0), universe, assignment);
  auto problem = createTestProblem(
      getUniversePtr(), {eVariable}, eVariable, {}, {}, false);
  auto digest = eVariable->digest(*problem);
  EXPECT_EQ(digest, "Variable [1 → 1] object:object0, container:container0\n");
}

TEST_F(DigestTest, Max) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  std::vector<std::shared_ptr<Expression>> expressions;
  expressions.push_back(
      std::make_shared<Variable>(
          object(0), container(0), universe, assignment));
  expressions.push_back(
      std::make_shared<Variable>(
          object(1), container(0), universe, assignment));
  auto eMax = std::make_shared<Max>(expressions, universe);
  auto problem =
      createTestProblem(getUniversePtr(), {eMax}, eMax, {}, {}, false);
  auto digest = eMax->digest(*problem);
  EXPECT_EQ(
      sortDigestLines(digest),
      sortDigestLines(
          "Max [1 → 1] = MAX(Variable(1), Variable(1))\n"
          "   ├─Variable [1 → 1] object:object0, container:container0\n"
          "   └─Variable [1 → 1] object:object1, container:container0\n"));
}

TEST_F(DigestTest, NthLargest) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1", "object3"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  std::vector<std::shared_ptr<Expression>> expressions;
  expressions.push_back(
      std::make_shared<Variable>(
          object(0), container(0), universe, assignment));
  expressions.push_back(
      std::make_shared<Variable>(
          object(1), container(0), universe, assignment));
  expressions.push_back(
      std::make_shared<Variable>(
          object(3), container(0), universe, assignment));
  auto eNthLargest =
      std::make_shared<NthLargest>(expressions, 2, false, universe);
  auto problem = createTestProblem(
      getUniversePtr(), {eNthLargest}, eNthLargest, {}, {}, false);
  auto digest = eNthLargest->digest(*problem);
  EXPECT_EQ(
      sortDigestLines(digest),
      sortDigestLines(
          "NthLargest [1 → 1]\n"
          "   ├─Variable [1 → 1] object:object0, container:container0\n"
          "   ├─Variable [1 → 1] object:object1, container:container0\n"
          "   └─Variable [1 → 1] object:object3, container:container0\n"));
}

TEST_F(DigestTest, AnyPositive) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1", "object2"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  std::vector<std::shared_ptr<Expression>> expressions;
  expressions.push_back(
      std::make_shared<Variable>(
          object(0), container(0), universe, assignment));
  expressions.push_back(
      std::make_shared<Variable>(
          object(1), container(0), universe, assignment));
  expressions.push_back(
      std::make_shared<Variable>(
          object(2), container(0), universe, assignment));
  auto eAnyPositive =
      std::make_shared<AnyPositive>(expressions, universe, 0.01);
  auto problem = createTestProblem(
      getUniversePtr(), {eAnyPositive}, eAnyPositive, {}, {}, false);
  auto digest = eAnyPositive->digest(*problem);
  EXPECT_EQ(
      sortDigestLines(digest),
      sortDigestLines(
          "AnyPositive [1 → 1] = ANY_POSITIVE(Variable(1), Variable(1), Variable(1))\n"
          "   ├─Variable [1 → 1] object:object0, container:container0\n"
          "   ├─Variable [1 → 1] object:object1, container:container0\n"
          "   └─Variable [1 → 1] object:object2, container:container0\n"));
}

TEST_F(DigestTest, ProductOperation) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  const ExprPtr e0 =
      std::make_shared<Variable>(object(0), container(0), universe, assignment);
  const ExprPtr e1 =
      std::make_shared<Variable>(object(1), container(0), universe, assignment);
  auto eProductOperation = std::make_shared<ProductOperation>(e0, e1);
  auto problem = createTestProblem(
      getUniversePtr(), {eProductOperation}, eProductOperation, {}, {}, false);
  auto digest = eProductOperation->digest(*problem);
  EXPECT_EQ(
      sortDigestLines(digest),
      sortDigestLines(
          "Product [1 → 1]\n"
          "   ├─Variable [1 → 1] object:object0, container:container0\n"
          "   └─Variable [1 → 1] object:object1, container:container0\n"));
}

TEST_F(DigestTest, QuotientOperation) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  const ExprPtr e0 =
      std::make_shared<Variable>(object(0), container(0), universe, assignment);
  const ExprPtr e1 =
      std::make_shared<Variable>(object(1), container(0), universe, assignment);
  auto eQuotientOperation = std::make_shared<QuotientOperation>(e0, e1);
  auto problem = createTestProblem(
      getUniversePtr(),
      {eQuotientOperation},
      eQuotientOperation,
      {},
      {},
      false);
  auto digest = eQuotientOperation->digest(*problem);
  EXPECT_EQ(
      sortDigestLines(digest),
      sortDigestLines(
          "Quotient [1 → 1]\n"
          "   ├─Variable [1 → 1] object:object0, container:container0\n"
          "   └─Variable [1 → 1] object:object1, container:container0\n"));
}

TEST_F(DigestTest, LinearSum) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1"}}});
  buildUniverse();
  const auto& universe = getUniverse();

  auto eLinearSum = std::make_shared<LinearSum>(universe, 3.1415);
  auto problem = createTestProblem(
      getUniversePtr(), {eLinearSum}, eLinearSum, {}, {}, false);
  auto digest = eLinearSum->digest(*problem);
  EXPECT_EQ(digest, "LinearSum [3.1415 → 3.1415] = 3.1415\n");
}

TEST_F(DigestTest, SumOverThreshold) {
  // Use 2 values (not 3) so all children are always visible — with 3 values
  // the digest truncates one child and which one is truncated depends on the
  // hash map iteration order of PackerMap, making the expected string fragile.
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  const ExprPtr threshold =
      std::make_shared<Variable>(object(0), container(0), universe, assignment);
  std::vector<std::shared_ptr<Expression>> values;
  values.push_back(
      std::make_shared<Variable>(
          object(0), container(0), universe, assignment));
  values.push_back(
      std::make_shared<Variable>(
          object(1), container(0), universe, assignment));
  auto eSumOverThreshold =
      std::make_shared<SumOverThreshold>(threshold, values, true);
  auto problem = createTestProblem(
      getUniversePtr(), {eSumOverThreshold}, eSumOverThreshold, {}, {}, false);
  auto digest = eSumOverThreshold->digest(*problem);
  EXPECT_EQ(
      sortDigestLines(digest),
      sortDigestLines(
          "SumOverThreshold [0 → 0] thresholdVariable = 1, over Variable(1), Variable(1))\n"
          "   ├─-1 * Variable [1 → 1] object:object0, container:container0\n"
          "   ├─Variable [1 → 1] object:object0, container:container0\n"
          "   ├─Variable [1 → 1] object:object1, container:container0\n"));
}

TEST_F(DigestTest, Piecewise) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  const ExprPtr expr =
      std::make_shared<Variable>(object(0), container(0), universe, assignment);
  std::vector<std::pair<double, double>> points;
  points.emplace_back(1.0, 9.0);
  points.emplace_back(2.0, 5.0);
  auto ePiecewise = std::make_shared<Piecewise>(points, expr);
  auto problem = createTestProblem(
      getUniversePtr(), {ePiecewise}, ePiecewise, {}, {}, false);
  auto digest = ePiecewise->digest(*problem);
  EXPECT_EQ(
      digest,
      "Piecewise [9 → 9] = PIECEWISE([1, 9], [2, 5])\n"
      "   └─Variable [1 → 1] object:object0, container:container0\n");
}

TEST_F(DigestTest, Swaps) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{{"bar", {"foo"}}});
  buildUniverse();
  const auto& universe = getUniverse();

  PackerMap<entities::ObjectId, entities::ContainerId> ia;
  ia.emplace(universe.getObjectId("foo"), universe.getContainerId("bar"));

  auto eSwaps = std::make_shared<Swaps>(ia, universe);
  auto problem =
      createTestProblem(getUniversePtr(), {eSwaps}, eSwaps, {}, {}, false);
  auto digest = eSwaps->digest(*problem);
  EXPECT_EQ(digest, "Swaps [1 → 1]\n");
}

TEST_F(DigestTest, BipartiteSwaps) {
  constexpr int kObjectCount = 3;
  entities::Map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto j : folly::irange(kObjectCount)) {
    initialAssignment[fmt::format("container{}", j)] = {
        fmt::format("object{}", j)};
  }
  setInitialAssignment(initialAssignment);
  buildUniverse();
  const auto& universe = getUniverse();

  PackerMap<entities::ObjectId, entities::ContainerId> ia;
  PackerSet<entities::ContainerId> l;
  PackerSet<entities::ContainerId> r;
  for (const auto j : folly::irange(kObjectCount)) {
    ia.emplace(object(j), container(j));
    if (j % 2 == 0) {
      l.insert(container(j));
    } else {
      r.insert(container(j));
    }
  }

  auto eBipartiteSwaps = std::make_shared<BipartiteSwaps>(ia, l, r, universe);
  auto problem = createTestProblem(
      getUniversePtr(), {eBipartiteSwaps}, eBipartiteSwaps, {}, {}, false);
  auto digest = eBipartiteSwaps->digest(*problem);
  EXPECT_EQ(digest, "BipartiteSwaps [1 → 1]\n");
}

TEST_F(DigestTest, ObjectVector) {
  constexpr int kObjectCount = 3;
  entities::Map<std::string, std::vector<std::string>> initialAssignment;
  initialAssignment["container0"] = {};
  for (const auto j : folly::irange(kObjectCount)) {
    initialAssignment["container0"].push_back(fmt::format("object{}", j));
  }
  setInitialAssignment(initialAssignment);
  buildUniverse();
  const auto& universe = getUniverse();

  entities::Map<entities::ObjectId, double> values;
  for (const auto j : folly::irange(kObjectCount)) {
    values.emplace(object(j), 11.5 * j);
  }
  auto eObjectVector = makeObjectVector(values, universe);
  ExprPtr expr = eObjectVector;
  EXPECT_EQ(sortDigestItems(expr->innerDigest()), "object1:11.5, object2:23");
}

TEST_F(DigestTest, Log) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  const ExprPtr expr =
      std::make_shared<Variable>(object(0), container(0), universe, assignment);
  auto eLog = std::make_shared<Log>(expr);
  auto problem =
      createTestProblem(getUniversePtr(), {eLog}, eLog, {}, {}, false);
  auto digest = eLog->digest(*problem);
  EXPECT_EQ(
      digest,
      "Log [0 → 0] Log(Variable(1))\n"
      "   └─Variable [1 → 1] object:object0, container:container0\n");
}

TEST_F(DigestTest, Ceil) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  const ExprPtr expr =
      std::make_shared<Variable>(object(0), container(0), universe, assignment);
  auto eCeil = std::make_shared<Ceil>(expr);
  auto problem =
      createTestProblem(getUniversePtr(), {eCeil}, eCeil, {}, {}, false);
  auto digest = eCeil->digest(*problem);
  EXPECT_EQ(
      digest,
      "Ceil [1 → 1] Ceil(Variable(1))\n"
      "   └─Variable [1 → 1] object:object0, container:container0\n");
}

TEST_F(DigestTest, Step) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  const ExprPtr expr =
      std::make_shared<Variable>(object(0), container(0), universe, assignment);
  auto eStep = std::make_shared<Step>(expr);
  auto problem =
      createTestProblem(getUniversePtr(), {eStep}, eStep, {}, {}, false);
  auto digest = eStep->digest(*problem);
  EXPECT_EQ(
      digest,
      "Step [1 → 1] Step(Variable(1))\n"
      "   └─Variable [1 → 1] object:object0, container:container0\n");
}

TEST_F(DigestTest, Square) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  const ExprPtr expr =
      std::make_shared<Variable>(object(0), container(0), universe, assignment);

  struct ApproximationHint const hint{
      .valid = true, .upper_bound = 1.0, .lower_bound = 2.0, .piece_count = 11};
  auto eSquare = std::make_shared<Square>(expr, hint);
  auto problem =
      createTestProblem(getUniversePtr(), {eSquare}, eSquare, {}, {}, false);
  auto digest = eSquare->digest(*problem);
  EXPECT_EQ(
      digest,
      "Square [1 → 1] Square(Variable(1))\n"
      "   └─Variable [1 → 1] object:object0, container:container0\n");
}

TEST_F(DigestTest, Power) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  const ExprPtr expr =
      std::make_shared<Variable>(object(0), container(0), universe, assignment);
  auto ePower = std::make_shared<Power>(expr, 2.5);
  auto problem =
      createTestProblem(getUniversePtr(), {ePower}, ePower, {}, {}, false);
  auto digest = ePower->digest(*problem);
  EXPECT_EQ(
      digest,
      "Power [1 → 1] Power(Variable(1))\n"
      "   └─Variable [1 → 1] object:object0, container:container0\n");
}

TEST_F(DigestTest, Rectangle) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  const ExprPtr expr =
      std::make_shared<Variable>(object(0), container(0), universe, assignment);
  auto eRectangle = std::make_shared<Rectangle>(expr, 1.3, 2.5);
  auto problem = createTestProblem(
      getUniversePtr(), {eRectangle}, eRectangle, {}, {}, false);
  auto digest = eRectangle->digest(*problem);
  EXPECT_EQ(
      digest,
      "Rectangle [0 → 0] rectangle(Variable(1), 1.3, 2.5)\n"
      "   └─Variable [1 → 1] object:object0, container:container0\n");
}

TEST_F(DigestTest, ObjectLookup) {
  constexpr int kContainerCount = 3;
  entities::Map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto j : folly::irange(kContainerCount)) {
    initialAssignment[fmt::format("container{}", j)] = {};
  }
  setInitialAssignment(initialAssignment);
  buildUniverse();
  const auto& universe = getUniverse();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>();
  for (const auto j : folly::irange(kContainerCount)) {
    containers->insert(container(j));
  }
  const auto numTotalObjects = universe.getNumObjects();
  auto objVector = makeObjectVector(
      entities::Map<entities::ObjectId, double>(),
      1,
      numTotalObjects,
      universe);
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  auto eObjectLookup = std::make_shared<ObjectLookup>(
      objVector, containers, universe, assignment);
  auto problem = createTestProblem(
      getUniversePtr(), {eObjectLookup}, eObjectLookup, {}, {}, false);
  auto digest = eObjectLookup->digest(*problem);
  EXPECT_EQ(
      "ObjectLookup [0 → 0] on Containers: container0, container1, container2\n",
      digest);
}

CO_TEST_F(DigestTest, ObjectPartitionLookup) {
  constexpr int kObjectCount = 10;
  constexpr int kGroupCount = 10;

  // Set up initial assignment
  entities::Map<std::string, std::vector<std::string>> initialAssignment;
  initialAssignment["container0"] = {};
  initialAssignment["container1"] = {};
  for (const auto j : folly::irange(kObjectCount)) {
    const auto objectName = fmt::format("object{}", j);
    if (j == 1 || j == 5 || j == 7 || j == 9) {
      initialAssignment["container0"].push_back(objectName);
    } else {
      initialAssignment["container1"].push_back(objectName);
    }
  }
  setInitialAssignment(initialAssignment);

  // Create partition with groups
  // Object j is in group j, for all j not in {1, 7}.
  // Object 1 is in group 2 and object 7 is in group 8.
  entities::Map<std::string, std::vector<std::string>> groupToObjects;
  for (const auto j : folly::irange(kGroupCount)) {
    const auto groupName = fmt::format("group{}", j);
    if (j == 1 || j == 7) {
      groupToObjects[groupName] = {};
    } else if (j == 2) {
      groupToObjects[groupName] = {"object1", "object2"};
    } else if (j == 8) {
      groupToObjects[groupName] = {"object7", "object8"};
    } else {
      groupToObjects[groupName] = {fmt::format("object{}", j)};
    }
  }
  co_await addPartition("partition1", groupToObjects);

  // Create object dimension
  // Objects 1 and 9 have negative weight.
  // Object 7 has weight 3; rest all have weight 1.
  entities::ObjectIdToDoubleMap objectToWeight(
      kObjectCount, /*defaultValue=*/1.0, /*expectedNonDefaultSize=*/3);
  objectToWeight.emplace(object(1), -2);
  objectToWeight.emplace(object(9), -1);
  objectToWeight.emplace(object(7), 3);

  co_await addObjectDimension(
      "object_weight",
      entities::ObjectDimensionData{
          std::make_unique<const entities::ObjectDimension>(
              std::move(objectToWeight))});

  // Create scope with one scope item containing container1
  co_await addScope(
      "scope1",
      entities::Map<std::string, std::vector<std::string>>{
          {"scopeItem0", {"container1"}}});

  buildUniverse();
  const auto& universe = getUniverse();

  const auto partition1Id = partitionId("partition1");
  const auto objectWeightDimensionId = dimensionId("object_weight");
  const auto container0 = container(0);
  const auto scope1Id = scopeId("scope1");
  const auto scopeItem0 = scopeItemId(scope1Id, "scopeItem0");

  // Create group limits (all zeros)
  PackerMap<entities::GroupId, double> groupLimits;
  for (const auto j : folly::irange(kGroupCount)) {
    const auto gId = groupId(partition1Id, fmt::format("group{}", j));
    groupLimits[gId] = 0;
  }

  auto objectPartition = std::make_shared<ObjectPartition>(
      std::make_shared<const PartitionInfo>(universe, partition1Id),
      objectWeightDimensionId,
      groupLimits);

  const Assignment assignment(universe.getContainers().getInitialAssignment());
  auto ePartitionLookup = std::make_shared<ObjectPartitionLookupDefault>(
      objectPartition,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container0}),
      scope1Id,
      scopeItem0,
      universe,
      assignment,
      /*groupLimitOverrides=*/PackerMap<entities::GroupId, double>({}),
      /*initialDuringObjects=*/
      PackerSet<entities::ObjectId>(
          {object(1), object(5), object(7), object(9)}));
  auto problem = createTestProblem(
      getUniversePtr(), {ePartitionLookup}, ePartitionLookup, {}, {}, false);
  auto digest = ePartitionLookup->digest(*problem);
  EXPECT_EQ(
      digest,
      "ObjectPartitionLookup [4 → 4] containers(container0) 4 initial objects, "
      "groupsAllowed_: 0, groupLimitOverrides_({}), "
      "partition_value: group2=-2, group5=1, group8=3 ... 1 more\n");
}

// digest(Problem*) tests: verify [initial → final] comparison format.
TEST_F(DigestTest, VariableWithProblem) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0"}}, {"container1", {}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  auto eVariable =
      std::make_shared<Variable>(object(0), container(0), universe, assignment);

  auto problem = createTestProblem(
      getUniversePtr(), {eVariable}, eVariable, {}, {}, false);
  problem->assignment =
      Assignment({{container(0), {}}, {container(1), {object(0)}}});

  auto digest = eVariable->digest(*problem);
  EXPECT_EQ(digest, "Variable [1 → 0] object:object0, container:container0\n");
}

TEST_F(DigestTest, ProblemMixedChanges) {
  // Move object0 out of container0, keep object1 in container0.
  // Covers recursive case in a single tree along with three cases:
  //   Parent unchanged: Max [1 → 1]
  //   Child moved out:  Variable(obj0) [1 → 0]
  //   Child stayed:     Variable(obj1) [1 → 1]
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1"}}, {"container1", {}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  std::vector<std::shared_ptr<Expression>> expressions;
  expressions.push_back(
      std::make_shared<Variable>(
          object(0), container(0), universe, assignment));
  expressions.push_back(
      std::make_shared<Variable>(
          object(1), container(0), universe, assignment));
  auto eMax = std::make_shared<Max>(expressions, universe);

  auto problem =
      createTestProblem(getUniversePtr(), {eMax}, eMax, {}, {}, false);
  problem->assignment =
      Assignment({{container(0), {object(1)}}, {container(1), {object(0)}}});

  auto digest = eMax->digest(*problem);
  EXPECT_EQ(
      sortDigestLines(digest),
      sortDigestLines(
          "Max [1 → 1] = MAX(Variable(1), Variable(1))\n"
          "   ├─Variable [1 → 0] object:object0, container:container0\n"
          "   └─Variable [1 → 1] object:object1, container:container0\n"));
}

TEST_F(DigestTest, ProblemReverseDirection) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {}}, {"container1", {"object0"}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());

  auto eVariable =
      std::make_shared<Variable>(object(0), container(0), universe, assignment);

  auto problem = createTestProblem(
      getUniversePtr(), {eVariable}, eVariable, {}, {}, false);
  problem->assignment =
      Assignment({{container(0), {object(0)}}, {container(1), {}}});

  auto digest = eVariable->digest(*problem);
  EXPECT_EQ(digest, "Variable [0 → 1] object:object0, container:container0\n");
}

} // namespace facebook::rebalancer::packer::tests
