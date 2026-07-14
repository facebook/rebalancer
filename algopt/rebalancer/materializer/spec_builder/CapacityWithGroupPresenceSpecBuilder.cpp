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

#include "algopt/rebalancer/materializer/spec_builder/CapacityWithGroupPresenceSpecBuilder.h"

#include "algopt/rebalancer/materializer/utils/FilterWrapper.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"

#include <folly/MapUtil.h>

namespace facebook::rebalancer::materializer {
namespace {
std::optional<std::vector<entities::GroupId>> getFilteredGroupIds(
    const GroupFilterWrapper& groupFilter) {
  return groupFilter.empty() ? std::nullopt
                             : std::make_optional(groupFilter.getGroupIds());
}

std::optional<std::vector<entities::ScopeItemId>> getFilteredScopeItemIds(
    const ScopeItemFilterWrapper& scopeItemFilter) {
  return scopeItemFilter.empty()
      ? std::nullopt
      : std::make_optional(scopeItemFilter.getScopeItemIds());
}

// Returns numObjects * maxDimensionValue which is an upper bound of the maximum
// possible continuous utilization. Returns 0 if empty or non-positive max.
double computeMaxPossibleUtil(
    size_t numObjects,
    const entities::ObjectScalarDimension& dimension) {
  const auto maxDimValue = dimension.getMaximumValue();
  if (numObjects == 0 || maxDimValue <= 0) {
    return 0.0;
  }
  return static_cast<double>(numObjects) * maxDimValue;
}

// Upper bound on penalty such that per-move penalty change never exceeds
// the minimum nonzero violation change. The 0.5 factor keeps penalty
// strictly subordinate to violations.
// With roundUp: violations are integer-valued, min change is 1.
// Without roundUp: violations can be as small as minPositiveDimValue.
double computePenaltyBound(
    bool roundUpGroupUtil,
    const entities::ObjectScalarDimension& dimension) {
  if (roundUpGroupUtil) {
    return 0.5;
  }
  const auto minPositiveDimValue = dimension.getMinimumPositiveValue();
  if (!minPositiveDimValue.has_value()) {
    return 0.0;
  }
  return 0.5 * std::min(*minPositiveDimValue, 1.0);
}

folly::F14FastMap<entities::ScopeItemId, folly::F14FastSet<entities::GroupId>>
buildScopeItemToAlwaysPresentGroups(
    const entities::Universe& universe,
    entities::ScopeId aggregationScopeId,
    entities::PartitionId aggregationPartitionId,
    const std::map<std::string, std::vector<std::string>>&
        scopeItemToAlwaysPresentGroups) {
  folly::F14FastMap<entities::ScopeItemId, folly::F14FastSet<entities::GroupId>>
      result;
  result.reserve(scopeItemToAlwaysPresentGroups.size());
  for (const auto& [scopeItemName, groupNames] :
       scopeItemToAlwaysPresentGroups) {
    const auto scopeItemId =
        universe.getScopeItemId(aggregationScopeId, scopeItemName);
    auto& groups = result[scopeItemId];
    groups.reserve(groupNames.size());
    for (const auto& groupName : groupNames) {
      groups.insert(universe.getGroupId(aggregationPartitionId, groupName));
    }
  }
  return result;
}

} // namespace

CapacityWithGroupPresenceSpecBuilder::CapacityWithGroupPresenceSpecBuilder(
    std::shared_ptr<const entities::Universe> universe,
    interface::CapacityWithGroupPresenceSpec spec,
    bool needsContinuousExpressions)
    : SpecBuilder(universe),
      spec_(std::move(spec)),
      needsContinuousExpressions_(needsContinuousExpressions),
      mainScopeId_(universe_->getScopeId(*spec_.scope())),
      mainScope_(universe_->getScope(mainScopeId_)),
      aggregationScopeId_(
          spec_.aggregationScope()
              ? universe_->getScopeId(*spec_.aggregationScope())
              : mainScopeId_),
      dimensionId_(universe_->getDimensionId(*spec_.dimension())),
      dimension_(universe_->getObjects().getDimension(dimensionId_).only()),
      mainPartitionId_(universe_->getPartitionId(*spec_.partition())),
      mainPartition_(universe_->getPartition(mainPartitionId_)),
      aggregationPartitionId_(
          spec_.aggregationPartition()
              ? universe_->getPartitionId(*spec_.aggregationPartition())
              : mainPartitionId_),
      aggregationPartition_(universe_->getPartition(aggregationPartitionId_)),
      capacityLimits_(LimitWrapper(
          *universe_,
          *spec_.scopeItemToLimit(),
          mainScopeId_,
          mainPartitionId_)),
      groupToPresenceWeight_(LimitWrapper(
          *universe_,
          *spec_.groupToPresenceWeight(),
          aggregationScopeId_,
          aggregationPartitionId_)),
      groupToExtraAdditivePenalty_(LimitWrapper(
          *universe_,
          *spec_.groupToExtraAdditivePenalty(),
          aggregationScopeId_,
          aggregationPartitionId_)),
      scopeItemToAlwaysPresentGroups_(buildScopeItemToAlwaysPresentGroups(
          *universe_,
          aggregationScopeId_,
          aggregationPartitionId_,
          *spec_.scopeItemToAlwaysPresentGroups())),
      filteredMainScopeItemIds_(getFilteredScopeItemIds(ScopeItemFilterWrapper(
          *universe_,
          *spec_.scopeItemFilter(),
          mainScopeId_))),
      filteredGroupIds_(getFilteredGroupIds(GroupFilterWrapper(
          *universe_,
          *spec_.groupFilter(),
          mainPartitionId_))),
      penaltyBound_(computePenaltyBound(
          *spec_.roundUpGroupUtilOnScopeItem(),
          dimension_)),
      totalObjectCount_([&]() -> size_t {
        size_t count = 0;
        for (const auto groupId : getRelevantMainGroupIds()) {
          count += mainPartition_.getObjectIds(groupId).size();
        }
        return count;
      }()) {
  auto throwMsg = [&](const std::string& reason) {
    throw std::runtime_error(
        fmt::format(
            "CapacityWithGroupPresenceSpecBuilder is currently not supported with {}",
            reason));
  };

  auto& objectDimension = universe_->getObjects().getDimension(dimensionId_);
  if (objectDimension.size() > 1) {
    throwMsg("non-scalar dimensions");
  }
  if (objectDimension.hasNegativeValues()) {
    throwMsg("dimensions with negative values");
  }

  if (spec_.intent() ==
          interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM &&
      mainPartitionId_ != aggregationPartitionId_) {
    throw std::runtime_error(
        fmt::format(
            "main partition and aggregation partition must be the same when CapacityWithGroupPresenceUsageIntent is PER_SCOPE_ITEM, but got mainPartition='{}', aggregationPartition='{}'",
            universe_->getEntityName(mainPartitionId_),
            universe_->getEntityName(aggregationPartitionId_)));
  }

  if (!spec_.groupUtilMultipliers()->empty()) {
    for (const auto& multiplier : *spec_.groupUtilMultipliers()) {
      auto limitWrapper = LimitWrapper(
          *universe_,
          *multiplier.value(),
          aggregationScopeId_,
          aggregationPartitionId_);
      groupUtilMultiplierMap_[*multiplier.target()].emplace_back(
          std::move(limitWrapper));
    }
  } else {
    for (const auto& multiplier : *spec_.multiplierList()) {
      groupUtilMultiplierMap_[interface::GroupUtilMultiplierTarget::COMMON]
          .emplace_back(LimitWrapper(
              *universe_,
              multiplier,
              aggregationScopeId_,
              aggregationPartitionId_));
    }
  }
}

folly::coro::Task<ExprPtr> CapacityWithGroupPresenceSpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  co_return getAggregatedConstraintViolation(
      co_await constraints(expressionBuilder), *universe_);
}

