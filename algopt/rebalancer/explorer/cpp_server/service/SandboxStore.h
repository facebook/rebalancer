#pragma once

#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <fmt/format.h>
#include <folly/container/F14Map.h>
#include <folly/coro/AsyncScope.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/SharedPromise.h>
#include <folly/coro/Sleep.h>
#include <folly/coro/Task.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/fibers/Semaphore.h>
#include <folly/logging/xlog.h>
#include <folly/ScopeGuard.h>
#include <folly/synchronization/RelaxedAtomic.h>
#include <folly/Synchronized.h>
#include <folly/system/HardwareConcurrency.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace facebook {
namespace rebalancer {
namespace explorer {

constexpr auto kDefaultInactiveSandboxTimeout = std::chrono::minutes(60);
constexpr auto kDropInactiveSandboxesInterval = std::chrono::minutes(1);
constexpr auto kMaxInactiveSandboxTtl =
    std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::duration::max());

inline std::chrono::steady_clock::duration inactiveSandboxTtlFromRequest(
    const HandleRequest& request) {
  const auto ttlSeconds = *request.ttlSeconds();
  if (ttlSeconds > 0) {
    if (ttlSeconds >= kMaxInactiveSandboxTtl.count()) {
      return kMaxInactiveSandboxTtl;
    }
    return std::chrono::seconds(ttlSeconds);
  }
  return kDefaultInactiveSandboxTimeout;
}

// Shutdown drops are censored for TTL analysis.
enum class SandboxDropReason { kIdleTtlExpiry, kShutdown };

// maxInterCallGap starts at load completion and excludes the final idle period.
struct DroppedSandboxInfo {
  std::string manifoldId;
  std::string clientId;
  std::chrono::steady_clock::duration maxInterCallGap{};
  std::chrono::steady_clock::duration configuredTtl{};
  std::chrono::steady_clock::duration lifetime{};
  int64_t callCount{0};
  SandboxDropReason reason{SandboxDropReason::kIdleTtlExpiry};
};

enum class SandboxLoadState { LOADING, LOADED, FAILED };

struct SandboxLoadStatus {
  SandboxLoadState state;
  std::optional<std::string> errorMessage;
};

struct SandboxStoreOptions {
  std::chrono::steady_clock::duration inactiveSandboxTimeout{
      kDefaultInactiveSandboxTimeout};
  int32_t maxConcurrentLoads{0};
  bool retainFailedSandboxes{false};
  bool evictLoadingSandboxes{false};
  // Called once for each loaded sandbox removed from the store.
  std::function<void(const DroppedSandboxInfo&)> onSandboxDropped{};
};

template <
    typename SandboxFactory,
    typename Sandbox,
    typename SandboxKey = std::string,
    typename SandboxKeyHash = folly::hasher<SandboxKey>>
class SandboxStore {
 public:
  explicit SandboxStore(
      std::chrono::steady_clock::duration inactiveSandboxTimeout =
          kDefaultInactiveSandboxTimeout,
      int32_t maxConcurrentLoads = 0,
      std::function<void(const DroppedSandboxInfo&)> onSandboxDropped = {})
      : SandboxStore(
            SandboxFactory{},
            SandboxStoreOptions{
                .inactiveSandboxTimeout = inactiveSandboxTimeout,
                .maxConcurrentLoads = maxConcurrentLoads,
                .onSandboxDropped = std::move(onSandboxDropped),
            }) {}

  explicit SandboxStore(
      SandboxFactory factory,
      SandboxStoreOptions options = {},
      std::shared_ptr<folly::Executor> executor = nullptr)
      : options_{std::move(options)},
        loadSemaphore_{static_cast<size_t>(
            options_.maxConcurrentLoads > 0 ? options_.maxConcurrentLoads : 1)},
        executor_(
            executor != nullptr
                ? std::move(executor)
                : std::make_shared<folly::CPUThreadPoolExecutor>(
                      folly::available_concurrency())),
        factory_(std::move(factory)) {
    scope_.add(
        folly::coro::co_withExecutor(
            executor_.get(), dropInactiveSandboxes(cancel_.getToken())));
  }

  SandboxStore(const SandboxStore&) = delete;
  SandboxStore& operator=(const SandboxStore&) = delete;
  SandboxStore(SandboxStore&&) = delete;
  SandboxStore& operator=(SandboxStore&&) = delete;

  ~SandboxStore() {
    shuttingDown_ = true;
    cancel_.requestCancellation();
    folly::coro::blockingWait(scope_.cancelAndJoinAsync());
    drainForShutdown();
  }

