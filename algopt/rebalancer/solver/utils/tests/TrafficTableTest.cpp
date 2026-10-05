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

#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/solver/utils/TrafficTable.h"

#include <boost/iterator/iterator_facade.hpp>
#include <gtest/gtest.h>

#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace facebook::rebalancer::packer::tests {

namespace {
template <typename K, typename V>
std::map<K, V> toStdMap(const entities::Map<K, V>& hashMap) {
  return std::map(hashMap.begin(), hashMap.end());
}
} // namespace

TEST(TrafficTableTest, Basic) {
  TrafficTableWithStats<int, int> table;

  auto row1 = entities::Map<int, TrafficLatencyPair>(
      {{2, {0.1, 10.0}}, {3, {0.3, 0.0}}, {4, {0.2, 50.0}}});
  table.appendToTrafficTable(1, row1);

  auto row2 = entities::Map<int, TrafficLatencyPair>(
      {{2, {0.05, 10.0}}, {3, {0.15, 5.0}}, {4, {0.2, 50.0}}});
  table.appendToTrafficTable(2, row2);

  EXPECT_NEAR(
      table.getWeightedAvgLatency(),
      22.25,
      1e-8); // 0.1*10 + 0.3*0 + 0.2*50 + 0.05*10 + 0.15*5 + 0.2*50

  EXPECT_NEAR(table.getMaxLatency(), 50, 1e-8); // 50

  EXPECT_NEAR(table.getPercentileLatency(100), 50, 1e-8); // 50

  EXPECT_NEAR(
      table.getPercentileLatency(99),
      50,
      1e-8); // 50 (40% of traffic have latency 50)

  EXPECT_NEAR(table.getPercentileLatency(50), 10, 1e-8); // 10

  EXPECT_NEAR(table.getMaxLatencyTraffic(), 0.4, 1e-8);

  EXPECT_NEAR(table.getTotalFractionOfTrafficTo(1), 0.0, 1e-8); // 0.0
  EXPECT_NEAR(table.getTotalFractionOfTrafficTo(2), 0.15, 1e-8); // 0.1 + 0.05
  EXPECT_NEAR(table.getTotalFractionOfTrafficTo(3), 0.45, 1e-8); // 0.3 + 0.15
  EXPECT_NEAR(table.getTotalFractionOfTrafficTo(4), 0.4, 1e-8); // 0.2 + 0.2

  EXPECT_EQ(
      (std::map<int, double>{{1, 0.0}, {2, 0.0}}),
      toStdMap(table.getTotalFractionOfTrafficFromEachOrigin(1)));
  EXPECT_EQ(
      (std::map<int, double>{{1, 0.1}, {2, 0.05}}),
      toStdMap(table.getTotalFractionOfTrafficFromEachOrigin(2)));
  EXPECT_EQ(
      (std::map<int, double>{{1, 0.3}, {2, 0.15}}),
      toStdMap(table.getTotalFractionOfTrafficFromEachOrigin(3)));
  EXPECT_EQ(
      (std::map<int, double>{{1, 0.2}, {2, 0.2}}),
      toStdMap(table.getTotalFractionOfTrafficFromEachOrigin(4)));
}

