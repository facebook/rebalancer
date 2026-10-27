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

#include "algopt/lp/detail/xpress/XpressConstraint.h"

#include "algopt/lp/detail/xpress/XpressProblem.h"

#include <stdexcept>

#ifdef REBALANCER_USE_XPRESS

#include <fmt/format.h>

namespace facebook::algopt::lp::detail {

XpressConstraint::XpressConstraint(
    dashoptimization::XPRBctr constraint,
    const XpressProblem* problem)
    : constraint_(constraint), problem_(problem) {}

dashoptimization::XPRBctr& XpressConstraint::get() {
  liveOrThrow("get");
  return constraint_;
}

void XpressConstraint::markDeleted() {
  deleted_ = true;
}

const dashoptimization::XPRBctr& XpressConstraint::liveOrThrow(
    std::string_view accessor) const {
  // delCtr() frees the BCL row, so constraint_ would dangle.
  if (deleted_) {
    throw std::runtime_error(
        fmt::format("{}() called on a deleted constraint", accessor));
  }
  return constraint_;
}

double XpressConstraint::getDual() const {
  const auto& constraint = liveOrThrow("getDual");
  // Under mipOptimize() BCL reports a dual of 0 for every row. Returning that
  // 0 would be far worse than throwing: a Benders cut or a column-generation
  // price built from a silently-zero dual is wrong without ever looking wrong.
  if (!problem_->hasLpSensitivity()) {
    throw std::runtime_error(
        "getDual() requires an optimal lpOptimize() solve "
        "with the model unchanged since; it was not solved to optimality, was "
        "modified after the solve, or has discrete entities, quadratic terms, "
        "native constraints or start values and went through mipOptimize(), "
        "where BCL reports every dual as 0");
  }
  return constraint.getDual();
}

double XpressConstraint::getSlack() const {
  return liveOrThrow("getSlack").getSlack();
}

double XpressConstraint::getRHS() const {
  return liveOrThrow("getRHS").getRHS();
}

} // namespace facebook::algopt::lp::detail

#endif
