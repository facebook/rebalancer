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

#include "rebalancer/explorer/cpp_server/lib/MetricsTabulator.h"

#include "algopt/rebalancer/interface/thrift/ThriftUtils.h"
#include "algopt/rebalancer/solver/summary/metrics/MetricCollection.h"
#include "algopt/rebalancer/solver/utils/Context.h"

#include <fmt/format.h>
#include <folly/MapUtil.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace facebook::rebalancer::explorer {

namespace {

double getRelativeUtilization(double absUtil, double scopeDimValue) {
  return scopeDimValue == 0 ? std::numeric_limits<double>::infinity()
                            : absUtil / scopeDimValue;
}

const std::string kNotApplicable = "N/A";

template <typename Collection>
const Collection& checkedCollectionCast(
    const MetricCollection& collection,
    interface::thrift::MetricCollectionType type) {
  const auto* typedCollection = dynamic_cast<const Collection*>(&collection);
  if (typedCollection == nullptr) {
    throw std::runtime_error(
        fmt::format(
            "Metric collection type {} is backed by an unexpected concrete collection",
            apache::thrift::util::enumNameSafe(type)));
  }
  return *typedCollection;
}

} // namespace

Table tabulate(
    const ScopeItemUtilMetrics& metrics,
    const TabulateConfig& config) {
  const auto& universe = config.universe;
  const auto& orchestrator = config.orchestrator;
  const auto& changeSetA = config.changeSetA;
  const auto& changeSetB = config.changeSetB;

  struct Row {
    std::string utilMetric;
    BorrowedString dimension;
    BorrowedString scope;
    BorrowedString scopeItem;
    BorrowedString partition;
    BorrowedString group;
    double scopeItemDimensionValue;
    double relativeUtilizationA;
    double relativeUtilizationB;
    double utilizationA;
    double utilizationB;
  };
  std::vector<Row> rows;

  Context contextA;
  contextA.changes() = changeSetA;
  Context contextB;
  contextB.changes() = changeSetB;
  metrics.forEachMetricExpressionForTabulation([&](const auto& key,
                                                   const auto& expr) {
    const auto& [utilMetric, scopeId, dimensionId, scopeItemId, partitionIdOpt, groupIdOpt] =
        key;
    const auto& scope = universe.getScope(scopeId);
    const double scopeDimValue =
        scope.getDimension(dimensionId).getValue(scopeItemId);
    const double absUtilA = orchestrator.evaluate(expr.get(), contextA);
    const double absUtilB = orchestrator.evaluate(expr.get(), contextB);
    const double relUtilA = getRelativeUtilization(absUtilA, scopeDimValue);
    const double relUtilB = getRelativeUtilization(absUtilB, scopeDimValue);

    rows.push_back(
        {.utilMetric = MetricCollection::toString(utilMetric),
         .dimension = universe.getEntityName(dimensionId),
         .scope = universe.getEntityName(scopeId),
         .scopeItem = universe.getEntityName(scopeItemId),
         .partition = partitionIdOpt ? universe.getEntityName(*partitionIdOpt)
                                     : kNotApplicable,
         .group =
             groupIdOpt ? universe.getEntityName(*groupIdOpt) : kNotApplicable,
         .scopeItemDimensionValue = scopeDimValue,
         .relativeUtilizationA = relUtilA,
         .relativeUtilizationB = relUtilB,
         .utilizationA = absUtilA,
         .utilizationB = absUtilB});
  });

  TableBuilder tableBuilder(rows);
  tableBuilder
      .add(
          {.name = "Util Metric",
           .type = ColumnType::STRING,
           .isPrimaryKey = true},
          [](const Row& row) { return row.utilMetric; })
      .add(
          {.name = "Dimension",
           .type = ColumnType::STRING,
           .isPrimaryKey = true},
          [](const Row& row) { return row.dimension; })
      .add(
          {.name = "Scope", .type = ColumnType::SCOPE, .isPrimaryKey = true},
          [](const Row& row) { return row.scope; })
      .add(
          {.name = "Scope Item",
           .type = ColumnType::STRING,
           .isPrimaryKey = true},
          [](const Row& row) { return row.scopeItem; })
      .add(
          {.name = "Partition",
           .type = ColumnType::PARTITION,
           .isPrimaryKey = true},
          [](const Row& row) { return row.partition; })
      .add(
          {.name = "Group", .type = ColumnType::STRING, .isPrimaryKey = true},
          [](const Row& row) { return row.group; })
      .add(
          {.name = "Scope Item Dimension Value", .type = ColumnType::DOUBLE},
          [](const Row& row) { return row.scopeItemDimensionValue; })
      .add(
          {.name = "Relative Utilization (A)", .type = ColumnType::UTILIZATION},
          [](const Row& row) { return row.relativeUtilizationA; })
      .add(
          {.name = "Relative Utilization (B)", .type = ColumnType::UTILIZATION},
          [](const Row& row) { return row.relativeUtilizationB; })
      .add(
          {.name = "Relative Utilization (B-A)", .type = ColumnType::DOUBLE},
          [](const Row& row) {
            return row.relativeUtilizationB - row.relativeUtilizationA;
          })
      .add(
          {.name = "Utilization (A)", .type = ColumnType::UTILIZATION},
          [](const Row& row) { return row.utilizationA; })
      .add(
          {.name = "Utilization (B)", .type = ColumnType::UTILIZATION},
          [](const Row& row) { return row.utilizationB; })
      .add(
          {.name = "Utilization (B-A)", .type = ColumnType::DOUBLE},
          [](const Row& row) { return row.utilizationB - row.utilizationA; });
  return tableBuilder.build();
}