folly::coro::Task<std::vector<ConstraintInfo>>
CapacityWithGroupPresenceSpecBuilder::constraints(
    ExpressionBuilder& expressionBuilder) const {
  switch (*spec_.intent()) {
    case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
      // Aggregation groups are only needed by (and computed for) the optimized
      // path.
      std::optional<entities::Set<entities::GroupId>> aggregationGroupIds;
      if (shouldUseOptimizedPath()) {
        aggregationGroupIds = buildAggregationGroupIds(expressionBuilder);
      }
      co_return co_await scopeItemConstraints(
          expressionBuilder, aggregationGroupIds);
    }
    case interface::CapacityWithGroupPresenceUsageIntent::
        PER_GROUP_AND_SCOPE_ITEM:
      co_return co_await groupAndScopeItemConstraints(expressionBuilder);
  }
}

folly::coro::Task<std::vector<ConstraintInfo>>
CapacityWithGroupPresenceSpecBuilder::scopeItemConstraints(
    ExpressionBuilder& expressionBuilder,
    const std::optional<entities::Set<entities::GroupId>>& aggregationGroupIds)
    const {
  const auto& scopeItemIds = getRelevantMainScopeItemIds();
  std::vector<ConstraintInfo> constraints;
  constraints.reserve(scopeItemIds.size());
  for (auto mainScopeItemId : scopeItemIds) {
    auto scopeItemUtil = co_await getScopeItemUtil(
        mainScopeItemId, expressionBuilder, aggregationGroupIds);
    auto constraintExpr = getConstraintExpr(
        mainScopeItemId, /*mainGroupIdOpt=*/std::nullopt, scopeItemUtil.util);
    auto additionalPenaltyExpr = getAdditionalPenaltyExpr(
        mainScopeItemId,
        /*mainGroupIdOpt=*/std::nullopt,
        expressionBuilder,
        scopeItemUtil.penaltyUtil);
    constraints.emplace_back(
        std::move(constraintExpr), std::move(additionalPenaltyExpr));
  }

  co_return constraints;
}

