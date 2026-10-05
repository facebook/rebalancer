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

#include "algopt/rebalancer/solver/summary/LabeledExpressions.h"

#include "algopt/rebalancer/solver/expressions/Expression.h"

#include <stdexcept>
#include <utility>

namespace facebook::rebalancer {

LabeledExpression::LabeledExpression(
    std::string name,
    ExprPtr expression,
    double weight)
    : name(std::move(name)),
      expression(std::move(expression)),
      weight(weight) {}

void LabeledExpressions::setRoot(ConstExprPtr expression) {
  root = expression;
}

LabeledExpressionSet::const_iterator LabeledExpressions::begin() const {
  return expressions.begin();
}

LabeledExpressionSet::const_iterator LabeledExpressions::end() const {
  return expressions.end();
}

LabeledExpressionSet::size_type LabeledExpressions::size() const {
  return expressions.size();
}

ConstExprPtr LabeledExpressions::getRoot() const {
  if (!root) {
    throw std::runtime_error("root not set in labeled expressions");
  }
  return root;
}

const LabeledExpressionSet& LabeledExpressions::getExpressions() const {
  return expressions;
}

void LabeledExpressions::addSingle(
    std::string name,
    ExprPtr expression,
    double weight) {
  expressions.push_back(
      std::make_shared<const LabeledExpression>(
          std::move(name), expression, weight));
}

void LabeledExpressions::addSingle(
    std::shared_ptr<const LabeledExpression> expr) {
  expressions.emplace_back(expr);
}

} // namespace facebook::rebalancer
