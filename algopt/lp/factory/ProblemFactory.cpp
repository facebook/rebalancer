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

#include "algopt/lp/factory/ProblemFactory.h"

#include "algopt/lp/detail/simplify/ProblemSimplifier.h"
#include "algopt/lp/fast/FastProblemImpl.h"
#ifdef REBALANCER_USE_GUROBI
#include "algopt/lp/detail/gurobi/GurobiProblem.h"
#endif
#ifdef REBALANCER_USE_XPRESS
#include "algopt/lp/detail/xpress/XpressProblem.h"
#endif
#ifdef REBALANCER_USE_HIGHS
#include "algopt/lp/detail/highs/HiGHSProblem.h"
#endif

#include <folly/logging/xlog.h>

#include <array>
#include <exception>
#include <memory>
// Used by the "not available in this build" throws below, which are dead in a
// build that compiles in every backend — the only config clang-tidy sees.
// @lint-ignore CLANGTIDY facebook-unused-include-check
#include <stdexcept>
#include <string_view>

namespace facebook::algopt::lp {

namespace {

// Loads a backend directly, with no fallback. The fallback chain below is built
// from these rather than from the public make*Problem() entry points, which
// would recurse back into their own chains.
Problem loadXpress() {
#ifdef REBALANCER_USE_XPRESS
#ifndef REBALANCER_OSS_BUILD
  lazyLoadXpress();
#else
  if (!XpressEnvironment::isInitialized()) {
    throw std::runtime_error(XpressEnvironment::notInitializedMsg.data());
  }
#endif
  return Problem(std::make_unique<detail::XpressProblem>());
#else
  throw std::runtime_error("XPRESS is not available in this build");
#endif
}

Problem loadGurobi() {
#ifdef REBALANCER_USE_GUROBI
#ifndef REBALANCER_OSS_BUILD
  // Always call lazyLoadGurobi() - it's thread-safe via std::call_once
  // and will be a no-op if already initialized
  lazyLoadGurobi();
#else
  if (!GurobiEnvironment::isInitialized()) {
    throw std::runtime_error(GurobiEnvironment::notInitializedMsg.data());
  }
#endif
  return Problem(std::make_unique<detail::GurobiProblem>());
#else
  throw std::runtime_error("Gurobi is not available in this build");
#endif
}

Problem loadHiGHS() {
#ifdef REBALANCER_USE_HIGHS
  return Problem(std::make_unique<detail::HiGHSProblem>());
#else
  throw std::runtime_error("HiGHS is not available in this build");
#endif
}

struct Fallback {
  // Matches Problem::backendName(), so the requested backend can be skipped.
  std::string_view name;
  Problem (*load)();
};

[[maybe_unused]] constexpr Fallback kXpress{
    .name = "XPRESS",
    .load = &loadXpress};
[[maybe_unused]] constexpr Fallback kGurobi{
    .name = "GUROBI",
    .load = &loadGurobi};
[[maybe_unused]] constexpr Fallback kHiGHS{.name = "HIGHS", .load = &loadHiGHS};

// Backends tried, in order, when the requested one fails to load. The requested
// backend is skipped, so a chain is a global solver preference order rather
// than a per-backend successor. A backend that is not compiled in throws on
// load and is skipped like any other failure, so no chain needs an availability
// guard.
//
// Falling back is opt-in: an unconfigured build keeps the pre-existing
// behavior of failing the solve when the requested backend will not load.
//
// Build-time selection, highest precedence first:
//   -DREBALANCER_NO_SOLVER_FALLBACK
//       Never fall back. Highest precedence, so a target can opt out of a
//       chain it would otherwise inherit.
//   -DREBALANCER_SOLVER_FALLBACK_CHAIN=kXpress,kGurobi,kHiGHS
//       Explicit chain, in terms of the constants above. No spaces: the value
//       reaches the compiler as a single argument.
//   -DREBALANCER_SOLVER_FALLBACK_TO_HIGHS_ONLY
//       XPRESS -> HIGHS and GUROBI -> HIGHS; never swap one commercial solver
//       for the other.
#if defined(REBALANCER_NO_SOLVER_FALLBACK)
constexpr std::array<Fallback, 0> kFallbackChain{};
#elif defined(REBALANCER_SOLVER_FALLBACK_CHAIN)
constexpr std::array kFallbackChain{REBALANCER_SOLVER_FALLBACK_CHAIN};
#elif defined(REBALANCER_SOLVER_FALLBACK_TO_HIGHS_ONLY)
constexpr std::array kFallbackChain{kHiGHS};
#else
constexpr std::array<Fallback, 0> kFallbackChain{};
#endif

} // namespace

namespace detail {

Problem loadWithFallback(
    std::string_view solverName,
    const std::function<Problem()>& load) {
  try {
    return load();
  } catch (const std::exception& e) {
    // Logged as it happens rather than once at the end, so the reason each
    // backend was rejected survives even when a later one loads and the solve
    // goes on to succeed.
    XLOGF(ERR, "Failed to load solver {}: {}", solverName, e.what());
    for (const auto& [name, next] : kFallbackChain) {
      if (name == solverName) {
        continue;
      }
      try {
        auto problem = next();
        XLOGF(ERR, "Falling back from solver {} to {}", solverName, name);
        return problem;
      } catch (const std::exception& fallbackError) {
        XLOGF(
            ERR,
            "Fallback solver {} for {} also failed to load: {}",
            name,
            solverName,
            fallbackError.what());
      }
    }
    XLOGF(
        ERR,
        "No fallback loaded for solver {}; propagating its original failure",
        solverName);
    throw;
  }
}

} // namespace detail

Problem ProblemFactory::makeXpressProblem() {
  return detail::loadWithFallback("XPRESS", &loadXpress);
}

Problem ProblemFactory::makeGenericProblem(
    const std::function<Problem()>& factory) {
  return Problem(std::make_unique<detail::GenericProblemImpl>(factory));
}

Problem ProblemFactory::makeSimplifiedProblem(
    const std::function<Problem()>& factory) {
  return Problem(std::make_unique<detail::ProblemSimplifier>(factory));
}

Problem ProblemFactory::makeGurobiProblem() {
  return detail::loadWithFallback("GUROBI", &loadGurobi);
}

Problem ProblemFactory::makeFastProblem() {
  return Problem(std::make_unique<FastProblemImpl>());
}

// HiGHS anchors the fallback chain, so it has nothing to fall back to.
Problem ProblemFactory::makeHiGHSProblem() {
  return loadHiGHS();
}

} // namespace facebook::algopt::lp
