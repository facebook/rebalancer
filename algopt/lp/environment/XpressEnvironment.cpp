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

#ifdef REBALANCER_USE_XPRESS

#include <fmt/core.h>

#include <filesystem>
#include <memory>
#include <string_view>

namespace fs = std::filesystem;

namespace facebook::algopt::lp {

namespace {
constexpr int DO_NOT_OVERWRITE_ENV_VAR = 0;
} // namespace

std::shared_ptr<XpressEnvironment> XpressEnvironment::self_ = nullptr;

bool XpressEnvironment::isInitialized() {
  return self_ != nullptr;
}

void XpressEnvironment::makeFromLicenseFile(
    std::string_view license_file_path) {
  if (self_) {
    throw std::runtime_error("XpressEnvironment already initialized");
  }

  // Set XPRESS env variable only if it's not already set.
  setenv("XPRESS", license_file_path.data(), DO_NOT_OVERWRITE_ENV_VAR);

  // If the file does not exist, we'll throw a runtime error. Otherwise, the
  // Xpress library will exit the process when invoked, not giving the
  // program a chance to handle the exception.

  // Check that the file set as XPRESS env variable exists.
  const auto raw_filepath = getenv("XPRESS");
  fs::path filepath;
  try {
    filepath = fs::canonical(raw_filepath);
  } catch (const fs::filesystem_error&) {
    throw std::runtime_error(
        fmt::format("xpress license file not found at {}", raw_filepath));
  }
  if (!fs::is_regular_file(filepath)) {
    throw std::runtime_error(
        fmt::format("xpress license file not found at {}", filepath.string()));
  }

  // Check the license here, where failure is a return code. Left to BCL, the
  // check happens on the first XPRBprob and exits the process on failure.
  // Nonzero includes 32, a community-license fallback that cannot solve
  // production-sized models.
  auto env = std::shared_ptr<XpressEnvironment>(new XpressEnvironment);
  if (const int rc = XPRSinit(nullptr); rc != 0) {
    char msg[512] = {};
    XPRSgetlicerrmsg(msg, sizeof(msg));
    throw std::runtime_error(
        fmt::format("xpress license check failed ({}): {}", rc, msg));
  }
  self_ = std::move(env);
}

std::shared_ptr<XpressEnvironment> XpressEnvironment::get() {
  if (!self_) {
    throw std::runtime_error(notInitializedMsg.data());
  }
  return self_;
}

XpressEnvironment::~XpressEnvironment() {
  // prevents a memory leak detected by LeakSanitizer
  XPRSfree();
}

} // namespace facebook::algopt::lp

#endif // USE_XPRESS