TEST(TrafficTableTest, BasicMaxUnequalToP99) {
  TrafficTableWithStats<int, int> table;

  auto row1 = entities::Map<int, TrafficLatencyPair>(
      {{2, {0.1, 10.0}}, {3, {0.65, 0.0}}, {4, {0.04, 15.0}}});
  table.appendToTrafficTable(1, row1);

  auto row2 = entities::Map<int, TrafficLatencyPair>(
      {{2, {0.05, 10.0}}, {3, {0.15, 5.0}}, {4, {0.01, 50.0}}});
  table.appendToTrafficTable(2, row2);

  EXPECT_NEAR(
      table.getWeightedAvgLatency(),
      3.35,
      1e-8); // 0.1*10 + 0.65*0 + 0.04*15 + 0.05*10 + 0.15*5 + 0.01*50

  EXPECT_NEAR(table.getMaxLatency(), 50, 1e-8); // 50

  EXPECT_NEAR(table.getPercentileLatency(100), 50, 1e-8); // 50

  EXPECT_NEAR(
      table.getPercentileLatency(99),
      15,
      1e-8); // 15 (1% of traffic has latency 50, rest have at most 15ms)

  EXPECT_NEAR(
      table.getPercentileLatency(99.1),
      50,
      1e-8); // 50 (1% of traffic has latency 50)

  EXPECT_NEAR(
      table.getPercentileLatency(96),
      15,
      1e-8); // 15 (65% has latency 0; 15% has latency 5; 10% has latency 10; 4%
             // has latency 15)

  EXPECT_NEAR(
      table.getPercentileLatency(95),
      10,
      1e-8); // 10 (65% has latency 0; 15% has latency 5; 10% has latency 10)

  EXPECT_NEAR(
      table.getPercentileLatency(90),
      10,
      1e-8); // 10 (65% has latency 0; 15% has latency 5; 10% has latency 10)

  EXPECT_NEAR(
      table.getPercentileLatency(64),
      0.0,
      1e-8); // 65% of the traffic has latency 0

  EXPECT_NEAR(
      table.getPercentileLatency(65),
      0.0,
      1e-8); // 65% of the traffic has latency 0

  EXPECT_NEAR(
      table.getPercentileLatency(66),
      5.0,
      1e-8); // 65% of the traffic has latency 0; next biggest is 5ms

  EXPECT_NEAR(table.getMaxLatencyTraffic(), 0.01, 1e-8);

  EXPECT_NEAR(table.getTotalFractionOfTrafficTo(1), 0.0, 1e-8); // 0.0
  EXPECT_NEAR(table.getTotalFractionOfTrafficTo(2), 0.15, 1e-8); // 0.1 + 0.05
  EXPECT_NEAR(table.getTotalFractionOfTrafficTo(3), 0.8, 1e-8); // 0.65 + 0.15
  EXPECT_NEAR(table.getTotalFractionOfTrafficTo(4), 0.05, 1e-8); // 0.04 + 0.01

  EXPECT_EQ(
      (std::map<int, double>{{1, 0.0}, {2, 0.0}}),
      toStdMap(table.getTotalFractionOfTrafficFromEachOrigin(1)));
  EXPECT_EQ(
      (std::map<int, double>{{1, 0.1}, {2, 0.05}}),
      toStdMap(table.getTotalFractionOfTrafficFromEachOrigin(2)));
  EXPECT_EQ(
      (std::map<int, double>{{1, 0.65}, {2, 0.15}}),
      toStdMap(table.getTotalFractionOfTrafficFromEachOrigin(3)));
  EXPECT_EQ(
      (std::map<int, double>{{1, 0.04}, {2, 0.01}}),
      toStdMap(table.getTotalFractionOfTrafficFromEachOrigin(4)));
}

TEST(TrafficTableTest, ErrorWhenTotalTrafficExceeds1) {
  TrafficTableWithStats<int, int> table1;
  auto row11 = entities::Map<int, TrafficLatencyPair>(
      {{2, {0.3, 10.0}}, {3, {0.6, 0.0}}, {4, {0.12, 50.0}}});
  EXPECT_THROW(table1.appendToTrafficTable(1, row11), std::runtime_error);

  TrafficTableWithStats<int, int> table2;
  auto row21 = entities::Map<int, TrafficLatencyPair>(
      {{2, {0.3, 10.0}}, {3, {0.6, 0.0}}, {4, {0.02, 50.0}}});
  // this entry is fine
  table2.appendToTrafficTable(1, row21);

  auto row22 = entities::Map<int, TrafficLatencyPair>({{2, {0.1, 10.0}}});
  // however with row22 the total traffic exceeds 1, so expect an error
  EXPECT_THROW(table2.appendToTrafficTable(2, row22), std::runtime_error);
}

TEST(TrafficTableTest, ExistsAndGetTraffic) {
  TrafficTableWithStats<int, int> table;

  auto row1 = entities::Map<int, TrafficLatencyPair>(
      {{2, {0.1, 10.0}}, {3, {0.3, 0.0}}, {4, {0.2, 50.0}}});
  table.appendToTrafficTable(1, row1);

  auto row2 = entities::Map<int, TrafficLatencyPair>(
      {{2, {0.05, 10.0}}, {3, {0.15, 5.0}}, {4, {0.2, 50.0}}});
  table.appendToTrafficTable(2, row2);

  // Test exists method
  EXPECT_TRUE(table.exists(1, 2));
  EXPECT_TRUE(table.exists(1, 3));
  EXPECT_TRUE(table.exists(1, 4));
  EXPECT_TRUE(table.exists(2, 2));
  EXPECT_TRUE(table.exists(2, 3));
  EXPECT_TRUE(table.exists(2, 4));
  EXPECT_FALSE(table.exists(1, 1));
  EXPECT_FALSE(table.exists(2, 1));
  EXPECT_FALSE(table.exists(3, 1));

  // Test getTraffic method
  EXPECT_NEAR(table.getTraffic(1, 2), 0.1, 1e-8);
  EXPECT_NEAR(table.getTraffic(1, 3), 0.3, 1e-8);
  EXPECT_NEAR(table.getTraffic(1, 4), 0.2, 1e-8);
  EXPECT_NEAR(table.getTraffic(2, 2), 0.05, 1e-8);
  EXPECT_NEAR(table.getTraffic(2, 3), 0.15, 1e-8);
  EXPECT_NEAR(table.getTraffic(2, 4), 0.2, 1e-8);
  EXPECT_NEAR(table.getTraffic(1, 1), 0.0, 1e-8);
  EXPECT_NEAR(table.getTraffic(2, 1), 0.0, 1e-8);
  EXPECT_NEAR(table.getTraffic(3, 1), 0.0, 1e-8);
}
} // namespace facebook::rebalancer::packer::tests
