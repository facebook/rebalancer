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

#include "algopt/rebalancer/treeprof/ThreadMemoryMonitor.h"

#include <folly/Benchmark.h>
#include <folly/BenchmarkUtil.h>
#include <folly/memory/Malloc.h>
#include <gtest/gtest.h>

#include <stdlib.h>
#include <string>

using namespace ::testing;
using namespace facebook::algopt::treeprof;

TEST(ThreadMemoryMonitorTest, Basic) {
  if (!folly::usingJEMalloc()) {
    // TODO: set up unit test to only run in @mode/opt
    return;
  }

  const int tolerance = 1e6;

  ThreadMemoryMonitor::reset();

  EXPECT_NEAR(0, ThreadMemoryMonitor::delta(), tolerance);
  EXPECT_NEAR(0, ThreadMemoryMonitor::peak(), tolerance);

  void* p0 = malloc(4e6);
  folly::doNotOptimizeAway(p0);

  EXPECT_NEAR(4e6, ThreadMemoryMonitor::delta(), tolerance);
  EXPECT_NEAR(4e6, ThreadMemoryMonitor::peak(), tolerance);

  void* p1 = malloc(8e6);
  folly::doNotOptimizeAway(p1);

  EXPECT_NEAR(12e6, ThreadMemoryMonitor::delta(), tolerance);
  EXPECT_NEAR(12e6, ThreadMemoryMonitor::peak(), tolerance);

  free(p1);

  EXPECT_NEAR(4e6, ThreadMemoryMonitor::delta(), tolerance);
  EXPECT_NEAR(12e6, ThreadMemoryMonitor::peak(), tolerance);

  ThreadMemoryMonitor::reset();

  EXPECT_NEAR(0, ThreadMemoryMonitor::delta(), tolerance);
  EXPECT_NEAR(0, ThreadMemoryMonitor::peak(), tolerance);

  void* p2 = malloc(16e6);
  folly::doNotOptimizeAway(p2);

  EXPECT_NEAR(16e6, ThreadMemoryMonitor::delta(), tolerance);
  EXPECT_NEAR(16e6, ThreadMemoryMonitor::peak(), tolerance);

  free(p2);

  void* p3 = malloc(8e6);
  folly::doNotOptimizeAway(p3);

  EXPECT_NEAR(8e6, ThreadMemoryMonitor::delta(), tolerance);
  EXPECT_NEAR(16e6, ThreadMemoryMonitor::peak(), tolerance);

  free(p3);
  free(p0);
}
