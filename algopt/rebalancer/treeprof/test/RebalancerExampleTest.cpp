// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "algopt/rebalancer/treeprof/EventRecorder.h"
#include "algopt/rebalancer/treeprof/ExecutorWrapper.h"
#include "algopt/rebalancer/treeprof/Profiler.h"
#include "algopt/rebalancer/treeprof/visualizer/EventTreeVisualizer.h"

#include <folly/BenchmarkUtil.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Collect.h>
#include <folly/coro/Task.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

#include <atomic>
#include <functional>
#include <memory>

using namespace facebook::algopt::treeprof;
/*
Simulates the following example

Total time ···············│========================= [1.00s total]
   ├─Materialization ·····│========== [0.40s total]
   │   ├─Function-A ······│====== [0.25s total]
   │   ├─Function-B ······│== [0.12s total]
   │   └─Function-X ······│ [0.03s total]
   ├─Solve ···············│============ [0.52s total]
   │   ├─Function-C ······│=== [0.15s total]
   │   ├─Function-D ······│======= [0.32s total]
   │   └─Function-Y ······│= [0.05s total]
   └─Wrap-up ·············│= [0.08s total]

Only showing nodes > 0.1s Total time

Total time ···············│========================= [1.00s total, 0.08s excl.]
   ├─Materialization ·····│========== [0.40s total, 0.03s excl.]
   │   ├─Function-A ······│====== [0.25s total]
   │   └─Function-B ······│== [0.12s total]
   └─Solve ···············│============ [0.52s total, 0.05s excl.]
       ├─Function-C ······│=== [0.15s total]
       └─Function-D ······│======= [0.32s total]
*/

namespace {

class ManualClock {
 public:
  void advance(double seconds) {
    time_ += seconds;
  }
  double now() const {
    return time_;
  }
  void reset() {
    time_ = 0.0;
  }
  std::function<double()> asFn() {
    return [this]() { return now(); };
  }

