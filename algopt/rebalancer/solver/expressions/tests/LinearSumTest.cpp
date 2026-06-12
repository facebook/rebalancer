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
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"
#include "algopt/rebalancer/solver/iterators/ExpressionContainersIterator.h"
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class LinearSumTest : public ExpressionTestsBase {
 protected:
  void setUpDefaultAssignment() {
    constexpr int kNumContainers = 20;
    entities::Map<std::string, std::vector<std::string>> initialAssignment;
    for (const auto i : folly::irange(kNumContainers)) {
      initialAssignment[fmt::format("container{}", i)] = {
          fmt::format("object{}", i)};
    }
    setInitialAssignment(initialAssignment);
  }
};

TEST_F(LinearSumTest, Caching) {
  setUpDefaultAssignment();
  const auto universe = buildUniverse();
  const Assignment assignment(
      {{container(1), {object(0)}}, {container(2), {}}, {container(0), {}}});

  auto v0 = variable(object(0), container(0), universe, assignment);
  auto v1 = variable(object(0), container(1), universe, assignment); // 1
  auto v2 = variable(object(0), container(2), universe, assignment);

  auto sum3 = 1 + v0 + 3 * v1 + v2; // 4
  auto sum4 = 1 + 5 * v0 + 2 * v1 + 10 * v2; // 3
  auto sum5 = 1 + 2 * v0 + 2 * v1 + 4 * v2; // 3
  auto sum6 = std::make_shared<LinearSum>(
      universe,
      0,
      PackerMap<std::shared_ptr<Expression>, double>{
          {sum3, -11.0}, {sum4, -2.0}, {sum5, 10.0}});

  EXPECT_EQ(-11 * 4 + -2 * 3 + 10 * 3, apply(sum6, assignment));

  EXPECT_TRUE(descendingChildPotentialsAsExpected(
      *sum6,
      {(-2.0 * -15.0), (-11.0 * -2.0), (10.0 * 2.0)},
      std::vector<ExprPtr>{sum4, sum3, sum5}));

  Context context;
  Orchestrator orchestrator;
  orchestrator.init(
      {sum3.get(), sum4.get(), sum5.get()}, AffectedByChangeDecisionData(1, 3));
  // for bottom up evaluation, all the values will be first saved into
  // the context and then served from there
  auto changes = ObjectToNewContainer{{object(0), container(2)}};
  context.changes() = getChangeSet(changes, assignment);
  EXPECT_EQ(
      evaluate(sum3, changes, assignment),
      orchestrator.evaluate(sum3.get(), context));
  EXPECT_EQ(
      evaluate(sum4, changes, assignment),
      orchestrator.evaluate(sum4.get(), context));
  EXPECT_EQ(
      evaluate(sum5, changes, assignment),
      orchestrator.evaluate(sum5.get(), context));

  EXPECT_TRUE(context.val().get(sum4->getId()));
  EXPECT_EQ(11, *context.val().get(sum4->getId()));
  {
    // apply the move
    const double expectedValue = -11.0 * 2.0 + -2.0 * 11.0 + 10.0 * 5.0;
    EXPECT_NEAR(
        expectedValue,
        applyChanges(sum6, {{object(0), container(2)}}, assignment),
        1e-8);
    EXPECT_TRUE(descendingChildPotentialsAsExpected(
        *sum6,
        {(-11.0 * -4.0), (10.0 * 4.0), (-2.0 * -7.0)},
        std::vector<ExprPtr>{sum3, sum5, sum4}));
  }
}

TEST_F(LinearSumTest, EquivalenceSetsLinearSum) {
  setUpDefaultAssignment();
  const auto universe = buildUniverse();
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  auto o1c1 = variable(object(1), container(1), universe, initialAssignment);
  auto o1c2 = variable(object(1), container(2), universe, initialAssignment);
  auto o2c1 = variable(object(2), container(1), universe, initialAssignment);
  auto o2c2 = variable(object(2), container(2), universe, initialAssignment);
  auto o3c1 = variable(object(3), container(1), universe, initialAssignment);
  auto o3c2 = variable(object(3), container(2), universe, initialAssignment);
  auto sum = o1c1 + 2 * o1c2 + o2c1 + 2 * o2c2 + o3c1 + o3c2;

  EquivalenceSets equivalenceSets(*universe);
  updateEquivalenceSets(equivalenceSets, *sum);

  EXPECT_EQ(equivalenceSets.size(), 2);
  EXPECT_EQ(equivalenceSets.at(object(1)), equivalenceSets.at(object(2)));
  EXPECT_NE(equivalenceSets.at(object(2)), equivalenceSets.at(object(3)));
  EXPECT_NE(equivalenceSets.at(object(1)), equivalenceSets.at(object(3)));
}