ExprPtr CapacityWithGroupPresenceSpecBuilder::getConstraintExpr(
    entities::ScopeItemId mainScopeItemId,
    std::optional<entities::GroupId> mainGroupIdOpt,
    const ExprPtr& util) const {
  const auto& scopeDimension = mainScope_.getDimension(dimensionId_);
  auto limit = mainGroupIdOpt
      ? capacityLimits_.getLimit(mainScopeItemId, *mainGroupIdOpt)
      : capacityLimits_.getLimit(mainScopeItemId);
  if (capacityLimits_.getType() == interface::LimitType::RELATIVE) {
    limit *= scopeDimension.getValue(mainScopeItemId);
  }
  switch (*spec_.bound()) {
    case interface::CapacityWithGroupPresenceBound::MAX:
      return (util - limit);
    case interface::CapacityWithGroupPresenceBound::MIN:
      return (limit - util);
  }

  throw std::runtime_error("Unknown bound type");
}

ExprPtr CapacityWithGroupPresenceSpecBuilder::getAdditionalPenaltyExpr(
    entities::ScopeItemId mainScopeItemId,
    std::optional<entities::GroupId> mainGroupIdOpt,
    ExpressionBuilder& /*expressionBuilder*/,
    const ExprPtr& penaltyUtil) const {
  if (penaltyUtil == nullptr) {
    return nullptr;
  }

  // if Util(G, S) is the utilization of a group G in a scopeItem S with
  // roundUp and GroupToPresenceWeight included (i.e, Util(G,S) is the
  // 'scopeItemUtil' computed above), then U'(G, S) <= U(G, S), where
  // U'(G, S) >= 0 is the scope item utilization computed by ignoring
  // roundUp and GroupToPresenceWeight.
  // We want additional penalty expressions such that the penalty expression
  // is continuous, it remains positive when the constraint is broken, and
  // also incentivizes moves in the same direction as that is needed to fix
  // the broken constraint.

  const auto normFactor = [&]() -> double {
    switch (*spec_.continuousPenaltyType()) {
      case interface::ContinuousPenaltyType::CONTINUOUS_UTILIZATION:
        return 1.0;
      case interface::ContinuousPenaltyType::
          NORMALIZED_CONTINUOUS_UTILIZATION: {
        if (penaltyBound_ == 0.0) {
          return 0.0;
        }
        const auto numObjects = mainGroupIdOpt.has_value()
            ? mainPartition_.getObjectIds(*mainGroupIdOpt).size()
            : totalObjectCount_;
        const auto maxPossibleUtil =
            computeMaxPossibleUtil(numObjects, dimension_);
        return maxPossibleUtil == 0 ? 0.0 : penaltyBound_ / maxPossibleUtil;
      }
    }
  }();

  if (normFactor == 0.0) {
    return nullptr;
  }

  switch (*spec_.bound()) {
    case interface::CapacityWithGroupPresenceBound::MAX:
      return normFactor == 1.0 ? penaltyUtil : penaltyUtil * normFactor;
    case interface::CapacityWithGroupPresenceBound::MIN: {
      auto expr =
          getConstraintExpr(mainScopeItemId, mainGroupIdOpt, penaltyUtil);
      return normFactor == 1.0 ? expr : expr * normFactor;
    }
  }
}

