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

#include "algopt/rebalancer/solver/summary/metrics/GroupRoutingLatencyMetrics.h"

#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Metrics_types.h"
#include "algopt/rebalancer/interface/thrift/ThriftUtils.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"

#include <folly/container/F14Map.h>
#include <folly/lang/Assume.h>
#include <folly/Synchronized.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer {
namespace thriftUtils = interface::thriftUtils;

using GroupId = entities::GroupId;
using RoutingConfigId = entities::RoutingConfigId;

interface::thrift::MetricCollectionType GroupRoutingLatencyMetrics::getType()
    const {
  return interface::thrift::MetricCollectionType::GROUP_ROUTING_LATENCY_VALUES;
}

void GroupRoutingLatencyMetrics::add(
    ExprPtr expr,
    RoutingConfigId routingConfigId,
    const interface::RoutingLatencyMetricInfo& latencyMetric,
    GroupId groupId) {
  auto wLockedLatencyExprs = innerCollection_.wlock();
  auto [_, insertSuccess] = wLockedLatencyExprs->emplace(
      std::make_tuple(
          routingConfigId,
          *latencyMetric.type(),
          latencyMetric.percentile().to_optional(),
          groupId),
      std::move(expr));
  throwOnInsertFailure(
      insertSuccess,
      "unexpected failure to insert to groupLatencyMetrics. Duplicates are unexpected since GroupRoutingLatencyLookup exprs are cached in expressionBuilder");
}

void GroupRoutingLatencyMetrics::addToSummary(
    const entities::Universe& universe,
    interface::thrift::Metrics& metricsSummary) const {
  auto rLockedLatencyExprs = innerCollection_.rlock();
  auto& routingConfigToGroupLatencyMetrics =
      *metricsSummary.routingConfigToGroupLatencyMetrics();
  for (auto& [key, expr] : *rLockedLatencyExprs) {
    auto& [configId, metricType, percentile, groupId] = key;
    auto& routingConfigName = universe.getEntityName(configId);
    auto& groupName = universe.getEntityName(groupId);

    interface::thrift::LatencyMetricValue metricValue;
    metricValue.metric() =
        thriftUtils::makeRoutingLatencyMetric(metricType, percentile);
    metricValue.value() = expr->value;

    routingConfigToGroupLatencyMetrics[routingConfigName]
        .groupToMetricValues()[groupName]
        .emplace_back(std::move(metricValue));
  }
}

} // namespace facebook::rebalancer