  template <typename SandboxStatus>
  SandboxStatus getStatus(const SandboxKey& key) const {
    const auto status = getLoadStatus(key);
    if (!status || status->state == SandboxLoadState::FAILED) {
      return SandboxStatus::NOT_LOADED;
    }
    if (status->state == SandboxLoadState::LOADING) {
      return SandboxStatus::LOADING;
    }
    return SandboxStatus::LOADED;
  }

  std::optional<SandboxLoadStatus> getLoadStatus(const SandboxKey& key) const {
    const auto holder = findSandboxHolder(key);
    if (holder == nullptr) {
      return std::nullopt;
    }
    return getLoadStatus(*holder);
  }

  bool contains(const SandboxKey& key) const {
    return findSandboxHolder(key) != nullptr;
  }

  void startLoadSandbox(
      SandboxKey key,
      std::optional<std::chrono::steady_clock::duration> ttl = std::nullopt,
      std::string clientId = "") {
    if (shuttingDown_) {
      return;
    }
    auto [holder, shouldStart] = getOrCreateSandboxHolder(
        key,
        ttl,
        /* retryFailed */ true,
        /* touchExisting */ true);
    if (shouldStart) {
      startLoad(std::move(key), std::move(clientId), std::move(holder));
    }
  }

  folly::coro::Task<std::shared_ptr<Sandbox>> getOrLoadSandbox(
      SandboxKey key,
      std::string clientId = "") {
    if (shuttingDown_) {
      throw std::runtime_error(
          "cannot load sandbox while store is shutting down");
    }
    auto [holder, shouldStart] = getOrCreateSandboxHolder(
        key,
        std::nullopt,
        /* retryFailed */ false,
        /* touchExisting */ false);
    if (shouldStart) {
      startLoad(key, std::move(clientId), holder);
    }
    auto sandbox = co_await holder->sandbox.getFuture();
    recordAccessIfCurrent(key, holder);
    co_return sandbox;
  }

  folly::coro::Task<std::shared_ptr<Sandbox>> getSandbox(
      const SandboxKey& key) {
    const auto holder = findSandboxHolder(key);
    if (holder == nullptr ||
        getLoadStatus(*holder).state != SandboxLoadState::LOADED) {
      throw std::runtime_error(
          fmt::format("key {} does not exist", describeKey(key)));
    }
    if (!recordAccessIfCurrent(key, holder)) {
      throw std::runtime_error(
          fmt::format("key {} does not exist", describeKey(key)));
    }
    auto sandbox = co_await holder->sandbox.getFuture();
    co_return sandbox;
  }

  struct SandboxStatusCounts {
    int64_t loading{0};
    int64_t loaded{0};
    int64_t failed{0};
  };

  SandboxStatusCounts getSandboxCounts() const {
    SandboxStatusCounts counts;
    const auto rlock = sandboxes_.rlock();
    for (const auto& [_, holder] : *rlock) {
      switch (getLoadStatus(*holder).state) {
        case SandboxLoadState::LOADING:
          ++counts.loading;
          break;
        case SandboxLoadState::LOADED:
          ++counts.loaded;
          break;
        case SandboxLoadState::FAILED:
          ++counts.failed;
          break;
      }
    }
    return counts;
  }

  void dropInactiveSandboxesForTesting() {
    dropInactiveSandboxes();
  }

 private:
  struct SandboxUsageStats {
    // Eviction holds this lock through erase so load completion cannot race it.
    SandboxLoadState loadState{SandboxLoadState::LOADING};
    std::chrono::steady_clock::time_point lastAccess{
        std::chrono::steady_clock::now()};
    std::chrono::steady_clock::duration maxInterCallGap{
        std::chrono::steady_clock::duration::zero()};
    std::chrono::steady_clock::time_point createdAt{
        std::chrono::steady_clock::now()};
    int64_t callCount{0};
    std::string clientId;
  };

  struct SandboxHolder {
    folly::coro::SharedPromise<std::shared_ptr<Sandbox>> sandbox;
    folly::relaxed_atomic<std::chrono::steady_clock::duration> ttl{
        kDefaultInactiveSandboxTimeout};
    folly::Synchronized<SandboxUsageStats> usageStats;
  };

  struct SandboxHolderLookup {
    std::shared_ptr<SandboxHolder> holder;
    // Set when this call created the holder, making the caller responsible for
    // starting its load. Nobody else will.
    bool shouldStart{false};
  };

  // The first gap starts at load completion; the final idle gap is excluded.
  static void recordAccess(SandboxHolder& holder) {
    auto stats = holder.usageStats.wlock();
    const auto now = std::chrono::steady_clock::now();
    const auto gap = now - stats->lastAccess;
    if (gap > stats->maxInterCallGap) {
      stats->maxInterCallGap = gap;
    }
    ++stats->callCount;
    stats->lastAccess = now;
  }

