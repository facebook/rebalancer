// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/service/fb/RebalancerExplorerServiceHandler.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <folly/coro/BlockingWait.h>
#include <gtest/gtest.h>

using namespace facebook::rebalancer::explorer;

// With no sandboxes loaded, getServerStatus reports zero counts and
// non-negative memory readings (parsed from /proc/meminfo).
TEST(RebalancerExplorerServiceHandlerTest, GetServerStatusEmptyStore) {
  RebalancerExplorerServiceHandler handler;
  const auto status = folly::coro::blockingWait(handler.co_getServerStatus());

  EXPECT_EQ(0, *status->loadingSandboxCount());
  EXPECT_EQ(0, *status->loadedSandboxCount());
  EXPECT_GE(*status->freeMemoryBytes(), 0);
  EXPECT_GE(*status->usedMemoryBytes(), 0);
}
