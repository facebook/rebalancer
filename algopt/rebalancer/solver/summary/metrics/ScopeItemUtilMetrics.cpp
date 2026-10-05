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

#include "algopt/rebalancer/solver/summary/metrics/ScopeItemUtilMetrics.h"

#include "algopt/rebalancer/algopt_common/AssociativeMap.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Metrics_types.h"
#include "algopt/rebalancer/materializer/utils/Descriptor.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"

#include <folly/container/F14Map.h>
#include <folly/container/HeterogeneousAccess.h>
#include <folly/lang/Assume.h>
#include <folly/Synchronized.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace facebook::rebalancer {

namespace {
using DimensionId = entities::DimensionId;
using ScopeId = entities::ScopeId;
using ScopeItemId = entities::ScopeItemId;
using Descriptor = materializer::Descriptor;
using UtilMetric = materializer::UtilMetric;
using PartitionId = entities::PartitionId;
using GroupId = entities::GroupId;

folly::F14FastMap<std::string, double> extractGroupToValue(
    const ObjectPartitionLookupDefault& lookup,
    const entities::Universe& universe) {
  folly::F14FastMap<std::string, double> groupToValue;
  const auto& groupObjectWeights = lookup.getGroupObjectWeights();
  for (auto& [group, objectWeights] : groupObjectWeights) {
    auto groupName = universe.getEntityName(group);
    groupToValue.emplace(groupName, objectWeights.query());
  }
  return groupToValue;
}
} // namespace

interface::thrift::MetricCollectionType ScopeItemUtilMetrics::getType() const {
  return interface::thrift::MetricCollectionType::SCOPE_ITEM_UTILIZATION_VALUES;
}

void ScopeItemUtilMetrics::add(
    ExprPtr expr,
    UtilMetric utilMetric,
    const Descriptor& descriptor) {
  // expect partitionId and groupId to both be set or both unset
  if (descriptor.partitionId.has_value() != descriptor.groupId.has_value()) {
    throw std::runtime_error(
        "partitionId and groupId are expected to be set together");
  }

  auto wLockedUtilExprs = innerCollection_.wlock();
  auto key = std::make_tuple(
      utilMetric,
      descriptor.scopeId,
      descriptor.dimensionId.value(),
      descriptor.scopeItemId.value(),
      descriptor.partitionId,
      descriptor.groupId);
  // duplicate keys are ignored; this is possible in case some expression is not
  // cached
  wLockedUtilExprs->emplace(std::move(key), std::move(expr));
}

void ScopeItemUtilMetrics::add(
    std::shared_ptr<ObjectPartitionLookupDefault> lookup,
    materializer::UtilMetric utilMetric) {
  auto key = std::make_tuple(
      utilMetric,
      lookup->getScopeId(),
      lookup->getDimensionId(),
      lookup->getScopeItemId(),
      lookup->getPartitionId(),
      /*groupIdOpt=*/std::nullopt);
  auto wLockedUtilExprs = innerCollection_.wlock();
  wLockedUtilExprs->emplace(std::move(key), std::move(lookup));
}

void ScopeItemUtilMetrics::addToSummary(
    const entities::Universe& universe,
    interface::thrift::Metrics& metricsSummary) const {
  auto rLockedUtilExprs = innerCollection_.rlock();
  for (auto& [key, expr] : *rLockedUtilExprs) {
    auto& [utilMetric, scopeId, dimensionId, scopeItemId, partitionIdOpt, groupIdOpt] =
        key;
    auto& utilMetricToScopeUtils = *metricsSummary.utilMetricToScopeUtils();
    auto& scopeName = universe.getEntityName(scopeId);
    auto& dimensionName = universe.getEntityName(dimensionId);
    auto& scopeItemName = universe.getEntityName(scopeItemId);
    auto& dimensionToScopeItemUtils =
        utilMetricToScopeUtils[toString(utilMetric)]
            .scopeToDimensionUtils()[scopeName]
            .dimensionToScopeItemUtils()[dimensionName];

    if (!partitionIdOpt && !groupIdOpt) {
      // if partitionId and groupId are not set, then insert the value into
      // scopeItemToValue
      dimensionToScopeItemUtils.scopeItemToValue()->emplace(
          scopeItemName, expr->value);
    } else if (partitionIdOpt && groupIdOpt) {
      auto& partitionName = universe.getEntityName(*partitionIdOpt);
      auto& groupName = universe.getEntityName(*groupIdOpt);
      // if partitionId and groupId are set, then insert the value into
      // partitionToGroupUtils
      dimensionToScopeItemUtils.scopeItemToPartitionUtils()[scopeItemName]
          .partitionToGroupUtils()[partitionName]
          .groupToValue()
          ->emplace(groupName, expr->value);
    } else if (
        const auto objectPartitionLookup =
            std::dynamic_pointer_cast<const ObjectPartitionLookupDefault>(
                expr)) {
      auto& partitionName = universe.getEntityName(*partitionIdOpt);
      dimensionToScopeItemUtils.scopeItemToPartitionUtils()[scopeItemName]
          .partitionToGroupUtils()[partitionName]
          .groupToValue() =
          extractGroupToValue(*objectPartitionLookup, universe);
    } else {
      throw std::runtime_error(
          "partitionId and groupId are expected to be set together unless the expression is an ObjectPartitionLookup");
    }
  }
}

} // namespace facebook::rebalancer
