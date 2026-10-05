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

#include "algopt/rebalancer/solver/summary/metrics/tests/GroupRoutingLatencyMetricsTest.h"

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "algopt/rebalancer/entities/Containers.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Metrics_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/interface/thrift/ThriftUtils.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/summary/metrics/GroupRoutingLatencyMetrics.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/Context.h"

#include "gmock/gmock.h"
#include <folly/container/F14Map.h>
#include <folly/lang/Assume.h>
#include <folly/lang/Hint.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <memory>
#include <optional>

namespace thriftUtils = facebook::rebalancer::interface::thriftUtils;

namespace facebook::rebalancer::packer::tests {

TEST_F(GroupRoutingLatencyMetricsTest, AddAndGetSummary) {
  const auto universe = buildUniverse();
  const auto routingConfig1Id = routingConfigId("routingConfig1");
  const auto routingConfig2Id = routingConfigId("routingConfig2");
  const auto tenant = partitionId("tenant");
  const auto tenant1Id = groupId(tenant, "tenant1");
  const auto tenant2Id = groupId(tenant, "tenant2");

  // Create latency metrics
  auto p99Metric = thriftUtils::makeRoutingLatencyMetric(
      interface::RoutingLatencyMetric::PERCENTILE, 99);
  auto p100Metric = thriftUtils::makeRoutingLatencyMetric(
      interface::RoutingLatencyMetric::PERCENTILE, 100);
  auto avgMetric = thriftUtils::makeRoutingLatencyMetric(
      interface::RoutingLatencyMetric::AVG);

  // Add some latency metrics
  auto p99Expr = const_expr(13, *universe);
  auto p100Expr = const_expr(20, *universe);
  auto avgExpr = const_expr(5, *universe);

  metrics.add(p99Expr, routingConfig1Id, p99Metric, tenant1Id);
  metrics.add(p100Expr, routingConfig1Id, p100Metric, tenant1Id);
  metrics.add(avgExpr, routingConfig2Id, avgMetric, tenant2Id);
  metrics.buildRootExpr(universe);

  // test rootExpr
  const auto rootExpr = metrics.getRootExprRawPtr();
  EXPECT_TRUE(rootExpr != nullptr);
  EXPECT_EQ(3, rootExpr->children().size());
  EXPECT_TRUE(rootExpr->children().contains(p99Expr));
  EXPECT_TRUE(rootExpr->children().contains(p100Expr));
  EXPECT_TRUE(rootExpr->children().contains(avgExpr));

  // Verify the type
  EXPECT_EQ(
      metrics.getType(),
      interface::thrift::MetricCollectionType::GROUP_ROUTING_LATENCY_VALUES);

  // Get metrics summary
  interface::thrift::Metrics metricsSummary;
  Context context;
  metrics.applyAndAddToSummary(
      Assignment(universe->getContainers().getInitialAssignment()),
      context,
      *universe,
      metricsSummary);

  // Verify the summary
  auto& routingConfigToGroupLatencyMetrics =
      *metricsSummary.routingConfigToGroupLatencyMetrics();

  // Check routing config 1
  EXPECT_TRUE(routingConfigToGroupLatencyMetrics.contains("routingConfig1"));
  auto& config1Metrics =
      routingConfigToGroupLatencyMetrics.at("routingConfig1");
  EXPECT_TRUE(config1Metrics.groupToMetricValues()->contains("tenant1"));
  auto& tenant1Metrics = config1Metrics.groupToMetricValues()->at("tenant1");
  EXPECT_EQ(tenant1Metrics.size(), 2);

  // Check p99 and p100 metrics for tenant1 in routingConfig1
  bool foundP99 = false;
  bool foundP100 = false;
  for (const auto& metricValue : tenant1Metrics) {
    if (*metricValue.metric()->type() ==
        interface::RoutingLatencyMetric::PERCENTILE) {
      if (*metricValue.metric()->percentile() == 99) {
        EXPECT_NEAR(*metricValue.value(), 13, 1e-8);
        foundP99 = true;
      } else if (*metricValue.metric()->percentile() == 100) {
        EXPECT_NEAR(*metricValue.value(), 20, 1e-8);
        foundP100 = true;
      }
    }
  }
  EXPECT_TRUE(foundP99);
  EXPECT_TRUE(foundP100);

  // Check routing config 2
  EXPECT_TRUE(routingConfigToGroupLatencyMetrics.contains("routingConfig2"));
  auto& config2Metrics =
      routingConfigToGroupLatencyMetrics.at("routingConfig2");
  EXPECT_TRUE(config2Metrics.groupToMetricValues()->contains("tenant2"));
  auto& tenant2Metrics = config2Metrics.groupToMetricValues()->at("tenant2");
  EXPECT_EQ(tenant2Metrics.size(), 1);

  // Check avg metric for tenant2 in routingConfig2
  EXPECT_EQ(
      *tenant2Metrics[0].metric()->type(),
      interface::RoutingLatencyMetric::AVG);
  EXPECT_NEAR(*tenant2Metrics[0].value(), 5, 1e-8);
}

TEST_F(GroupRoutingLatencyMetricsTest, DuplicateInsertFailure) {
  const auto universe = buildUniverse();
  auto routingConfig1Id = routingConfigId("routingConfig1");
  const auto tenantId = partitionId("tenant");
  auto tenant1Id = groupId(tenantId, "tenant1");

  // Create a latency metric
  auto p99Metric = thriftUtils::makeRoutingLatencyMetric(
      interface::RoutingLatencyMetric::PERCENTILE, 99);

  // Add a metric
  auto expr1 = const_expr(10, *universe);
  metrics.add(expr1, routingConfig1Id, p99Metric, tenant1Id);

  // Try to add another metric with the same key
  auto expr2 = const_expr(20, *universe);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      metrics.add(expr2, routingConfig1Id, p99Metric, tenant1Id),
      "unexpected failure to insert to groupLatencyMetrics. Duplicates are unexpected since GroupRoutingLatencyLookup exprs are cached in expressionBuilder");
}

} // namespace facebook::rebalancer::packer::tests
