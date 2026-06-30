// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/service/SandboxStore.h"

#include <folly/coro/BlockingWait.h>
#include <folly/coro/Sleep.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
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

// Polls `done` until it holds or a safety bound elapses; returns whether it
// held.
template <typename Predicate>
bool waitUntil(Predicate done) {
  constexpr int kMaxPolls = 500; // 500 * 10ms = 5s safety bound.
  for (int polls = 0; polls < kMaxPolls; ++polls) {
    if (done()) {
      return true;
    }
    // Bounded poll for detached async loads to settle; the 5s cap keeps it
    // non-flaky, and the store exposes no condition variable to wait on.
    // @lint-ignore CLANGTIDY facebook-hte-BadCall-sleep_for
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return done();
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
  // Exactly the cap: matching it (vs a serialized peak of 1) shows the
  // semaphore both limits concurrency and still admits the parallelism.
  EXPECT_EQ(FakeSandboxFactory::peakConcurrentLoads(), kMaxConcurrentLoads);
}

TEST(SandboxStoreTest, DedupesConcurrentLoadsOfSameId) {
  FakeSandboxFactory::resetCounters();
  TestSandboxStore store(kDefaultInactiveSandboxTimeout, kMaxConcurrentLoads);

  // A dogpile of concurrent requests for the same id must load it exactly once.
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

} // namespace facebook::rebalancer::explorer
