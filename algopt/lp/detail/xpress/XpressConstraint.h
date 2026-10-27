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
#include "algopt/lp/generic/Constraint.h"

#include <string_view>

namespace facebook::algopt::lp::detail {

class XpressProblem;

class XpressConstraint : public ConstraintImpl {
 public:
  XpressConstraint(
      dashoptimization::XPRBctr constraint,
      const XpressProblem* problem);

  dashoptimization::XPRBctr& get();
  void markDeleted();

  double getDual() const override;
  double getSlack() const override;
  double getRHS() const override;

 private:
  const dashoptimization::XPRBctr& liveOrThrow(std::string_view accessor) const;

  dashoptimization::XPRBctr constraint_;
  // Non-owning; the problem outlives every constraint it hands out.
  const XpressProblem* problem_;
  bool deleted_ = false;
};

} // namespace facebook::algopt::lp::detail

#endif
