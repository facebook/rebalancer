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

#include "rebalancer/explorer/cpp_server/RebalancerExplorerHandlerBase.h"
#include "rebalancer/explorer/cpp_server/server/FileSandboxFactory.h"
#include "rebalancer/explorer/cpp_server/server/ModelServer.h"
#include "rebalancer/explorer/cpp_server/service/SandboxStore.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <cstdint>
#include <string>

namespace facebook::rebalancer::explorer {

// Standalone Explorer service: one process serves all endpoints at a fixed
// host:port (no SMC tier / ServiceRouter). Bundles load on demand from disk
// (manifoldId is a path) via a SandboxStore; clients poll getSandboxStatus
// until LOADED before querying.
class StandaloneExplorerServiceHandler : public RebalancerExplorerHandlerBase {
 public:
  // host/port are stamped into the Handle returned by getHandle().
  StandaloneExplorerServiceHandler(std::string host, int32_t port);

  folly::coro::Task<std::unique_ptr<HandleResponse>> co_getHandle(
      std::unique_ptr<HandleRequest> request) override;

  folly::coro::Task<std::unique_ptr<SandboxStatusResponse>> co_getSandboxStatus(
      std::unique_ptr<Handle> handle) override;

  folly::coro::Task<std::shared_ptr<const ModelServer>> getBackend(
      const Handle& handle) override;

 private:
  std::string host_;
  int32_t port_;
  SandboxStore<FileSandboxFactory, SandboxStatus, ModelServer> store_;
};

} // namespace facebook::rebalancer::explorer
