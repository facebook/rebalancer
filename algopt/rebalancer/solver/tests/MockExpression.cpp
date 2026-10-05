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

#include "algopt/rebalancer/solver/tests/MockExpression.h"

#include "algopt/lp/generic/Expression.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/solver/expressions/BottomToTopEvaluator.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/LpEvaluator.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/utils/AffectedByChangeInfo.h"

#include <boost/iterator/iterator_facade.hpp>
#include <folly/container/MapUtil.h>
#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>

namespace facebook::rebalancer {

namespace {
const entities::Universe& dummyUniverse() {
  static const auto u = std::make_shared<const entities::Universe>();
  return *u;
}
} // namespace

// MockExpression is only used to test functionality of Orchestrator
// if initialValue not equals to newValue, the node changed value
MockExpression::MockExpression(
    double initialValue,
    double newValue,
    std::shared_ptr<TestContext> testContext)
    : Expression(dummyUniverse(), initialValue),
      testContext_(std::move(testContext)) {
  testContext_->newValue[(Expression*)this] = newValue;
}

double MockExpression::evaluate(
    const BottomToTopEvaluator& evaluator,
    [[maybe_unused]] const ChangeSet& changes) const {
  auto& changedChildren = evaluator.getChangedChildren((Expression*)this);
  auto expectedChangedChildrenPtr =
      folly::get_ptr(testContext_->expectedChangedChildren, (Expression*)this);
  if (expectedChangedChildrenPtr) {
    EXPECT_EQ(*expectedChangedChildrenPtr, changedChildren);
  } else {
    EXPECT_TRUE(changedChildren.empty());
  }
  // check if changedChildren have been evaluated before
  for (auto& child : changedChildren) {
    EXPECT_TRUE(testContext_->alreadyEvaluated.count(child));
  }
  // insert this node to alreadyEvaluated
  testContext_->alreadyEvaluated.insert((Expression*)this);
  // return newValue
  return testContext_->newValue[(Expression*)this];
}

double MockExpression::innerFullApply(
    const TopToBottomEvaluator& /* evaluator */,
    const Assignment& /* assignment */) {
  for (const auto& child : children()) {
    EXPECT_TRUE(testContext_->alreadyEvaluated.count(child.get()));
  }
  testContext_->alreadyEvaluated.insert((Expression*)this);
  return testContext_->newValue[(Expression*)this];
}

double MockExpression::innerPartialApply(
    const BottomToTopEvaluator& evaluator,
    const Assignment& /* assignment */,
    [[maybe_unused]] const ChangeSet& changes) {
  // test if the changed children are the same as expectedChangedChildren
  auto& changedChildren = evaluator.getChangedChildren((Expression*)this);
  auto expectedChangedChildrenPtr =
      folly::get_ptr(testContext_->expectedChangedChildren, (Expression*)this);
  if (expectedChangedChildrenPtr) {
    EXPECT_EQ(*expectedChangedChildrenPtr, changedChildren);
  } else {
    EXPECT_TRUE(changedChildren.empty());
  }
  // test if changed children evaluated
  for (auto& child : changedChildren) {
    EXPECT_TRUE(testContext_->alreadyEvaluated.count(child));
  }
  // mark this node as alreadyEvaluated
  testContext_->alreadyEvaluated.insert((Expression*)this);
  return testContext_->newValue[(Expression*)this];
}

algopt::lp::Expression MockExpression::lp(
    [[maybe_unused]] const LpEvaluator& evaluator,
    bool /* unused */,
    const interface::OptimalSolverSpec& /* unused */) {
  return algopt::lp::Expression();
}

std::optional<AffectedByChange> MockExpression::isAffectedByChange(
    const AffectedByChangeDecisionData& /*data*/) const {
  // return true if the node is leaf node
  return children().empty() ? std::make_optional(AffectedByChange(true))
                            : std::nullopt;
}

Bounds MockExpression::innerLowerAndUpperBounds(
    Context& /* unused */,
    const BoundConstraints& /* unused */) const {
  throw std::runtime_error(
      "innerLowerAndUpperBounds() is not implemented for MockExpression");
}

const std::string_view& MockExpression::getType() const {
  throw std::runtime_error("no type defined for MockExpression");
}

MockExpressionWithTempNode::MockExpressionWithTempNode(
    double initialValue,
    double newValue,
    std::shared_ptr<TestContext> testContext)
    : MockExpression(initialValue, newValue, std::move(testContext)) {}

// MockExpressionWithTempNode overrides lp function
// is used to test if orchestrator can evaluate temporary created nodes
algopt::lp::Expression MockExpressionWithTempNode::lp(
    const LpEvaluator& evaluator,
    bool minimizing,
    const interface::OptimalSolverSpec& configs) {
  // creat a temp node
  // mock the behavor of lp function in LinearSum and Max
  const std::shared_ptr<MockExpression> tempNode =
      std::make_shared<MockExpression>(1.0, 1.0, testContext_);
  // evaluate tempNode
  evaluator.lp(tempNode.get(), minimizing, configs);
  // tempNode should be evaluated
  EXPECT_TRUE(testContext_->alreadyEvaluated.count(tempNode.get()));
  // evaluate non-temp children
  return MockExpression::lp(evaluator, minimizing, configs);
}

} // namespace facebook::rebalancer
