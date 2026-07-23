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

#include "algopt/rebalancer/entities/tests/Utils.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/iterators/ExpressionContainersIterator.h"

#include <folly/container/irange.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>

#include <memory>

namespace facebook::rebalancer::packer::tests {

static void verifyIfNodesInSubgraphAffectSameContainers(
    const std::vector<std::pair<ExprPtr, bool>>& exprAnsPairs) {
  for (const auto i : folly::irange(exprAnsPairs.size())) {
    auto& [expr, expectedAns] = exprAnsPairs.at(i);
    const bool computedAns =
        (expr->getUniqueAffectedContainersInSubgraphIfExists().getSetPtr() !=
         nullptr); // true if not nullptr, false otherwise

    EXPECT_EQ(computedAns, expectedAns)
        << "expr at pos " << i << " is incorrect";
  }
}

class ExpressionContainersIteratorTest : public ExpressionTestsBase {};

TEST_F(ExpressionContainersIteratorTest, ExpressionContainersTraversal) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1", {"object1"}},
          {"container2", {"object2"}},
          {"container3", {"object3"}},
      });
  const auto universe = buildUniverse();
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  auto a = 4 * variable(object(1), container(1), *universe, assignment); // 4
  auto b = 2 * variable(object(2), container(2), *universe, assignment); // 2
  auto c = 8 * variable(object(3), container(3), *universe, assignment); // 8
  auto d = rebalancer::max({a, b}, *universe); // 4
  auto e = c + d; // 12
  Context context;

  auto objective = GlobalObjective::Builder{}
                       .addToObjective(0, e, *universe)
                       .build(*universe);
  auto objectiveExpr = objective.getOnlyObjective();
  objectiveExpr->fullApply(TopToBottomEvaluator(context), assignment);
  objectiveExpr->init_unconstrained_bounds(context);

  const DescendingExpressionContainersTraversal descending(objective.getView());
  const std::vector<entities::ContainerId> descending_expected = {
      container(3), container(1), container(2)};
  const std::vector<entities::ContainerId> descending_actual(
      descending.begin(), descending.end());
  EXPECT_EQ(descending_expected, descending_actual);

  verifyIfNodesInSubgraphAffectSameContainers(
      {{a, true}, {b, true}, {c, true}, {d, false}, {e, false}});
}

CO_TEST_F(ExpressionContainersIteratorTest, ContainerTies) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1", {"object1"}},
          {"container2", {}},
          {"container3", {}},
          {"container4", {}},
          {"container5", {}},
          {"container6", {}}});

  co_await addPartition(
      "partition1",
      entities::Map<std::string, std::vector<std::string>>{
          {"group1", {"object1"}}});

  co_await addScope(
      "scope",
      entities::Map<std::string, std::vector<std::string>>{
          {"scopeItem", {"container1"}}});

  const auto universe = buildUniverse();
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  const auto partitionId = ExpressionTestsBase::partitionId("partition1");
  const auto scopeId = ExpressionTestsBase::scopeId("scope");
  const auto dimensionId = ExpressionTestsBase::dimensionId("object_count");
  const auto scopeItemId =
      ExpressionTestsBase::scopeItemId(scopeId, "scopeItem");

  auto op = object_partition(
      std::make_shared<const PartitionInfo>(*universe, partitionId),
      dimensionId,
      {});
  auto lookup1 = object_partition_lookup(
      op,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{
              container(1), container(2), container(3)}),
      scopeId,
      scopeItemId,
      assignment);
  auto lookup2 = object_partition_lookup(
      op,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{
              container(4), container(5), container(2)}),
      scopeId,
      scopeItemId,
      assignment);
  auto root = lookup1 + lookup2;

  Context context;
  auto objective = GlobalObjective::Builder{}
                       .addToObjective(0, root, *universe)
                       .build(*universe);
  auto objectiveExpr = objective.getOnlyObjective();
  objectiveExpr->fullApply(TopToBottomEvaluator(context), assignment);
  objectiveExpr->init_unconstrained_bounds(context);

  auto container_weights =
      std::make_shared<PackerMap<entities::ContainerId, double>>(
          PackerMap<entities::ContainerId, double>(
              {{container(1), 0},
               {container(2), 10},
               {container(3), -10},
               {container(5), 10},
               {container(6), -10}}));

  // At this point, lookup1 equals 1 and lookup2 equals 0. Containers touching
  // expressions with larger potentials must go first. Note that lookup1 has
  // potential value of 1, while lookup2 has potential value of 0. Ties are
  // resolved using the successive expressions touched.
  {
    // Case1: optimalExpressions are not skipped
    // In this case, container 2 must go first, because it touches the largest
    // expression lookup1 as well as the  second lookup2, and no other container
    // touches both. Then 1 and 3 must go because they touch lookup1 while 4 and
    // 5 don't. 4 and 5 go last. The tie between 1 and 3 can't be resolved by
    // expression values, and their relative order can be any, same with 4
    // and 5.
    const DescendingExpressionContainersTraversal descending(
        objective.getView());
    std::vector<entities::ContainerId> descending_actual(
        descending.begin(), descending.end());
    EXPECT_EQ(5, descending_actual.size());
    EXPECT_EQ(container(2), descending_actual.at(0));
    EXPECT_EQ(
        std::set<entities::ContainerId>({container(1), container(3)}),
        std::set<entities::ContainerId>(
            descending_actual.begin() + 1, descending_actual.begin() + 3));
    EXPECT_EQ(
        std::set<entities::ContainerId>({container(4), container(5)}),
        std::set<entities::ContainerId>(
            descending_actual.begin() + 3, descending_actual.begin() + 5));
  }

  { // Case2: optimalExpressions are skipped
    // In this case, since lookup2 has zero potential, we will not add that
    // expression to the PreOrder traversal stack. This means that containers
    // affecting lookup1 (1, 2, 3) can be returned in any order
    const DescendingExpressionContainersTraversal descending(
        objective.getView(), true /*skipOptimalExpressions*/);
    std::vector<entities::ContainerId> descending_actual(
        descending.begin(), descending.end());
    EXPECT_EQ(3, descending_actual.size());
    EXPECT_EQ(
        std::set<entities::ContainerId>(
            {container(1), container(2), container(3)}),
        std::set<entities::ContainerId>(
            descending_actual.begin(), descending_actual.end()));
  }

  verifyIfNodesInSubgraphAffectSameContainers(
      {{op, true}, {lookup1, true}, {lookup2, true}, {root, false}});
}

