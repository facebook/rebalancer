// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/service/SandboxStore.h"

#include <folly/coro/Baton.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Sleep.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace facebook::rebalancer::explorer {

struct FakeSandbox {};

enum class FakeSandboxStatus { NOT_LOADED, LOADING, LOADED };

class SandboxLoadError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

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

using TestSandboxStore = SandboxStore<FakeSandboxFactory, FakeSandbox>;

class FailingSandboxFactory {
 public:
  static folly::coro::Task<std::shared_ptr<FakeSandbox>> create(std::string) {
    totalLoads_.fetch_add(1);
    throw SandboxLoadError("test sandbox load failed");
    co_return nullptr;
  }

  static void reset() {
    totalLoads_.store(0);
  }

  static int totalLoads() {
    return totalLoads_.load();
  }

 private:
  static inline std::atomic<int> totalLoads_{0};
};

using FailingSandboxStore = SandboxStore<FailingSandboxFactory, FakeSandbox>;

class ControlledFailingSandboxFactory {
 public:
  static folly::coro::Task<std::shared_ptr<FakeSandbox>> create(std::string) {
    co_await failureGate_;
    throw SandboxLoadError("delayed sandbox load failed");
  }

  static void reset() {
    failureGate_.reset();
  }

  static void failPendingLoads() {
    failureGate_.post();
  }

 private:
  static inline folly::coro::Baton failureGate_;
};

using ControlledFailingSandboxStore =
    SandboxStore<ControlledFailingSandboxFactory, FakeSandbox>;

class ControlledSuccessfulSandboxFactory {
 public:
  static folly::coro::Task<std::shared_ptr<FakeSandbox>> create(std::string) {
    co_await loadGate_;
    co_return std::make_shared<FakeSandbox>();
  }

  static void reset() {
    loadGate_.reset();
  }

  static void finishPendingLoads() {
    loadGate_.post();
  }

 private:
  static inline folly::coro::Baton loadGate_;
};

using ControlledSuccessfulSandboxStore =
    SandboxStore<ControlledSuccessfulSandboxFactory, FakeSandbox>;

class FailOnceSandboxFactory {
 public:
  static folly::coro::Task<std::shared_ptr<FakeSandbox>> create(std::string) {
    if (totalLoads_.fetch_add(1) == 0) {
      throw std::runtime_error("first load failed");
    }
    co_return std::make_shared<FakeSandbox>();
  }

  static void reset() {
    totalLoads_.store(0);
  }

  static int totalLoads() {
    return totalLoads_.load();
  }

 private:
  static inline std::atomic<int> totalLoads_{0};
};

using FailOnceSandboxStore = SandboxStore<FailOnceSandboxFactory, FakeSandbox>;

using CompoundKey = std::pair<std::string, std::string>;

class StatefulCompoundKeyFactory {
 public:
  explicit StatefulCompoundKeyFactory(
      std::shared_ptr<std::atomic<int>> totalLoads)
      : totalLoads_(std::move(totalLoads)) {}

  folly::coro::Task<std::shared_ptr<FakeSandbox>> create(CompoundKey) {
    totalLoads_->fetch_add(1);
    co_return std::make_shared<FakeSandbox>();
  }

 private:
  std::shared_ptr<std::atomic<int>> totalLoads_;
};

using CompoundKeySandboxStore =
    SandboxStore<StatefulCompoundKeyFactory, FakeSandbox, CompoundKey>;

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

template <typename Store>
bool waitUntilLoaded(Store& store, const std::string& id) {
  return waitUntil([&]() {
    return store.template getStatus<FakeSandboxStatus>(id) ==
        FakeSandboxStatus::LOADED;
  });
}

template <typename Store>
FakeSandboxStatus statusOf(Store& store, const std::string& id) {
  return store.template getStatus<FakeSandboxStatus>(id);
}

// Short TTL expires within tests; long TTL does not.
constexpr auto kShortTtl = std::chrono::milliseconds(20);
constexpr auto kLongTtl = std::chrono::seconds(10);
constexpr auto kPastShortTtl = std::chrono::milliseconds(100);
constexpr auto kDelayedLoadTtl = std::chrono::milliseconds(100);
constexpr auto kPastDelayedLoadTtl = std::chrono::milliseconds(200);