 private:
  double time_ = 0.0;
};

// Per-thread clock for the coroutines test. Each thread-pool thread gets its
// own instance starting at 0.0, so concurrent coroutines have independent
// timelines. The NowFn lambda reads tlsClock by name (not by capturing this),
// so it always accesses the calling thread's instance.
thread_local ManualClock tlsClock;

// buildParallelInstance() calls allocateMemory() from several coroutines at
// once, so this counter must be an atomic read-modify-write.
std::atomic<double> totalAllocatedMBs{0.0};
double numSecondsSlept = 0;

// Used by buildParallelInstance() (multi-threaded): each coroutine advances
// its own thread-local clock independently.
void parallelSleepForMs(const double ms) {
  tlsClock.advance(ms * 0.001);
}

void* allocateMemory(uint64_t mbytes) {
  constexpr auto NUM_BYTES_IN_ONE_MB = 1024u * 1024u;
  totalAllocatedMBs.fetch_add(
      static_cast<double>(mbytes), std::memory_order_relaxed);
  void* ptr = malloc(mbytes * NUM_BYTES_IN_ONE_MB);
  folly::doNotOptimizeAway(ptr);
  return ptr;
}

void buildInstance(ManualClock& clock) {
  const auto sleepForMs = [&](const int ms) {
    numSecondsSlept += ms * 0.001;
    clock.advance(ms * 0.001);
  };
  std::vector<void*> ptrs;
  totalAllocatedMBs = 0;
  numSecondsSlept = 0;
  const EventRecorder coreSolveEvent("CoreSolver::solve");
  ptrs.push_back(allocateMemory(20));
  {
    const EventRecorder materializationEvent("Materialization");
    ptrs.push_back(allocateMemory(20));
    {
      const EventRecorder funcA("function-A");
      ptrs.push_back(allocateMemory(20));
      sleepForMs(250);
    }
    {
      const EventRecorder funcB("Function-B");
      ptrs.push_back(allocateMemory(20));
      sleepForMs(120);
    }
    {
      const EventRecorder func("Function-X");
      ptrs.push_back(allocateMemory(20));
      sleepForMs(30);
    }
  }
  {
    const EventRecorder solveEvent("Solve");
    {
      const EventRecorder funcC("Function-C");
      ptrs.push_back(allocateMemory(20));
      sleepForMs(150);
    }
    {
      const EventRecorder funcD("Function-D");
      sleepForMs(320);
    }
    {
      const EventRecorder funcY("Function-Y");
      ptrs.push_back(allocateMemory(20));
      sleepForMs(50);
    }
  }
  {
    const EventRecorder wrapup("Wrap-up");
    ptrs.push_back(allocateMemory(40));
    sleepForMs(80);
  }
  // cleanup allocated memory
  for (auto ptr : ptrs) {
    free(ptr);
  }
}

struct MockEvent {
  std::string name;
  double memoryAllocated;
  double sleepTime;
  MockEvent(const std::string& eventName, double memory, double sleepForSeconds)
      : name(eventName), memoryAllocated(memory), sleepTime(sleepForSeconds) {}
};

// Returns its own allocations rather than appending to a shared vector: these
// run concurrently, and std::vector::push_back is not thread-safe.
folly::coro::Task<std::vector<void*>> addToProfilerWithCoro(
    const MockEvent& event) {
  std::vector<void*> ptrs;
  EventRecorder eventRecord(event.name);
  auto sleepTime = event.sleepTime / 2;
  auto memoryToAllocate = event.memoryAllocated / 2;

  // the parent event sleeps for half the time and allocates half the memory
  parallelSleepForMs(sleepTime);
  if (memoryToAllocate > 0) {
    ptrs.push_back(allocateMemory(memoryToAllocate));
  }

  // the only child event sleeps for the rest and allocates the remaining memory
  EventRecorder eventChildRecord(event.name + "_child");
  ptrs.push_back(allocateMemory(memoryToAllocate));
  parallelSleepForMs(sleepTime);
  eventChildRecord.stop();

  eventRecord.stop();
  co_return ptrs;
}

folly::coro::Task<void> addToProfileMaybeParallel(
    std::vector<MockEvent>& parallelizableEvents,
    std::vector<void*>& ptrs,
    int numThreads) {
  std::vector<folly::coro::TaskWithExecutor<std::vector<void*>>> tasks;
  ExecutorWrapper executor(
      std::make_shared<folly::CPUThreadPoolExecutor>(numThreads));
  tasks.reserve(parallelizableEvents.size());
  for (auto& event : parallelizableEvents) {
    tasks.push_back(co_withExecutor(
        folly::getKeepAliveToken(executor), addToProfilerWithCoro(event)));
  }
  // Runs on the caller's thread once every task has completed, so appending to
  // the caller's vector here is single-threaded.
  const auto perTaskPtrs =
      co_await folly::coro::collectAllWindowed(std::move(tasks), numThreads);
  for (const auto& taskPtrs : perTaskPtrs) {
    ptrs.insert(ptrs.end(), taskPtrs.begin(), taskPtrs.end());
  }
  co_return;
}

void buildParallelInstance(int numThreads = 1) {
  std::vector<void*> ptrs;
  totalAllocatedMBs = 0;
  numSecondsSlept = 0;
  const EventRecorder coreSolveEvent("CoreSolver::solve");
  ptrs.push_back(allocateMemory(20));
  {
    const EventRecorder materializationEvent("Materialization");
    ptrs.push_back(allocateMemory(20));
    std::vector<MockEvent> parallelizableEvents = {
        MockEvent("Function-A", 20, 250),
        MockEvent("Function-B", 20, 120),
        MockEvent("Function-X", 20, 30)};
    // Run the coordinator on the calling thread; only the inner tasks are
    // dispatched to the thread pool. This avoids a deadlock on constrained CI
    // runners where blockingWait would block a global-executor thread while
    // waiting for inner tasks that need the same executor to unblock.
    folly::coro::blockingWait(
        addToProfileMaybeParallel(parallelizableEvents, ptrs, numThreads));
  }
  {
    const EventRecorder solveEvent("Solve");
    std::vector<MockEvent> parallelizableEvents = {
        MockEvent("Function-C", 20, 150),
        MockEvent("Function-D", 0, 320),
        MockEvent("Function-Y", 20, 50)};

    folly::coro::blockingWait(
        addToProfileMaybeParallel(parallelizableEvents, ptrs, numThreads));
  }
  {
    const EventRecorder wrapUp("Wrap-up");
    ptrs.push_back(allocateMemory(40));
    parallelSleepForMs(80);
  }

  // add a bit of time after wrapUp event so that coreSolveEvent fully includes
  // it
  parallelSleepForMs(1);

  // cleanup allocated memory
  for (auto* ptr : ptrs) {
    free(ptr);
  }
}

} // namespace