  static void touchLastAccess(SandboxHolder& holder) {
    holder.usageStats.wlock()->lastAccess = std::chrono::steady_clock::now();
  }

  bool recordAccessIfCurrent(
      const SandboxKey& key,
      const std::shared_ptr<SandboxHolder>& holder) {
    const auto rlock = sandboxes_.rlock();
    const auto it = rlock->find(key);
    if (it == rlock->end() || it->second != holder) {
      return false;
    }
    recordAccess(*holder);
    return true;
  }

  static std::string getErrorMessage(
      const folly::exception_wrapper& exception) {
    if (const auto* error = exception.get_exception<std::exception>()) {
      return error->what();
    }
    return exception.what().toStdString();
  }

  static SandboxLoadStatus getLoadStatus(const SandboxHolder& holder) {
    const auto result = holder.sandbox.poll();
    if (!result) {
      return {.state = SandboxLoadState::LOADING};
    }
    if (result->hasValue()) {
      return {.state = SandboxLoadState::LOADED};
    }
    return {
        .state = SandboxLoadState::FAILED,
        .errorMessage = getErrorMessage(result->exception()),
    };
  }

  std::shared_ptr<SandboxHolder> findSandboxHolder(
      const SandboxKey& key) const {
    const auto rlock = sandboxes_.rlock();
    const auto it = rlock->find(key);
    return it == rlock->end() ? nullptr : it->second;
  }

  SandboxHolderLookup getOrCreateSandboxHolder(
      const SandboxKey& key,
      std::optional<std::chrono::steady_clock::duration> ttl,
      bool retryFailed,
      bool touchExisting) {
    const auto now = std::chrono::steady_clock::now();
    auto wlock = sandboxes_.wlock();
    auto it = wlock->find(key);
    if (it != wlock->end()) {
      auto holder = it->second;
      const auto state = getLoadStatus(*holder).state;
      if (retryFailed && state == SandboxLoadState::FAILED) {
        wlock->erase(it);
      } else {
        if (ttl && *ttl > holder->ttl.load()) {
          holder->ttl = *ttl;
        }
        if (touchExisting) {
          if (state == SandboxLoadState::LOADED) {
            recordAccess(*holder);
          } else if (state == SandboxLoadState::LOADING) {
            touchLastAccess(*holder);
          }
        }
        return {.holder = std::move(holder), .shouldStart = false};
      }
    }

    auto holder = std::make_shared<SandboxHolder>();
    holder->usageStats.wlock()->lastAccess = now;
    holder->ttl = ttl.value_or(options_.inactiveSandboxTimeout);
    wlock->emplace(key, holder);
    return {.holder = std::move(holder), .shouldStart = true};
  }

  void startLoad(
      SandboxKey key,
      std::string clientId,
      std::shared_ptr<SandboxHolder> holder) {
    scope_.add(
        folly::coro::co_withExecutor(
            executor_.get(),
            loadSandbox(
                std::move(key), std::move(clientId), std::move(holder))));
  }

  std::string describeKey(const SandboxKey& key) const {
    if constexpr (requires { factory_.describe(key); }) {
      return factory_.describe(key);
    } else if constexpr (std::is_convertible_v<SandboxKey, std::string>) {
      return std::string(key);
    } else {
      return "<compound key>";
    }
  }

  folly::coro::Task<void> loadSandbox(
      SandboxKey key,
      std::string clientId,
      std::shared_ptr<SandboxHolder> holder) {
    bool semaphoreAcquired = false;
    SCOPE_EXIT {
      if (semaphoreAcquired) {
        loadSemaphore_.signal();
      }
    };

    const auto keyDescription = describeKey(key);
    try {
      if (options_.maxConcurrentLoads > 0) {
        co_await loadSemaphore_.co_wait();
        semaphoreAcquired = true;
      }

      XLOG(INFO) << "loading sandbox " << keyDescription;
      const auto start = std::chrono::steady_clock::now();
      std::shared_ptr<Sandbox> sandbox;
      if constexpr (requires { factory_.create(key, clientId); }) {
        sandbox = co_await factory_.create(key, clientId);
      } else {
        sandbox = co_await factory_.create(key);
      }
      const auto end = std::chrono::steady_clock::now();
      // Exclude load time from idle-gap statistics.
      {
        auto stats = holder->usageStats.wlock();
        if (end > stats->lastAccess) {
          stats->lastAccess = end;
        }
        stats->createdAt = end;
        stats->maxInterCallGap = std::chrono::steady_clock::duration::zero();
        stats->callCount = 0;
        stats->clientId = std::move(clientId);
        stats->loadState = SandboxLoadState::LOADED;
      }
      holder->sandbox.setValue(sandbox);

      const std::chrono::duration<double> elapsed = end - start;
      XLOG(INFO) << fmt::format(
          "loading sandbox {} took {:.3f} seconds",
          keyDescription,
          elapsed.count());
    } catch (...) {
      auto exception = folly::exception_wrapper(std::current_exception());
      const auto errorMessage = getErrorMessage(exception);
      {
        auto stats = holder->usageStats.wlock();
        stats->lastAccess = std::chrono::steady_clock::now();
        stats->loadState = SandboxLoadState::FAILED;
      }
      holder->sandbox.setException(std::move(exception));
      XLOG(ERR) << "sandbox for " << keyDescription
                << " failed to load: " << errorMessage;

      if (!options_.retainFailedSandboxes) {
        const auto wlock = sandboxes_.wlock();
        const auto it = wlock->find(key);
        if (it != wlock->end() && it->second == holder) {
          wlock->erase(it);
        }
      }
    }
  }

