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

#include "algopt/rebalancer/materializer/spec_builder/SpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/Descriptor.h"
#include "algopt/rebalancer/materializer/utils/LimitWrapper.h"

#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>

#include <vector>

namespace facebook::rebalancer::materializer {
/**
Given a scope `S`, dimension `D`, partition `P`, and `groupToPresenceWeight`
which maps each group in `P` to a weight, `CapacityWithGroupPresence` computes
the utilization of each scope item in `S` w.r.t. `D` where a group's
contribution to the utilization of scope item `I` is
`max(groupToPresenceWeight[G][I], sum of dimension values of all objects in G
that are in I)`.

In other words, just the mere presence of a group `G` adds a minimum utilization
of `groupToPresenceWeight[G][I]` to the scope item `I`.

The continuous penalty (local search only) is a sum of per-group terms, each
gated to 0 once the group can no longer move in the constraint-fixing direction
-- so it stays separable even across aggregation groups (partition !=
aggregationPartition). MAX: per-group utilization, gated at the lower bound.
MIN: per-group upper-bound complement (upperBound - utilization), gated at the
upper bound.
*/
class CapacityWithGroupPresenceSpecBuilder : public SpecBuilder {
 public:
  CapacityWithGroupPresenceSpecBuilder(
      std::shared_ptr<const entities::Universe> universe,
      interface::CapacityWithGroupPresenceSpec spec,
      bool needsContinuousExpressions);

  folly::coro::Task<ExprPtr> goalCoro(
      ExpressionBuilder& expressionBuilder) const override;

  folly::coro::Task<std::vector<ConstraintInfo>> constraints(
      ExpressionBuilder& expressionBuilder) const override;

  std::string description() const override;

  SpecParameters getSpecInfo() const override;

 private:
  // The constraint utilization and its continuous-penalty utilization for a
  // scope item (or group-in-scope-item).
  struct UtilExprs {
    ExprPtr util;
    ExprPtr penaltyUtil = nullptr;
  };

  // Builds one constraint per scope item.
  folly::coro::Task<std::vector<ConstraintInfo>> scopeItemConstraints(
      UtilMetric metric,
      ExpressionBuilder& expressionBuilder) const;

  folly::coro::Task<std::vector<ConstraintInfo>> groupAndScopeItemConstraints(
      UtilMetric metric,
      ExpressionBuilder& expressionBuilder) const;

  ExprPtr getConstraintExpr(
      entities::ScopeItemId mainScopeItemId,
      std::optional<entities::GroupId> mainGroupIdOpt,
      const ExprPtr& util) const;

  ExprPtr getAdditionalPenaltyExpr(
      std::optional<entities::GroupId> mainGroupIdOpt,
      const ExprPtr& penaltyUtil) const;

  UtilExprs zeroUtilExprs() const;

  static void addUtilExprs(UtilExprs& acc, UtilExprs contribution);

  folly::coro::Task<UtilExprs> getScopeItemUtil(
      UtilMetric metric,
      entities::ScopeItemId mainScopeItemId,
      ExpressionBuilder& expressionBuilder,
      const std::shared_ptr<const entities::Set<entities::GroupId>>&
          aggregationGroupIds) const;

  bool shouldUseOptimizedPath(UtilMetric metric) const;

  UtilExprs buildOptimizedScopeItemUtilExprForStaticDimension(
      UtilMetric metric,
      const entities::ScopeItemId& mainScopeItemId,
      ExpressionBuilder& expressionBuilder,
      const std::shared_ptr<const entities::Set<entities::GroupId>>&
          aggregationGroupIds) const;

  UtilExprs buildOptimizedScopeItemUtilExprForDynamicDimension(
      UtilMetric metric,
      const entities::ScopeItemId& mainScopeItemId,
      ExpressionBuilder& expressionBuilder,
      const std::shared_ptr<const entities::Set<entities::GroupId>>&
          aggregationGroupIds) const;

  std::shared_ptr<const entities::Set<entities::GroupId>>
  buildAggregationGroupIds(ExpressionBuilder& expressionBuilder) const;

  UtilExprs createGroupUtilExpr(
      UtilMetric metric,
      ExprPtr objectPartition,
      entities::ScopeItemId aggregationScopeItemId,
      const Assignment& initialAssignment) const;

  folly::coro::Task<UtilExprs> getGroupUtilInMainScopeItem(
      UtilMetric metric,
      entities::GroupId mainGroupId,
      entities::ScopeItemId mainScopeItemId,
      ExpressionBuilder& expressionBuilder) const;

  folly::coro::Task<UtilExprs> getGroupUtilContributionToScopeItemUtil(
      UtilMetric metric,
      entities::GroupId aggregationGroupId,
      entities::ScopeItemId aggregationScopeItemId,
      ExpressionBuilder& expressionBuilder) const;

  ExprPtr getWeightedExpr(
      ExprPtr& expr,
      entities::GroupId groupId,
      entities::ScopeItemId aggregationScopeItemId,
      const std::vector<interface::GroupUtilMultiplierTarget>& targets,
      bool applyCeilAfterEach = false) const;

  const std::vector<entities::GroupId>& getRelevantMainGroupIds() const;
  const std::vector<entities::ScopeItemId>& getRelevantMainScopeItemIds() const;

  bool isGroupAlwaysPresent(
      entities::ScopeItemId aggregationScopeItemId,
      entities::GroupId aggregationGroupId) const;

  const interface::CapacityWithGroupPresenceSpec spec_;
  const bool needsContinuousExpressions_;
  const entities::ScopeId mainScopeId_;
  const entities::Scope& mainScope_;
  const entities::ScopeId aggregationScopeId_;
  const entities::DimensionId dimensionId_;
  const entities::ObjectScalarDimension& dimension_;
  const entities::PartitionId mainPartitionId_;
  const entities::Partition& mainPartition_;
  const entities::PartitionId aggregationPartitionId_;
  const entities::Partition& aggregationPartition_;
  const LimitWrapper capacityLimits_;
  const std::optional<std::vector<entities::ScopeItemId>>
      filteredMainScopeItemIds_;
  const std::optional<std::vector<entities::GroupId>> filteredGroupIds_;
  const std::shared_ptr<const MinPresenceConfig> minPresenceConfig_;
  const double penaltyBound_;
  const size_t totalObjectCount_;
  // True when the dimension is dynamic and defined on a scope that differs from
  // the aggregation scope. In that case ObjectPartitionLookup cannot represent
  // DURING (via initialDuringObjects), so DURING falls back to the unoptimized
  // path.
  const bool dimensionScopeDiffersFromAggScope_;
  // The utilization metrics this spec enforces, derived from its (fixed)
  // definition and computed once at construction (see getUtilMetrics()).
  const std::vector<UtilMetric> utilMetrics_;
};

} // namespace facebook::rebalancer::materializer
