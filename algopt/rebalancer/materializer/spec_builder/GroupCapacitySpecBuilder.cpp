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

#include "algopt/rebalancer/materializer/spec_builder/GroupCapacitySpecBuilder.h"

#include "algopt/rebalancer/solver/expressions/GroupScopeItemTransformUtil.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include <algopt/rebalancer/entities/Identifiers.h>
#include <algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h>
#include <algopt/rebalancer/materializer/spec_builder/SpecBuilder.h>
#include <algopt/rebalancer/materializer/utils/FilterWrapper.h>

#include <thrift/lib/cpp/util/EnumUtils.h>

#include <cstdint>
#include <stdexcept>

using apache::thrift::can_throw;

namespace facebook::rebalancer::materializer {

using namespace interface;

GroupCapacitySpecBuilder::GroupCapacitySpecBuilder(
    std::shared_ptr<const entities::Universe> universe,
    interface::GroupCapacitySpec spec)
    : SpecBuilder(std::move(universe)),
      spec_(std::move(spec)),
      mainPartitionId_(universe_->getPartitionId(*spec_.partitionName())),
      contributionPartitionId_(
          spec_.contributionPartition().has_value()
              ? universe_->getPartitionId(*spec_.contributionPartition())
              : mainPartitionId_),
      scopeId_(universe_->getScopeId(*spec_.scope())),
      objectCountDimensionId_(universe_->getDimensionId(
          fmt::format("{}_count", universe_->getObjectTypeName()))),
      groupToCapacityLimit_(
          LimitWrapper(*universe_, *spec_.limit(), scopeId_, mainPartitionId_)),
      contributionMultipliers_(LimitWrapper(
          *universe_,
          *spec_.contribution(),
          scopeId_,
          contributionPartitionId_)),
      mainGroupToContributionGroups_(getMainGroupIdToContributionGroupIds()) {
  if (auto bundleConfig = spec_.bundleConfig()) {
    perScopeItemBundleSize_ = LimitWrapper(*universe_, *bundleConfig, scopeId_);
  }
}

folly::coro::Task<ExprPtr> GroupCapacitySpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  co_return getAggregatedConstraintViolation(
      co_await constraints(expressionBuilder), *universe_);
}

/**
  This spec ensures that for every group G and scopeItem S, the capacity
  given by  count(G, S) * contribution(G, S) is bounded by LIMIT

  LIMIT is given by limits of the partition
  contibution(G,S) is given by limits of contribution partition
*/
std::string GroupCapacitySpecBuilder::description() const {
  return fmt::format(
      "{} capacity {} for groups of {} on scope {} using contribution partition {}",
      apache::thrift::util::enumNameSafe(*spec_.bound()),
      apache::thrift::util::enumNameSafe(*spec_.definition()),
      *spec_.partitionName(),
      *spec_.scope(),
      spec_.contributionPartition().value_or(*spec_.partitionName()));
}

SpecParameters GroupCapacitySpecBuilder::getSpecInfo() const {
  return SpecParameters{
      .name = *spec_.name(),
      .scope = *spec_.scope(),
      .partition = *spec_.partitionName(),
      .definition = apache::thrift::util::enumNameSafe(*spec_.definition()),
      .boundType = apache::thrift::util::enumNameSafe(*spec_.bound()),
      .limitType = apache::thrift::util::enumNameSafe(*spec_.limit()->type()),
      .filterAllowListSize = spec_.filter()->itemsWhitelist()
          ? static_cast<int>(spec_.filter()->itemsWhitelist()->size())
          : 0,
      .filterBlockListSize = spec_.filter()->itemsBlacklist()
          ? static_cast<int>(spec_.filter()->itemsBlacklist()->size())
          : 0};
}

