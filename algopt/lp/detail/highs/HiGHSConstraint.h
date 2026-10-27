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

#ifdef REBALANCER_USE_HIGHS

#include "algopt/lp/detail/highs/highs.h"
#include "algopt/lp/generic/Constraint.h"

#include <atomic>

namespace facebook::algopt::lp::detail {

class HiGHSProblem;

// Deleting a row shifts every later one, so row_ is only a hint: HiGHSProblem
// confirms it by identity and searches toward row 0 when it is stale. The
// atomic only keeps concurrent const reads race-free; like the rest of the
// problem, adding or deleting constraints still needs external synchronization.
class HiGHSConstraint : public ConstraintImpl {
 public:
  HiGHSConstraint(const HiGHSProblem* problem, HighsInt row);

  double getDual() const override;
  double getSlack() const override;
  double getRHS() const override;

  // Possibly stale; see the class comment.
  HighsInt rowHint() const {
    return row_.load(std::memory_order_relaxed);
  }

 private:
  HighsInt getRow() const;
  double rhsAt(HighsInt row) const;

  const HiGHSProblem* problem_; // Non-owning pointer
  mutable std::atomic<HighsInt> row_;
};

} // namespace facebook::algopt::lp::detail

#endif