CO_TEST_F(ExpressionContainersIteratorTest, ContainerTiesObjectiveTuple) {
  // derives from Test:ContainerTies
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1", {"object1"}},
          {"container2", {}},
          {"container3", {"object2", "object3"}},
          {"container4", {}},
          {"container5", {}},
          {"container6", {}},
      });

  co_await addPartition(
      "partition1",
      entities::Map<std::string, std::vector<std::string>>{
          {"group1", {"object1"}},
          {"group2", {"object2"}},
          {"group3", {"object3"}}});

  co_await addScope(
      "scope",
      entities::Map<std::string, std::vector<std::string>>{
          {"scopeItem", {"container1"}}});

  const auto universe = buildUniverse();
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  const auto partitionId = ExpressionTestsBase::partitionId("partition1");
  const auto scopeId = ExpressionTestsBase::scopeId("scope");
  const auto dimensionId = ExpressionTestsBase::dimensionId("object_count");
  const auto scopeItemId =
      ExpressionTestsBase::scopeItemId(scopeId, "scopeItem");

  auto op = object_partition(
      std::make_shared<const PartitionInfo>(*universe, partitionId),
      dimensionId,
      {});
  auto lookup1 = object_partition_lookup(
      op,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{
              container(1), container(2), container(3)}),
      scopeId,
      scopeItemId,
      assignment); // value = 3, lb = 0, potential = 3
  lookup1->description = "lookup1";
  auto lookup2 = object_partition_lookup(
      op,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{
              container(4), container(5), container(2)}),
      scopeId,
      scopeItemId,
      assignment); // value = 0, lb = 0, potential = 0
  lookup2->description = "lookup2";
  auto ls1 = variable(object(1), container(1), *universe, assignment) +
      2 * variable(object(2), container(3), *universe, assignment) -
      variable(object(3), container(5), *universe, assignment);
  // lb = -1, potential = 4
  ls1->description = "ls1";

  auto ls2 =
      variable(object(1), container(6), *universe, assignment) -
      variable(
          object(2), container(3), *universe, assignment); // value = -1, lb =
                                                           // -1, potential = 0
  ls2->description = "ls2";

  auto objective = GlobalObjective::Builder{}
                       .addToObjective(0, lookup1, *universe)
                       .addToObjective(1, lookup2, *universe)
                       .addToObjective(2, ls1, *universe)
                       // to check that everything is de-duped and
                       // all containers are in global seen
                       .addToObjective(3, ls1, *universe)
                       .addToObjective(4, ls2, *universe)
                       .build(*universe);

  Context context;
  objective.fullApply(TopToBottomEvaluator(context), assignment);
  objective.initUnconstrainedBounds();

  {
    const DescendingExpressionContainersTraversal descending(
        objective.getView());
    const std::vector<entities::ContainerId> descendingExpected = {
        container(2),
        container(3),
        container(1),
        container(5),
        container(4),
        container(6)};
    const std::vector<entities::ContainerId> descendingActual(
        descending.begin(), descending.end());
    EXPECT_EQ(descendingExpected, descendingActual);
  }

  {
    const DescendingExpressionContainersTraversal descending(
        objective.getView(), true /*skipOptimalExpressions*/);
    const std::vector<entities::ContainerId> descendingExpected = {
        container(2), container(3), container(1), container(5), container(4)};
    const std::vector<entities::ContainerId> descendingActual(
        descending.begin(), descending.end());
    EXPECT_EQ(descendingExpected, descendingActual);
  }

  verifyIfNodesInSubgraphAffectSameContainers(
      {{op, true}, {lookup1, true}, {lookup2, true}, {ls1, false}});
}

