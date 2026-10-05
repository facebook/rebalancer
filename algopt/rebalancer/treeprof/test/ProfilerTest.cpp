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

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "algopt/rebalancer/treeprof/Event.h"
#include "algopt/rebalancer/treeprof/EventRecorder.h"
#include "algopt/rebalancer/treeprof/ExecutorWrapper.h"
#include "algopt/rebalancer/treeprof/Profiler.h"
#include "algopt/rebalancer/treeprof/visualizer/EventTreeVisualizer.h"
#include "algopt/rebalancer/treeprof/visualizer/VisualizationFilter.h"

#include "gmock/gmock.h"
#include <folly/BenchmarkUtil.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Collect.h>
#include <folly/coro/Task.h>
#include <folly/ExceptionWrapper.h>
#include <folly/Executor.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/logging/LogStreamProcessor.h>
#include <folly/logging/xlog.h>
#include <folly/memory/Malloc.h>
#include <folly/ScopeGuard.h>
#include <folly/tracing/AsyncStack.h>
#include <gtest/gtest.h>

#include <atomic>
#include <coroutine>
#include <functional>
#include <memory>
#include <ostream>
#include <stdlib.h>
#include <string>
#include <utility>
#include <vector>

using namespace ::testing;
using namespace facebook::algopt::treeprof;

// A simple manual clock for deterministic tests. advance() moves time forward;
// asFn() returns a NowFn suitable for passing to Profiler.
class ManualClock {
 public:
  void advance(double seconds) {
    time_ += seconds;
  }
  double now() const {
    return time_;
  }
  std::function<double()> asFn() {
    return [this]() { return now(); };
  }

 private:
  double time_ = 0.0;
};

// Advance mock time instead of sleeping so tests are instant and deterministic.
void advanceMs(ManualClock& clock, int ms) {
  clock.advance(ms * 0.001);
}

std::shared_ptr<const Event> get(
    std::shared_ptr<const Event> event,
    const std::vector<int>& path) {
  for (const int index : path) {
    event = event->getChildren().at(index);
  }
  return event;
}

void setupBasicTest(ManualClock& clock) {
  auto f1 = [&]() {
    const EventRecorder event("A");
    advanceMs(clock, 10);
  };

  auto f2 = [&]() {
    const EventRecorder event1("B");
    advanceMs(clock, 10);
    {
      const EventRecorder event2("C");
      advanceMs(clock, 10);
      f1();
    }
    f1();
  };

  auto f3 = [&]() {
    const EventRecorder event("D");
    advanceMs(clock, 10);
    f1();
  };

  auto f4 = [&]() {
    const EventRecorder event("E");
    advanceMs(clock, 10);
    f2();
    f3();
  };
  f4();
}

TEST(ProfilerTest, Basic) {
  ManualClock clock;
  Profiler profiler("root", clock.asFn());
  advanceMs(clock, 10);
  setupBasicTest(clock);
  profiler.stop();

  // Expected tree:
  //
  //   root
  //   └─ E
  //      ├─ B
  //      │  ├─ C
  //      │  │  └─ A
  //      │  └─ A
  //      └─ D
  //         └─ A

  auto root = profiler.getRoot();

  EXPECT_EQ("root", root->getName());
  EXPECT_EQ(1, root->getChildren().size());

  auto e = root->getChildren().at(0);

  EXPECT_EQ("E", e->getName());
  EXPECT_EQ(2, e->getChildren().size());
  EXPECT_GE(e->getBeginTime(), root->getBeginTime());
  EXPECT_LE(e->getEndTime(), root->getEndTime());

  auto b = get(e, {0});
  auto d = get(e, {1});

  EXPECT_EQ("B", b->getName());
  EXPECT_EQ(2, b->getChildren().size());
  EXPECT_GE(b->getBeginTime(), e->getBeginTime());
  EXPECT_LE(b->getEndTime(), d->getBeginTime());

  EXPECT_EQ("C", get(b, {0})->getName());
  EXPECT_EQ(1, get(b, {0})->getChildren().size());

  EXPECT_EQ("A", get(b, {0, 0})->getName());
  EXPECT_EQ(0, get(b, {0, 0})->getChildren().size());

  EXPECT_EQ("A", get(b, {1})->getName());
  EXPECT_EQ(0, get(b, {1})->getChildren().size());

  EXPECT_EQ("D", d->getName());
  EXPECT_EQ(1, d->getChildren().size());
  EXPECT_GE(d->getBeginTime(), b->getEndTime());
  EXPECT_LE(d->getEndTime(), e->getEndTime());

  EXPECT_EQ("A", get(d, {0})->getName());
  EXPECT_EQ(0, get(d, {0})->getChildren().size());
}

