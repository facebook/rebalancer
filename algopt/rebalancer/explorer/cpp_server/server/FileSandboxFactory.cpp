/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "rebalancer/explorer/cpp_server/server/FileSandboxFactory.h"

#include "algopt/rebalancer/interface/serialization/Serializer.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/AssignmentProblem_types.h"

#include <fmt/core.h>
#include <folly/FileUtil.h>
#include <gflags/gflags.h>

#include <stdexcept>
#include <utility>

DEFINE_string(
    bundle_dir,
    "",
    "Base directory prepended to relative bundle ids. When set, a manifoldId "
    "like \"eightqueens.bundle\" is read from <bundle_dir>/eightqueens.bundle. "
    "Bundle ids that are already absolute (start with '/') are used as-is. "
    "Empty (the default) means the manifoldId is used verbatim as the path.");

namespace facebook::rebalancer::explorer {

namespace {

// Maps a bundle id (manifoldId) to the file path to read. A relative id is
// resolved under --bundle_dir so callers can pass a bare name like
// "eightqueens.bundle"; absolute ids (and the empty-base-dir default) are used
// verbatim. The id itself stays the SandboxStore cache key — only the on-disk
// lookup is rewritten here.
std::string resolveBundlePath(const std::string& manifoldId) {
  if (FLAGS_bundle_dir.empty() || manifoldId.empty() ||
      manifoldId.front() == '/') {
    return manifoldId;
  }
  std::string base = FLAGS_bundle_dir;
  while (base.size() > 1 && base.back() == '/') {
    base.pop_back();
  }
  return fmt::format("{}/{}", base, manifoldId);
}

} // namespace

folly::coro::Task<std::shared_ptr<ModelServer>> FileSandboxFactory::create(
    std::string manifoldId) {
  const auto path = resolveBundlePath(manifoldId);
  std::string content;
  if (!folly::readFile(path.c_str(), content)) {
    throw std::runtime_error(
        fmt::format("Unable to read bundle from {}", path));
  }
  // Bundles on disk use the same format as Manifold (zstd-compressed Binary).
  auto bundle =
      interface::Serializer::deserializeBinaryZstd<interface::Bundle>(content);
  co_return std::make_shared<ModelServer>(std::move(bundle));
}

} // namespace facebook::rebalancer::explorer
