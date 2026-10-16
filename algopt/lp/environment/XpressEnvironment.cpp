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

bool XpressEnvironment::isCommunityLicense() {
  int status = 0;
  XPRSfeaturequery("Community", &status);
  return status == 1;
}

void XpressEnvironment::makeFromLicenseFile(
    std::string_view license_file_path) {
  if (self_) {
    throw std::runtime_error("XpressEnvironment already initialized");
  }

  self_ = std::shared_ptr<XpressEnvironment>(new XpressEnvironment);

  // Set XPAUTH_PATH only if it's not already set. XPAUTH_PATH is the current
  // Xpress license-path variable.
  setenv("XPAUTH_PATH", license_file_path.data(), DO_NOT_OVERWRITE_ENV_VAR);

  // If the file does not exist, we'll throw a runtime error. Otherwise, the
  // Xpress library will exit the process when invoked, not giving the
  // program a chance to handle the exception.

  // Check that the file set as XPAUTH_PATH exists.
  const auto raw_filepath = getenv("XPAUTH_PATH");
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

  // Initializes BCL/Optimizer immediately, so a bad license surfaces here
  // rather than lazily during the first solve.
  if (dashoptimization::XPRB::init() != 0) {
    throw std::runtime_error(
        fmt::format(
            "failed to initialize Xpress with license file {}",
            filepath.string()));
  }
}

std::shared_ptr<XpressEnvironment> XpressEnvironment::get() {
  if (!self_) {
    throw std::runtime_error(notInitializedMsg.data());
  }
  return self_;
}

XpressEnvironment::~XpressEnvironment() {
  // Symmetric with the explicit XPRB::init() call in makeFromLicenseFile();
  // also prevents a memory leak detected by LeakSanitizer.
  dashoptimization::XPRB::free();
}

} // namespace facebook::algopt::lp

#endif // USE_XPRESS
