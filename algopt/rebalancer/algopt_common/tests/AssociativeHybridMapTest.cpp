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

#include "algopt/rebalancer/algopt_common/AssociativeHybridMap.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <gtest/gtest.h>

#include <string>

using namespace std;
using namespace facebook::algopt;

TEST(AssociativeHybridMapTest, Sum) {
  SumHybridMap<string, int> sum;
  EXPECT_EQ(0, sum.query());
  sum.init({{"a", 10}, {"b", 20}, {"c", 40}});
  EXPECT_EQ(70, sum.query());
  sum.update("b", 21);
  EXPECT_EQ(71, sum.query());
  sum.remove("a");
  EXPECT_EQ(61, sum.query());
  sum.update("e", 160);
  sum.update("d", 80);
  EXPECT_EQ(301, sum.query());
}

TEST(AssociativeHybridMapTest, QueryLessThanOrEqual) {
  SumHybridMap<string, int> sum;
  sum.init({{"a", 10}, {"d", 40}});
  sum.update("b", 20);
  sum.update("e", 80);
  EXPECT_EQ(0, sum.query_lt(""));
  EXPECT_EQ(0, sum.query_le(""));
  EXPECT_EQ(0, sum.query_lt("a"));
  EXPECT_EQ(10, sum.query_le("a"));
  EXPECT_EQ(10, sum.query_lt("b"));
  EXPECT_EQ(30, sum.query_le("b"));
  EXPECT_EQ(30, sum.query_lt("c"));
  EXPECT_EQ(30, sum.query_le("c"));
  EXPECT_EQ(30, sum.query_lt("d"));
  EXPECT_EQ(70, sum.query_le("d"));
  EXPECT_EQ(70, sum.query_lt("e"));
  EXPECT_EQ(150, sum.query_le("e"));
  EXPECT_EQ(150, sum.query_lt("z"));
  EXPECT_EQ(150, sum.query_le("z"));
}

TEST(AssociativeHybridMapTest, NoNodeTree) {
  SumHybridMap<std::string, int> m;
  m.init({});
}

TEST(AssociativeHybridMapTest, OneNodeTree) {
  SumHybridMap<std::string, int> m;
  m.init({{"a", 10}});
}

TEST(AssociativeHybridMapTest, Init) {
  SumHybridMap<string, int> sum;
  // increasing init size
  std::vector<std::pair<std::string, int>> m;
  m.reserve(10);
  for (const auto i : folly::irange(10)) {
    m.emplace_back(fmt::format("a{}", i), 1);
  }
  sum.init(m);
  EXPECT_EQ(10, sum.query());
  m.clear();
  m.reserve(10000);
  for (const auto i : folly::irange(10000)) {
    m.emplace_back(fmt::format("a{}", i), 1);
  }
  sum.init(m);
  EXPECT_EQ(10000, sum.query());
  m.clear();
  m.reserve(2000000);
  for (const auto i : folly::irange(2000000)) {
    m.emplace_back(fmt::format("a{}", i), 1);
  }
  sum.init(m);
  EXPECT_EQ(2000000, sum.query());

  // decreasing init size
  m.clear();
  m.reserve(10000);
  for (const auto i : folly::irange(10000)) {
    m.emplace_back(fmt::format("a{}", i), 1);
  }
  sum.init(m);
  EXPECT_EQ(10000, sum.query());
  m.clear();
  m.reserve(10);
  for (const auto i : folly::irange(10)) {
    m.emplace_back(fmt::format("a{}", i), 1);
  }
  sum.init(m);
  EXPECT_EQ(10, sum.query());
  m.clear();
  sum.init(m);
  EXPECT_EQ(0, sum.query());
}

TEST(AssociativeHybridMapTest, Remove) {
  SumHybridMap<string, int> sum;
  // Removing from empty map
  sum.remove("a1");
  EXPECT_EQ(0, sum.query());

  std::vector<std::pair<std::string, int>> m;
  m.reserve(10);
  for (const auto i : folly::irange(10)) {
    m.emplace_back(fmt::format("a{}", i), 1);
  }
  sum.init(m);
  EXPECT_EQ(10, sum.query());
  sum.remove("a1");
  EXPECT_EQ(9, sum.query());

  // removing non-existing key
  sum.remove("a1");
  EXPECT_EQ(9, sum.query());
}
