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

#include "algopt/rebalancer/materializer/spec_builder/GroupDiversitySpecBuilder.h"

#include "algopt/rebalancer/materializer/utils/FilterWrapper.h"
#include "algopt/rebalancer/materializer/utils/LimitWrapper.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"

#include <optional>

namespace facebook::rebalancer::materializer {

GroupDiversitySpecBuilder::GroupDiversitySpecBuilder(
    std::shared_ptr<const entities::Universe> universe,
    interface::GroupDiversitySpec spec,
    bool needContinuousExpressions)
    : SpecBuilder(std::move(universe)),
      spec_(std::move(spec)),
      needContinuousExpressions_(needContinuousExpressions),
      scopeId_(universe_->getScopeId(*spec_.scope())),
      partitionId_(universe_->getPartitionId(*spec_.partition())),
      dimensionId_(universe_->getDimensionId(*spec_.dimension())),
      dimension_(universe_->getObjects().getDimension(dimensionId_).only()) {}

folly::coro::Task<ExprPtr> GroupDiversitySpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  co_return getAggregatedConstraintViolation(
      co_await constraints(expressionBuilder), universe_);
}

ExprPtr GroupDiversitySpecBuilder::buildLookup(
    ExpressionBuilder& expressionBuilder,
    entities::ScopeItemId scopeItemId,
    ObjectPartitionLookupPenaltyTransform transform,
    double coefficient) const {
  std::optional<ExpressionBuilder::ScopeParams> scopeParams;
  if (dimension_.isDynamic()) {
    scopeParams = ExpressionBuilder::ScopeParams{
        .scopeId = scopeId_, .scopeItemId = scopeItemId};
  }
  const auto partition = expressionBuilder.getObjectPartition(
      /*groupLimits=*/{},
      dimensionId_,
      partitionId_,
      /*normalizeByGroupSize=*/false,
      scopeParams,
      /*filteredGroupIds=*/std::nullopt,
      /*defaultGroupCoefficient=*/coefficient);
  return expressionBuilder.getObjectPartitionLookup(
      UtilMetric::AFTER,
      scopeId_,
      scopeItemId,
      partition,
      /*overrides=*/{},
      transform,
      /*groupsAllowed=*/0,
      /*minBound=*/false);
}

folly::coro::Task<std::vector<ConstraintInfo>>
GroupDiversitySpecBuilder::constraints(
    ExpressionBuilder& expressionBuilder) const {
  const LimitWrapper limits(universe_, *spec_.limit(), scopeId_);
  const ScopeItemFilterWrapper filter(*universe_, *spec_.filter(), scopeId_);

  std::vector<ConstraintInfo> result;
  for (auto scopeItemId : filter.getScopeItemIds()) {
    // With group limits = 0, every present group gets a positive penalty
    // and STEP maps each to 1; the sum is the count of distinct groups
    // present in this scope item.
    auto groupsInScopeItem = buildLookup(
        expressionBuilder,
        scopeItemId,
        ObjectPartitionLookupPenaltyTransform::STEP);

    auto limit = limits.getLimit(scopeItemId);
    ExprPtr constraintExpr = nullptr;
    if (*spec_.bound() == interface::GroupDiversityBound::MIN) {
      constraintExpr = limit - groupsInScopeItem;
    } else if (*spec_.bound() == interface::GroupDiversityBound::MAX) {
      constraintExpr = groupsInScopeItem - limit;
    } else {
      throw std::runtime_error("unsupported bound");
    }

    auto additionalPenalty = co_await getContinuousPenaltyExpr(
        expressionBuilder, scopeItemId, *spec_.bound());

    result.emplace_back(constraintExpr, additionalPenalty);
  }
  co_return result;
}