folly::coro::Task<std::vector<ConstraintInfo>>
CapacityWithGroupPresenceSpecBuilder::groupAndScopeItemConstraints(
    ExpressionBuilder& expressionBuilder) const {
  const auto& scopeItemIds = getRelevantMainScopeItemIds();
  const auto& mainGroupIds = getRelevantMainGroupIds();
  const auto numGroups = mainGroupIds.size();
  const auto totalPairs = scopeItemIds.size() * numGroups;

  std::vector<ConstraintInfo> constraints(totalPairs, ConstraintInfo{nullptr});
  co_await CoroUtils::runEachTaskBatched<size_t>(
      0, totalPairs, [&](size_t pairIdx) -> folly::coro::Task<void> {
        const auto mainScopeItemId = scopeItemIds[pairIdx / numGroups];
        const auto mainGroupId = mainGroupIds[pairIdx % numGroups];

        auto groupScopeItemUtil = co_await getGroupUtilInMainScopeItem(
            mainGroupId, mainScopeItemId, expressionBuilder);
        auto constraintExpr = getConstraintExpr(
            mainScopeItemId, mainGroupId, groupScopeItemUtil.util);
        auto additionalPenaltyExpr = getAdditionalPenaltyExpr(
            mainScopeItemId,
            mainGroupId,
            expressionBuilder,
            groupScopeItemUtil.penaltyUtil);

        constraints[pairIdx] = ConstraintInfo{
            std::move(constraintExpr), std::move(additionalPenaltyExpr)};
      });

  co_return constraints;
}

CapacityWithGroupPresenceSpecBuilder::UtilExprs
CapacityWithGroupPresenceSpecBuilder::zeroUtilExprs() const {
  // `penaltyUtil` is not needed if `needsContinuousExpressions_` is false.
  return UtilExprs{
      .util = const_expr(0, *universe_),
      .penaltyUtil =
          needsContinuousExpressions_ ? const_expr(0, *universe_) : nullptr};
}

void CapacityWithGroupPresenceSpecBuilder::addUtilExprs(
    UtilExprs& acc,
    UtilExprs contribution) {
  acc.util += std::move(contribution.util);
  if (acc.penaltyUtil != nullptr && contribution.penaltyUtil != nullptr) {
    acc.penaltyUtil += std::move(contribution.penaltyUtil);
  }
}

folly::coro::Task<CapacityWithGroupPresenceSpecBuilder::UtilExprs>
CapacityWithGroupPresenceSpecBuilder::getScopeItemUtil(
    entities::ScopeItemId mainScopeItemId,
    ExpressionBuilder& expressionBuilder,
    const std::optional<entities::Set<entities::GroupId>>& aggregationGroupIds)
    const {
  if (shouldUseOptimizedPath()) {
    if (!aggregationGroupIds.has_value() || aggregationGroupIds->empty()) {
      co_return zeroUtilExprs();
    }
    const auto& dimension =
        universe_->getObjects().getDimension(dimensionId_).only();
    co_return dimension.isDynamic()
        ? buildOptimizedScopeItemUtilExprForDynamicDimension(
              mainScopeItemId, expressionBuilder, *aggregationGroupIds)
        : buildOptimizedScopeItemUtilExprForStaticDimension(
              mainScopeItemId, expressionBuilder, *aggregationGroupIds);
  }

  auto utilExprs = zeroUtilExprs();
  for (auto mainGroupId : getRelevantMainGroupIds()) {
    addUtilExprs(
        utilExprs,
        co_await getGroupUtilInMainScopeItem(
            mainGroupId, mainScopeItemId, expressionBuilder));
  }

  co_return utilExprs;
}

bool CapacityWithGroupPresenceSpecBuilder::shouldUseOptimizedPath() const {
  const auto& dimension =
      universe_->getObjects().getDimension(dimensionId_).only();

  // Don't use the optimized expression if:
  // - we are using optimal solver
  // - the dimension is routing-config based
  return needsContinuousExpressions_ && !dimension.isRoutingConfigBased();
}