TEST_F(LinearSumTest, LinearSumIsBinary) {
  setUpDefaultAssignment();
  const auto universe = buildUniverse();
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  auto t1 = variable(object(0), container(1), universe, initialAssignment);
  auto t2 = variable(object(0), container(0), universe, initialAssignment);
  auto linearsum = 20 + 4 * t1 - 2 * t2 + 4 * t1;
  linearsum /= 2;
  linearsum += t1;
  linearsum -= t2;

  Context context;
  EXPECT_FALSE(linearsum->is_binary(context));
  linearsum = t1;
  EXPECT_TRUE(linearsum->is_binary(context));
  linearsum = const_expr(1, universe);
  EXPECT_TRUE(linearsum->is_binary(context));
  linearsum = const_expr(0, universe);
  EXPECT_TRUE(linearsum->is_binary(context));
  linearsum = t1;
  EXPECT_TRUE(linearsum->is_binary(context));
}

TEST_F(LinearSumTest, LinearSumEqual) {
  setUpDefaultAssignment();
  const auto universe = buildUniverse();
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  auto t1 = variable(object(0), container(1), universe, initialAssignment);
  auto t2 = const_expr(0, universe);
  auto t3 = const_expr(1, universe);
  EXPECT_FALSE(t1 == 0);
  EXPECT_FALSE(t2 == 1);
  EXPECT_TRUE(t2 == 0);
  EXPECT_FALSE(t3 == 0);
  EXPECT_TRUE(t3 == 1);
}

TEST_F(LinearSumTest, ZeroOptimization) {
  setUpDefaultAssignment();
  const auto universe = buildUniverse();
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  auto t1 = variable(object(0), container(1), universe, initialAssignment);
  auto t2 = const_expr(0, universe);
  auto t3 = const_expr(1, universe);
  EXPECT_TRUE(0 * t1 == 0);
  EXPECT_TRUE(5.3 * t2 == 0);
  t1 *= 0;
  EXPECT_TRUE(t1 == 0);
}

TEST_F(LinearSumTest, LinearSumBoundsTests) {
  setUpDefaultAssignment();
  const auto universe = buildUniverse();
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  auto t1 = variable(object(0), container(1), universe, initialAssignment);
  auto t2 = variable(object(0), container(0), universe, initialAssignment);
  auto linearsum = 20 + 4 * t1 - 2 * t2 + 4 * t1;
  linearsum /= 2;
  linearsum += t1;
  linearsum -= t2;

  EXPECT_EQ(15, upper_bound(*linearsum));
  EXPECT_EQ(8, lower_bound(*linearsum));

  ASSERT_EQ(2, linearsum->children().size());
  // ensure that children's unconstrained bounds have also been computed and
  // memoized
  for (const auto& child : linearsum->children()) {
    EXPECT_EQ(0, lower_bound(*child));
    EXPECT_EQ(1, upper_bound(*child));
  }
}

TEST_F(LinearSumTest, AmplifiedSubToleranceChildChangesPropagate) {
  setUpDefaultAssignment();
  // Two-level sum:
  //   innerSum_i = 1.0 + kSmallFactor * v_i
  //   obj        = sum_i kLargeFactor * innerSum_i, for i in [0, kN)
  //
  // Flipping any v_i 0->1 moves innerSum_i by kSmallFactor (5e-11), below the
  // 1e-10 default tolerance, so a precision-gated child->parent notification
  // would not fire. That is incorrect: `obj`'s true change is
  // kN * kLargeFactor * kSmallFactor (5e-4). The consequences:
  //  - evaluate scores a strictly worsening move as neutral or better (better
  //    if `obj` is a higher-priority objective).
  //  - apply leaves obj->value stale.
  const auto universe = buildUniverse();
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());

  constexpr int kN = 10;
  constexpr double kSmallFactor = 5e-11; // below 1e-10 tolerance
  constexpr double kLargeFactor = 1e6;

  PackerMap<std::shared_ptr<Expression>, double> outerCoeffs;
  ObjectToNewContainer moves;
  moves.reserve(kN);
  for (const auto i : folly::irange(kN)) {
    auto v = variable(object(i), container(i + 1), universe, initialAssignment);
    outerCoeffs.emplace(1.0 + kSmallFactor * v, kLargeFactor);
    moves.emplace_back(object(i), container(i + 1));
  }
  // Explicit ctor keeps each innerSum as a distinct child
  auto obj = std::make_shared<LinearSum>(universe, 0.0, outerCoeffs);

  const auto initial = kN * kLargeFactor; // 1e7
  ASSERT_DOUBLE_EQ(initial, _apply(*obj, initialAssignment));

  const auto changes = getChangeSet(moves, initialAssignment);
  const auto expected =
      initial + kN * kLargeFactor * kSmallFactor; // 1e7 + 5e-4
  EXPECT_NEAR(expected, evaluate(*obj, changes), 1e-8);

  Context context;
  context.changes() = changes;
  EXPECT_NEAR(
      expected,
      _applyChanges(
          *obj, context, getModifiedAssignment(initialAssignment, moves)),
      1e-8);
  EXPECT_NEAR(expected, obj->value, 1e-8);
}

