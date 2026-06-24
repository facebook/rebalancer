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

// Lets each C++ example optionally emit its solved problem as a Bundle the
// Rebalancer Explorer can load via the --bundle_out gflag.
#include "algopt/rebalancer/interface/ProblemSolver.h"

#include <gflags/gflags.h>

DECLARE_string(bundle_out);

namespace facebook::rebalancer::examples {

// If --bundle_out is set, write the solver's bundle there. Call after solve().
void maybeSaveBundle(const interface::ProblemSolver& solver);

} // namespace facebook::rebalancer::examples
