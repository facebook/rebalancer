// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/service/SandboxStore.h"

#include <folly/coro/BlockingWait.h>
#include <folly/coro/Sleep.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace facebook::rebalancer::explorer {

struct FakeSandbox {};

enum class FakeSandboxStatus { NOT_LOADED, LOADING, LOADED };

class FakeSandboxFactory {
 public:
  folly::coro::Task<std::shared_ptr<FakeSandbox>> create(std::string) {
    totalLoads_.fetch_add(1);
    const int current = concurrentLoads_.fetch_add(1) + 1;
    // Atomic fetch-max so a concurrent peak is never lost (no std fetch_max).
    int prevPeak = peakConcurrentLoads_.load();
    while (current > prevPeak &&
           !peakConcurrentLoads_.compare_exchange_weak(prevPeak, current)) {
    }
    co_await folly::coro::sleep(loadDelay_);
    concurrentLoads_.fetch_sub(1);
    co_return std::make_shared<FakeSandbox>();
  }

  // The store owns the factory privately, so counters are static to let tests
  // observe them. Call resetCounters() at the start of each test.
  static void resetCounters() {
    concurrentLoads_.store(0);
    peakConcurrentLoads_.store(0);
    totalLoads_.store(0);
  }

  static int peakConcurrentLoads() {
    return peakConcurrentLoads_.load();
  }

  static int totalLoads() {
    return totalLoads_.load();
  }

 private:
  std::chrono::milliseconds loadDelay_{10};
  static inline std::atomic<int> concurrentLoads_{0};
  static inline std::atomic<int> peakConcurrentLoads_{0};
  static inline std::atomic<int> totalLoads_{0};
};

using TestSandboxStore =
    SandboxStore<FakeSandboxFactory, FakeSandboxStatus, FakeSandbox>;

constexpr int kMaxConcurrentLoads = 2;