entities::Set<entities::GroupId>
CapacityWithGroupPresenceSpecBuilder::buildAggregationGroupIds(
    ExpressionBuilder& expressionBuilder) const {
  entities::Set<entities::GroupId> aggregationGroupIds;
  for (const auto mainGroupId : getRelevantMainGroupIds()) {
    const auto& nestedGroupIds = expressionBuilder.getNestedImage(
        mainPartitionId_, aggregationPartitionId_, mainGroupId);
    aggregationGroupIds.insert(nestedGroupIds.begin(), nestedGroupIds.end());
  }
  return aggregationGroupIds;
}

CapacityWithGroupPresenceSpecBuilder::UtilExprs
CapacityWithGroupPresenceSpecBuilder::createGroupUtilExpr(
    ExprPtr objectPartition,
    entities::ScopeItemId aggregationScopeItemId,
    const Assignment& initialAssignment) const {
  // The constraint lookup and its penalty lookup are two reads of the SAME
  // objectPartition, so it is captured by reference and intentionally not moved
  // -- both makeLookup() calls must share it. (Don't add std::move here.)
  const auto makeLookup = [&](bool makeContinuousPenaltyTerm) -> ExprPtr {
    return std::make_shared<ObjectPartitionLookupWithMinPresence>(
        objectPartition,
        universe_->getScope(aggregationScopeId_)
            .getContainerIdsPtr(aggregationScopeItemId),
        aggregationScopeId_,
        aggregationScopeItemId,
        *universe_,
        initialAssignment,
        /*groupLimitOverrides=*/PackerMap<entities::GroupId, double>{},
        /*initialDuringObjects=*/PackerSet<entities::ObjectId>{},
        /*defaultGroupLimitOverride=*/std::nullopt,
        /*penaltyTransform=*/
        ObjectPartitionLookupPenaltyTransform::IDENTITY,
        /*groupsAllowed=*/0,
        ObjectPartitionLookup<
            ObjectPartitionLookupWithMinPresencePolicy>::Bound::MAX,
        ObjectPartitionLookupWithMinPresencePolicy::Data(
            groupToPresenceWeight_,
            groupToExtraAdditivePenalty_,
            groupUtilMultiplierMap_,
            makeContinuousPenaltyTerm,
            *spec_.roundUpGroupUtilOnScopeItem(),
            scopeItemToAlwaysPresentGroups_));
  };

  return UtilExprs{
      .util = makeLookup(/*makeContinuousPenaltyTerm=*/false),
      .penaltyUtil = needsContinuousExpressions_
          ? makeLookup(/*makeContinuousPenaltyTerm=*/true)
          : nullptr};
}

CapacityWithGroupPresenceSpecBuilder::UtilExprs
CapacityWithGroupPresenceSpecBuilder::
    buildOptimizedScopeItemUtilExprForStaticDimension(
        const entities::ScopeItemId& mainScopeItemId,
        ExpressionBuilder& expressionBuilder,
        const entities::Set<entities::GroupId>& aggregationGroupIds) const {
  auto utilExprs = zeroUtilExprs();
  auto objectPartition = expressionBuilder.getObjectPartition(
      /*groupLimits=*/{},
      dimensionId_,
      aggregationPartitionId_,
      /*normalizeByGroupSize=*/false,
      /*scopeParams=*/std::nullopt,
      aggregationGroupIds);

  for (const auto& aggregationScopeItemId : expressionBuilder.getNestedImage(
           mainScopeId_, aggregationScopeId_, mainScopeItemId)) {
    addUtilExprs(
        utilExprs,
        createGroupUtilExpr(
            objectPartition,
            aggregationScopeItemId,
            expressionBuilder.getInitialAssignment()));
  }

  return utilExprs;
}

