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

#include "algopt/lp/detail/xpress/XpressVariable.h"

#include <stdexcept>

#ifdef REBALANCER_USE_XPRESS

#include "algopt/lp/detail/xpress/XpressExpression.h"
#include "algopt/lp/detail/xpress/XpressProblem.h"
#include "algopt/lp/generic/SolverError.h"

#include <fmt/format.h>

namespace facebook::algopt::lp::detail {

XpressVariable::XpressVariable(
    dashoptimization::XPRBvar variable,
    Type type,
    XpressProblem* problem)
    : variable_(variable), problem_(problem) {
  type_ = type;
}

void XpressVariable::setLB(double lb) {
  if (hasThreshold()) {
    throw SolverError(
        fmt::format(
            "cannot set lower bound on a {} variable. Did you mean to set threshold?",
            fmt::underlying(type_)));
  }
  problem_->markModified();
  variable_.setLB(lb);
}

void XpressVariable::setUB(double ub) {
  problem_->markModified();
  variable_.setUB(ub);
}

void XpressVariable::setThreshold(double threshold) {
  if (!hasThreshold()) {
    throw SolverError(
        fmt::format(
            "cannot set threshold on a {} variable", fmt::underlying(type_)));
  }
  problem_->markModified();
  variable_.setLim(threshold);
}

double XpressVariable::getValue() const {
  return variable_.getSol();
}

double XpressVariable::getReducedCost() const {
  // Same reasoning as XpressConstraint::getDual(): mipOptimize() leaves BCL's
  // reduced costs at 0, and a silent 0 is worse than a throw.
  if (!problem_->hasLpSensitivity()) {
    throw std::runtime_error(
        "getReducedCost() requires an optimal lpOptimize() solve "
        "with the model unchanged since; it was not "
        "solved to optimality, was modified after the solve, or has discrete entities, "
        "quadratic terms, native constraints or start values and went "
        "through mipOptimize(), where BCL reports every reduced cost as 0");
  }
  return variable_.getRCost();
}

std::shared_ptr<ExpressionImpl> XpressVariable::makeExpression(
    double coeff) const {
  return std::make_shared<XpressExpression>(
      dashoptimization::XPRBexpr(coeff, variable_), 0.0);
}

const dashoptimization::XPRBvar& XpressVariable::get() const {
  return variable_;
}

} // namespace facebook::algopt::lp::detail

#endif