TEST_F(
    ExpressionContainersIteratorTest,
    AllChildrenYieldSameAffectedContainers) {
  // when all child nodes affect the same set of containers, we do not expand
  // the parent but expect all affected containers of the parent to be added to
  // incremental priority queue
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1", {"object1"}},
          {"container2", {"object2"}},
          {"container3", {"object3"}},
      });
  const auto universe = buildUniverse();

  auto allContainers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{
          container(1), container(2), container(3)});
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  auto lookup1 = object_lookup(
      makeObjectVector(
          PackerMap<entities::ObjectId, double>{}, 1, 3, *universe),
      allContainers,
      assignment);
  auto lookup2 = object_lookup(
      makeObjectVector(
          PackerMap<entities::ObjectId, double>{}, 10, 3, *universe),
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{
              container(3), container(1), container(2)}),
      assignment);

  auto obj = lookup1 + lookup2;

  Context context;
  auto objective = GlobalObjective::Builder{}
                       .addToObjective(0, obj, *universe)
                       .build(*universe);
  auto objectiveExpr = objective.getOnlyObjective();
  objectiveExpr->fullApply(TopToBottomEvaluator(context), assignment);
  objectiveExpr->init_unconstrained_bounds(context);

  const DescendingExpressionContainersTraversal descending(objective.getView());

  const PackerSet<entities::ContainerId> descendingActual(
      descending.begin(), descending.end());

  for (auto contId : *allContainers) {
    EXPECT_TRUE(descendingActual.contains(contId));
  }

  verifyIfNodesInSubgraphAffectSameContainers(
      {{lookup1, true}, {lookup2, true}, {obj, true}});
}

TEST_F(
    ExpressionContainersIteratorTest,
    NotAllChildrenYieldSameAffectedContainers) {
  // when not all child nodes affect the same set of containers, we should
  // expand the parent. This test just checks we get the expected order and that
  // the parent is indeed expanded to get this order
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1", {"object1"}},
          {"container2", {"object2"}},
          {"container3", {"object3"}},
          {"container4", {"object4"}},
      });
  const auto universe = buildUniverse();

  const Assignment assignment(universe->getContainers().getInitialAssignment());

  auto allContainers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{
          container(1), container(2), container(3), container(4)});
  auto sum1 = const_expr(0, *universe);
  inplace_add(
      sum1,
      object_lookup(
          makeObjectVector(
              PackerMap<entities::ObjectId, double>{}, 5, 4, *universe),
          allContainers,
          assignment)); // 20 (0 + 5 * 4))

  auto sum2 = 4 * variable(object(3), container(3), *universe, assignment);
  inplace_add(sum2, const_expr(0, *universe)); // 4 (4 + 0)

  auto sum3 = const_expr(10, *universe) + const_expr(3, *universe) +
      object_lookup(
                  makeObjectVector(
                      PackerMap<entities::ObjectId, double>{}, 5, 4, *universe),
                  std::make_shared<PackerSet<entities::ContainerId>>(),
                  assignment); // sum constants and empty
                               // lookup; 13 (10 + 3)

  auto lookup2 = object_lookup(
      makeObjectVector(
          PackerMap<entities::ObjectId, double>{}, 3, 4, *universe),
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(4)}),
      assignment); // 3 (1 * 3; since container 4 has one object in it)

  auto lookup3 = object_lookup(
      makeObjectVector(
          PackerMap<entities::ObjectId, double>{}, 2, 4, *universe),
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(2)}),
      assignment); // 2 (1 * 2)

  auto obj = (sum1 + sum2 + sum3) + (lookup2 + lookup3);

  auto objective = GlobalObjective::Builder{}
                       .addToObjective(0, obj, *universe)
                       .build(*universe);
  Context context;
  auto objectiveExpr = objective.getOnlyObjective();
  objectiveExpr->fullApply(TopToBottomEvaluator(context), assignment);
  objectiveExpr->init_unconstrained_bounds(context);

  const DescendingExpressionContainersTraversal descending(objective.getView());

  const std::vector<entities::ContainerId> descendingActual(
      descending.begin(), descending.end());
  const std::vector<entities::ContainerId> descendingExpected = {
      container(3), container(4), container(2), container(1)};
  EXPECT_EQ(descendingExpected, descendingActual);

  verifyIfNodesInSubgraphAffectSameContainers(
      {{sum1, true},
       {sum2, true},
       {sum3, true},
       {lookup2, true},
       {lookup3, true},
       {obj, false}});
}