TEST_F(LinearSumTest, ContainerOrder) {
  /*
   *                   SUM=8
   *                /    |   \
   *              8     -10   5
   *            /        |     \
   *        MAX=1      MAX=0  MAX=0
   *        /  |       /  \     \
   *      v2=1 v3=1   v4  v1    v1
   *      c3   c1     c2  c3    c3
   * Desired order when NOT skipping optimal expressions: (c2, c3, c1). In this
   * case, child2 has the maximum potential initially, and both v1 and v4 have
   * zero potential. Although there is no priority among c2/c3 based
   * on potentials, we break ties in favor of expressions that have larger id.
   * Here v4 will have a larger id than v1, and hence the expected order is (c2,
   * c3, c1).
   *
   * Desired order when skipping optimal expressions: (c1, c3). In this case,
   * although child2 has the maximum potential initially, both v1 and v4 have
   * zero potential. Therefore, they will be skipped. Following that, Child1 has
   * the next max potential, and both its children v2 and v3 have potential
   * = 1. Due to deterministic tie-breaking in favor of expression with larger
   * id, the expected order is (c1, c3)
   */
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container1", {"object1"}},
          {"container2", {}},
          {"container3", {"object0"}}});
  const auto universe = buildUniverse();

  // false placement
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  auto v1 = variable(object(1), container(3), universe, initialAssignment);
  // true placement
  auto v2 = variable(object(0), container(3), universe, initialAssignment);
  // true placement
  auto v3 = variable(object(1), container(1), universe, initialAssignment);
  // false placement
  auto v4 = variable(object(1), container(2), universe, initialAssignment);

  Assignment assignment(
      {{container(1), {object(1)}}, {container(3), {object(0)}}});

  auto child1 = rebalancer::max(v2, v3, universe);
  auto child2 = rebalancer::max(v1, v4, universe);
  auto child3 = rebalancer::max({v1}, universe);
  auto linearsum = std::make_shared<LinearSum>(
      universe,
      0,
      PackerMap<std::shared_ptr<Expression>, double>{
          {child1, 8.0}, {child2, -10.0}, {child3, 5.0}});

  auto objective = GlobalObjective::Builder{}
                       .addToObjective(0, linearsum, universe)
                       .build(universe);

  Context context;
  auto objExpr = objective.getOnlyObjective();
  objExpr->init_unconstrained_bounds(context);
  _apply(*objExpr, assignment);

  // verify that expression tree confirms to the representation above
  EXPECT_EQ(linearsum->value, 8);
  auto children =
      linearsum->get_sorted_children(true); // get descending sorted children
  ASSERT_EQ(3, children.size());
  auto child = children.begin();
  EXPECT_EQ(0, child->first->value);
  EXPECT_EQ(-10, child->second);
  child++;
  EXPECT_EQ(1, child->first->value);
  EXPECT_EQ(8, child->second);
  child++;
  EXPECT_EQ(0, child->first->value);
  EXPECT_EQ(5, child->second);

  // verify DescendingChildPotentials
  EXPECT_TRUE(descendingChildPotentialsAsExpected(
      *linearsum,
      {10.0, 8.0, 0.0},
      std::vector<ExprPtr>{child2, child1, child3}));

  {
    // case1: optimal expressions are NOT skipped
    const DescendingExpressionContainersTraversal descending(
        objective.getView());
    std::vector<entities::ContainerId> order(
        descending.begin(), descending.end());
    ASSERT_EQ(3, order.size());
    EXPECT_EQ(container(2), order[0]);
    EXPECT_EQ(container(3), order[1]);
    EXPECT_EQ(container(1), order[2]);
  }

  {
    // case2: optimal expressions are skipped
    const DescendingExpressionContainersTraversal descending(
        objective.getView(), true /*skipOptimalExpressions*/);
    std::vector<entities::ContainerId> order(
        descending.begin(), descending.end());
    ASSERT_EQ(2, order.size());
    EXPECT_EQ(container(1), order[0]);
    EXPECT_EQ(container(3), order[1]);
  }

  {
    // apply the move where object(1) is moved to container(2) and object(0) is
    // moved to container(1)
    Context applyContext;
    assignment.moveTo(object(1), container(2));
    assignment.moveTo(object(0), container(1));
    applyContext.changes() = ChangeSet(
        {Change(object(1), container(1), -1),
         Change(object(1), container(2), 1),
         Change(object(0), container(3), -1),
         Change(object(0), container(1), 1)});
    _applyChanges(*linearsum, applyContext, assignment);

    // Note that although all the children of linearSum have the same potential,
    // the order below follows from the following deterministic tie-breaking
    // rule: 1) first prefer expressions that have non-zero uniquelyAffected +
    // directlyAffected containers, 2) if there is a tie, then prefer
    // expressions that have larger ids. Therefore, here child3 comes first
    // (because it has 1 uniquelyAffectedContainer c3), child2 and child1 are
    // tied (neither have any uniquelyAffectedContainers in their subgraph nor
    // any directlyAffectedContainers); child2 comes before child1 because it
    // has a larger id
    EXPECT_TRUE(descendingChildPotentialsAsExpected(
        *linearsum,
        {0.0, 0.0, 0.0},
        std::vector<ExprPtr>{child3, child2, child1}));
  }
}

} // namespace facebook::rebalancer::packer::tests
