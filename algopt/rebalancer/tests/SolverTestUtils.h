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

#include "algopt/lp/environment/Environment.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"

#include <gtest/gtest.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <optional>
#include <stdexcept>

namespace facebook::algopt {

/// Returns the best available MIP solver package, preferring
/// Gurobi > XPRESS > HiGHS. Returns std::nullopt if none is available.
inline constexpr std::optional<rebalancer::interface::OptimalSolverPackage>
getAvailableMIPSolver() {
  if constexpr (isGurobiAvailable()) {
    return rebalancer::interface::OptimalSolverPackage::GUROBI;
  } else if constexpr (isXpressAvailable()) {
    return rebalancer::interface::OptimalSolverPackage::XPRESS;
  } else if constexpr (isHiGHSAvailable()) {
    return rebalancer::interface::OptimalSolverPackage::HIGHS;
  } else {
    return std::nullopt;
  }
}

inline constexpr const char* solverName(
    rebalancer::interface::OptimalSolverPackage solver) {
  switch (solver) {
    case rebalancer::interface::OptimalSolverPackage::GUROBI:
      return "Gurobi";
    case rebalancer::interface::OptimalSolverPackage::XPRESS:
      return "Xpress";
    case rebalancer::interface::OptimalSolverPackage::HIGHS:
      return "HiGHS";
    default:
      throw std::runtime_error("Unhandled OptimalSolverPackage");
  }
}

inline constexpr bool isSolverUnavailable(
    rebalancer::interface::OptimalSolverPackage solver) {
  switch (solver) {
    case rebalancer::interface::OptimalSolverPackage::GUROBI:
      return !isGurobiAvailable();
    case rebalancer::interface::OptimalSolverPackage::XPRESS:
      return !isXpressAvailable();
    case rebalancer::interface::OptimalSolverPackage::HIGHS:
      return !isHiGHSAvailable();
    default:
      throw std::runtime_error("Unhandled OptimalSolverPackage");
  }
}

inline auto testSolverPackages() {
  return ::testing::Values(
      rebalancer::interface::OptimalSolverPackage::GUROBI,
      rebalancer::interface::OptimalSolverPackage::XPRESS,
      rebalancer::interface::OptimalSolverPackage::HIGHS);
}

/// Returns an OptimalSolverSpec configured with the best available MIP solver.
/// Requires that at least one MIP solver is available (call
/// REBALANCER_SKIP_IF_NO_MIP_SOLVER() first).
inline rebalancer::interface::OptimalSolverSpec
makeAvailableOptimalSolverSpec() {
  rebalancer::interface::OptimalSolverSpec spec;
  auto solver = getAvailableMIPSolver();
  if (solver.has_value()) {
    spec.solverPackage() = *solver;
  }
  return spec;
}

} // namespace facebook::algopt

// clang-format off

#define REBALANCER_SKIP_IF_NO_MIP_SOLVER()                                   \
  do {                                                            \
    if constexpr (!facebook::algopt::isMipSolverAvailable()) {              \
      GTEST_SKIP() << "No MIP solver available";                  \
    }                                                             \
  } while (0)

// Coroutine-compatible variants: use co_return instead of return so they
// work inside CO_TEST_F (folly::coro::Task) test bodies.
#define REBALANCER_CO_SKIP_IF_NO_MIP_SOLVER()                                \
  do {                                                            \
    if constexpr (!facebook::algopt::isMipSolverAvailable()) {              \
      GTEST_MESSAGE_("No MIP solver available",                   \
                     ::testing::TestPartResult::kSkip);            \
      co_return;                                                  \
    }                                                             \
  } while (0)

#define REBALANCER_SKIP_IF_NO_GUROBI()                                       \
  do {                                                            \
    if constexpr (!facebook::algopt::isGurobiAvailable()) {                 \
      GTEST_SKIP() << "Gurobi not available";                     \
    }                                                             \
  } while (0)

#define REBALANCER_SKIP_IF_NO_XPRESS()                                       \
  do {                                                            \
    if constexpr (!facebook::algopt::isXpressAvailable()) {                 \
      GTEST_SKIP() << "XPRESS not available";                     \
    }                                                             \
  } while (0)

#define REBALANCER_SKIP_IF_NO_HIGHS()                                        \
  do {                                                            \
    if constexpr (!facebook::algopt::isHiGHSAvailable()) {                  \
      GTEST_SKIP() << "HiGHS not available";                      \
    }                                                             \
  } while (0)

#define REBALANCER_SKIP_IF_NO_MANIFOLD()                                     \
  do {                                                            \
    if constexpr (!facebook::algopt::isManifoldAvailable()) {               \
      GTEST_SKIP() << "Manifold not available in OSS build";      \
    }                                                             \
  } while (0)

// clang-format on
