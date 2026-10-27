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

#include "algopt/lp/detail/gurobi/GurobiConstraint.h"

#ifdef REBALANCER_USE_GUROBI

#include <fmt/format.h>

#include <stdexcept>

namespace facebook::algopt::lp::detail {

GurobiConstraint::GurobiConstraint(
    const std::variant<GRBConstr, GRBQConstr>& constraint)
    : constraint_(
          std::visit(
              [](auto c) -> std::variant<GRBConstr, GRBQConstr, GRBGenConstr> {
                return c;
              },
              constraint)) {}

const std::variant<GRBConstr, GRBQConstr, GRBGenConstr>& GurobiConstraint::get()
    const {
  return constraint_;
}

std::variant<GRBConstr, GRBQConstr, GRBGenConstr>& GurobiConstraint::get() {
  return constraint_;
}

void GurobiConstraint::setGenConstr(GRBGenConstr gc) {
  constraint_ = gc;
}

GRBConstr GurobiConstraint::linearOrThrow(std::string_view accessor) const {
  if (const auto* linear = std::get_if<GRBConstr>(&constraint_)) {
    return *linear;
  }
  throw std::runtime_error(
      fmt::format(
          "{}() is only defined for linear constraints; this is a {} constraint",
          accessor,
          std::holds_alternative<GRBQConstr>(constraint_) ? "quadratic"
                                                          : "general"));
}

double GurobiConstraint::getDual() const try {
  return linearOrThrow("getDual").get(GRB_DoubleAttr::GRB_DoubleAttr_Pi);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

double GurobiConstraint::getSlack() const try {
  if (const auto* quadratic = std::get_if<GRBQConstr>(&constraint_)) {
    return GRBQConstr(*quadratic).get(GRB_DoubleAttr::GRB_DoubleAttr_QCSlack);
  }
  return linearOrThrow("getSlack").get(GRB_DoubleAttr::GRB_DoubleAttr_Slack);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

double GurobiConstraint::getRHS() const try {
  if (const auto* quadratic = std::get_if<GRBQConstr>(&constraint_)) {
    return GRBQConstr(*quadratic).get(GRB_DoubleAttr::GRB_DoubleAttr_QCRHS);
  }
  return linearOrThrow("getRHS").get(GRB_DoubleAttr::GRB_DoubleAttr_RHS);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

} // namespace facebook::algopt::lp::detail

#endif