folly::coro::Task<std::vector<ConstraintInfo>>
GroupCapacitySpecBuilder::constraints(
    ExpressionBuilder& expressionBuilder) const {
  std::vector<GroupCapacitySpecDefinition> definitionTypes;
  if (*spec_.definition() == GroupCapacitySpecDefinition::DURING_AND_AFTER) {
    definitionTypes.push_back(GroupCapacitySpecDefinition::DURING);
    definitionTypes.push_back(GroupCapacitySpecDefinition::AFTER);
  } else {
    definitionTypes.push_back(*spec_.definition());
  }

  auto scopeItemFilter =
      ScopeItemFilterWrapper(*universe_, *spec_.filter(), scopeId_);
  auto scopeItemIds = scopeItemFilter.getScopeItemIds();
  auto relevantContainersPtr =
      std::make_shared<entities::Set<entities::ContainerId>>();
  for (auto scopeItemId : scopeItemIds) {
    auto& scopeItemContainers =
        universe_->getScope(scopeId_).getContainerIds(scopeItemId);
    relevantContainersPtr->insert(
        scopeItemContainers.begin(), scopeItemContainers.end());
  }

  std::vector<ConstraintInfo> exprs;
  for (auto definitionType : definitionTypes) {
    // for each scopeItem, ensure that utilization of every group respects the
    // capacity limits specified for that group
    auto currExprs = co_await getConstraint(
        expressionBuilder, definitionType, scopeItemIds, relevantContainersPtr);
    exprs.insert(exprs.end(), currExprs.begin(), currExprs.end());
  }
  co_return exprs;
}

folly::coro::Task<std::vector<ConstraintInfo>>
GroupCapacitySpecBuilder::getConstraint(
    ExpressionBuilder& expressionBuilder,
    GroupCapacitySpecDefinition definition,
    const std::vector<entities::ScopeItemId>& scopeItemIds,
    std::shared_ptr<const entities::Set<entities::ContainerId>>
        relevantContainersPtr) const {
  std::vector<ConstraintInfo> exprs;
  for (auto& [mainGroupId, contributionGroupIds] :
       mainGroupToContributionGroups_) {
    auto groupUtil = const_expr(0, *universe_);

    if (definition == GroupCapacitySpecDefinition::AFTER) {
      groupUtil = getAfterUtilForMainGroup(
          contributionGroupIds,
          scopeItemIds,
          relevantContainersPtr,
          expressionBuilder.getUpdatedInitialAssignment());
    } else {
      groupUtil = co_await getDuringUtilForMainGroup(
          expressionBuilder, contributionGroupIds, scopeItemIds);
    }
    double limit = groupToCapacityLimit_.getLimit(mainGroupId);

    auto maxConstraintExpr = [&groupUtil, &limit]() {
      // sum(util * contrib) <= limit
      return groupUtil - limit;
    };
    auto minConstraintExpr = [&groupUtil, &limit]() {
      // sum(util * contrib) >= limit
      return limit - groupUtil;
    };

    switch (*spec_.bound()) {
      case GroupCapacitySpecBound::MIN:
        exprs.emplace_back(minConstraintExpr());
        break;
      case GroupCapacitySpecBound::MAX:
        exprs.emplace_back(maxConstraintExpr());
        break;
      case GroupCapacitySpecBound::EXACT:
        exprs.emplace_back(maxConstraintExpr());
        exprs.emplace_back(minConstraintExpr());
        break;
    }
  }

  co_return exprs;
}

ExprPtr GroupCapacitySpecBuilder::getAfterUtilForMainGroup(
    const std::vector<entities::GroupId>& contributionGroupIds,
    const std::vector<entities::ScopeItemId>& scopeItemIds,
    std::shared_ptr<const entities::Set<entities::ContainerId>>
        relevantContainersPtr,
    const Assignment& updatedInitialAssignment) const {
  auto getTransformType = [this]() {
    switch (*spec_.utilType()) {
      case GroupCapacitySpecUtilType::LINEAR:
        return GroupScopeItemTransformUtil::TransformFunctionType::IDENTITY;
      case GroupCapacitySpecUtilType::STEP:
        return GroupScopeItemTransformUtil::TransformFunctionType::STEP;
      case GroupCapacitySpecUtilType::STEP_MOD_K:
        return GroupScopeItemTransformUtil::TransformFunctionType::STEP_MOD_K;
      default:
        throw std::runtime_error("Unhandled GroupCapacitySpecUtilType");
    }
  };

  auto groupScopeItemTransformUtilType = getTransformType();
  auto groupUtil = const_expr(0, *universe_);
  TransformFunctionData transformData;
  if (perScopeItemBundleSize_.has_value()) {
    // problem checker ensures that provided bundle sizes are positive integers
    // so this cast is safe
    transformData.kForModKTransform = ModKTransformData(
        static_cast<uint32_t>(perScopeItemBundleSize_->getGlobalLimit()),
        perScopeItemBundleSize_->checkAndGetPositiveIntegerScopeItemLimits());
  }
  for (auto contributionGroupId : contributionGroupIds) {
    folly::F14FastMap<entities::ScopeItemId, double> scopeItemIdToMultiplier;
    for (auto scopeItemId : scopeItemIds) {
      scopeItemIdToMultiplier.emplace(
          scopeItemId,
          contributionMultipliers_.getLimit(scopeItemId, contributionGroupId));
    }

    auto util = std::make_shared<GroupScopeItemTransformUtil>(
        *universe_,
        contributionPartitionId_,
        contributionGroupId,
        objectCountDimensionId_,
        scopeId_,
        scopeItemIds,
        relevantContainersPtr,
        updatedInitialAssignment,
        scopeItemIdToMultiplier,
        /*scopeItemDefaultWeight=*/1.0,
        groupScopeItemTransformUtilType,
        /*normalizationConstant=*/1,
        transformData);

    inplace_add(groupUtil, util);
  }

  return groupUtil;
}