TEST(ProfilerTest, Coroutines) {
  const int tolerance = 1e6;
  ExecutorWrapper executor(std::make_shared<folly::CPUThreadPoolExecutor>(2));
  std::atomic<int> counter = 0;

  // Increment counter once and wait for it to be incremented a second time by
  // another thread.
  auto f1 = [&](std::string name) -> folly::coro::Task<void> {
    // Allocate memory to be accounted for in the parent event.
    void* p0 = malloc(2e6);
    folly::doNotOptimizeAway(p0);

    const EventRecorder event(std::move(name));

    // Allocate memory to be accounted for in the current event.
    void* p1 = malloc(8e6);
    folly::doNotOptimizeAway(p1);

    // Wait for the other call to reach the same point.
    ++counter;
    while (counter < 2) {
    }

    // Free up all memory while inside the current event.
    free(p1);
    free(p0);

    co_return;
  };

  // Call f1 twice in parallel.
  auto f2 = [&]() -> folly::coro::Task<void> {
    std::vector<folly::coro::TaskWithExecutor<void>> tasks;
    tasks.push_back(
        co_withExecutor(folly::getKeepAliveToken(executor), f1("A")));
    tasks.push_back(
        co_withExecutor(folly::getKeepAliveToken(executor), f1("B")));
    co_await folly::coro::collectAllWindowed(std::move(tasks), 2);
    co_return;
  };

  Profiler profiler("root");
  folly::coro::blockingWait(
      co_withExecutor(folly::getKeepAliveToken(executor), f2()));
  profiler.stop();

  EXPECT_EQ(2, counter);

  auto root = profiler.getRoot();

  EXPECT_EQ("root", root->getName());

  auto children = root->getChildren();
  EXPECT_EQ(2, children.size());

  if (children.at(0)->getName() != "A") {
    std::swap(children.at(0), children.at(1));
  }

  if (folly::usingJEMalloc()) {
    EXPECT_NEAR(0, root->getMemoryDelta(), tolerance);
    EXPECT_NEAR(2e7, root->getMemoryPeak(), tolerance);
  } else {
    EXPECT_EQ(0, root->getMemoryDelta());
    EXPECT_EQ(0, root->getMemoryPeak());
  }

  auto a = children.at(0);
  EXPECT_EQ("A", a->getName());
  EXPECT_EQ(0, a->getChildren().size());
  EXPECT_LE(root->getBeginTime(), a->getBeginTime());
  EXPECT_LE(a->getBeginTime(), a->getEndTime());
  EXPECT_LE(a->getEndTime(), root->getEndTime());

  if (folly::usingJEMalloc()) {
    EXPECT_NEAR(-2e6, a->getMemoryDelta(), tolerance);
    EXPECT_NEAR(8e6, a->getMemoryPeak(), tolerance);
  } else {
    EXPECT_EQ(0, a->getMemoryDelta());
    EXPECT_EQ(0, a->getMemoryPeak());
  }

  auto b = children.at(1);
  EXPECT_EQ("B", b->getName());
  EXPECT_EQ(0, b->getChildren().size());
  EXPECT_LE(root->getBeginTime(), b->getBeginTime());
  EXPECT_LE(b->getBeginTime(), b->getEndTime());
  EXPECT_LE(b->getEndTime(), root->getEndTime());

  if (folly::usingJEMalloc()) {
    EXPECT_NEAR(-2e6, b->getMemoryDelta(), tolerance);
    EXPECT_NEAR(8e6, b->getMemoryPeak(), tolerance);
  } else {
    EXPECT_EQ(0, b->getMemoryDelta());
    EXPECT_EQ(0, b->getMemoryPeak());
  }

  // Events A and B are expected to intersect.
  EXPECT_LE(a->getBeginTime(), b->getEndTime());
  EXPECT_LE(b->getBeginTime(), a->getEndTime());
}

