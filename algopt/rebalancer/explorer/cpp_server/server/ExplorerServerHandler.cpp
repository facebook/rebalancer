// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/server/ExplorerServerHandler.h"

#include "rebalancer/explorer/cpp_server/server/ModelServer.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

namespace facebook::rebalancer::explorer {

ExplorerServerHandler::ExplorerServerHandler(
    std::shared_ptr<ModelServer> modelServer)
    : modelServer_(std::move(modelServer)) {}

folly::coro::Task<std::shared_ptr<const ModelServer>>
ExplorerServerHandler::getBackend(const Handle& /*handle*/) {
  co_return modelServer_;
}

folly::coro::Task<std::unique_ptr<HandleResponse>>
ExplorerServerHandler::co_getHandle(
    std::unique_ptr<HandleRequest> /*request*/) {
  throw std::runtime_error("co_getHandle() is not supported in local server");
}

folly::coro::Task<std::unique_ptr<SandboxStatusResponse>>
ExplorerServerHandler::co_getSandboxStatus(std::unique_ptr<Handle> /*handle*/) {
  throw std::runtime_error(
      "co_getSandboxStatus() is not supported in local server");
}

} // namespace facebook::rebalancer::explorer