CapacityWithGroupPresenceSpecBuilder::UtilExprs
CapacityWithGroupPresenceSpecBuilder::
    buildOptimizedScopeItemUtilExprForDynamicDimension(
        const entities::ScopeItemId& mainScopeItemId,
        ExpressionBuilder& expressionBuilder,
        const entities::Set<entities::GroupId>& aggregationGroupIds) const {
  auto utilExprs = zeroUtilExprs();

  for (const auto& aggregationScopeItemId : expressionBuilder.getNestedImage(
           mainScopeId_, aggregationScopeId_, mainScopeItemId)) {
    auto utilScopeParams = materializer::ExpressionBuilder::ScopeParams{
        .scopeId = aggregationScopeId_, .scopeItemId = aggregationScopeItemId};
    auto objectPartition = expressionBuilder.getObjectPartition(
        {},
        dimensionId_,
        aggregationPartitionId_,
        false,
        std::move(utilScopeParams),
        aggregationGroupIds);

    addUtilExprs(
        utilExprs,
        createGroupUtilExpr(
            std::move(objectPartition),
            aggregationScopeItemId,
            expressionBuilder.getInitialAssignment()));
  }

  return utilExprs;
}

folly::coro::Task<CapacityWithGroupPresenceSpecBuilder::UtilExprs>
CapacityWithGroupPresenceSpecBuilder::getGroupUtilInMainScopeItem(
    entities::GroupId mainGroupId,
    entities::ScopeItemId mainScopeItemId,
    ExpressionBuilder& expressionBuilder) const {
  const auto& aggregationScopeItemIds = expressionBuilder.getNestedImage(
      mainScopeId_, aggregationScopeId_, mainScopeItemId);
  const auto& aggregationGroupIds = expressionBuilder.getNestedImage(
      mainPartitionId_, aggregationPartitionId_, mainGroupId);

  auto utilExprs = zeroUtilExprs();
  for (auto aggregationScopeItemId : aggregationScopeItemIds) {
    for (auto aggregationGroupId : aggregationGroupIds) {
      addUtilExprs(
          utilExprs,
          co_await getGroupUtilContributionToScopeItemUtil(
              aggregationGroupId, aggregationScopeItemId, expressionBuilder));
    }
  }
  co_return utilExprs;
}

folly::coro::Task<CapacityWithGroupPresenceSpecBuilder::UtilExprs>
CapacityWithGroupPresenceSpecBuilder::getGroupUtilContributionToScopeItemUtil(
    entities::GroupId aggregationGroupId,
    entities::ScopeItemId aggregationScopeItemId,
    ExpressionBuilder& expressionBuilder) const {
  auto actualGroupUtilInScopeItem = co_await expressionBuilder.getAbsoluteUtil(
      UtilMetric::AFTER,
      dimensionId_,
      aggregationScopeId_,
      aggregationScopeItemId,
      aggregationPartitionId_,
      aggregationGroupId);
  // Snapshot the raw util for the continuous penalty term before the weighting
  // and rounding below mutate the util expression. Null when the penalty term
  // is not needed (e.g. the optimal solver).
  ExprPtr unweightedPenalty = needsContinuousExpressions_
      ? folly::copy(actualGroupUtilInScopeItem)
      : nullptr;

  const auto presenceWeight = groupToPresenceWeight_.getLimit(
      aggregationScopeItemId, aggregationGroupId);
  auto minContributionToUtil =
      isGroupAlwaysPresent(aggregationScopeItemId, aggregationGroupId)
      ? const_expr(presenceWeight, *universe_)
      : presenceWeight * step(actualGroupUtilInScopeItem);

  // Apply multipliers which targets to presence weight.
  minContributionToUtil = getWeightedExpr(
      minContributionToUtil,
      aggregationGroupId,
      aggregationScopeItemId,
      {interface::GroupUtilMultiplierTarget::PRESENCE_WEIGHT,
       interface::GroupUtilMultiplierTarget::COMMON},
      *spec_.roundUpGroupUtilOnScopeItem());
  // Apply multipliers which targets to actual util.
  actualGroupUtilInScopeItem = getWeightedExpr(
      actualGroupUtilInScopeItem,
      aggregationGroupId,
      aggregationScopeItemId,
      {interface::GroupUtilMultiplierTarget::UTILIZATION,
       interface::GroupUtilMultiplierTarget::COMMON},
      *spec_.roundUpGroupUtilOnScopeItem());
  auto finalUtil = max(minContributionToUtil, actualGroupUtilInScopeItem);

  if (unweightedPenalty == nullptr) {
    co_return UtilExprs{.util = std::move(finalUtil), .penaltyUtil = nullptr};
  }

  const auto extraAdditivePenalty = groupToExtraAdditivePenalty_.getLimit(
      aggregationScopeItemId, aggregationGroupId);
  if (!universe_->getPrecision().isEqual(extraAdditivePenalty, 0.0)) {
    unweightedPenalty = max(unweightedPenalty + extraAdditivePenalty, 0.0);
  }
  auto penaltyUtil = getWeightedExpr(
      unweightedPenalty,
      aggregationGroupId,
      aggregationScopeItemId,
      {interface::GroupUtilMultiplierTarget::PRESENCE_WEIGHT,
       interface::GroupUtilMultiplierTarget::UTILIZATION,
       interface::GroupUtilMultiplierTarget::COMMON});
  // TODO(@yangsea): add support for min bound
  if (*spec_.bound() == interface::CapacityWithGroupPresenceBound::MAX) {
    penaltyUtil = product(
        step(
            finalUtil -
            const_expr(
                expressionBuilder.getLowerBound(*finalUtil), *universe_)),
        std::move(penaltyUtil));
  }

  co_return UtilExprs{
      .util = std::move(finalUtil), .penaltyUtil = std::move(penaltyUtil)};
}