CO_TEST_F(
    ExpressionContainersIteratorTest,
    ContainerOrderingWithAndWithoutTraversalConfig) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1", {"object1"}},
          {"container2", {}},
          {"container3", {"object2", "object3"}},
          {"container4", {}},
          {"container5", {}},
          {"container6", {}},
      });

  co_await addPartition(
      "partition1",
      entities::Map<std::string, std::vector<std::string>>{
          {"group1", {"object1"}},
          {"group2", {"object2"}},
          {"group3", {"object3"}}});

  co_await addScope(
      "scope",
      entities::Map<std::string, std::vector<std::string>>{
          {"scopeItem", {"container1"}}});

  const auto universe = buildUniverse();
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  const auto partitionId = ExpressionTestsBase::partitionId("partition1");
  const auto scopeId = ExpressionTestsBase::scopeId("scope");
  const auto dimensionId = ExpressionTestsBase::dimensionId("object_count");
  const auto scopeItemId =
      ExpressionTestsBase::scopeItemId(scopeId, "scopeItem");

  const auto op = object_partition(
      std::make_shared<const PartitionInfo>(*universe, partitionId),
      dimensionId,
      {});
  const auto lookup1 = object_partition_lookup(
      op,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{
              container(1), container(2), container(3)}),
      scopeId,
      scopeItemId,
      assignment); // value = 3, lb = 0, potential = 3
  lookup1->description = "lookup1";
  const auto lookup2 = object_partition_lookup(
      op,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{
              container(4), container(5), container(2)}),
      scopeId,
      scopeItemId,
      assignment); // value = 0, lb = 0, potential = 0
  lookup2->description = "lookup2";
  const auto ls1 = variable(object(1), container(1), *universe, assignment) +
      2 * variable(object(2), container(3), *universe, assignment) -
      2 *
          variable(
              object(3),
              container(5),
              *universe,
              assignment); // value =  3 ( 1 + 2 - 0), lb = -2, potential = 5
  ls1->description = "ls1";

  const auto ls2 =
      variable(object(1), container(6), *universe, assignment) -
      variable(
          object(2), container(3), *universe, assignment); // value = -1, lb =
                                                           // -1, potential = 0
  ls2->description = "ls2";

  auto objective = GlobalObjective::Builder{}
                       .addToObjective(0, lookup1, *universe)
                       .addToObjective(1, lookup2, *universe)
                       .addToObjective(2, ls1, *universe)
                       .addToObjective(3, ls2, *universe)
                       .build(*universe);

  Context context;
  objective.fullApply(TopToBottomEvaluator(context), assignment);
  objective.initUnconstrainedBounds();

  {
    // if pruneOptimalSubgraphs is false, we expect to first explore
    // container(2) since although the objective at tuple position 2 is optimal,
    // it is not pruned completely
    interface::HottestTraversalConfig config;
    config.pruneOptimalSubgraphs() = false;
    const DescendingExpressionContainersTraversal descending(
        objective.getView(), /*skipOptimalExpressions=*/true, config);
    const std::vector<entities::ContainerId> descendingExpected = {
        container(2),
        container(3),
        container(1),
        container(5),
        container(4),
    };

    const std::vector<entities::ContainerId> descendingActual(
        descending.begin(), descending.end());
    EXPECT_EQ(descendingExpected, descendingActual);
  }

  {
    // with default config, where pruneOptimalSubgraphs is true
    interface::HottestTraversalConfig config;
    config.pruneOptimalSubgraphs() = true;
    const DescendingExpressionContainersTraversal descending(
        objective.getView(), /*skipOptimalExpressions=*/true, config);
    const std::vector<entities::ContainerId> descendingExpected = {
        container(3),
        container(1),
        container(2),
        container(5),
        // note how container 4 is missing because lookup2 has zero potential
        // and skipOptimalExpressions is true
    };
    const std::vector<entities::ContainerId> descendingActual(
        descending.begin(), descending.end());
    EXPECT_EQ(descendingExpected, descendingActual);
  }

  verifyIfNodesInSubgraphAffectSameContainers(
      {{op, true}, {lookup1, true}, {lookup2, true}, {ls1, false}});
}

} // namespace facebook::rebalancer::packer::tests
