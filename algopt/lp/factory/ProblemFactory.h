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
#include "algopt/lp/generic/Problem.h"

#include <functional>
#include <string_view>

namespace facebook::algopt::lp {

namespace detail {

// Runs `load`, and if it throws, tries each backend in the build-time fallback
// chain until one loads, skipping the requested backend. `solverName` must
// match Problem::backendName(), e.g. "XPRESS". The chain is opt-in and is
// configured by the REBALANCER_*FALLBACK* macros documented in
// ProblemFactory.cpp; it is empty unless a build sets one, in which case
// `load`'s exception simply propagates. Where a chain reaching HiGHS is
// configured, a commercial solver that fails to load (most often an expired
// license) degrades to a slower solve rather than failing outright. If nothing
// in the chain loads, `load`'s exception propagates too, so a build
// misconfiguration is not masked.
//
// Internal to the make*Problem() implementations below; declared here only so
// tests can drive it with a loader that fails on demand.
Problem loadWithFallback(
    std::string_view solverName,
    const std::function<Problem()>& load);

} // namespace detail

class ProblemFactory {
 public:
  static Problem makeXpressProblem();
  static Problem makeGurobiProblem();
  static Problem makeHiGHSProblem();
  // Generic problem that can be realized and solved using factory where
  // factory is one of makeXpressProblem or makeGurobiProblem
  static Problem makeGenericProblem(const std::function<Problem()>& factory);
  // Generic problem + some transforms that simplify the MIP model by deleting
  // unnecessary variables and constrainys
  static Problem makeSimplifiedProblem(const std::function<Problem()>& factory);
  static Problem makeFastProblem();
};

} // namespace facebook::algopt::lp
