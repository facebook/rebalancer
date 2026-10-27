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

#include "algopt/lp/detail/gurobi/GurobiVariable.h"

#ifdef REBALANCER_USE_GUROBI

#include "algopt/lp/detail/gurobi/GurobiExpression.h"

#include <fmt/format.h>
#include <folly/logging/xlog.h>

namespace facebook::algopt::lp::detail {

GurobiVariable::GurobiVariable(GRBVar variable, Type type)
    : variable_(variable) {
  type_ = type;
}

void GurobiVariable::setLB(double lb) try {
  if (hasThreshold()) {
    throw SolverError(
        fmt::format(
            "cannot set lower bound on a {} variable. Did you mean to set threshold?",
            fmt::underlying(type_)));
  }
  variable_.set(GRB_DoubleAttr::GRB_DoubleAttr_LB, lb);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

void GurobiVariable::setUB(double ub) try {
  variable_.set(GRB_DoubleAttr::GRB_DoubleAttr_UB, ub);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

void GurobiVariable::setThreshold(double threshold) try {
  if (!hasThreshold()) {
    throw SolverError(
        fmt::format(
            "cannot set threshold on a {} variable", fmt::underlying(type_)));
  }
  variable_.set(GRB_DoubleAttr::GRB_DoubleAttr_LB, threshold);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

void GurobiVariable::setPartition(int /* partitionId */) {
  /* Variable partition heuristic is known to cause memory leak:
  https://fb.workplace.com/groups/880827432818453/permalink/960441458190383/

  This heuristic is triggered automatically if partitionIds are set.
  Since, setting it does not offer any measurable benefit and risks memory leak,
  disabling it altogether.

  TODO: deprecate the option: enablePartitionHeuristic_ref() as the partitionIds
  will only be used by FReSH solver and not by any low level solvers.
  */
  // variable_.set(GRB_IntAttr_Partition, partitionId);
}

double GurobiVariable::getValue() const try {
  return variable_.get(GRB_DoubleAttr::GRB_DoubleAttr_X);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

double GurobiVariable::getReducedCost() const try {
  return variable_.get(GRB_DoubleAttr::GRB_DoubleAttr_RC);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

std::shared_ptr<ExpressionImpl> GurobiVariable::makeExpression(
    double coeff) const try {
  return std::make_shared<GurobiExpression>(coeff * variable_);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

const GRBVar& GurobiVariable::get() const {
  return variable_;
}

} // namespace facebook::algopt::lp::detail

#endif
