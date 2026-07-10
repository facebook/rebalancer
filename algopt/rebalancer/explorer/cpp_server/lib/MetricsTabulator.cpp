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

#include <limits>
#include <stdexcept>
#include <string>

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

  TableBuilder tableBuilder;
  tableBuilder
      .addColumnDefinition(
          {.name = "Util Metric",
           .type = ColumnType::STRING,
           .isPrimaryKey = true})
      .addColumnDefinition(
          {.name = "Dimension",
           .type = ColumnType::STRING,
           .isPrimaryKey = true})
      .addColumnDefinition(
          {.name = "Scope", .type = ColumnType::SCOPE, .isPrimaryKey = true})
      .addColumnDefinition(
          {.name = "Scope Item",
           .type = ColumnType::STRING,
           .isPrimaryKey = true})
      .addColumnDefinition(
          {.name = "Partition",
           .type = ColumnType::PARTITION,
           .isPrimaryKey = true})
      .addColumnDefinition(
          {.name = "Group", .type = ColumnType::STRING, .isPrimaryKey = true})
      .addColumnDefinition(
          {.name = "Scope Item Dimension Value", .type = ColumnType::DOUBLE})
      .addColumnDefinition(
          {.name = "Relative Utilization (A)", .type = ColumnType::UTILIZATION})
      .addColumnDefinition(
          {.name = "Relative Utilization (B)", .type = ColumnType::UTILIZATION})
      .addColumnDefinition(
          {.name = "Relative Utilization (B-A)", .type = ColumnType::DOUBLE})
      .addColumnDefinition(
          {.name = "Utilization (A)", .type = ColumnType::UTILIZATION})
      .addColumnDefinition(
          {.name = "Utilization (B)", .type = ColumnType::UTILIZATION})
      .addColumnDefinition(
          {.name = "Utilization (B-A)", .type = ColumnType::DOUBLE});

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

    double relUtilBMinusA = relUtilB - relUtilA;
    if (relUtilA == std::numeric_limits<double>::infinity() ||
        relUtilB == std::numeric_limits<double>::infinity()) {
      relUtilBMinusA = std::numeric_limits<double>::infinity();
    }
    tableBuilder.addRow(
        MetricCollection::toString(utilMetric),
        universe.getEntityName(dimensionId),
        universe.getEntityName(scopeId),
        universe.getEntityName(scopeItemId),
        partitionIdOpt ? universe.getEntityName(*partitionIdOpt)
                       : kNotApplicable,
        groupIdOpt ? universe.getEntityName(*groupIdOpt) : kNotApplicable,
        scopeDimValue,
        relUtilA,
        relUtilB,
        relUtilBMinusA,
        absUtilA,
        absUtilB,
        absUtilB - absUtilA);
  });

  return tableBuilder.build();
}

Table tabulate(
    const GroupRoutingTrafficMetrics& metrics,
    const TabulateConfig& config) {
  auto& universe = config.universe;
  auto& orchestrator = config.orchestrator;

  TableBuilder tableBuilder;
  tableBuilder
      .addColumnDefinition(
          {.name = "Routing Config",
           .type = ColumnType::STRING,
           .isPrimaryKey = true})
      .addColumnDefinition(
          {.name = "Partition",
           .type = ColumnType::PARTITION,
           .isPrimaryKey = true})
      .addColumnDefinition(
          {.name = "Group", .type = ColumnType::STRING, .isPrimaryKey = true})
      .addColumnDefinition(
          {.name = "Scope", .type = ColumnType::SCOPE, .isPrimaryKey = true})
      .addColumnDefinition(
          {.name = "Source Scope Item",
           .type = ColumnType::STRING,
           .isPrimaryKey = true})
      .addColumnDefinition(
          {.name = "Destination Scope Item",
           .type = ColumnType::STRING,
           .isPrimaryKey = true})
      .addColumnDefinition({.name = "Traffic (A)", .type = ColumnType::DOUBLE})
      .addColumnDefinition({.name = "Traffic (B)", .type = ColumnType::DOUBLE})
      .addColumnDefinition(
          {.name = "Traffic (B-A)", .type = ColumnType::DOUBLE});

  auto addRow = [&](entities::RoutingConfigId routingConfigId,
                    entities::GroupId groupId,
                    entities::ScopeItemId sourceId,
                    entities::ScopeItemId destinationId,
                    double trafficA,
                    double trafficB) {
    auto& routingConfig = universe.getRoutingConfig(routingConfigId);
    auto partitionId = routingConfig.getPartitionId();
    auto scopeId = routingConfig.getScopeId();

    tableBuilder.addRow(
        universe.getEntityName(routingConfigId),
        universe.getEntityName(partitionId),
        universe.getEntityName(groupId),
        universe.getEntityName(scopeId),
        universe.getEntityName(sourceId),
        universe.getEntityName(destinationId),
        trafficA,
        trafficB,
        trafficB - trafficA);
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

  return tableBuilder.build();
}

Table tabulate(
    const GroupRoutingLatencyMetrics& metrics,
    const TabulateConfig& config) {
  using namespace facebook::rebalancer::interface;

  auto& universe = config.universe;
  auto& orchestrator = config.orchestrator;

  TableBuilder tableBuilder;
  tableBuilder
      .addColumnDefinition(
          {.name = "Latency Metric",
           .type = ColumnType::STRING,
           .isPrimaryKey = true})
      .addColumnDefinition(
          {.name = "Routing Config",
           .type = ColumnType::STRING,
           .isPrimaryKey = true})
      .addColumnDefinition(
          {.name = "Partition",
           .type = ColumnType::PARTITION,
           .isPrimaryKey = true})
      .addColumnDefinition(
          {.name = "Group", .type = ColumnType::STRING, .isPrimaryKey = true})
      .addColumnDefinition({.name = "Latency (A)", .type = ColumnType::DOUBLE})
      .addColumnDefinition({.name = "Latency (B)", .type = ColumnType::DOUBLE})
      .addColumnDefinition(
          {.name = "Latency (B-A)", .type = ColumnType::DOUBLE});

  Context contextA;
  contextA.changes() = config.changeSetA;
  Context contextB;
  contextB.changes() = config.changeSetB;
  metrics.forEachMetricExpressionForTabulation(
      [&](const auto& key, const auto& expr) {
        const auto& [routingConfigId, metricType, percentile, groupId] = key;
        auto partitionId =
            universe.getRoutingConfig(routingConfigId).getPartitionId();
        const double valueA = orchestrator.evaluate(expr.get(), contextA);
        const double valueB = orchestrator.evaluate(expr.get(), contextB);

        tableBuilder.addRow(
            thriftUtils::toString(
                thriftUtils::makeRoutingLatencyMetric(metricType, percentile)),
            universe.getEntityName(routingConfigId),
            universe.getEntityName(partitionId),
            universe.getEntityName(groupId),
            valueA,
            valueB,
            valueB - valueA);
      });

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
