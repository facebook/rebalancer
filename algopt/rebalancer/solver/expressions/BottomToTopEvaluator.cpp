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

#include "algopt/rebalancer/solver/expressions/BottomToTopEvaluator.h"

#include "algopt/rebalancer/solver/expressions/Expression.h"

namespace facebook::rebalancer {
BottomToTopEvaluator::BottomToTopEvaluator(Context& context)
    : context_(context) {}

double BottomToTopEvaluator::evaluate(
    const Expression* expr,
    const ChangeSet& /* changes */) const {
  auto cached = context_.val().get(expr->getId());
  if (!cached) {
    return expr->value;
  }
  return *cached;
}

double BottomToTopEvaluator::apply(
    Expression* expr,
    const Assignment& /* assignment */) const {
  auto cached = context_.apply().get(expr->getId());
  if (!cached) {
    return expr->value;
  }
  return *cached;
}

bool BottomToTopEvaluator::isPositive(
    const Expression* expr,
    const ChangeSet& changes) const {
  return expr->getPrecision().isStrictlyGtZero(evaluate(expr, changes));
}

const folly::F14FastSet<Expression*>& BottomToTopEvaluator::getChangedChildren(
    const Expression* const expr) const {
  return context_.changedChildren()[expr];
}

Context& BottomToTopEvaluator::getContext() const {
  return context_;
}

} // namespace facebook::rebalancer