  void notifySandboxDropped(
      const SandboxKey& key,
      const SandboxHolder& holder,
      SandboxDropReason reason) const noexcept {
    if (!options_.onSandboxDropped) {
      return;
    }
    try {
      DroppedSandboxInfo info;
      {
        const auto stats = holder.usageStats.rlock();
        info.clientId = stats->clientId;
        info.maxInterCallGap = stats->maxInterCallGap;
        info.configuredTtl = holder.ttl.load();
        info.lifetime = std::chrono::steady_clock::now() - stats->createdAt;
        info.callCount = stats->callCount;
      }
      info.manifoldId = describeKey(key);
      info.reason = reason;
      options_.onSandboxDropped(info);
    } catch (const std::exception& error) {
      XLOG(ERR) << "onSandboxDropped callback failed: " << error.what();
    } catch (...) {
      XLOG(ERR) << "onSandboxDropped callback failed with unknown exception";
    }
  }

  void drainForShutdown() {
    if (!options_.onSandboxDropped) {
      return;
    }

    std::vector<std::pair<SandboxKey, std::shared_ptr<SandboxHolder>>> loaded;
    {
      const auto rlock = sandboxes_.rlock();
      loaded.reserve(rlock->size());
      for (const auto& [key, holder] : *rlock) {
        if (getLoadStatus(*holder).state == SandboxLoadState::LOADED) {
          loaded.emplace_back(key, holder);
        }
      }
    }

    for (const auto& [key, holder] : loaded) {
      notifySandboxDropped(key, *holder, SandboxDropReason::kShutdown);
    }
  }

  void dropInactiveSandboxes() {
    struct RemovedSandbox {
      SandboxKey key;
      std::shared_ptr<SandboxHolder> holder;
      bool wasLoaded;
    };
    std::vector<RemovedSandbox> removed;
    const auto now = std::chrono::steady_clock::now();
    {
      auto wlock = sandboxes_.wlock();
      for (auto it = wlock->begin(); it != wlock->end();) {
        const auto holder = it->second;
        const auto stats = holder->usageStats.rlock();
        const auto state = stats->loadState;
        const bool canEvict = state != SandboxLoadState::LOADING ||
            options_.evictLoadingSandboxes;
        const bool expired = now - stats->lastAccess > holder->ttl.load();
        if (canEvict && expired) {
          removed.push_back(
              {it->first, holder, state == SandboxLoadState::LOADED});
          it = wlock->erase(it);
        } else {
          ++it;
        }
      }
    }

    for (const auto& [key, holder, wasLoaded] : removed) {
      XLOG(INFO) << "dropped inactive sandbox " << describeKey(key);
      if (wasLoaded) {
        notifySandboxDropped(key, *holder, SandboxDropReason::kIdleTtlExpiry);
      }
    }
  }

  folly::coro::Task<void> dropInactiveSandboxes(
      folly::CancellationToken token) {
    while (!token.isCancellationRequested()) {
      co_await folly::coro::sleepReturnEarlyOnCancel(
          kDropInactiveSandboxesInterval);
      if (token.isCancellationRequested()) {
        break;
      }
      dropInactiveSandboxes();
    }
  }

  SandboxStoreOptions options_;
  folly::fibers::Semaphore loadSemaphore_;
  std::shared_ptr<folly::Executor> executor_;
  SandboxFactory factory_;
  folly::Synchronized<folly::F14FastMap<
      SandboxKey,
      std::shared_ptr<SandboxHolder>,
      SandboxKeyHash>>
      sandboxes_;
  folly::coro::CancellableAsyncScope scope_;
  folly::CancellationSource cancel_;
  folly::relaxed_atomic<bool> shuttingDown_{false};
};

} // namespace explorer
} // namespace rebalancer
} // namespace facebook
