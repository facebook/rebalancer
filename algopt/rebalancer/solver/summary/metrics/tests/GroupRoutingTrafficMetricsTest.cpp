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

#include "algopt/rebalancer/solver/summary/metrics/tests/GroupRoutingTrafficMetricsTest.h"

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Metrics_types.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/summary/metrics/GroupRoutingTrafficMetrics.h"
#include "algopt/rebalancer/solver/utils/Context.h"

#include "gmock/gmock.h"
#include <folly/container/F14Map.h>
#include <folly/lang/Assume.h>
#include <folly/lang/Hint.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/FieldRef.h>

namespace facebook::rebalancer::packer::tests {

TEST_F(GroupRoutingTrafficMetricsTest, AddAndGetSummary) {
  // Create mock routing rings
  const auto universe = buildUniverse();
  auto mockRing1 = createRoutingRing(routingConfig(1), tenant(1), universe);
  auto mockRing2 = createRoutingRing(routingConfig(1), tenant(2), universe);
  auto mockRing3 = createRoutingRing(routingConfig(2), tenant(1), universe);

  // Add the routing rings to the metrics
  metrics.add(mockRing1, routingConfig(1), tenant(1));
  metrics.add(mockRing3, routingConfig(2), tenant(1));
  metrics.add(mockRing2, routingConfig(1), tenant(2));
  metrics.buildRootExpr(universe);

  // test rootExpr
  const auto rootExpr = metrics.getRootExprRawPtr();
  EXPECT_TRUE(rootExpr != nullptr);
  EXPECT_EQ(3, rootExpr->children().size());
  EXPECT_TRUE(rootExpr->children().contains(mockRing1));
  EXPECT_TRUE(rootExpr->children().contains(mockRing2));
  EXPECT_TRUE(rootExpr->children().contains(mockRing3));

  // Verify the type
  EXPECT_EQ(
      metrics.getType(),
      interface::thrift::MetricCollectionType::GROUP_ROUTING_TRAFFIC_VALUES);

  // Get metrics summary
  interface::thrift::Metrics metricsSummary;
  Context context;
  metrics.applyAndAddToSummary(
      Assignment(universe->getContainers().getInitialAssignment()),
      context,
      *universe,
      metricsSummary);

  // Verify the summary
  auto& routingConfigToGroupTrafficMetrics =
      *metricsSummary.routingConfigToGroupTrafficMetrics();

  // Check routing config 1
  EXPECT_TRUE(routingConfigToGroupTrafficMetrics.contains("routingConfig1"));
  auto& config1Metrics =
      routingConfigToGroupTrafficMetrics.at("routingConfig1");
  EXPECT_TRUE(config1Metrics.groupToSourceTraffic()->contains("tenant1"));
  EXPECT_TRUE(config1Metrics.groupToSourceTraffic()->contains("tenant2"));

  // Check tenant1 traffic in routingConfig1
  auto& tenant1Traffic = config1Metrics.groupToSourceTraffic()->at("tenant1");
  EXPECT_EQ(
      tenant1Traffic.sourceToDestinationTraffic()->size(),
      2); // region0 and region1

  // Check region0 traffic
  EXPECT_TRUE(tenant1Traffic.sourceToDestinationTraffic()->contains("region0"));
  auto& region0Traffic =
      tenant1Traffic.sourceToDestinationTraffic()->at("region0");
  EXPECT_EQ(region0Traffic.scopeItemToValue()->size(), 1); // region0
  EXPECT_NEAR(region0Traffic.scopeItemToValue()->at("region0"), 0.6, 1e-8);

  // Check region1 traffic
  EXPECT_TRUE(tenant1Traffic.sourceToDestinationTraffic()->contains("region1"));
  auto& region1Traffic =
      tenant1Traffic.sourceToDestinationTraffic()->at("region1");
  EXPECT_EQ(region1Traffic.scopeItemToValue()->size(), 1); // region1
  EXPECT_NEAR(region1Traffic.scopeItemToValue()->at("region1"), 0.4, 1e-8);

  // Check routing config 2
  EXPECT_TRUE(routingConfigToGroupTrafficMetrics.contains("routingConfig2"));
  auto& config2Metrics =
      routingConfigToGroupTrafficMetrics.at("routingConfig2");
  EXPECT_TRUE(config2Metrics.groupToSourceTraffic()->contains("tenant1"));

  // Check tenant2 traffic in routingConfig2
  auto& tenant2Traffic = config2Metrics.groupToSourceTraffic()->at("tenant1");
  EXPECT_EQ(
      tenant2Traffic.sourceToDestinationTraffic()->size(),
      2); // region0 and region1
}

TEST_F(GroupRoutingTrafficMetricsTest, DuplicateInsertFailure) {
  // Create mock routing rings
  const auto universe = buildUniverse();
  auto mockRing1 = createRoutingRing(routingConfig(1), tenant(1), universe);
  auto mockRing2 = createRoutingRing(routingConfig(1), tenant(1), universe);

  // Add a routing ring
  metrics.add(mockRing1, routingConfig(1), tenant(1));

  // Try to add another routing ring with the same key
  REBALANCER_EXPECT_RUNTIME_ERROR(
      metrics.add(mockRing2, routingConfig(1), tenant(1)),
      "unexpected failure to insert to groupTrafficMetrics. Duplicates are unexpected since GroupRoutingRing exprs are cached in expressionBuilder");
}

} // namespace facebook::rebalancer::packer::tests
