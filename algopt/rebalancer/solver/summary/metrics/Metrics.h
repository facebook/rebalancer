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

#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Metrics_types.h"
#include "algopt/rebalancer/materializer/utils/Descriptor.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/GroupRoutingRing.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/summary/metrics/MetricCollection.h"

#include <folly/Synchronized.h>

namespace facebook::rebalancer {

class Metrics {
 public:
  class Builder;

  interface::thrift::Metrics getSummary(
      const entities::Universe& universe,
      const Assignment& assignment) const;

  const entities::Map<
      interface::thrift::MetricCollectionType,
      std::shared_ptr<const MetricCollection>>&
  getAvailableCollections() const;

  void fullApply(const Assignment& assignment) const;

  void pushAllExprsTo(std::vector<Expression*>& exprs) const;

 private:
  explicit Metrics(
      entities::Map<
          interface::thrift::MetricCollectionType,
          std::shared_ptr<const MetricCollection>>&& metricTypeToCollection);

 private:
  const entities::Map<
      interface::thrift::MetricCollectionType,
      std::shared_ptr<const MetricCollection>>
      metricTypeToCollection_;
};

class Metrics::Builder {
 public:
  void addToUtilCollection(
      ExprPtr expr,
      materializer::UtilMetric utilMetric,
      const materializer::Descriptor& descriptor);

  void addToUtilCollection(
      std::shared_ptr<ObjectPartitionLookupDefault> lookup,
      materializer::UtilMetric utilMetric);

  void addToGroupRoutingTrafficCollection(
      std::shared_ptr<GroupRoutingRing> expr,
      entities::RoutingConfigId routingConfigId,
      entities::GroupId groupId);

  void addToGroupRoutingLatencyCollection(
      ExprPtr expr,
      entities::RoutingConfigId routingConfigId,
      const interface::RoutingLatencyMetricInfo& latencyMetric,
      entities::GroupId groupId);

  Metrics build(std::shared_ptr<const entities::Universe> universe);

 private:
  template <typename T>
  std::shared_ptr<T> getCollection(
      interface::thrift::MetricCollectionType type);

  void addToScopeItemUtilCollection(
      ExprPtr expr,
      materializer::UtilMetric utilMetric,
      const materializer::Descriptor& descriptor);

  void addToScopeItemUtilCollection(
      std::shared_ptr<ObjectPartitionLookupDefault> lookup,
      materializer::UtilMetric utilMetric);

  void throwIfAttemptToAddAfterBuilding() const;

 private:
  // all data structures in this class need to be thread-safe
  folly::Synchronized<entities::Map<
      interface::thrift::MetricCollectionType,
      std::shared_ptr<MetricCollection>>>
      metricTypeToCollection_;

  std::atomic<bool> built_{false};
};
} // namespace facebook::rebalancer