TEST(ProfilerTest, Memory) {
  if (!folly::usingJEMalloc()) {
    // Trivially pass unit test when not using JEMalloc.
    return;
  }

  Profiler profiler("A");

  void* p1 = malloc(2e6);
  folly::doNotOptimizeAway(p1);

  void* p2 = nullptr;

  {
    const EventRecorder event("B");
    p2 = malloc(4e6);
    folly::doNotOptimizeAway(p2);
  }

  {
    const EventRecorder event("C");
    void* p3 = malloc(8e6);
    folly::doNotOptimizeAway(p3);
    free(p3);
  }

  {
    const EventRecorder event("D");
    free(p2);
  }

  free(p1);

  profiler.stop();

  // Expected tree:
  //
  //   A
  //   ├─ B
  //   ├─ C
  //   └─ D

  const int tolerance = 1e6;

  auto a = profiler.getRoot();

  EXPECT_EQ("A", a->getName());
  EXPECT_EQ(3, a->getChildren().size());
  EXPECT_NEAR(14e6, a->getMemoryPeak(), tolerance);
  EXPECT_NEAR(0, a->getMemoryDelta(), tolerance);

  auto b = a->getChildren().at(0);

  EXPECT_EQ("B", b->getName());
  EXPECT_EQ(0, b->getChildren().size());
  EXPECT_NEAR(4e6, b->getMemoryPeak(), tolerance);
  EXPECT_NEAR(4e6, b->getMemoryDelta(), tolerance);

  auto c = a->getChildren().at(1);

  EXPECT_EQ("C", c->getName());
  EXPECT_EQ(0, c->getChildren().size());
  EXPECT_NEAR(8e6, c->getMemoryPeak(), tolerance);
  EXPECT_NEAR(0, c->getMemoryDelta(), tolerance);

  auto d = a->getChildren().at(2);

  EXPECT_EQ("D", d->getName());
  EXPECT_EQ(0, d->getChildren().size());
  EXPECT_NEAR(0, d->getMemoryPeak(), tolerance);
  EXPECT_NEAR(-4e6, d->getMemoryDelta(), tolerance);
}

TEST(ProfilerTest, Memory2) {
  // This test asserts that memory allocations within an event are accounted for
  // regardless of whether they occur on a thread different than the one
  // creating the event. ExecutionWrapper guarantees this behavior.
  if (!folly::usingJEMalloc()) {
    // Trivially pass unit test when not using JEMalloc.
    return;
  }

  const int tolerance = 1e6;
  ExecutorWrapper executor(std::make_shared<folly::CPUThreadPoolExecutor>(2));
  std::atomic<int> counter = 0;

  // Allocates memory and then releases it, while making sure there is overlap
  // of execution with another instance of f1.
  auto f1 = [&]() -> folly::coro::Task<void> {
    void* p = malloc(4e6);
    folly::doNotOptimizeAway(p);

    // Wait for the other call to reach the same point.
    ++counter;
    while (counter < 2) {
    }

    free(p);
    co_return;
  };

  // Call f1 twice in parallel.
  auto f2 = [&]() -> folly::coro::Task<void> {
    std::vector<folly::coro::TaskWithExecutor<void>> tasks;
    // At least one of the f1() calls will be executed on a thread other than
    // the current.
    tasks.push_back(co_withExecutor(folly::getKeepAliveToken(executor), f1()));
    tasks.push_back(co_withExecutor(folly::getKeepAliveToken(executor), f1()));
    co_await folly::coro::collectAllWindowed(std::move(tasks), 2);
    co_return;
  };

  Profiler profiler("root");
  folly::coro::blockingWait(
      co_withExecutor(folly::getKeepAliveToken(executor), f2()));
  profiler.stop();

  auto root = profiler.getRoot();
  EXPECT_EQ("root", root->getName());
  EXPECT_EQ(0, root->getChildren().size());
  EXPECT_NEAR(0, root->getMemoryDelta(), tolerance);
  EXPECT_NEAR(8e6, root->getMemoryPeak(), tolerance);
}

