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
#include "algopt/rebalancer/treeprof/EventRecorder.h"
#include "algopt/rebalancer/treeprof/visualizer/EventTreeVisualizer.h"
#include "algopt/rebalancer/treeprof/visualizer/VisualizationFilter.h"

#include <fmt/core.h>
#include <folly/container/irange.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <ranges>
#include <string>
#include <utility>

using namespace ::testing;
using namespace facebook::algopt::treeprof;

std::shared_ptr<Event>
makeLeafEvent(std::string name, double start, double finish) {
  auto event = std::make_shared<Event>(std::move(name));
  event->addInterval(
      Interval{
          .beginTime = start,
          .endTime = finish,
          .memoryDelta = 0,
          .memoryPeak = 0});
  event->finalize();
  return event;
}

std::shared_ptr<Event> buildTwoLevelTree() {
  auto runningTime = 0;
  constexpr int width = 4;

  auto root = std::make_shared<Event>("root");
  for (const auto i : folly::irange(1, width + 1)) {
    auto outerEvent = std::make_shared<Event>(fmt::format("outer_{}", i));
    for (const auto j : folly::irange(1, width + 1)) {
      auto timeBefore = runningTime;
      runningTime += j;
      outerEvent->addChild(makeLeafEvent(
          fmt::format("inner_{}_{}", i, j), timeBefore, runningTime));
    }
    outerEvent->finalize();
    root->addChild(outerEvent);
  }
  root->finalize();
  return root;
}

std::string expected_full =
    "root ····················│==================================================================================================== [40.00s total]\n"
    "   ├─outer_1 ············│========================= [10.00s total]\n"
    "   │   ├─inner_1_1 ······│=== [1.00s total]\n"
    "   │   ├─inner_1_2 ······│===== [2.00s total]\n"
    "   │   ├─inner_1_3 ······│======== [3.00s total]\n"
    "   │   └─inner_1_4 ······│========== [4.00s total]\n"
    "   ├─outer_2 ············│========================= [10.00s total]\n"
    "   │   ├─inner_2_1 ······│=== [1.00s total]\n"
    "   │   ├─inner_2_2 ······│===== [2.00s total]\n"
    "   │   ├─inner_2_3 ······│======== [3.00s total]\n"
    "   │   └─inner_2_4 ······│========== [4.00s total]\n"
    "   ├─outer_3 ············│========================= [10.00s total]\n"
    "   │   ├─inner_3_1 ······│=== [1.00s total]\n"
    "   │   ├─inner_3_2 ······│===== [2.00s total]\n"
    "   │   ├─inner_3_3 ······│======== [3.00s total]\n"
    "   │   └─inner_3_4 ······│========== [4.00s total]\n"
    "   └─outer_4 ············│========================= [10.00s total]\n"
    "       ├─inner_4_1 ······│=== [1.00s total]\n"
    "       ├─inner_4_2 ······│===== [2.00s total]\n"
    "       ├─inner_4_3 ······│======== [3.00s total]\n"
    "       └─inner_4_4 ······│========== [4.00s total]\n\n";

auto expected_non_inner =
    "root ············│==================================================================================================== [40.00s total]\n"
    "   ├─outer_1 ····│========================= [10.00s total, 10.00s excl.]\n"
    "   ├─outer_2 ····│========================= [10.00s total, 10.00s excl.]\n"
    "   ├─outer_3 ····│========================= [10.00s total, 10.00s excl.]\n"
    "   └─outer_4 ····│========================= [10.00s total, 10.00s excl.]\n\n";

TEST(VisualizationFilterTest, basic) {
  auto root = buildTwoLevelTree();
  EXPECT_EQ(
      expected_full,
      EventTreeVisualizer(root).digest(
          EventTreeVisualizer::MetricType::RUNTIME));
  EXPECT_EQ(
      expected_non_inner,
      EventTreeVisualizer(root, std::make_shared<EventNameHasWord>("outer"))
          .digest(EventTreeVisualizer::MetricType::RUNTIME));
}

TEST(VisualizationFilterTest, Not) {
  auto root = buildTwoLevelTree();
  auto notInner =
      std::make_shared<Not>(std::make_shared<EventNameHasWord>("inner"));
  EXPECT_EQ(
      expected_non_inner,
      EventTreeVisualizer(root, notInner)
          .digest(EventTreeVisualizer::MetricType::RUNTIME));
}

TEST(VisualizationFilterTest, Or) {
  auto root = buildTwoLevelTree();
  auto filter_1_4 = std::make_shared<EventNameHasWord>("_1_4");
  auto filter_2_4 = std::make_shared<EventNameHasWord>("_2_4");
  auto filter_3_4 = std::make_shared<EventNameHasWord>("_3_4");
  auto filter_4_4 = std::make_shared<EventNameHasWord>("_4_4");

  auto filter_4 = std::make_shared<Or>(
      std::make_shared<Or>(filter_1_4, filter_2_4),
      std::make_shared<Or>(filter_3_4, filter_4_4));

  auto expected_filter_4 =
      "root ····················│==================================================================================================== [40.00s total]\n"
      "   ├─outer_1 ············│========================= [10.00s total, 6.00s excl.]\n"
      "   │   └─inner_1_4 ······│========== [4.00s total]\n"
      "   ├─outer_2 ············│========================= [10.00s total, 6.00s excl.]\n"
      "   │   └─inner_2_4 ······│========== [4.00s total]\n"
      "   ├─outer_3 ············│========================= [10.00s total, 6.00s excl.]\n"
      "   │   └─inner_3_4 ······│========== [4.00s total]\n"
      "   └─outer_4 ············│========================= [10.00s total, 6.00s excl.]\n"
      "       └─inner_4_4 ······│========== [4.00s total]\n\n";
  EXPECT_EQ(
      expected_filter_4,
      EventTreeVisualizer(root, filter_4)
          .digest(EventTreeVisualizer::MetricType::RUNTIME));
}

TEST(VisualizationFilterTest, And) {
  auto root = buildTwoLevelTree();
  auto filter_inner = std::make_shared<EventNameHasWord>("inner");
  auto filter_2 = std::make_shared<EventNameHasWord>("r_2");
  auto filter_4 = std::make_shared<EventNameHasWord>("_4");

  auto filter_inner_2_4 = std::make_shared<And>(
      std::make_shared<And>(filter_inner, filter_2), filter_4);

  auto expected =
      "root ··················│==================================================================================================== [40.00s total, 30.00s excl.]\n"
      "   └─outer_2 ··········│========================= [10.00s total, 6.00s excl.]\n"
      "       └─inner_2_4 ····│========== [4.00s total]\n\n";

  EXPECT_EQ(
      expected,
      EventTreeVisualizer(root, filter_inner_2_4)
          .digest(EventTreeVisualizer::MetricType::RUNTIME));
}

TEST(VisualizationFilterTest, runtime) {
  auto root = buildTwoLevelTree();
  EXPECT_EQ(
      expected_non_inner,
      EventTreeVisualizer(root, std::make_shared<RuntimeAtLeastX>(10))
          .digest(EventTreeVisualizer::MetricType::RUNTIME));
}
