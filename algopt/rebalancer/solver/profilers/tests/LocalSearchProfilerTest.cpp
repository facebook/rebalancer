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

#include "algopt/rebalancer/solver/profilers/LocalSearchProfiler.h"

#include <gtest/gtest.h>

using namespace facebook::rebalancer;
using namespace facebook::rebalancer::interface;

static MoveTypeEvent makeEvent(
    int moveTypeIndex,
    double duration,
    double initialValue,
    double finalValue) {
  MoveTypeEvent event;
  event.moveTypeIndex() = moveTypeIndex;
  event.duration() = duration;
  event.initialValue() = initialValue;
  event.finalValue() = finalValue;
  return event;
}

TEST(LocalSearchProfilerTest, LocalSearchProfiler) {
  LocalSearchProfiler profiler(std::vector<std::string>{"a", "b", "c"}, {42.0});

  EXPECT_EQ(std::vector<double>({42.0}), profiler.getCurrentValue());

  profiler.add({makeEvent(0, 0.1, 42.0, 39.0)});

  EXPECT_EQ(std::vector<double>({39.0}), profiler.getCurrentValue());

  EXPECT_EQ(0, profiler.getMoveTypeIndex("a"));
  EXPECT_EQ(1, profiler.getMoveTypeIndex("b"));
  EXPECT_EQ(2, profiler.getMoveTypeIndex("c"));

  profiler.add({makeEvent(0, 0.2, 39.0, 39.0)});
  profiler.add({makeEvent(1, 0.4, 39.0, 39.0)});
  profiler.add({makeEvent(0, 0.8, 39.0, 39.0)});
  profiler.add({makeEvent(2, 1.6, 39.0, 39.0)});

  EXPECT_EQ(std::vector<double>({39.0}), profiler.getCurrentValue());

  profiler.add({makeEvent(0, 3.2, 39.0, 33.0)});

  EXPECT_EQ(std::vector<double>({33.0}), profiler.getCurrentValue());

  auto profiles = profiler.takeProfiles();
  auto& profile = profiles.at(0);

  auto& names = *profile.moveTypeNames();
  EXPECT_EQ(3, names.size());
  EXPECT_EQ("a", names.at(0));
  EXPECT_EQ("b", names.at(1));
  EXPECT_EQ("c", names.at(2));

  auto& events = *profile.moveTypeEvents();
  EXPECT_EQ(5, events.size());

  EXPECT_EQ(profiler.getMoveTypeIndex("a"), *events.at(0).moveTypeIndex());
  EXPECT_EQ(0.1, *events.at(0).duration());
  EXPECT_EQ(42.0, *events.at(0).initialValue());
  EXPECT_EQ(39.0, *events.at(0).finalValue());

  EXPECT_EQ(profiler.getMoveTypeIndex("a"), *events.at(1).moveTypeIndex());
  EXPECT_EQ(1.0, *events.at(1).duration());
  EXPECT_EQ(39.0, *events.at(1).initialValue());
  EXPECT_EQ(39.0, *events.at(1).finalValue());

  EXPECT_EQ(profiler.getMoveTypeIndex("b"), *events.at(2).moveTypeIndex());
  EXPECT_EQ(0.4, *events.at(2).duration());
  EXPECT_EQ(39.0, *events.at(2).initialValue());
  EXPECT_EQ(39.0, *events.at(2).finalValue());

  EXPECT_EQ(profiler.getMoveTypeIndex("c"), *events.at(3).moveTypeIndex());
  EXPECT_EQ(1.6, *events.at(3).duration());
  EXPECT_EQ(39.0, *events.at(3).initialValue());
  EXPECT_EQ(39.0, *events.at(3).finalValue());

  EXPECT_EQ(profiler.getMoveTypeIndex("a"), *events.at(4).moveTypeIndex());
  EXPECT_EQ(3.2, *events.at(4).duration());
  EXPECT_EQ(39.0, *events.at(4).initialValue());
  EXPECT_EQ(33.0, *events.at(4).finalValue());
}

TEST(LocalSearchProfilerTest, MoveTypeProfiler) {
  LocalSearchProfiler profiler(std::vector<std::string>{"a", "b", "c"}, {42.0});

  {
    MoveTypeProfiler move(profiler, "a");
    move.updateValue({39.0});
  }

  {
    const MoveTypeProfiler move(profiler, "a");
  }
  {
    const MoveTypeProfiler move(profiler, "b");
  }
  {
    const MoveTypeProfiler move(profiler, "a");
  }
  {
    const MoveTypeProfiler move(profiler, "c");
  }

  {
    MoveTypeProfiler move(profiler, "a");
    move.updateValue({33.0});
  }

  auto profiles = profiler.takeProfiles();
  auto& profile = profiles.at(0);

  auto& events = *profile.moveTypeEvents();
  EXPECT_EQ(5, events.size());

  EXPECT_EQ(profiler.getMoveTypeIndex("a"), *events.at(0).moveTypeIndex());
  EXPECT_LT(0.0, *events.at(0).duration());
  EXPECT_EQ(42.0, *events.at(0).initialValue());
  EXPECT_EQ(39.0, *events.at(0).finalValue());

  EXPECT_EQ(profiler.getMoveTypeIndex("a"), *events.at(1).moveTypeIndex());
  EXPECT_LT(0.0, *events.at(1).duration());
  EXPECT_EQ(39.0, *events.at(1).initialValue());
  EXPECT_EQ(39.0, *events.at(1).finalValue());

  EXPECT_EQ(profiler.getMoveTypeIndex("b"), *events.at(2).moveTypeIndex());
  EXPECT_LT(0.0, *events.at(2).duration());
  EXPECT_EQ(39.0, *events.at(2).initialValue());
  EXPECT_EQ(39.0, *events.at(2).finalValue());

  EXPECT_EQ(profiler.getMoveTypeIndex("c"), *events.at(3).moveTypeIndex());
  EXPECT_LT(0.0, *events.at(3).duration());
  EXPECT_EQ(39.0, *events.at(3).initialValue());
  EXPECT_EQ(39.0, *events.at(3).finalValue());

  EXPECT_EQ(profiler.getMoveTypeIndex("a"), *events.at(4).moveTypeIndex());
  EXPECT_LT(0.0, *events.at(4).duration());
  EXPECT_EQ(39.0, *events.at(4).initialValue());
  EXPECT_EQ(33.0, *events.at(4).finalValue());
}
