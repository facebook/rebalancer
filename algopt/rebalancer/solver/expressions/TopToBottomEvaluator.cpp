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

#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"

#include "algopt/rebalancer/solver/expressions/Expression.h"

#include <stdexcept>

namespace facebook::rebalancer {
TopToBottomEvaluator::TopToBottomEvaluator(Context& context)
    : context_(context) {}

double TopToBottomEvaluator::evaluate(
    const Expression* /* expr */,
    const ChangeSet& /* changes */) const {
  throw std::runtime_error(
      "Evaluate with changes cannot be called with TopToBottomEvaluator");
}

double TopToBottomEvaluator::apply(
    Expression* expr,
    const Assignment& assignment) const {
  // by design, TopToBottomEvaluator always does a fullApply
  return expr->fullApply(*this, assignment);
}

bool TopToBottomEvaluator::isPositive(
    const Expression* /* expr */,
    const ChangeSet& /* changes */) const {
  throw std::runtime_error(
      "isPositive with changes cannot be called with TopToBottomEvaluator");
}

const folly::F14FastSet<Expression*>& TopToBottomEvaluator::getChangedChildren(
    const Expression* const /*expr*/) const {
  throw std::runtime_error(
      "getChangedChildren cannot be called with TopToBottomEvaluator");
}

Context& TopToBottomEvaluator::getContext() const {
  return context_;
}

} // namespace facebook::rebalancer
