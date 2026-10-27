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

#ifdef REBALANCER_USE_HIGHS

#include "algopt/lp/detail/highs/HiGHSVariable.h"

#include "algopt/lp/detail/highs/HiGHSExpression.h"

#include <fmt/format.h>

#include <stdexcept>

namespace facebook::algopt::lp::detail {

HiGHSVariable::HiGHSVariable(
    Highs* highs,
    HighsInt colIndex,
    const std::string& name,
    Type type)
    : highs_(highs), colIndex_(colIndex), name_(name) {
  type_ = type;
}

void HiGHSVariable::setLB(const double lb) {
  // Get current upper bound from the model
  const auto& lp = highs_->getLp();
  const double currentUb = lp.col_upper_[colIndex_];
  REBALANCER_HIGHS_CALL(highs_->changeColBounds(colIndex_, lb, currentUb));
}

void HiGHSVariable::setUB(const double ub) {
  // Get current lower bound from the model
  const auto& lp = highs_->getLp();
  const double currentLb = lp.col_lower_[colIndex_];
  REBALANCER_HIGHS_CALL(highs_->changeColBounds(colIndex_, currentLb, ub));
}

void HiGHSVariable::setThreshold(double threshold) {
  // For semi-continuous and semi-integer variables, the threshold is the
  // lower bound
  const auto& lp = highs_->getLp();
  const double currentUb = lp.col_upper_[colIndex_];
  REBALANCER_HIGHS_CALL(
      highs_->changeColBounds(colIndex_, threshold, currentUb));
}

double HiGHSVariable::getValue() const {
  return highs_->getSolution().col_value[colIndex_];
}

double HiGHSVariable::getReducedCost() const {
  const auto& solution = highs_->getSolution();
  if (!solution.dual_valid) {
    // No dual solution: not solved yet, solve failed, or this is a MIP.
    throw std::runtime_error(
        fmt::format(
            "HiGHS has no dual solution for column {}; reduced costs require a solved LP",
            name_));
  }
  return solution.col_dual[colIndex_];
}

const std::string& HiGHSVariable::getName() const {
  return name_;
}

std::shared_ptr<ExpressionImpl> HiGHSVariable::makeExpression(
    double coeff) const {
  return std::make_shared<HiGHSExpression>(*this, coeff);
}

HighsInt HiGHSVariable::getColIndex() const {
  return colIndex_;
}

Highs* HiGHSVariable::getHighs() const {
  return highs_;
}

} // namespace facebook::algopt::lp::detail

#endif