TEST(ProfilerTest, BasicVisualization) {
  ManualClock clock;
  Profiler profiler("root", clock.asFn());
  advanceMs(clock, 10);
  setupBasicTest(clock);
  profiler.stop();
  // Although we can compare the entire visualization, but the vsualization may
  // differ a little bit based on runtimes on different machines, so we just
  // compare the tree structure
  const auto expected =
      "root ······················│\n"
      "   └─E ····················│\n"
      "       ├─B ················│\n"
      "       │   ├─C ············│\n"
      "       │   │   └─A ········│\n"
      "       │   └─A ············│\n"
      "       └─D ················│\n"
      "           └─A ············│\n\n";

  EventTreeVisualizer visualizer(profiler.getRoot());
  XLOG(INFO) << "\n"
             << visualizer.digest(EventTreeVisualizer::MetricType::RUNTIME);

  EXPECT_EQ(
      expected,
      visualizer.digest(
          EventTreeVisualizer::MetricType::RUNTIME,
          true /* only show th tree */));

  // Test filter by name
  EventTreeVisualizer visualizerWithFilter(
      profiler.getRoot(), std::make_shared<EventNameHasWord>("B"));

  auto expectedB =
      "root ··········│\n"
      "   └─E ········│\n"
      "       └─B ····│\n\n";
  EXPECT_EQ(
      expectedB,
      visualizerWithFilter.digest(
          EventTreeVisualizer::MetricType::RUNTIME, true));

  // with filter "A", all nodes should be visible as every node contains A as
  // its descendent
  EventTreeVisualizer visualizerWithFilterA(
      profiler.getRoot(), {std::make_shared<EventNameHasWord>("A")});
  EXPECT_EQ(
      expected,
      visualizerWithFilterA.digest(
          EventTreeVisualizer::MetricType::RUNTIME, true));

  // With the mock clock each leaf event is exactly 10ms. The 15ms threshold
  // keeps only events whose endTime-beginTime > 15ms (C=20ms, B=40ms,
  // D=20ms, E=70ms, root=80ms), filtering out all leaf A events (10ms each).
  EventTreeVisualizer visualizerWithRuntimeFilter(
      profiler.getRoot(), std::make_shared<RuntimeAtLeastX>(0.015));
  const auto expectedWithRuntimeFilter =
      "root ················│\n"
      "   └─E ··············│\n"
      "       ├─B ··········│\n"
      "       │   └─C ······│\n"
      "       └─D ··········│\n\n";
  EXPECT_EQ(
      expectedWithRuntimeFilter,
      visualizerWithRuntimeFilter.digest(
          EventTreeVisualizer::MetricType::RUNTIME, true));
}

TEST(ProfilerTest, Nested) {
  Profiler outer("root");
  {
    REBALANCER_EXPECT_RUNTIME_ERROR(
        Profiler("inner_root"),
        "Nested profilers are not allowed, one treeprof instance already seems to be active in this scope");
  }
  outer.stop();
}

// An EventRecorder on a thread that has never had ThreadMemoryMonitor::reset
// called must not inflate its parent's duration.
TEST(ProfilerTest, EventOnFreshThreadDoesNotInflateRootDuration) {
  folly::CPUThreadPoolExecutor executor(1);
  Profiler profiler("root");
  folly::coro::blockingWait(co_withExecutor(
      folly::getKeepAliveToken(executor), []() -> folly::coro::Task<void> {
        const EventRecorder event("event");
        co_return;
      }()));
  profiler.stop();
  EXPECT_LT(profiler.getRoot()->duration(), 60.0);
}
