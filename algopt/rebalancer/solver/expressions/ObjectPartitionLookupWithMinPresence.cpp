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

#include "algopt/rebalancer/solver/expressions/ObjectPartition.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"

#include <folly/MapUtil.h>

namespace facebook::rebalancer {

// Precision-aware ceil function that matches Ceil::perform_transform
double precisionAwareCeil(double value, const Precision& precision) {
  const double closestInteger = std::round(value);
  return precision.isEqual(value, closestInteger) ? closestInteger
                                                  : std::ceil(value);
}

double ObjectPartitionLookupWithMinPresencePolicy::Data::applyWeights(
    double value,
    const entities::GroupId& groupId,
    const entities::ScopeItemId& scopeItemId,
    std::initializer_list<interface::GroupUtilMultiplierTarget> targets,
    const Precision& precision,
    bool applyCeil) const {
  if (applyCeil) {
    value = precisionAwareCeil(value, precision);
  }

  static const folly::small_vector<materializer::LimitWrapper, 2>
      kEmptyMultipliers;
  for (const auto& target : targets) {
    for (const auto& multiplier : folly::get_ref_default(
             groupUtilMultiplierMap, target, kEmptyMultipliers)) {
      const auto weight = multiplier.getLimit(scopeItemId, groupId);
      if (algopt::Precision::isEqual(weight, 0)) {
        return 0;
      }

      value *= weight;
      if (applyCeil) {
        value = precisionAwareCeil(value, precision);
      }
    }
  }

  return value;
}

double ObjectPartitionLookupWithMinPresencePolicy::Data::transformWeight(
    double weight,
    const entities::GroupId& groupId,
    const entities::ScopeItemId& scopeItemId,
    const Precision& precision) const {
  if (makeContinuousPenaltyTerm) {
    const auto extraAdditivePenalty =
        groupToExtraAdditivePenalty.getLimit(scopeItemId, groupId);
    if (!precision.isEqual(extraAdditivePenalty, 0.0)) {
      weight = std::max(weight + extraAdditivePenalty, 0.0);
    }
    return applyWeights(
        weight,
        groupId,
        scopeItemId,
        {interface::GroupUtilMultiplierTarget::PRESENCE_WEIGHT,
         interface::GroupUtilMultiplierTarget::UTILIZATION,
         interface::GroupUtilMultiplierTarget::COMMON},
        precision);
  }

  // Apply multipliers which targets to actual util.
  weight = applyWeights(
      weight,
      groupId,
      scopeItemId,
      {interface::GroupUtilMultiplierTarget::UTILIZATION,
       interface::GroupUtilMultiplierTarget::COMMON},
      precision,
      roundUpGroupUtilOnScopeItem);

  const auto presentGroupsPtr =
      folly::get_ptr(scopeItemToAlwaysPresentGroups, scopeItemId);
  const auto groupAlwaysPresent =
      presentGroupsPtr && presentGroupsPtr->contains(groupId);
  if (precision.isStrictlyGtZero(weight) || groupAlwaysPresent) {
    // Apply multipliers which targets to presence weight.
    auto minContributionToUtil =
        groupToPresenceWeight.getLimit(scopeItemId, groupId);
    minContributionToUtil = applyWeights(
        minContributionToUtil,
        groupId,
        scopeItemId,
        {interface::GroupUtilMultiplierTarget::PRESENCE_WEIGHT,
         interface::GroupUtilMultiplierTarget::COMMON},
        precision,
        roundUpGroupUtilOnScopeItem);

    weight = std::max(minContributionToUtil, weight);
  }

  return weight;
}

template <>
double ObjectPartitionLookup<ObjectPartitionLookupWithMinPresencePolicy>::
    getGroupPenalty(double weight, entities::GroupId groupId) const {
  weight =
      getData().transformWeight(weight, groupId, scopeItemId_, getPrecision());
  const double normalizedDeviationFromLimit =
      (weight - getGroupLimit(groupId)) *
      objectPartition_->getGroupCoefficient(groupId);
  return computePenalty(normalizedDeviationFromLimit);
}

template <>
Bounds ObjectPartitionLookup<
    ObjectPartitionLookupWithMinPresencePolicy>::getBounds() const {
  /*
  The default implementation uses pre-computed deviations from ObjectPartition
  as an optimization, only recomputing deviations for groups with overrides or
  initialDuringObjects. This works because the default implementation does not
  need to transform the group weights before calculating the deviation from the
  group limit.

  However, this implementation ALWAYS recomputes deviations for ALL groups
  because weights must be transformed (via transformWeight) BEFORE computing
  penalties. The transformations (min presence penalties, rounding, multipliers)
  are applied per (groupId, scopeItemId) pair and cannot be pre-computed in
  ObjectPartition. Therefore, we must call getGroupPenalty (which applies
  transformWeight) on the raw max/min weights for every group.

  lowerBound computation:
    -- if bound is MAX: the minimum possible penalty of a group is obtained
  when all objects with negative weights in that group and
  initialDuringObjects with positive weights in that group are present (note
  that initialDuringObjects are always present).

    -- if bound is MIN: the minimum possible penalty is obtained when all
  objects with positive weights in that group and initialDuringObjects with
  negative weights in that group are present.

  upperBound computation:
    -- if bound is MAX: the maximum possible penalty of a group is obtained
  when all objects with positive weights in that group and
  initialDuringObjects with negative weights in that group are present (note
  that initialDuringObjects are always present).

    -- if bound is MIN: the maximum penalty of a group is obtained when all
  objects with negative weights in that group  and initialDuringObjects with
  positive weights in that group are present.
  */
  Bounds bounds{.lower_bound = 0.0, .upper_bound = 0.0};
  const auto& groupToTotalPositiveAndNegativeWeights =
      objectPartition_->getGroupToTotalPositiveAndNegativeWeights();

  for (const auto& [groupId, maxAndMinWeights] :
       groupToTotalPositiveAndNegativeWeights) {
    const auto maxPenalty = getGroupPenalty(maxAndMinWeights.first, groupId);
    const auto minPenalty = getGroupPenalty(maxAndMinWeights.second, groupId);

    if (bound_ == Bound::MAX) {
      bounds.lower_bound += minPenalty;
      bounds.upper_bound += maxPenalty;
    } else {
      bounds.lower_bound += maxPenalty;
      bounds.upper_bound += minPenalty;
    }
  }

  for (const auto& [groupId, _] : groupLimitOverrides_) {
    if (!groupToTotalPositiveAndNegativeWeights.contains(groupId)) {
      const auto penalty = getGroupPenalty(0, groupId);
      bounds.lower_bound += penalty;
      bounds.upper_bound += penalty;
    }
  }

  return bounds;
}

template <>
algopt::lp::Expression
ObjectPartitionLookup<ObjectPartitionLookupWithMinPresencePolicy>::lp(
    const LpEvaluator& /* evaluator */,
    bool /* minimizing */,
    const interface::OptimalSolverSpec& /* configs */) {
  throw std::runtime_error(
      "LP expressions are not yet implemented for ObjectPartitionWithMinPresence");
}

} // namespace facebook::rebalancer