Table tabulate(
    const GroupRoutingTrafficMetrics& metrics,
    const TabulateConfig& config) {
  auto& universe = config.universe;
  auto& orchestrator = config.orchestrator;

  struct Row {
    BorrowedString routingConfig;
    BorrowedString partition;
    BorrowedString group;
    BorrowedString scope;
    BorrowedString sourceScopeItem;
    BorrowedString destinationScopeItem;
    double trafficA;
    double trafficB;
  };
  std::vector<Row> rows;

  const auto addRow = [&](entities::RoutingConfigId routingConfigId,
                          entities::GroupId groupId,
                          entities::ScopeItemId sourceId,
                          entities::ScopeItemId destinationId,
                          double trafficA,
                          double trafficB) {
    const auto& routingConfig = universe.getRoutingConfig(routingConfigId);
    const auto partitionId = routingConfig.getPartitionId();
    const auto scopeId = routingConfig.getScopeId();

    rows.push_back(
        {.routingConfig = universe.getEntityName(routingConfigId),
         .partition = universe.getEntityName(partitionId),
         .group = universe.getEntityName(groupId),
         .scope = universe.getEntityName(scopeId),
         .sourceScopeItem = universe.getEntityName(sourceId),
         .destinationScopeItem = universe.getEntityName(destinationId),
         .trafficA = trafficA,
         .trafficB = trafficB});
  };

  Context contextA;
  contextA.changes() = config.changeSetA;
  Context contextB;
  contextB.changes() = config.changeSetB;
  metrics.forEachMetricExpressionForTabulation([&](const auto& key,
                                                   const auto& expr) {
    const auto& [routingConfigId, groupId] = key;

    orchestrator.evaluate(expr.get(), contextA);
    orchestrator.evaluate(expr.get(), contextB);

    const auto& trafficTableA =
        contextA.groupToTempTrafficTable().contains(expr->getId())
        ? contextA.groupToTempTrafficTable().at(expr->getId())
        : expr->getTrafficTableWithStats();
    const auto& trafficTableB =
        contextB.groupToTempTrafficTable().contains(expr->getId())
        ? contextB.groupToTempTrafficTable().at(expr->getId())
        : expr->getTrafficTableWithStats();

    for (const auto& [sourceId, destinationsA] :
         trafficTableA.getTrafficTable()) {
      for (const auto& [destinationId, trafficLatencyPairA] : destinationsA) {
        const double trafficA = trafficLatencyPairA.first;
        const double trafficB =
            trafficTableB.getTraffic(sourceId, destinationId);
        addRow(
            routingConfigId,
            groupId,
            sourceId,
            destinationId,
            trafficA,
            trafficB);
      }
    }

    // Process source-destination pairs in B that aren't in A
    for (const auto& [sourceId, destinationsB] :
         trafficTableB.getTrafficTable()) {
      for (const auto& [destinationId, trafficLatencyPairB] : destinationsB) {
        const bool alreadyProcessed =
            trafficTableA.exists(sourceId, destinationId);

        if (!alreadyProcessed) {
          constexpr double trafficA = 0.0;
          const double trafficB = trafficLatencyPairB.first;
          addRow(
              routingConfigId,
              groupId,
              sourceId,
              destinationId,
              trafficA,
              trafficB);
        }
      }
    }
  });

  TableBuilder tableBuilder(rows);
  tableBuilder
      .add(
          {.name = "Routing Config",
           .type = ColumnType::STRING,
           .isPrimaryKey = true},
          [](const Row& row) { return row.routingConfig; })
      .add(
          {.name = "Partition",
           .type = ColumnType::PARTITION,
           .isPrimaryKey = true},
          [](const Row& row) { return row.partition; })
      .add(
          {.name = "Group", .type = ColumnType::STRING, .isPrimaryKey = true},
          [](const Row& row) { return row.group; })
      .add(
          {.name = "Scope", .type = ColumnType::SCOPE, .isPrimaryKey = true},
          [](const Row& row) { return row.scope; })
      .add(
          {.name = "Source Scope Item",
           .type = ColumnType::STRING,
           .isPrimaryKey = true},
          [](const Row& row) { return row.sourceScopeItem; })
      .add(
          {.name = "Destination Scope Item",
           .type = ColumnType::STRING,
           .isPrimaryKey = true},
          [](const Row& row) { return row.destinationScopeItem; })
      .add(
          {.name = "Traffic (A)", .type = ColumnType::DOUBLE},
          [](const Row& row) { return row.trafficA; })
      .add(
          {.name = "Traffic (B)", .type = ColumnType::DOUBLE},
          [](const Row& row) { return row.trafficB; })
      .add(
          {.name = "Traffic (B-A)", .type = ColumnType::DOUBLE},
          [](const Row& row) { return row.trafficB - row.trafficA; });
  return tableBuilder.build();
}

