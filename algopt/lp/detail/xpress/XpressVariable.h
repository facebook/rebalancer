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

#include "algopt/lp/environment/Environment.h" // NOLINT

#ifdef REBALANCER_USE_XPRESS

#include "algopt/lp/detail/xpress/xpress.h"
#include "algopt/lp/generic/Variable.h"

namespace facebook::algopt::lp::detail {

class XpressProblem;

class XpressVariable : public VariableImpl {
 public:
  XpressVariable(
      dashoptimization::XPRBvar variable,
      Type type,
      XpressProblem* problem);

  void setLB(double lb) override;
  void setUB(double ub) override;
  void setThreshold(double threshold) override;

  double getValue() const override;
  double getReducedCost() const override;

  std::shared_ptr<ExpressionImpl> makeExpression(double coeff) const override;

  const dashoptimization::XPRBvar& get() const;

 private:
  dashoptimization::XPRBvar variable_;
  // Non-owning; the problem outlives every variable it hands out.
  XpressProblem* problem_;
};

} // namespace facebook::algopt::lp::detail

#endif