// Keep the observed and fatal idle gaps distinct.
constexpr auto kDeliberateGap = std::chrono::milliseconds(60);
constexpr auto kFatalIdle = std::chrono::milliseconds(200);
constexpr int kConcurrentAccessThreads = 8;
constexpr int kAccessesPerThread = 50;

// Drive eviction without waiting for the background sweep.
template <typename Store>
void sleepThenSweep(
    Store& store,
    std::chrono::milliseconds delay = kPastShortTtl) {
  // @lint-ignore CLANGTIDY facebook-hte-BadCall-sleep_for
  std::this_thread::sleep_for(delay);
  store.dropInactiveSandboxesForTesting();
}

std::shared_ptr<FakeSandbox> getSandboxBlocking(
    TestSandboxStore& store,
    const std::string& id) {
  return folly::coro::blockingWait(store.getSandbox(id));
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
      if (store.getStatus<FakeSandboxStatus>(id) != FakeSandboxStatus::LOADED) {
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
    return store.getStatus<FakeSandboxStatus>("sandbox_x") ==
        FakeSandboxStatus::LOADED;
  });

  ASSERT_TRUE(loaded);
  EXPECT_EQ(1, FakeSandboxFactory::totalLoads());
}

TEST(SandboxStoreTest, GetOrLoadAwaitsExistingLoad) {
  FakeSandboxFactory::resetCounters();
  TestSandboxStore store;

  store.startLoadSandbox("sandbox_x");
  const auto sandbox =
      folly::coro::blockingWait(store.getOrLoadSandbox("sandbox_x"));

  EXPECT_NE(nullptr, sandbox);
  EXPECT_EQ(1, FakeSandboxFactory::totalLoads());
  EXPECT_EQ(FakeSandboxStatus::LOADED, statusOf(store, "sandbox_x"));
}

TEST(SandboxStoreTest, GetOrLoadReturnsSuccessfulEvictedLoad) {
  ControlledSuccessfulSandboxFactory::reset();
  ControlledSuccessfulSandboxStore store(
      ControlledSuccessfulSandboxFactory{},
      SandboxStoreOptions{
          .inactiveSandboxTimeout = std::chrono::steady_clock::duration::zero(),
          .evictLoadingSandboxes = true,
      });
  std::shared_ptr<FakeSandbox> sandbox;
  std::exception_ptr loadException;
  std::thread caller([&] {
    try {
      sandbox = folly::coro::blockingWait(store.getOrLoadSandbox("sandbox_x"));
    } catch (...) {
      loadException = std::current_exception();
    }
  });

  const bool loadStarted = waitUntil([&] {
    return statusOf(store, "sandbox_x") == FakeSandboxStatus::LOADING;
  });
  if (loadStarted) {
    store.dropInactiveSandboxesForTesting();
  }
  ControlledSuccessfulSandboxFactory::finishPendingLoads();
  caller.join();

  ASSERT_TRUE(loadStarted);
  EXPECT_EQ(FakeSandboxStatus::NOT_LOADED, statusOf(store, "sandbox_x"));
  EXPECT_FALSE(loadException);
  EXPECT_NE(nullptr, sandbox);
}

TEST(SandboxStoreTest, SupportsInjectedFactoryAndCompoundKey) {
  auto totalLoads = std::make_shared<std::atomic<int>>(0);
  auto executor = std::make_shared<folly::CPUThreadPoolExecutor>(1);
  CompoundKeySandboxStore store(
      StatefulCompoundKeyFactory{totalLoads}, {}, executor);
  const CompoundKey key{"loader", "key"};

  const auto sandbox = folly::coro::blockingWait(store.getOrLoadSandbox(key));

  EXPECT_NE(nullptr, sandbox);
  EXPECT_EQ(1, totalLoads->load());
  const auto status = store.getLoadStatus(key);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(SandboxLoadState::LOADED, status->state);
}

