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

#pragma once

#include "rebalancer/explorer/cpp_server/server/ModelServer.h"

#include <folly/coro/Task.h>

#include <memory>
#include <string>

namespace facebook::rebalancer::explorer {

// Builds a ModelServer from a serialized Bundle on disk (manifoldId is the
// file path; no Manifold in OSS). Used as a SandboxStore factory.
class FileSandboxFactory {
 public:
  static folly::coro::Task<std::shared_ptr<ModelServer>> create(
      std::string manifoldId);
};

} // namespace facebook::rebalancer::explorer
