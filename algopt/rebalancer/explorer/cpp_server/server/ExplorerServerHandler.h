#pragma once

#include "rebalancer/explorer/cpp_server/RebalancerExplorerHandlerBase.h"
#include "rebalancer/explorer/cpp_server/server/ModelServer.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

namespace facebook::rebalancer::explorer {

class ExplorerServerHandler : public RebalancerExplorerHandlerBase {
 public:
  explicit ExplorerServerHandler(std::shared_ptr<ModelServer> modelServer);

  folly::coro::Task<std::unique_ptr<HandleResponse>> co_getHandle(
      std::unique_ptr<HandleRequest> request) override;

  folly::coro::Task<std::unique_ptr<SandboxStatusResponse>> co_getSandboxStatus(
      std::unique_ptr<Handle> handle) override;

  folly::coro::Task<std::shared_ptr<const ModelServer>> getBackend(
      const Handle& /* handle */) override;

 private:
  std::shared_ptr<ModelServer> modelServer_;
};

} // namespace facebook::rebalancer::explorer
