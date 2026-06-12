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

#include "algopt/rebalancer/solver/expressions/Rectangle.h"

#include "algopt/rebalancer/solver/expressions/LpEvaluator.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/PropertiesHelper.h"
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"

namespace {
constexpr std::string_view type = "Rectangle";
}

namespace facebook::rebalancer {

Rectangle::Rectangle(
    std::shared_ptr<Expression> expr,
    double lowerBound,
    double upperBound,
    std::shared_ptr<const entities::Universe> universe)
    : Transform(std::move(expr), std::move(universe)) {
  lowerBound_ = lowerBound;
  upperBound_ = upperBound;
  setInitialValue(perform_transform(getOnlyChildRawPtr()->getInitialValue()));
}

const std::string_view& Rectangle::getType() const {
  return type;
}

double Rectangle::perform_transform(double val) const {
  if (val >= lowerBound_ && val <= upperBound_) {
    return 1.0;
  }
  return 0.0;
}

bool Rectangle::inner_is_integer(Context& /* not used */) {
  return true;
}

void Rectangle::lpIntent(const LpEvaluator& evaluator, bool minimizing) {
  evaluator.computeLpIntent(getOnlyChild(), minimizing);
}

algopt::lp::Expression Rectangle::lp(
    const LpEvaluator& evaluator,
    bool minimizing,
    const interface::OptimalSolverSpec& configs) {
  if (!exprForLp_) {
    auto child = getOnlyChild();
    auto kEps = getUniverse().getPrecision().getTolerances().absolute().value();

    auto atLeastLb = step(child - (lowerBound_ - kEps), getUniversePtr());
    auto atMostUb =
        step((upperBound_ + kEps) - std::move(child), getUniversePtr());

    // if lowerBound_ <= child <= upperBound_, we want the value to be 1, else 0
    exprForLp_ =
        min(std::move(atLeastLb), std::move(atMostUb), getUniversePtr());
  }

  return evaluator.lp(exprForLp_.get(), minimizing, configs);
}

ExpressionProperties Rectangle::getProperties() const {
  ExpressionProperties properties;
  properties.properties()->emplace(
      "lower_bound", PropertiesHelper::makeDoubleValue(lowerBound_));
  properties.properties()->emplace(
      "upper_bound", PropertiesHelper::makeDoubleValue(upperBound_));
  return properties;
}

Bounds Rectangle::innerLowerAndUpperBounds(
    Context& /* unused */,
    const BoundConstraints& /* unused */) const {
  return {.lower_bound = 0, .upper_bound = 1};
}

std::string Rectangle::innerDigest(size_t /* maxChildren */) const {
  const auto transformExpr = getOnlyChildRawPtr();
  return fmt::format(
      "rectangle({}({}), {}, {})",
      transformExpr->getType(),
      transformExpr->value,
      lowerBound_,
      upperBound_);
}

} // namespace facebook::rebalancer
