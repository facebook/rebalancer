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

#include <memory>
#include <string_view>

namespace facebook::algopt::lp {

/// Manages XPRESS's environment. `XpressEnvironment::makeFromLicenseFile` must
/// be called before using XPRESS. The make call is not thread safe.
class XpressEnvironment {
 public:
  static void makeFromLicenseFile(std::string_view license_file_path);
  static std::shared_ptr<XpressEnvironment> get();
  static bool isInitialized();
  /// Returns true if the current license is the free community license,
  /// which is limited to problems with 5000 rows plus columns (Xpress error 120).
  static bool isCommunityLicense();
  static constexpr std::string_view notInitializedMsg =
      "XpressEnvironment is not initialized; please use XpressEnvironment::makeFromLicenseFile().";

  ~XpressEnvironment();

 private:
  XpressEnvironment() = default;
  XpressEnvironment(const XpressEnvironment&) = delete;
  XpressEnvironment& operator=(const XpressEnvironment&) = delete;
  XpressEnvironment(XpressEnvironment&&) = delete;
  XpressEnvironment& operator=(XpressEnvironment&&) = delete;

  static std::shared_ptr<XpressEnvironment> self_; // NOLINT
};

} // namespace facebook::algopt::lp

#endif
