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

#ifdef REBALANCER_USE_GUROBI

#include "algopt/lp/detail/gurobi/gurobi.h"
#include "algopt/lp/generic/Variable.h"

namespace facebook::algopt::lp::detail {

class GurobiVariable : public VariableImpl {
 public:
  explicit GurobiVariable(GRBVar variable, Type type);

  void setLB(double lb) override;
  void setUB(double ub) override;
  void setThreshold(double ub) override;
  void setPartition(int partitionId) override;

  double getValue() const override;
  double getReducedCost() const override;

  std::shared_ptr<ExpressionImpl> makeExpression(double coeff) const override;

  const GRBVar& get() const;

 private:
  GRBVar variable_;
};

} // namespace facebook::algopt::lp::detail

#endif