ExprPtr CapacityWithGroupPresenceSpecBuilder::getWeightedExpr(
    ExprPtr& expr,
    entities::GroupId aggregationGroupId,
    entities::ScopeItemId aggregationScopeItemId,
    const std::vector<interface::GroupUtilMultiplierTarget>& targets,
    bool applyCeilAfterEach) const {
  if (applyCeilAfterEach) {
    expr = ceil(expr);
  }
  auto weightedExpr = 1 * expr;
  for (const auto& target : targets) {
    for (const auto& multiplier :
         folly::get_default(groupUtilMultiplierMap_, target, {})) {
      auto weight =
          multiplier.getLimit(aggregationScopeItemId, aggregationGroupId);
      if (universe_->getPrecision().isEqual(weight, 0)) {
        return const_expr(0, *universe_);
      }

      weightedExpr *= weight;
      if (applyCeilAfterEach) {
        weightedExpr = ceil(weightedExpr);
      }
    }
  }

  return weightedExpr;
}

const std::vector<entities::GroupId>&
CapacityWithGroupPresenceSpecBuilder::getRelevantMainGroupIds() const {
  return filteredGroupIds_.has_value() ? *filteredGroupIds_
                                       : mainPartition_.getGroupIds();
}

const std::vector<entities::ScopeItemId>&
CapacityWithGroupPresenceSpecBuilder::getRelevantMainScopeItemIds() const {
  return filteredMainScopeItemIds_.has_value() ? *filteredMainScopeItemIds_
                                               : mainScope_.getScopeItemIds();
}

bool CapacityWithGroupPresenceSpecBuilder::isGroupAlwaysPresent(
    entities::ScopeItemId aggregationScopeItemId,
    entities::GroupId aggregationGroupId) const {
  const auto* presentGroups =
      folly::get_ptr(scopeItemToAlwaysPresentGroups_, aggregationScopeItemId);
  return presentGroups && presentGroups->contains(aggregationGroupId);
}

std::string CapacityWithGroupPresenceSpecBuilder::description() const {
  return fmt::format(
      "Capacity with group presence {} w.r.t. dimension '{}', partition '{}' (aggregationPartition '{}'), scope '{}' (aggregationScope '{}'), bound '{}', roundUp = {}",
      apache::thrift::util::enumNameSafe(*spec_.intent()),
      *spec_.dimension(),
      *spec_.partition(),
      spec_.aggregationPartition().value_or(*spec_.partition()),
      *spec_.scope(),
      spec_.aggregationScope().value_or(*spec_.scope()),
      apache::thrift::util::enumNameSafe(*spec_.bound()),
      *spec_.roundUpGroupUtilOnScopeItem() ? "true" : "false");
}

SpecParameters CapacityWithGroupPresenceSpecBuilder::getSpecInfo() const {
  SpecParameters info;
  info.name = *spec_.name();
  info.scope = *spec_.scope();
  info.partition = *spec_.partition();
  info.dimension = *spec_.dimension();
  info.limitType =
      apache::thrift::util::enumNameSafe(*spec_.scopeItemToLimit()->type());
  return info;
}

} // namespace facebook::rebalancer::materializer
