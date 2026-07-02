// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "rebalancer/explorer/cpp_server/server/ExplorerServerHandler.h"

#include <folly/coro/BlockingWait.h>
#include <gtest/gtest.h>

using namespace facebook::rebalancer::explorer;

// The single-problem local server serves one preloaded problem and has no
// SandboxStore, so server-level status is not applicable there.
TEST(ExplorerServerHandlerTest, GetServerStatusNotSupported) {
  ExplorerServerHandler handler(/*modelServer=*/nullptr);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      folly::coro::blockingWait(handler.co_getServerStatus()),
      "co_getServerStatus() is not supported in local server");
}