// Polls detached async work with a safety bound.
template <typename Predicate>
bool waitUntil(Predicate done) {
  constexpr int kMaxPolls = 500; // 500 * 10ms = 5s safety bound.
  for (int polls = 0; polls < kMaxPolls; ++polls) {
    if (done()) {
      return true;
    }
    // No condition variable is exposed for detached loads.
    // @lint-ignore CLANGTIDY facebook-hte-BadCall-sleep_for
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return done();
}

bool waitUntilLoaded(TestSandboxStore& store, const std::string& id) {
  return waitUntil([&]() {
    return folly::coro::blockingWait(store.getStatus(id)) ==
        FakeSandboxStatus::LOADED;
  });
}

FakeSandboxStatus statusOf(TestSandboxStore& store, const std::string& id) {
  return folly::coro::blockingWait(store.getStatus(id));
}

// Short TTL expires within tests; long TTL does not.
constexpr auto kShortTtl = std::chrono::milliseconds(20);
constexpr auto kLongTtl = std::chrono::seconds(10);
constexpr auto kPastShortTtl = std::chrono::milliseconds(100);

// Drive eviction without waiting for the background sweep.
void sleepThenSweep(TestSandboxStore& store) {
  // @lint-ignore CLANGTIDY facebook-hte-BadCall-sleep_for
  std::this_thread::sleep_for(kPastShortTtl);
  store.dropInactiveSandboxesForTesting();
}

TEST(SandboxStoreTest, CapsConcurrentLoads) {
  FakeSandboxFactory::resetCounters();
  TestSandboxStore store(kDefaultInactiveSandboxTimeout, kMaxConcurrentLoads);

  const std::vector<std::string> ids = {
      "sandbox_a", "sandbox_b", "sandbox_c", "sandbox_d"};
  for (const auto& id : ids) {
    store.startLoadSandbox(id);
  }

  const bool allLoaded = waitUntil([&]() {
    for (const auto& id : ids) {
      if (folly::coro::blockingWait(store.getStatus(id)) !=
          FakeSandboxStatus::LOADED) {
        return false;
      }
    }
    return true;
  });

  ASSERT_TRUE(allLoaded);
  // Shows the semaphore limits without serializing.
  EXPECT_EQ(FakeSandboxFactory::peakConcurrentLoads(), kMaxConcurrentLoads);
}

TEST(SandboxStoreTest, DedupesConcurrentLoadsOfSameId) {
  FakeSandboxFactory::resetCounters();
  TestSandboxStore store(kDefaultInactiveSandboxTimeout, kMaxConcurrentLoads);

  // Same-id dogpiles must load once.
  constexpr int kDogpile = 8;
  for (int i = 0; i < kDogpile; ++i) {
    store.startLoadSandbox("sandbox_x");
  }

  const bool loaded = waitUntil([&]() {
    return folly::coro::blockingWait(store.getStatus("sandbox_x")) ==
        FakeSandboxStatus::LOADED;
  });

  ASSERT_TRUE(loaded);
  EXPECT_EQ(1, FakeSandboxFactory::totalLoads());
}

TEST(SandboxStoreTest, InactiveSandboxTtlFromRequestParsesRequestTtl) {
  HandleRequest request;

  EXPECT_EQ(
      kDefaultInactiveSandboxTimeout, inactiveSandboxTtlFromRequest(request));

  request.ttlSeconds() = 7;
  EXPECT_EQ(std::chrono::seconds(7), inactiveSandboxTtlFromRequest(request));

  request.ttlSeconds() = 0;
  EXPECT_EQ(
      kDefaultInactiveSandboxTimeout, inactiveSandboxTtlFromRequest(request));

  request.ttlSeconds() = -1;
  EXPECT_EQ(
      kDefaultInactiveSandboxTimeout, inactiveSandboxTtlFromRequest(request));

  request.ttlSeconds() = std::numeric_limits<int64_t>::max();
  EXPECT_EQ(kMaxInactiveSandboxTtl, inactiveSandboxTtlFromRequest(request));
}

TEST(SandboxStoreTest, EvictsUsingPerSandboxTtl) {
  FakeSandboxFactory::resetCounters();
  TestSandboxStore store(kDefaultInactiveSandboxTimeout);

  store.startLoadSandbox("short", kShortTtl);
  store.startLoadSandbox("long", kLongTtl);
  ASSERT_TRUE(waitUntilLoaded(store, "short"));
  ASSERT_TRUE(waitUntilLoaded(store, "long"));

  sleepThenSweep(store);

  // Only the short-TTL sandbox expired.
  EXPECT_EQ(FakeSandboxStatus::NOT_LOADED, statusOf(store, "short"));
  EXPECT_EQ(FakeSandboxStatus::LOADED, statusOf(store, "long"));
}

TEST(SandboxStoreTest, TtlIsRaisedNotLowered) {
  FakeSandboxFactory::resetCounters();
  TestSandboxStore store(kDefaultInactiveSandboxTimeout);

  // Later longer TTL keeps it alive.
  store.startLoadSandbox("raised", kShortTtl);
  ASSERT_TRUE(waitUntilLoaded(store, "raised"));
  store.startLoadSandbox("raised", kLongTtl);

  // Later shorter TTL must not shorten lifetime.
  store.startLoadSandbox("kept", kLongTtl);
  ASSERT_TRUE(waitUntilLoaded(store, "kept"));
  store.startLoadSandbox("kept", kShortTtl);

  sleepThenSweep(store);

  EXPECT_EQ(FakeSandboxStatus::LOADED, statusOf(store, "raised"));
  EXPECT_EQ(FakeSandboxStatus::LOADED, statusOf(store, "kept"));
}

TEST(SandboxStoreTest, FallsBackToDefaultTtlWhenUnset) {
  FakeSandboxFactory::resetCounters();
  // Unset TTL uses the store default.
  TestSandboxStore store(kShortTtl);

  store.startLoadSandbox("no_ttl");
  ASSERT_TRUE(waitUntilLoaded(store, "no_ttl"));

  sleepThenSweep(store);

  EXPECT_EQ(FakeSandboxStatus::NOT_LOADED, statusOf(store, "no_ttl"));
}

} // namespace facebook::rebalancer::explorer
