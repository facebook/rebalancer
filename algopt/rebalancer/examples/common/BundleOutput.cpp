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

#include "algopt/rebalancer/examples/common/BundleOutput.h"

#include <fmt/core.h>
#include <folly/logging/xlog.h>
#include <gflags/gflags.h>

DEFINE_string(
    bundle_out,
    "",
    "If set, write the solved problem+solution as a Bundle to this path (same "
    "format as Manifold: zstd-compressed Thrift Binary), loadable by the "
    "Rebalancer Explorer.");

namespace facebook::rebalancer::examples {

void maybeSaveBundle(const interface::ProblemSolver& solver) {
  if (FLAGS_bundle_out.empty()) {
    return;
  }
  solver.saveBundle(FLAGS_bundle_out);
  XLOG(INFO) << fmt::format("Wrote problem bundle to '{}'.", FLAGS_bundle_out);
}

} // namespace facebook::rebalancer::examples
