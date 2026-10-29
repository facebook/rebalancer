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

#include "algopt/rebalancer/algopt_common/AssociativeFixedMap.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <gtest/gtest.h>

#include <string>

using namespace std;
using namespace facebook::algopt;

TEST(AssociativeFixedMapTest, Basic) {
  SumFixedMap<string, int> sum;
  EXPECT_EQ(0, sum.query());
  sum.init({{"a", 10}, {"b", 20}, {"c", 40}, {"e", 0}, {"d", 0}});
  EXPECT_EQ(70, sum.query());
  sum.update("b", 21);
  EXPECT_EQ(71, sum.query());
  sum.update("a", 0);
  EXPECT_EQ(61, sum.query());
  sum.update("e", 160);
  sum.update("d", 80);
  EXPECT_EQ(301, sum.query());

  EXPECT_EQ(1, sum.count("a"));
  EXPECT_EQ(1, sum.count("e"));
  EXPECT_EQ(1, sum.count("d"));
  EXPECT_EQ(0, sum.count("f"));
}

TEST(AssociativeFixedMapTest, QueryLessThanOrEqual) {
  SumFixedMap<string, int> sum;
  sum.init({{"a", 10}, {"b", 20}, {"d", 40}, {"e", 80}});
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

TEST(AssociativeFixedMapTest, UpdateNotFound) {
  SumFixedMap<string, int> sum;
  sum.init({{"b", 10}, {"d", 40}});
  EXPECT_THROW(sum.update("a", 50), std::runtime_error);
  EXPECT_THROW(sum.update("c", 50), std::runtime_error);
  EXPECT_THROW(sum.update("e", 50), std::runtime_error);
}

TEST(AssociativeFixedMapTest, NoNodeTree) {
  SumFixedMap<std::string, int> m;
  m.init({});
}

TEST(AssociativeFixedMapTest, OneNodeTree) {
  SumFixedMap<std::string, int> m;
  m.init({{"a", 10}});
}

TEST(AssociativeFixedMapTest, MultipleInitCalls) {
  SumFixedMap<string, int> sum;
  // increasing init size
  std::vector<std::pair<std::string, int>> m;
  m.reserve(2000000);
  for (const auto i : folly::irange(10)) {
    m.emplace_back(fmt::format("a{}", i), 1);
  }
  sum.init(m);
  EXPECT_EQ(10, sum.query());
  m.clear();
  for (const auto i : folly::irange(10000)) {
    m.emplace_back(fmt::format("a{}", i), 1);
  }
  sum.init(m);
  EXPECT_EQ(10000, sum.query());
  m.clear();
  for (const auto i : folly::irange(2000000)) {
    m.emplace_back(fmt::format("a{}", i), 1);
  }
  sum.init(m);
  EXPECT_EQ(2000000, sum.query());

  // decreasing init size
  m.clear();
  for (const auto i : folly::irange(10000)) {
    m.emplace_back(fmt::format("a{}", i), 1);
  }
  sum.init(m);
  EXPECT_EQ(10000, sum.query());
  m.clear();
  for (const auto i : folly::irange(10)) {
    m.emplace_back(fmt::format("a{}", i), 1);
  }
  sum.init(m);
  EXPECT_EQ(10, sum.query());
  m.clear();
  sum.init(m);
  EXPECT_EQ(0, sum.query());
}