folly::coro::Task<ExprPtr> GroupDiversitySpecBuilder::getContinuousPenaltyExpr(
    ExpressionBuilder& expressionBuilder,
    entities::ScopeItemId scopeItemId,
    interface::GroupDiversityBound bound) const {
  if (!needContinuousExpressions_) {
    co_return nullptr;
  }

  if (bound == interface::GroupDiversityBound::MIN) {
    // For a given scopeItem, the constraint expression is
    // [L - SUM_g(step(UTIL(g))]. Penalty will be added when
    // SUM_g(step(UTIL(g)) < L. The goal of the penalty is to
    // make sure we make every local progress is positively
    // accounted.
    // Observe, that in this case, we only make local progress
    // if an object of unused group is moved to this scopeItem,
    // which will be reflected by step(UTIL(g)).
    // so adding an additional penalty is not needed.
    co_return nullptr;
  }

  auto& partition = universe_->getPartition(partitionId_);
  // We need to normalize the utilization values to be in [0, 1] range, for that
  // we will compute an upperbound of the group utilization
  double maxUtilUpperbound = 0;
  const auto maxObjectValue = dimension_.getMaximumValue();
  for (auto groupId : partition.getGroupIds()) {
    double groupUtilUpperBound =
        partition.getObjectIds(groupId).size() * maxObjectValue;
    maxUtilUpperbound = std::max(maxUtilUpperbound, groupUtilUpperBound);
  }

  if (maxUtilUpperbound == 0) {
    // if max possible util is zero, this expression can never improve
    co_return nullptr;
  }

  constexpr double QUADRATIC_TERM_MULTIPLIER = 0.1;

  // Now, we are in the MAX case where the penalty is [SUM_g(step(UTIL(g)) - L].
  // Penalty will be added when SUM_g(step(UTIL(g)) > L, and local progress
  // happens when we "work toward" removing objects of used groups. However,
  // step(UTIL(g)) will not account for any progress towards removing objects of
  // used groups. So, we need to add an additional penalty which improves more
  // for underuitilized groups (UTIL(g) is small) and less for overutilized
  // groups. We choose this penalty to be (UTIL(g) - C * UTIL(g)^2), where
  // UTIL(g) is the normalized utilization value <= 1 and C is a constant < 0.5
  // The quadratic component ensures that objects of under-utilized groups are
  // moved first. The linear component ensures that increasing the utilization
  // across all the groups is penalized, and reducing the utilization across all
  // the groups is prioritized. Concretely, when the number of used groups is
  // over the limit, we have three cases involving used group objects:
  // Case 1: SINGLE move-out of a used group object
  //   * linear term will decrease and encourage move-out, quadratic term will
  //   decrease the least for an underutilized group
  // Case 2: SINGLE move-in of an used group object
  //   * linear term will increase and discourage move-in and dominate quadratic
  //   term which will decrease and encourage move-in
  // Case 3: SWAP of two used group objects
  //   * Linear term will remain unchanged but quadratic term will encourage
  //   that we move out of the most underutilized group to another group
  if (!dimension_.hasNegativeValues() && !dimension_.isRoutingConfigBased()) {
    // Use a uniform normalized coefficient here
    const auto coefficient = 1.0 / maxUtilUpperbound;
    const auto term = [&](ObjectPartitionLookupPenaltyTransform transform) {
      return buildLookup(
          expressionBuilder, scopeItemId, transform, coefficient);
    };
    co_return term(ObjectPartitionLookupPenaltyTransform::IDENTITY) -
        (QUADRATIC_TERM_MULTIPLIER *
         term(ObjectPartitionLookupPenaltyTransform::SQUARE));
  }

  auto totalPenaltyExpression = const_expr(0, universe_);
  for (auto groupId : partition.getGroupIds()) {
    auto groupUtil = co_await expressionBuilder.getAbsoluteUtil(
        UtilMetric::AFTER,
        dimensionId_,
        scopeId_,
        scopeItemId,
        partitionId_,
        groupId);
    auto normUtil = groupUtil / maxUtilUpperbound;
    // Let x = normUtil which is in [0, 1]
    // Suppose f(x) =  x -  C * x^2
    // we want f(x) to be strictly increasing for x in [0, 1]
    // we have, f'(x) = 1 - 2C * x
    // we want, f'(x) >= 0 =>  1 - 2C * x >= 0 => C < 1/2
    // So choosing C = QUADRATIC_TERM_MULTIPLIER ensures that
    // increasing the UTIL increases the penalty, and decreasing the UTIL
    // decreases the penalty.
    // When UTIL stays the same (object move-in and move out of the scope, SWAP
    // moves), then quadratic factor ensures we move objects of under-utilized
    // groups first.
    totalPenaltyExpression +=
        normUtil - QUADRATIC_TERM_MULTIPLIER * power(normUtil, 2, universe_);
  }
  co_return totalPenaltyExpression;
}

std::string GroupDiversitySpecBuilder::description() const {
  return fmt::format(
      "Group diversity of partition {} in scope {} for dimension {}",
      *spec_.partition(),
      *spec_.scope(),
      *spec_.dimension());
}

SpecParameters GroupDiversitySpecBuilder::getSpecInfo() const {
  SpecParameters info;
  info.name = *spec_.name();
  info.scope = *spec_.scope();
  info.partition = *spec_.partition();
  info.dimension = *spec_.dimension();
  return info;
}

} // namespace facebook::rebalancer::materializer
