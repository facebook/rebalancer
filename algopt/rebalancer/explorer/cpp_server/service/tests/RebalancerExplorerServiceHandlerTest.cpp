// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/service/fb/RebalancerExplorerServiceHandler.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <folly/coro/BlockingWait.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

using namespace facebook::rebalancer::explorer;

namespace {

struct UnloadEventFields {
  std::string eventType;
  std::string manifoldId;
  std::string clientId;
  int64_t maxInterCallGapSeconds{0};
  int64_t configuredTtlSeconds{0};
  int64_t sandboxLifetimeSeconds{0};
  int64_t callCount{0};
  std::string unloadReason;

  bool operator==(const UnloadEventFields&) const = default;
};

class FakeUnloadEventLogger {
 public:
  void setEventType(std::string value) {
    fields.eventType = std::move(value);
  }
  void setManifoldID(std::string value) {
    fields.manifoldId = std::move(value);
  }
  void setClientID(std::string value) {
    fields.clientId = std::move(value);
  }
  void setMaxInterCallGapSeconds(int64_t value) {
    fields.maxInterCallGapSeconds = value;
  }
  void setConfiguredTtlSeconds(int64_t value) {
    fields.configuredTtlSeconds = value;
  }
  void setSandboxLifetimeSeconds(int64_t value) {
    fields.sandboxLifetimeSeconds = value;
  }
  void setCallCount(int64_t value) {
    fields.callCount = value;
  }
  void setUnloadReason(std::string value) {
    fields.unloadReason = std::move(value);
  }

  UnloadEventFields fields;
};

TEST(RebalancerExplorerServiceHandlerTest, PopulatesUnloadEventLoggerFields) {
  FakeUnloadEventLogger logger;

  detail::populateUnloadEventLogger(
      logger,
      {
          .manifoldId = "manifold_id",
          .clientId = "client_id",
          .maxInterCallGap = std::chrono::seconds(11),
          .configuredTtl = std::chrono::seconds(22),
          .lifetime = std::chrono::seconds(33),
          .callCount = 44,
          .reason = SandboxDropReason::kIdleTtlExpiry,
      });

  const UnloadEventFields expected{
      .eventType = "unload",
      .manifoldId = "manifold_id",
      .clientId = "client_id",
      .maxInterCallGapSeconds = 11,
      .configuredTtlSeconds = 22,
      .sandboxLifetimeSeconds = 33,
      .callCount = 44,
      .unloadReason = "idle_ttl_expiry",
  };
  EXPECT_EQ(expected, logger.fields);
}

TEST(RebalancerExplorerServiceHandlerTest, PopulatesShutdownUnloadReason) {
  FakeUnloadEventLogger logger;

  detail::populateUnloadEventLogger(
      logger, {.reason = SandboxDropReason::kShutdown});

  EXPECT_EQ("shutdown", logger.fields.unloadReason);
}

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

} // namespace
