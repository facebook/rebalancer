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

#include "gmock/gmock.h"
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace ::testing;
using namespace facebook::algopt::treeprof;

TEST(EventTest, Basic) {
  auto a = std::make_shared<Event>("a");
  a->addInterval(
      Interval{
          .beginTime = 10, .endTime = 20, .memoryDelta = 30, .memoryPeak = 40});
  a->finalize();

  auto b = std::make_shared<Event>("b");
  a->addChild(b);

  auto c = std::make_shared<Event>("c");
  a->addChild(c);

  EXPECT_EQ("a", a->getName());
  EXPECT_EQ(10, a->getBeginTime());
  EXPECT_EQ(20, a->getEndTime());
  EXPECT_EQ(30, a->getMemoryDelta());
  EXPECT_EQ(40, a->getMemoryPeak());
  EXPECT_EQ(2, a->getChildren().size());
  EXPECT_EQ(b, a->getChildren().at(0));
  EXPECT_EQ(c, a->getChildren().at(1));
}

TEST(EventTest, NotFinalized) {
  const Event e("e");
  REBALANCER_EXPECT_RUNTIME_ERROR(e.getBeginTime(), "event not finalized");
  REBALANCER_EXPECT_RUNTIME_ERROR(e.getEndTime(), "event not finalized");
  REBALANCER_EXPECT_RUNTIME_ERROR(e.getMemoryDelta(), "event not finalized");
  REBALANCER_EXPECT_RUNTIME_ERROR(e.getMemoryPeak(), "event not finalized");
}

TEST(EventTest, Empty) {
  Event e("e");
  REBALANCER_EXPECT_RUNTIME_ERROR(e.finalize(), "empty event");
}

TEST(EventTest, Finalize) {
  auto a = std::make_shared<Event>("a");
  a->addInterval(
      Interval{
          .beginTime = 0, .endTime = 1, .memoryDelta = 10, .memoryPeak = 20});
  a->addInterval(
      Interval{
          .beginTime = 2, .endTime = 3, .memoryDelta = -5, .memoryPeak = 15});
  a->addInterval(
      Interval{
          .beginTime = 4, .endTime = 6, .memoryDelta = 0, .memoryPeak = 10});

  auto b = std::make_shared<Event>("b");
  b->addInterval(
      Interval{
          .beginTime = 5, .endTime = 7, .memoryDelta = 0, .memoryPeak = 20});
  b->finalize();

  a->addChild(b);
  a->finalize();

  EXPECT_EQ(0, a->getBeginTime());
  EXPECT_EQ(7, a->getEndTime());
  EXPECT_EQ(5, a->getMemoryDelta());
  EXPECT_EQ(35, a->getMemoryPeak());

  EXPECT_EQ(5, b->getBeginTime());
  EXPECT_EQ(7, b->getEndTime());
  EXPECT_EQ(0, b->getMemoryDelta());
  EXPECT_EQ(20, b->getMemoryPeak());
}