TEST(SandboxStoreTest, RecordsEachGetOrLoadAccessOnce) {
  FakeSandboxFactory::resetCounters();
  std::vector<DroppedSandboxInfo> dropped;
  {
    TestSandboxStore store(
        kDefaultInactiveSandboxTimeout,
        /*maxConcurrentLoads=*/0,
        [&](const DroppedSandboxInfo& info) { dropped.push_back(info); });

    folly::coro::blockingWait(store.getOrLoadSandbox("sandbox_x"));
    // @lint-ignore CLANGTIDY facebook-hte-BadCall-sleep_for
    std::this_thread::sleep_for(kDeliberateGap);
    folly::coro::blockingWait(store.getOrLoadSandbox("sandbox_x"));
  }

  ASSERT_EQ(1u, dropped.size());
  EXPECT_EQ(2, dropped[0].callCount);
  // A cache hit must measure the gap from the previous call, not from its own
  // lookup.
  EXPECT_GE(dropped[0].maxInterCallGap, kDeliberateGap);
}

TEST(SandboxStoreTest, RetainsFailureForAwaitingConsumers) {
  FailingSandboxFactory::reset();
  FailingSandboxStore store(
      FailingSandboxFactory{},
      SandboxStoreOptions{.retainFailedSandboxes = true});

  EXPECT_THROW(
      folly::coro::blockingWait(store.getOrLoadSandbox("sandbox_x")),
      SandboxLoadError);

  const auto status = store.getLoadStatus("sandbox_x");
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(SandboxLoadState::FAILED, status->state);
  EXPECT_EQ("test sandbox load failed", status->errorMessage);
  EXPECT_EQ(1, store.getSandboxCounts().failed);

  EXPECT_THROW(
      folly::coro::blockingWait(store.getOrLoadSandbox("sandbox_x")),
      SandboxLoadError);
  EXPECT_EQ(1, FailingSandboxFactory::totalLoads());
}

