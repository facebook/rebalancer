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

#ifdef REBALANCER_USE_XPRESS

#include "algopt/lp/detail/xpress/XpressExpression.h"

#include <fmt/format.h>

namespace facebook::algopt::lp::detail {

XpressVariable::XpressVariable(dashoptimization::XPRBvar variable, Type type)
    : variable_(variable) {
  type_ = type;
}

void XpressVariable::setLB(double lb) {
  if (hasThreshold()) {
    throw std::runtime_error(
        fmt::format(
            "cannot set lower bound on a {} variable. Did you mean to set threshold?",
            type_));
  }
  variable_.setLB(lb);
}

void XpressVariable::setUB(double ub) {
  variable_.setUB(ub);
}

void XpressVariable::setThreshold(double threshold) {
  if (!hasThreshold()) {
    throw std::runtime_error(
        fmt::format("cannot set threshold on a {} variable", type_));
  }
  variable_.setLim(threshold);
}

double XpressVariable::getValue() const {
  return variable_.getSol();
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
