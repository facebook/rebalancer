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

#include "algopt/rebalancer/treeprof/Event.h"
#include "algopt/rebalancer/treeprof/EventHolder.h"
#include "algopt/rebalancer/treeprof/ExecutorWrapper.h"

#include <folly/Benchmark.h>
#include <folly/BenchmarkUtil.h>
#include <folly/Executor.h>
#include <folly/Function.h>
#include <folly/io/async/Request.h>
#include <folly/memory/Malloc.h>
#include <gtest/gtest.h>

#include <memory>
#include <stdlib.h>
#include <string>
#include <thread>
#include <utility>

using namespace ::testing;
using namespace facebook::algopt::treeprof;

// Join-able single thread executor.
class SingleThreadExecutor : public folly::Executor {
 public:
  void add(folly::Func function) override {
    thread_ =
        std::thread([function = std::move(function)]() mutable { function(); });
  }

  void join() {
    thread_.join();
  }

 private:
  std::thread thread_;
};

TEST(ExecutorWrapperTest, Basic) {
  auto executor = std::make_shared<SingleThreadExecutor>();
  auto wrapper = std::make_shared<ExecutorWrapper>(executor);

  const folly::RequestContextScopeGuard request;
  auto event = std::make_shared<Event>("root");
  folly::RequestContext::get()->setContextData(
      EventHolder::key(), std::make_unique<EventHolder>(event));

  // Schedule a task which allocates 4 MB of memory.
  wrapper->add([]() {
    void* p = malloc(4e6);
    folly::doNotOptimizeAway(p);
    free(p);
  });

  // Wait for the above task to complete.
  executor->join();

  event->finalize();

  if (folly::usingJEMalloc()) {
    const int tolerance = 1e6;
    EXPECT_NEAR(0, event->getMemoryDelta(), tolerance);
    EXPECT_NEAR(4e6, event->getMemoryPeak(), tolerance);
  } else {
    EXPECT_EQ(0, event->getMemoryDelta());
    EXPECT_EQ(0, event->getMemoryPeak());
  }
}
