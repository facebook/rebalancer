#pragma once

#include "algopt/rebalancer/materializer/utils/Cache.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <fmt/format.h>
#include <folly/concurrency/ConcurrentHashMap.h>
#include <folly/coro/AsyncScope.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Sleep.h>
#include <folly/coro/Task.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/fibers/Semaphore.h>
#include <folly/logging/xlog.h>
#include <folly/MapUtil.h>
#include <folly/ScopeGuard.h>
#include <folly/system/HardwareConcurrency.h>

namespace facebook {
namespace rebalancer {
namespace explorer {

constexpr auto kDefaultInactiveSandboxTimeout = std::chrono::minutes(60);
constexpr auto kDropInactiveSandboxesInterval = std::chrono::minutes(1);

template <typename SandboxFactory, typename SandboxStatus, typename Sandbox>
class SandboxStore {
 public:
  explicit SandboxStore(
      std::chrono::steady_clock::duration inactiveSandboxTimeout =
          kDefaultInactiveSandboxTimeout,
      int32_t maxConcurrentLoads = 0)
      : inactiveSandboxTimeout_{inactiveSandboxTimeout},
        // When disabled (maxConcurrentLoads <= 0) the semaphore is never waited
        // on; capacity 1 is an unused placeholder, not a one-load limit.
        loadSemaphore_{static_cast<size_t>(
            maxConcurrentLoads > 0 ? maxConcurrentLoads : 1)},
        loadSemaphoreEnabled_{maxConcurrentLoads > 0},
        executor_(
            std::make_shared<folly::CPUThreadPoolExecutor>(
                folly::available_concurrency())) {
    // Start background task for dropping inactive sandboxes.
    scope_.add(
        folly::coro::co_withExecutor(
            executor_.get(), dropInactiveSandboxes(cancel_.getToken())));
  }

  SandboxStore(const SandboxStore&) = delete;
  SandboxStore& operator=(const SandboxStore&) = delete;
  SandboxStore(SandboxStore&&) = delete;
  SandboxStore& operator=(SandboxStore&&) = delete;

  ~SandboxStore() {
    // Cancel background task for dropping inactive sandboxes.
    cancel_.requestCancellation();
    folly::coro::blockingWait(scope_.cancelAndJoinAsync());
  }

  folly::coro::Task<SandboxStatus> getStatus(std::string manifoldId) {
    co_return folly::get_default(
        status_, manifoldId, SandboxStatus::NOT_LOADED);
  }

  void startLoadSandbox(std::string manifoldId) {
    folly::coro::co_withExecutor(executor_.get(), loadSandbox(manifoldId))
        .start();
  }

  folly::coro::Task<void> loadSandbox(std::string manifoldId) {
    // Atomically claim the load. If another coroutine already claimed it or the
    // sandbox is already loaded, bail without taking a semaphore token so
    // duplicate requests can't starve unrelated loads of slots.
    if (!status_.insert({manifoldId, SandboxStatus::LOADING}).second) {
      co_return;
    }

    // The guard releases the token on every exit path iff we acquired one
    // (skipped when the semaphore is disabled).
    bool semaphoreAcquired = false;
    SCOPE_EXIT {
      if (semaphoreAcquired) {
        loadSemaphore_.signal();
      }
    };

    try {
      // Throttle concurrent loads to prevent OOM from dogpile bursts. The wait
      // is inside the try so a throwing or cancelled acquire rolls back the
      // claim above instead of orphaning a LOADING entry that would block every
      // future load of this id.
      if (loadSemaphoreEnabled_) {
        co_await loadSemaphore_.co_wait();
        semaphoreAcquired = true;
      }

      XLOG(INFO) << "loading sandbox " << manifoldId;

      const auto start = std::chrono::steady_clock::now();
      auto sandbox = co_await factory_.create(manifoldId);
      sandboxes_.getSavedOrCompute(
          manifoldId, [&sandbox]() { return sandbox; });
      const auto end = std::chrono::steady_clock::now();

      const std::chrono::duration<double> elapsed = end - start;
      XLOG(INFO) << fmt::format(
          "loading sandbox {} took {:.3f} seconds",
          manifoldId,
          elapsed.count());

      lastAccess_.insert_or_assign(manifoldId, end);

      status_.assign_if_equal(
          manifoldId, SandboxStatus::LOADING, SandboxStatus::LOADED);

    } catch (const std::exception& e) {
      XLOG(ERR) << "sandbox for " << manifoldId
                << " failed to load: " << e.what();

      status_.erase(manifoldId);
      lastAccess_.erase(manifoldId);
    }

    co_return;
  }

  folly::coro::Task<std::shared_ptr<Sandbox>> getSandbox(
      std::string manifoldId) {
    auto sandbox = sandboxes_.at(manifoldId);
    lastAccess_.insert_or_assign(manifoldId, std::chrono::steady_clock::now());
    co_return sandbox;
  }

  struct SandboxStatusCounts {
    int64_t loading{0};
    int64_t loaded{0};
  };

  SandboxStatusCounts getSandboxCounts() const {
    SandboxStatusCounts counts;
    for (const auto& [_, status] : status_) {
      if (status == SandboxStatus::LOADING) {
        ++counts.loading;
      } else if (status == SandboxStatus::LOADED) {
        ++counts.loaded;
      }
    }
    return counts;
  }

 private:
  void dropInactiveSandboxes() {
    for (auto& [manifoldId, status] : status_) {
      if (status != SandboxStatus::LOADED) {
        continue;
      }
      const auto lastAccess = lastAccess_.at(manifoldId);
      const auto now = std::chrono::steady_clock::now();
      const auto elapsed = now - lastAccess;
      if (elapsed > inactiveSandboxTimeout_) {
        const auto elapsedSec = std::chrono::duration<double>(elapsed).count();
        const auto timeoutSec =
            std::chrono::duration<double>(inactiveSandboxTimeout_).count();
        XLOG(INFO) << fmt::format(
            "dropping sandbox {} after being inactive for {:.3f} seconds, which is more than the limit of {:.3f} seconds",
            manifoldId,
            elapsedSec,
            timeoutSec);
        sandboxes_.erase(manifoldId);
        status_.erase(manifoldId);
        lastAccess_.erase(manifoldId);
        XLOG(INFO) << "dropped inactive sandbox " << manifoldId;
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

  std::chrono::steady_clock::duration inactiveSandboxTimeout_;
  folly::fibers::Semaphore loadSemaphore_;
  bool loadSemaphoreEnabled_;
  // Separate executor so heavy sandbox loads don't starve Thrift serving
  // threads.
  std::shared_ptr<folly::Executor> executor_;
  SandboxFactory factory_;
  facebook::rebalancer::materializer::
      Cache<std::string, std::shared_ptr<Sandbox>>
          sandboxes_;
  folly::ConcurrentHashMap<std::string, SandboxStatus> status_;
  folly::ConcurrentHashMap<std::string, std::chrono::steady_clock::time_point>
      lastAccess_;

  // Async scope for background tasks.
  folly::coro::CancellableAsyncScope scope_;
  folly::CancellationSource cancel_;
};

} // namespace explorer
} // namespace rebalancer
} // namespace facebook
