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
#include <exception>
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
      int32_t maxConcurrentLoads = 0)
      : SandboxStore(
            SandboxFactory{},
            SandboxStoreOptions{
                .inactiveSandboxTimeout = inactiveSandboxTimeout,
                .maxConcurrentLoads = maxConcurrentLoads,
            }) {}

  explicit SandboxStore(
      SandboxFactory factory,
      SandboxStoreOptions options = {},
      std::shared_ptr<folly::Executor> executor = nullptr)
      : options_{options},
        loadSemaphore_{static_cast<size_t>(
            options.maxConcurrentLoads > 0 ? options.maxConcurrentLoads : 1)},
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
    cancel_.requestCancellation();
    folly::coro::blockingWait(scope_.cancelAndJoinAsync());
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
    auto [holder, shouldStart] = getOrCreateSandboxHolder(
        key,
        std::nullopt,
        /* retryFailed */ false,
        /* touchExisting */ false);
    if (shouldStart) {
      startLoad(std::move(key), std::move(clientId), holder);
    }
    auto sandbox = co_await holder->sandbox.getFuture();
    holder->lastAccess = std::chrono::steady_clock::now();
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
    auto sandbox = co_await holder->sandbox.getFuture();
    holder->lastAccess = std::chrono::steady_clock::now();
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
  struct SandboxHolder {
    folly::coro::SharedPromise<std::shared_ptr<Sandbox>> sandbox;
    folly::relaxed_atomic<std::chrono::steady_clock::time_point> lastAccess{
        std::chrono::steady_clock::now()};
    folly::relaxed_atomic<std::chrono::steady_clock::duration> ttl{
        kDefaultInactiveSandboxTimeout};
  };

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

  std::pair<std::shared_ptr<SandboxHolder>, bool> getOrCreateSandboxHolder(
      const SandboxKey& key,
      std::optional<std::chrono::steady_clock::duration> ttl,
      bool retryFailed,
      bool touchExisting) {
    const auto now = std::chrono::steady_clock::now();
    auto wlock = sandboxes_.wlock();
    auto it = wlock->find(key);
    if (it != wlock->end()) {
      auto holder = it->second;
      if (retryFailed &&
          getLoadStatus(*holder).state == SandboxLoadState::FAILED) {
        wlock->erase(it);
      } else {
        if (ttl && *ttl > holder->ttl.load()) {
          holder->ttl = *ttl;
        }
        if (touchExisting) {
          holder->lastAccess = now;
        }
        return {std::move(holder), false};
      }
    }

    auto holder = std::make_shared<SandboxHolder>();
    holder->lastAccess = now;
    holder->ttl = ttl.value_or(options_.inactiveSandboxTimeout);
    wlock->emplace(key, holder);
    return {std::move(holder), true};
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
      holder->lastAccess = end;
      holder->sandbox.setValue(sandbox);

      const std::chrono::duration<double> elapsed = end - start;
      XLOG(INFO) << fmt::format(
          "loading sandbox {} took {:.3f} seconds",
          keyDescription,
          elapsed.count());
    } catch (...) {
      auto exception = folly::exception_wrapper(std::current_exception());
      const auto errorMessage = getErrorMessage(exception);
      holder->lastAccess = std::chrono::steady_clock::now();
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

  void dropInactiveSandboxes() {
    std::vector<std::pair<SandboxKey, std::shared_ptr<SandboxHolder>>> removed;
    const auto now = std::chrono::steady_clock::now();
    {
      auto wlock = sandboxes_.wlock();
      for (auto it = wlock->begin(); it != wlock->end();) {
        const auto& holder = *it->second;
        const bool canEvict =
            getLoadStatus(holder).state != SandboxLoadState::LOADING ||
            options_.evictLoadingSandboxes;
        if (canEvict && now - holder.lastAccess.load() > holder.ttl.load()) {
          removed.emplace_back(*it);
          it = wlock->erase(it);
        } else {
          ++it;
        }
      }
    }

    for (const auto& [key, _] : removed) {
      XLOG(INFO) << "dropped inactive sandbox " << describeKey(key);
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
};

} // namespace explorer
} // namespace rebalancer
} // namespace facebook