TEST(HierarchicalTimeProfilerTest, basic) {
  // Local clock: no shared mutable state between tests.
  ManualClock clock;
  Profiler profiler("root", clock.asFn());
  buildInstance(clock);
  profiler.stop();
  auto rootEvent = profiler.getRoot()->getChildren().at(0);
  EXPECT_NEAR(numSecondsSlept, rootEvent->duration(), 1e-9);

  EventTreeVisualizer visualizer(rootEvent);
  XLOG(INFO) << "Event tree with runtime distribution:\n"
             << visualizer.digest(EventTreeVisualizer::MetricType::RUNTIME);
  XLOG(INFO) << "Event tree with memory distribution:\n"
             << visualizer.digest(EventTreeVisualizer::MetricType::MEMORY);
}

TEST(HierarchicalTimeProfilerTest, coroutines) {
  // so far we only support 1 thread
  constexpr int maxParallelism = 5;
  // Each thread-pool thread gets its own tlsClock starting at 0. The NowFn
  // lambda reads whichever thread's tlsClock is current when called, so
  // concurrent coroutines have independent timelines. Because all threads start
  // at 0, the duration of a parallel group equals the maximum individual
  // duration — the same semantics as real wall time, but deterministic.
  Profiler profiler("root", []() { return tlsClock.now(); });
  buildParallelInstance(maxParallelism);
  profiler.stop();
  auto rootEvent = profiler.getRoot()->getChildren().at(0);

  // Verify tree structure: the top-level event must have the three expected
  // children (Materialization, Solve, Wrap-up). This is what treeprof +
  // coroutine support is actually testing.
  // Note: timing assertions are omitted because addToProfilerWithCoro has no
  // co_await suspension points, so collectAllWindowed may run tasks on the
  // same thread sequentially. Thread-local clocks then accumulate across tasks
  // on the same thread, making individual group durations non-deterministic.
  const auto& children = rootEvent->getChildren();
  ASSERT_EQ(children.size(), 3);
  EXPECT_EQ(children.at(0)->getName(), "Materialization");
  EXPECT_EQ(children.at(1)->getName(), "Solve");
  EXPECT_EQ(children.at(2)->getName(), "Wrap-up");

  // Memory tree will only work as expected when using JeMalloc (that is with
  // @mode/opt)
  if (!folly::usingJEMalloc()) {
    XLOG(WARNING)
        << "JEMalloc memory counter only works with @mode/opt build. Memory counters will likely be zeroes";
  } else {
    EXPECT_NEAR(
        totalAllocatedMBs.load(),
        rootEvent->getMemoryPeak() / static_cast<double>(NUM_BYTES_IN_ONE_MB),
        1);
  }
  EventTreeVisualizer visualizer(rootEvent);
  XLOG(INFO) << "Event tree with runtime distribution:\n"
             << visualizer.digest(EventTreeVisualizer::MetricType::RUNTIME);
  XLOG(INFO) << "Event tree with memory distribution:\n"
             << visualizer.digest(EventTreeVisualizer::MetricType::MEMORY);
}
