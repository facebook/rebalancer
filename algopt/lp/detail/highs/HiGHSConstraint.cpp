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

#include "algopt/lp/detail/highs/HiGHSConstraint.h"

#include "algopt/lp/detail/highs/HiGHSProblem.h"

#include <fmt/format.h>

#include <stdexcept>

namespace facebook::algopt::lp::detail {

HiGHSConstraint::HiGHSConstraint(const HiGHSProblem* problem, HighsInt row)
    : problem_(problem), row_(row) {}

HighsInt HiGHSConstraint::getRow() const {
  const HighsInt row =
      problem_->getRowIndex(*this, row_.load(std::memory_order_relaxed));
  row_.store(row, std::memory_order_relaxed);
  return row;
}

double HiGHSConstraint::getDual() const {
  const HighsInt row = getRow();
  const auto& solution = problem_->getHighs().getSolution();
  if (!solution.dual_valid) {
    // No dual solution: not solved yet, solve failed, or this is a MIP.
    throw std::runtime_error(
        fmt::format(
            "HiGHS has no dual solution for row {}; duals require a solved LP",
            row));
  }
  return solution.row_dual[row];
}

double HiGHSConstraint::getSlack() const {
  // HiGHS reports row activity, not slack. Xpress and Gurobi both define
  // slack as RHS minus activity, so derive it the same way.
  const HighsInt row = getRow();
  const auto& solution = problem_->getHighs().getSolution();
  if (!solution.value_valid) {
    throw std::runtime_error(
        fmt::format(
            "HiGHS has no primal solution for row {}; slack requires a solve",
            row));
  }
  return rhsAt(row) - solution.row_value[row];
}

double HiGHSConstraint::getRHS() const {
  return rhsAt(getRow());
}

double HiGHSConstraint::rhsAt(HighsInt row) const {
  // newConstraint() stores a one-sided row as [-inf, rhs] or [rhs, +inf], and
  // an equality as [rhs, rhs]; the finite bound is the RHS in both cases.
  const auto& lp = problem_->getHighs().getLp();
  const double upper = lp.row_upper_[row];
  return upper < kHighsInf ? upper : lp.row_lower_[row];
}

} // namespace facebook::algopt::lp::detail

#endif
