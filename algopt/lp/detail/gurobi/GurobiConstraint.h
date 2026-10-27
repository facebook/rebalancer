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

#include "algopt/lp/environment/Environment.h" // NOLINT

#ifdef REBALANCER_USE_GUROBI

#pragma once

#include "algopt/lp/detail/gurobi/gurobi.h"
#include "algopt/lp/generic/Constraint.h"

#include <string_view>
#include <variant>

namespace facebook::algopt::lp::detail {

class GurobiConstraint : public ConstraintImpl {
 public:
  explicit GurobiConstraint(
      const std::variant<GRBConstr, GRBQConstr>& constraint);

  const std::variant<GRBConstr, GRBQConstr, GRBGenConstr>& get() const;
  std::variant<GRBConstr, GRBQConstr, GRBGenConstr>& get();

  // Replaces the stored handle with a general constraint (used by
  // setIndicatorOnConstraint to swap a linear constraint for an indicator).
  void setGenConstr(GRBGenConstr gc);

  // getDual() is defined only for a linear constraint; getSlack() and getRHS()
  // also cover quadratic ones. On anything else they throw.
  double getDual() const override;
  double getSlack() const override;
  double getRHS() const override;

 private:
  // Throws unless the stored handle is a plain linear constraint. Returns by
  // value: GRBConstr is a handle, and GRBConstr::get() is not const-qualified.
  GRBConstr linearOrThrow(std::string_view accessor) const;

  std::variant<GRBConstr, GRBQConstr, GRBGenConstr> constraint_;
};

} // namespace facebook::algopt::lp::detail

#endif
