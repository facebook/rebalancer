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

#include "algopt/lp/generic/Constraint.h"

namespace facebook::algopt::lp::detail {

// Deliberately holds no row index: deleting a row shifts every later one, so
// HiGHSProblem finds a constraint's current row by identity instead.
class HiGHSConstraint : public ConstraintImpl {};

} // namespace facebook::algopt::lp::detail

#endif
