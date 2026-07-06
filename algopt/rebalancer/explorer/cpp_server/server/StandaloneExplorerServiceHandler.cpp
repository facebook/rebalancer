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

#include "rebalancer/explorer/cpp_server/server/StandaloneExplorerServiceHandler.h"

#include "algopt/rebalancer/algopt_common/MemoryStats.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <utility>

namespace facebook::rebalancer::explorer {

StandaloneExplorerServiceHandler::StandaloneExplorerServiceHandler(
    std::string host,
    int32_t port)
    : host_(std::move(host)), port_(port) {}

folly::coro::Task<std::shared_ptr<const ModelServer>>
StandaloneExplorerServiceHandler::getBackend(const Handle& handle) {
  // Throws if not loaded yet; clients poll getSandboxStatus() until LOADED.
  co_return co_await store_.getSandbox(*handle.manifoldId());
}

folly::coro::Task<std::unique_ptr<HandleResponse>>
StandaloneExplorerServiceHandler::co_getHandle(
    std::unique_ptr<HandleRequest> request) {
  auto response = std::make_unique<HandleResponse>();
  auto& handle = *response->handle();
  // Start loading asynchronously; the client polls getSandboxStatus().
  store_.startLoadSandbox(
      *request->manifoldId(), inactiveSandboxTtlFromRequest(*request));
  handle.manifoldId() = *request->manifoldId();
  handle.host() = host_;
  handle.port() = port_;
  handle.taskId() = 0;
  co_return response;
}

folly::coro::Task<std::unique_ptr<SandboxStatusResponse>>
StandaloneExplorerServiceHandler::co_getSandboxStatus(
    std::unique_ptr<Handle> handle) {
  auto response = std::make_unique<SandboxStatusResponse>();
  response->status() = co_await store_.getStatus(*handle->manifoldId());
  co_return response;
}

folly::coro::Task<std::unique_ptr<ServerStatus>>
StandaloneExplorerServiceHandler::co_getServerStatus() {
  auto response = std::make_unique<ServerStatus>();

  const auto counts = store_.getSandboxCounts();
  response->loadingSandboxCount() = counts.loading;
  response->loadedSandboxCount() = counts.loaded;

  const auto memoryStats = algopt::MemoryStats::get();
  response->freeMemoryBytes() = memoryStats.freeMemoryBytes;
  response->usedMemoryBytes() = memoryStats.usedMemoryBytes;

  co_return response;
}

} // namespace facebook::rebalancer::explorer
