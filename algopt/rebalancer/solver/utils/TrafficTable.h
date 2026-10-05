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

#pragma once

#include "algopt/rebalancer/algopt_common/Precision.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/Universe.h"

#include <fmt/core.h>
#include <folly/container/MapUtil.h>

#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace facebook::rebalancer {

using TrafficLatencyPair = std::pair<
    double, /* traffic from origin to destination expressed as a fraction of the
               total traffic (i.e., sum of traffic from all origins) */
    double /* latency for (origin, destination) pair */>;

template <typename Origin, typename Destination>
using TrafficTable =
    entities::Map<Origin, entities::Map<Destination, TrafficLatencyPair>>;

template <typename Origin, typename Destination>
using LatencyTablePtr =
    std::shared_ptr<entities::Map<Origin, entities::Map<Destination, double>>>;

struct LatencyStats {
  inline void updateLatencyStats(double latency, double traffic) {
    weightedAvgLatency += traffic * latency;

    if (maxLatency < latency) {
      maxLatency = latency;
      maxLatencyTraffic = traffic;
    } else if (maxLatency == latency) {
      maxLatencyTraffic += traffic;
    }
  }

  double weightedAvgLatency = 0.0;
  double maxLatency = std::numeric_limits<double>::lowest();
  double maxLatencyTraffic = 0.0;
};

template <typename Origin, typename Destination>
class TrafficTableWithStats {
 public:
  TrafficTableWithStats() = default;

  inline void appendToTrafficTable(
      Origin origin,
      const entities::Map<Destination, TrafficLatencyPair>&
          destinationToTraffic) {
    if (trafficTable_.contains(origin)) {
      throw std::runtime_error(
          "row corresponding to the given origin already exists; cannot append with same origin.");
    }

    double totalTrafficFromOrigin = 0.0;
    auto& destMap = trafficTable_[origin];
    for (auto& [destination, trafficLatencyPair] : destinationToTraffic) {
      destMap.emplace(destination, trafficLatencyPair);

      auto& [traffic, latency] = trafficLatencyPair;
      latencyStats_.updateLatencyStats(latency, traffic);

      destinationToTraffic_[destination] += traffic;
      totalTrafficFromOrigin += traffic;
    }

    totalTrafficFromAllOrigins_ += totalTrafficFromOrigin;

    // expect totalTrafficFromAllOrigins_ <= 1.0
    if (algopt::Precision::compare(totalTrafficFromAllOrigins_, 1.0) == 1) {
      throw std::runtime_error(
          fmt::format(
              "Expected the total traffic from all origins to not exceed 1.0, but found {}. Do the traffic values for each (origin, destination) pair reflect the fraction of total traffic from all origins to that destination?",
              totalTrafficFromAllOrigins_));
    }
  }

  inline double getWeightedAvgLatency() const {
    throwIfTotalTrafficFromAllOriginsIsNot1();
    return latencyStats_.weightedAvgLatency;
  }

  // total fraction of traffic to a given destination from all origins
  inline double getTotalFractionOfTrafficTo(Destination destination) const {
    throwIfTotalTrafficFromAllOriginsIsNot1();
    return folly::get_default(destinationToTraffic_, destination, 0.0);
  }

  // total fraction of traffic to a given destination from each origin
  inline entities::Map<Origin, double> getTotalFractionOfTrafficFromEachOrigin(
      Destination destination) const {
    throwIfTotalTrafficFromAllOriginsIsNot1();

    entities::Map<Origin, double> originToTraffic;
    for (auto& [origin, destinationsToTrafficLatencyPair] : trafficTable_) {
      auto trafficLatencyPairFromOriginPtr =
          folly::get_ptr(destinationsToTrafficLatencyPair, destination);
      originToTraffic[origin] = trafficLatencyPairFromOriginPtr
          ? trafficLatencyPairFromOriginPtr->first
          : 0.0;
    }

    return originToTraffic;
  }

  inline double getMaxLatency() const {
    throwIfTotalTrafficFromAllOriginsIsNot1();
    return latencyStats_.maxLatency;
  }

  inline double getMaxLatencyTraffic() const {
    throwIfTotalTrafficFromAllOriginsIsNot1();
    return latencyStats_.maxLatencyTraffic;
  }

  inline double getPercentileLatency(double percentileValue) const {
    throwIfTotalTrafficFromAllOriginsIsNot1();

    if (percentileValue <= 0 || percentileValue > 100) {
      throw std::runtime_error(
          "percentile value should be greater than 0 and at most 100");
    }

    // if the amount of traffic that incurs a latency of at most maxLatency is
    // less than the percentile value, then we can just return the maxLatency
    // value
    if (percentileValue > 100 * (1 - latencyStats_.maxLatencyTraffic)) {
      return latencyStats_.maxLatency;
    }

    // build sorted latency to frequency table
    std::map<double, double, std::greater<double>> sortedLatencyToFrequencyMap;
    for (auto& [origin, destinationToTrafficLatencyPair] : trafficTable_) {
      for (auto& [destination, trafficLatencyPair] :
           destinationToTrafficLatencyPair) {
        auto& [traffic, latency] = trafficLatencyPair;
        sortedLatencyToFrequencyMap[latency] += traffic;
      }
    }

    double accumulatedFrequency = 0.0;
    for (auto& [latency, frequency] : sortedLatencyToFrequencyMap) {
      accumulatedFrequency += frequency;

      if (percentileValue > 100 * (1 - accumulatedFrequency)) {
        return latency;
      }
    }

    throw std::runtime_error("could not find percentile value");
  }

  inline const TrafficTable<Origin, Destination>& getTrafficTable() const {
    return trafficTable_;
  }

  // Check if there's non-zero traffic between a source and destination
  inline bool exists(Origin source, Destination destination) const {
    auto dstMapPtr = folly::get_ptr(trafficTable_, source);
    if (!dstMapPtr) {
      return false;
    }
    return dstMapPtr->contains(destination);
  }

  // Get the traffic value between a source and destination
  // Returns 0.0 if there's no traffic between the source and destination
  inline double getTraffic(Origin source, Destination destination) const {
    auto dstMapPtr = folly::get_ptr(trafficTable_, source);
    if (!dstMapPtr) {
      return 0.0;
    }

    auto trafficLatencyPairPtr = folly::get_ptr(*dstMapPtr, destination);
    if (!trafficLatencyPairPtr) {
      return 0.0;
    }

    return trafficLatencyPairPtr->first;
  }

 private:
  inline void throwIfTotalTrafficFromAllOriginsIsNot1() const {
    // after all data is entered, expect totalTrafficFromAllOrigins_ = 1.0
    if (algopt::Precision::compare(totalTrafficFromAllOrigins_, 1.0) != 0.0) {
      throw std::runtime_error(
          fmt::format(
              "Expected total traffic from all origins to equal 1.0, but found {}. Do the traffic values for each (origin, destination) pair reflect the fraction of total traffic from all origins to that destination?",
              totalTrafficFromAllOrigins_));
    }
  }

 private:
  TrafficTable<Origin, Destination> trafficTable_;
  LatencyStats latencyStats_;
  entities::Map<Destination, double> destinationToTraffic_;
  double totalTrafficFromAllOrigins_ = 0.0;
};

} // namespace facebook::rebalancer
