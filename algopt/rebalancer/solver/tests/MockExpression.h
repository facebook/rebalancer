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

#pragma once

#include "algopt/rebalancer/solver/expressions/Evaluator.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <folly/container/F14Set.h>
#include <folly/lang/Hint.h>

#include <memory>
#include <string_view>
#include <utility>

namespace facebook::rebalancer {

// used for saving context of graph built by MockExpression
struct TestContext {
  PackerSet<Expression*> alreadyEvaluated;
  PackerMap<Expression*, folly::F14FastSet<Expression*>>
      expectedChangedChildren;
  PackerMap<Expression*, PackerSet<Expression*>>
      expectedChangedChildrenByContainers;
  PackerMap<Expression*, double> newValue;
  void clear() {
    alreadyEvaluated.clear();
  }
};

class MockExpression : public Expression {
 public:
  MockExpression(
      double initialValue,
      double newValue,
      std::shared_ptr<TestContext> testContext);

  virtual double evaluate(
      const BottomToTopEvaluator& evaluator,
      const ChangeSet& changes) const override;

  virtual algopt::lp::Expression lp(
      const LpEvaluator& evaluator,
      bool minimizing,
      const interface::OptimalSolverSpec& configs) override;

  virtual double innerFullApply(
      const TopToBottomEvaluator& evaluator,
      const Assignment& assignment) override;

  virtual double innerPartialApply(
      const BottomToTopEvaluator& evaluator,
      const Assignment& assignment,
      const ChangeSet& changes) override;

  virtual std::optional<AffectedByChange> isAffectedByChange(
      const AffectedByChangeDecisionData& data) const override;

  virtual const std::string_view& getType() const override;

 protected:
  virtual Bounds innerLowerAndUpperBounds(
      Context& context,
      const BoundConstraints& bc) const override;

  std::shared_ptr<TestContext> testContext_;
};

class MockExpressionWithTempNode : public MockExpression {
 public:
  MockExpressionWithTempNode(
      double initialValue,
      double newValue,
      std::shared_ptr<TestContext> testContext);

  virtual algopt::lp::Expression lp(
      const LpEvaluator& evaluator,
      bool minimizing,
      const interface::OptimalSolverSpec& configs) override;
};

} // namespace facebook::rebalancer
