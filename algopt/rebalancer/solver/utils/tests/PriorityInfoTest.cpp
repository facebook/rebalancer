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
#include "algopt/rebalancer/solver/utils/PriorityInfo.h"

#include <gtest/gtest.h>

#include <string>

namespace facebook::rebalancer::packer::tests {

TEST(TrafficTableTest, Equality) {
  const PriorityInfo p1{.rootIdx = 1, .height = 1};
  const PriorityInfo p2{.rootIdx = 1, .height = 1};
  const PriorityInfo p3{.rootIdx = 2, .height = 1};
  const PriorityInfo p4{.rootIdx = 1, .height = 2};

  EXPECT_TRUE(p1 == p2);
  EXPECT_FALSE(p1 == p3);
  EXPECT_FALSE(p1 == p4);
}

TEST(TrafficTableTest, Greater) {
  const PriorityInfo p1{.rootIdx = 1, .height = 1};
  const PriorityInfo p2{.rootIdx = 1, .height = 1};
  const PriorityInfo p3{.rootIdx = 2, .height = 1};
  const PriorityInfo p4{.rootIdx = 1, .height = 2};

  EXPECT_FALSE(p1 > p2);
  EXPECT_FALSE(p2 > p1);

  EXPECT_TRUE(p1 > p3);
  EXPECT_FALSE(p3 > p1);

  EXPECT_TRUE(p1 > p4);
  EXPECT_FALSE(p4 > p1);

  EXPECT_TRUE(p2 > p3);
  EXPECT_FALSE(p3 > p2);

  EXPECT_TRUE(p2 > p4);
  EXPECT_FALSE(p4 > p2);
}

TEST(TrafficTableTest, Less) {
  const PriorityInfo p1{.rootIdx = 1, .height = 1};
  const PriorityInfo p2{.rootIdx = 1, .height = 1};
  const PriorityInfo p3{.rootIdx = 2, .height = 1};
  const PriorityInfo p4{.rootIdx = 10, .height = 2};

  EXPECT_FALSE(p1 < p2);
  EXPECT_FALSE(p2 < p1);

  EXPECT_FALSE(p1 < p3);
  EXPECT_TRUE(p3 < p1);

  EXPECT_FALSE(p1 < p4);
  EXPECT_TRUE(p4 < p1);

  EXPECT_FALSE(p2 < p3);
  EXPECT_TRUE(p3 < p2);

  EXPECT_FALSE(p2 < p4);
  EXPECT_TRUE(p4 < p2);
}
} // namespace facebook::rebalancer::packer::tests
