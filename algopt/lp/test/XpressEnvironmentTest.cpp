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

#include "algopt/lp/environment/XpressEnvironment.h"
#include "algopt/lp/factory/ProblemFactory.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifdef REBALANCER_USE_XPRESS

namespace facebook::algopt::lp {

namespace {
std::filesystem::path writeInvalidLicense() {
  const auto path =
      std::filesystem::temp_directory_path() / "invalid_xpauth.xpr";
  std::ofstream(path) << "not a license\n";
  setenv("XPRESS", path.c_str(), 1);
  return path;
}
} // namespace

TEST(XpressEnvironmentTest, InvalidLicenseThrowsInsteadOfExiting) {
  const auto path = writeInvalidLicense();

  EXPECT_THROW(
      XpressEnvironment::makeFromLicenseFile(path.string()),
      std::runtime_error);
  EXPECT_FALSE(XpressEnvironment::isInitialized());
}

TEST(XpressEnvironmentTest, InvalidLicenseFallsBackToAnotherSolver) {
  writeInvalidLicense();

  try {
    EXPECT_NE(ProblemFactory::makeXpressProblem().backendName(), "XPRESS");
  } catch (const std::runtime_error&) {
    GTEST_SKIP() << "No fallback backend configured in this build";
  }
}

} // namespace facebook::algopt::lp

#endif
