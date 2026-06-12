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

#include "algopt/rebalancer/algopt_common/AssociativeMap.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"

namespace facebook::rebalancer {

class SumOverThreshold : public Expression {
 public:
  explicit SumOverThreshold(
      std::shared_ptr<Expression> threshold,
      const std::vector<std::shared_ptr<Expression>>& values,
      bool square,
      std::shared_ptr<const entities::Universe> universe);

  AbstractContainer<ObjectPotential> getObjectPotentials(
      bool descending) const override;

  using Expression::evaluate;
  virtual double evaluate(
      const BottomToTopEvaluator& evaluator,
      const ChangeSet& changes) const override;

  using Expression::lp;
  virtual algopt::lp::Expression lp(
      const LpEvaluator& evaluator,
      bool minimizing,
      const interface::OptimalSolverSpec& configs) override;

  virtual void lpIntent(const LpEvaluator& evaluator, bool minimizing) override;

  virtual const std::string_view& getType() const override;

  double getChildCoefficient(Expression* child) const override;

  void updateEquivalenceSets(EquivalenceSets& equivSets) const override;

 private:
  virtual double innerFullApply(
      const TopToBottomEvaluator& evaluator,
      const Assignment& assignment) override;

  virtual double innerPartialApply(
      const BottomToTopEvaluator& evaluator,
      const Assignment& assignment,
      const ChangeSet& changes) override;

  virtual Bounds innerLowerAndUpperBounds(
      Context& context,
      const BoundConstraints& bc) const override;

  double evaluate_collection(double threshold) const;
  double evaluate_single(double value, double threshold) const;
  // Rebuilds `values` and `collection` from every child using `valueFn`,
  // and returns the snapped evaluation at the threshold's value.
  template <typename ValueFn>
  double populateAndEvaluate(ValueFn&& valueFn);

  std::string innerDigest(size_t maxChildren = 10) const override;

  struct Node {
    double sum;
    double sum_squares;
    int count;
  };

  PackerMap<Expression*, double> values;
  bool squares;
  algopt::AssociativeMap<std::pair<double, Expression*>, Node> collection;
  std::shared_ptr<Expression> threshold_expr;
};
} // namespace facebook::rebalancer