folly::coro::Task<ExprPtr> GroupCapacitySpecBuilder::getDuringUtilForMainGroup(
    ExpressionBuilder& expressionBuilder,
    const std::vector<entities::GroupId>& contributionGroupIds,
    const std::vector<entities::ScopeItemId>& scopeItemIds) const {
  auto getTransformedUtil = [this](
                                const ExprPtr& util,
                                entities::ScopeItemId scopeItemId) {
    switch (*spec_.utilType()) {
      case GroupCapacitySpecUtilType::LINEAR:
        return util;
      case GroupCapacitySpecUtilType::STEP:
        return step(util);
      case GroupCapacitySpecUtilType::STEP_MOD_K:
        assert(perScopeItemBundleSize_.has_value());
        return step_mod_k(util, perScopeItemBundleSize_->getLimit(scopeItemId));
      default:
        throw std::runtime_error("Unhandled GroupCapacitySpecUtilType");
    }
  };
  auto groupUtil = const_expr(0, *universe_);
  for (auto contributionGroupId : contributionGroupIds) {
    for (auto scopeItemId : scopeItemIds) {
      auto util = co_await expressionBuilder.getAbsoluteUtil(
          UtilMetric::DURING,
          objectCountDimensionId_,
          scopeId_,
          scopeItemId,
          contributionPartitionId_,
          contributionGroupId);
      auto contributionMultiplier =
          contributionMultipliers_.getLimit(scopeItemId, contributionGroupId);
      groupUtil +=
          getTransformedUtil(util, scopeItemId) * contributionMultiplier;
    }
  }

  co_return groupUtil;
}

entities::Map<entities::GroupId, std::vector<entities::GroupId>>
GroupCapacitySpecBuilder::getMainGroupIdToContributionGroupIds() const {
  // The current implementation requires each contribution group to be either
  // contained within a single group of the main partition, or completely
  // outside of the main partition. This means each contribution group can be
  // unambiguously mapped to at most one group of the main partition.
  // Contribution groups which are completely outside of the main partition
  // are ignored.
  auto& mainPartition = universe_->getPartition(mainPartitionId_);
  if (!mainPartition.isDisjoint()) {
    throw std::runtime_error(
        fmt::format(
            "GroupCapacitySpecBuilder expects the main partition '{}' to be disjoint",
            universe_->getEntityName(mainPartitionId_)));
  }
  auto& objectIdToMainGroupIds = mainPartition.getObjectIdToGroupIds();
  auto& contributionPartition =
      universe_->getPartition(contributionPartitionId_);

  entities::Map<entities::GroupId, std::vector<entities::GroupId>>
      mainGroupIdToContributionGroupIds;
  for (auto contributionGroupId : contributionPartition.getGroupIds()) {
    std::optional<entities::GroupId> mainGroupId = std::nullopt;
    bool first = true;
    for (auto objectId :
         contributionPartition.getObjectIds(contributionGroupId)) {
      auto newMainGroupIdPtr = folly::get_ptr(objectIdToMainGroupIds, objectId);
      auto newMainGroupId = newMainGroupIdPtr
          ? std::make_optional(newMainGroupIdPtr->at(0))
          : std::nullopt;
      if (first) {
        mainGroupId = newMainGroupId;
        first = false;
      } else if (mainGroupId != newMainGroupId) {
        throw std::runtime_error(
            fmt::format(
                "Contribution group {} has objects in multiple groups of partition {}",
                universe_->getEntityName(contributionGroupId),
                universe_->getEntityName(mainPartitionId_)));
      }
    }
    if (mainGroupId) {
      mainGroupIdToContributionGroupIds[*mainGroupId].push_back(
          contributionGroupId);
    }
  }

  return mainGroupIdToContributionGroupIds;
}

} // namespace facebook::rebalancer::materializer