TEST(SandboxStoreTest, LegacyStartLoadRetriesAfterFailure) {
  FailOnceSandboxFactory::reset();
  FailOnceSandboxStore store;

  store.startLoadSandbox("sandbox_x");
  ASSERT_TRUE(
      waitUntil([]() { return FailOnceSandboxFactory::totalLoads() == 1; }));
  ASSERT_TRUE(waitUntil([&]() {
    return statusOf(store, "sandbox_x") == FakeSandboxStatus::NOT_LOADED;
  }));

  store.startLoadSandbox("sandbox_x");
  ASSERT_TRUE(waitUntilLoaded(store, "sandbox_x"));
  EXPECT_EQ(2, FailOnceSandboxFactory::totalLoads());
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

TEST(SandboxStoreTest, IdleTtlStartsAfterLoadFailure) {
  ControlledFailingSandboxFactory::reset();
  std::vector<DroppedSandboxInfo> dropped;
  ControlledFailingSandboxStore store(
      ControlledFailingSandboxFactory{},
      SandboxStoreOptions{
          .inactiveSandboxTimeout = kDelayedLoadTtl,
          .retainFailedSandboxes = true,
          .evictLoadingSandboxes = false,
          .onSandboxDropped =
              [&](const DroppedSandboxInfo& info) { dropped.push_back(info); },
      });

  store.startLoadSandbox("sandbox_x");
  sleepThenSweep(store, kPastDelayedLoadTtl);
  EXPECT_EQ(FakeSandboxStatus::LOADING, statusOf(store, "sandbox_x"));

  ControlledFailingSandboxFactory::failPendingLoads();
  ASSERT_TRUE(waitUntil([&]() {
    const auto status = store.getLoadStatus("sandbox_x");
    return status && status->state == SandboxLoadState::FAILED;
  }));
  store.dropInactiveSandboxesForTesting();
  ASSERT_TRUE(store.getLoadStatus("sandbox_x").has_value());

  sleepThenSweep(store, kPastDelayedLoadTtl);
  EXPECT_FALSE(store.getLoadStatus("sandbox_x").has_value());
  EXPECT_TRUE(dropped.empty());
}

TEST(SandboxStoreTest, DoesNotEmitDropForLoadingSandbox) {
  ControlledFailingSandboxFactory::reset();
  std::vector<DroppedSandboxInfo> dropped;
  {
    ControlledFailingSandboxStore store(
        ControlledFailingSandboxFactory{},
        SandboxStoreOptions{
            .inactiveSandboxTimeout = kDelayedLoadTtl,
            .retainFailedSandboxes = true,
            .evictLoadingSandboxes = true,
            .onSandboxDropped =
                [&](const DroppedSandboxInfo& info) {
                  dropped.push_back(info);
                },
        });

    store.startLoadSandbox("sandbox_x");
    sleepThenSweep(store, kPastDelayedLoadTtl);
    EXPECT_EQ(FakeSandboxStatus::NOT_LOADED, statusOf(store, "sandbox_x"));
    ControlledFailingSandboxFactory::failPendingLoads();
  }

  EXPECT_TRUE(dropped.empty());
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

TEST(SandboxStoreTest, EmitsLongestCallGapOnIdleEviction) {
  FakeSandboxFactory::resetCounters();
  std::vector<DroppedSandboxInfo> dropped;
  TestSandboxStore store(
      kDefaultInactiveSandboxTimeout,
      /*maxConcurrentLoads=*/0,
      [&](const DroppedSandboxInfo& info) { dropped.push_back(info); });

  store.startLoadSandbox("s", kShortTtl, "client_a");
  ASSERT_TRUE(waitUntilLoaded(store, "s"));

  getSandboxBlocking(store, "s");
  // @lint-ignore CLANGTIDY facebook-hte-BadCall-sleep_for
  std::this_thread::sleep_for(kDeliberateGap);
  getSandboxBlocking(store, "s");

  // @lint-ignore CLANGTIDY facebook-hte-BadCall-sleep_for
  std::this_thread::sleep_for(kFatalIdle);
  store.dropInactiveSandboxesForTesting();

  ASSERT_EQ(1u, dropped.size());
  const auto& info = dropped[0];
  EXPECT_EQ("s", info.manifoldId);
  EXPECT_EQ("client_a", info.clientId);
  EXPECT_EQ(SandboxDropReason::kIdleTtlExpiry, info.reason);
  EXPECT_EQ(kShortTtl, info.configuredTtl);
  EXPECT_EQ(2, info.callCount);
  EXPECT_GE(info.maxInterCallGap, kDeliberateGap);
  EXPECT_LT(info.maxInterCallGap, info.lifetime);
}

TEST(SandboxStoreTest, IncludesGapFromLoadCompletionToFirstAccess) {
  FakeSandboxFactory::resetCounters();
  std::vector<DroppedSandboxInfo> dropped;
  {
    TestSandboxStore store(
        kDefaultInactiveSandboxTimeout,
        /*maxConcurrentLoads=*/0,
        [&](const DroppedSandboxInfo& info) { dropped.push_back(info); });
    store.startLoadSandbox("s", kLongTtl, "client_a");
    ASSERT_TRUE(waitUntilLoaded(store, "s"));

    // @lint-ignore CLANGTIDY facebook-hte-BadCall-sleep_for
    std::this_thread::sleep_for(kDeliberateGap);
    getSandboxBlocking(store, "s");
  }

  ASSERT_EQ(1u, dropped.size());
  EXPECT_EQ(1, dropped[0].callCount);
  EXPECT_GE(dropped[0].maxInterCallGap, kDeliberateGap);
}

TEST(SandboxStoreTest, PreservesConcurrentAccessCount) {
  FakeSandboxFactory::resetCounters();
  std::vector<DroppedSandboxInfo> dropped;
  {
    TestSandboxStore store(
        kDefaultInactiveSandboxTimeout,
        /*maxConcurrentLoads=*/0,
        [&](const DroppedSandboxInfo& info) { dropped.push_back(info); });
    store.startLoadSandbox("s", kLongTtl, "client_a");
    ASSERT_TRUE(waitUntilLoaded(store, "s"));

    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    threads.reserve(kConcurrentAccessThreads);
    for (int i = 0; i < kConcurrentAccessThreads; ++i) {
      threads.emplace_back([&] {
        while (!start.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }
        for (int access = 0; access < kAccessesPerThread; ++access) {
          getSandboxBlocking(store, "s");
        }
      });
    }
    start.store(true, std::memory_order_release);
    for (auto& thread : threads) {
      thread.join();
    }
  }

  ASSERT_EQ(1u, dropped.size());
  EXPECT_EQ(
      kConcurrentAccessThreads * kAccessesPerThread, dropped[0].callCount);
}

TEST(SandboxStoreTest, EmitsShutdownReasonForLiveSandbox) {
  FakeSandboxFactory::resetCounters();
  std::vector<DroppedSandboxInfo> dropped;
  {
    TestSandboxStore store(
        kDefaultInactiveSandboxTimeout,
        /*maxConcurrentLoads=*/0,
        [&](const DroppedSandboxInfo& info) { dropped.push_back(info); });
    store.startLoadSandbox("s", kLongTtl, "client_a");
    ASSERT_TRUE(waitUntilLoaded(store, "s"));
  }

  ASSERT_EQ(1u, dropped.size());
  EXPECT_EQ("s", dropped[0].manifoldId);
  EXPECT_EQ("client_a", dropped[0].clientId);
  EXPECT_EQ(SandboxDropReason::kShutdown, dropped[0].reason);
}

TEST(SandboxStoreTest, IgnoresLoadStartedByDropCallbackDuringShutdown) {
  FakeSandboxFactory::resetCounters();
  TestSandboxStore* storePtr = nullptr;
  bool callbackReturned = false;
  {
    TestSandboxStore store(
        kDefaultInactiveSandboxTimeout,
        /*maxConcurrentLoads=*/0,
        [&](const DroppedSandboxInfo&) {
          storePtr->startLoadSandbox("reentered", kLongTtl);
          callbackReturned = true;
        });
    storePtr = &store;
    store.startLoadSandbox("s", kLongTtl);
    ASSERT_TRUE(waitUntilLoaded(store, "s"));
  }

  EXPECT_TRUE(callbackReturned);
  EXPECT_EQ(1, FakeSandboxFactory::totalLoads());
}

TEST(SandboxStoreTest, ContainsDropCallbackExceptions) {
  FakeSandboxFactory::resetCounters();
  int callbackCalls = 0;
  TestSandboxStore store(
      kDefaultInactiveSandboxTimeout,
      /*maxConcurrentLoads=*/0,
      [&](const DroppedSandboxInfo&) {
        ++callbackCalls;
        throw std::runtime_error("test callback failure");
      });

  store.startLoadSandbox("a", std::chrono::steady_clock::duration::zero());
  store.startLoadSandbox("b", std::chrono::steady_clock::duration::zero());
  ASSERT_TRUE(waitUntilLoaded(store, "a"));
  ASSERT_TRUE(waitUntilLoaded(store, "b"));

  EXPECT_NO_THROW(store.dropInactiveSandboxesForTesting());
  EXPECT_EQ(2, callbackCalls);
  EXPECT_EQ(FakeSandboxStatus::NOT_LOADED, statusOf(store, "a"));
  EXPECT_EQ(FakeSandboxStatus::NOT_LOADED, statusOf(store, "b"));
}

TEST(SandboxStoreTest, ContainsDropCallbackExceptionsDuringShutdown) {
  FakeSandboxFactory::resetCounters();
  int callbackCalls = 0;
  {
    TestSandboxStore store(
        kDefaultInactiveSandboxTimeout,
        /*maxConcurrentLoads=*/0,
        [&](const DroppedSandboxInfo&) {
          ++callbackCalls;
          throw std::runtime_error("test callback failure");
        });
    store.startLoadSandbox("s", kLongTtl);
    ASSERT_TRUE(waitUntilLoaded(store, "s"));
  }

  EXPECT_EQ(1, callbackCalls);
}

} // namespace facebook::rebalancer::explorer