Table tabulate(
    const GroupRoutingLatencyMetrics& metrics,
    const TabulateConfig& config) {
  using namespace facebook::rebalancer::interface;

  auto& universe = config.universe;
  auto& orchestrator = config.orchestrator;

  struct Row {
    std::string latencyMetric;
    BorrowedString routingConfig;
    BorrowedString partition;
    BorrowedString group;
    double latencyA;
    double latencyB;
  };
  std::vector<Row> rows;

  Context contextA;
  contextA.changes() = config.changeSetA;
  Context contextB;
  contextB.changes() = config.changeSetB;
  metrics.forEachMetricExpressionForTabulation(
      [&](const auto& key, const auto& expr) {
        const auto& [routingConfigId, metricType, percentile, groupId] = key;
        const auto partitionId =
            universe.getRoutingConfig(routingConfigId).getPartitionId();
        const double valueA = orchestrator.evaluate(expr.get(), contextA);
        const double valueB = orchestrator.evaluate(expr.get(), contextB);

        rows.push_back(
            {.latencyMetric = thriftUtils::toString(
                 thriftUtils::makeRoutingLatencyMetric(metricType, percentile)),
             .routingConfig = universe.getEntityName(routingConfigId),
             .partition = universe.getEntityName(partitionId),
             .group = universe.getEntityName(groupId),
             .latencyA = valueA,
             .latencyB = valueB});
      });

  TableBuilder tableBuilder(rows);
  tableBuilder
      .add(
          {.name = "Latency Metric",
           .type = ColumnType::STRING,
           .isPrimaryKey = true},
          [](const Row& row) { return row.latencyMetric; })
      .add(
          {.name = "Routing Config",
           .type = ColumnType::STRING,
           .isPrimaryKey = true},
          [](const Row& row) { return row.routingConfig; })
      .add(
          {.name = "Partition",
           .type = ColumnType::PARTITION,
           .isPrimaryKey = true},
          [](const Row& row) { return row.partition; })
      .add(
          {.name = "Group", .type = ColumnType::STRING, .isPrimaryKey = true},
          [](const Row& row) { return row.group; })
      .add(
          {.name = "Latency (A)", .type = ColumnType::DOUBLE},
          [](const Row& row) { return row.latencyA; })
      .add(
          {.name = "Latency (B)", .type = ColumnType::DOUBLE},
          [](const Row& row) { return row.latencyB; })
      .add(
          {.name = "Latency (B-A)", .type = ColumnType::DOUBLE},
          [](const Row& row) { return row.latencyB - row.latencyA; });
  return tableBuilder.build();
}

Table tabulateMetricCollection(
    const Metrics& metrics,
    interface::thrift::MetricCollectionType type,
    const TabulateConfig& config) {
  const auto* collectionPtr =
      folly::get_ptr(metrics.getAvailableCollections(), type);
  if (collectionPtr == nullptr) {
    throw std::runtime_error(
        fmt::format(
            "No collection of type {} found in metrics",
            apache::thrift::util::enumNameSafe(type)));
  }
  const auto& collection = **collectionPtr;
  switch (type) {
    case interface::thrift::MetricCollectionType::SCOPE_ITEM_UTILIZATION_VALUES:
      return tabulate(
          checkedCollectionCast<ScopeItemUtilMetrics>(collection, type),
          config);
    case interface::thrift::MetricCollectionType::GROUP_ROUTING_LATENCY_VALUES:
      return tabulate(
          checkedCollectionCast<GroupRoutingLatencyMetrics>(collection, type),
          config);
    case interface::thrift::MetricCollectionType::GROUP_ROUTING_TRAFFIC_VALUES:
      return tabulate(
          checkedCollectionCast<GroupRoutingTrafficMetrics>(collection, type),
          config);
  }
  throw std::runtime_error(
      fmt::format(
          "Unsupported metric collection type {}",
          apache::thrift::util::enumNameSafe(type)));
}

} // namespace facebook::rebalancer::explorer
