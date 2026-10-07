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

#include "algopt/rebalancer/materializer/spec_builder/GroupCountSpecBuilder.h"

#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/materializer/utils/LimitWrapper.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"

#include <thrift/lib/cpp/util/EnumUtils.h>

#include <stdexcept>

using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::interface;

namespace {
constexpr double kRectangleTolerance = 1e-5;

double getGroupWeight(
    const Universe& universe,
    PartitionId partitionId,
    GroupId groupId,
    DimensionId dimensionId,
    std::optional<ScopeItemId> scopeItemId) {
  auto& dimension = universe.getObjects().getDimension(dimensionId);
  // TODO: handle non-scalar dimensions
  if (dimension.size() != 1) {
    throw std::runtime_error("non-scalar dimensions not supported");
  }

  const bool isObjectCountDimension =
      (dimensionId ==
       universe.getDimensionId(
           fmt::format("{}_count", universe.getObjectTypeName())));

  auto& partition = universe.getPartition(partitionId);
  if (isObjectCountDimension) {
    return partition.getObjectIds(groupId).size();
  }

  const auto& objectDimension = dimension.at(0);
  return objectDimension.values(scopeItemId)
      .sum(partition.getObjectIds(groupId));
}

} // namespace

namespace facebook::rebalancer::materializer {

static UtilMetric toUtilMetric(GroupCountSpecDefinition definition) {
  switch (definition) {
    case GroupCountSpecDefinition::AFTER:
      return UtilMetric::AFTER;
    case GroupCountSpecDefinition::DURING:
      return UtilMetric::DURING;
    case GroupCountSpecDefinition::STAYED:
      return UtilMetric::STAYED;
    default:
      throw std::runtime_error("unknown group count spec definition");
  }
}

GroupCountSpecBuilder::GroupCountSpecBuilder(
    std::shared_ptr<const Universe> universe,
    GroupCountSpec spec)
    : SpecBuilder(std::move(universe)),
      spec_(std::move(spec)),
      scopeId_(universe_->getScopeId(*spec_.scope())),
      partitionId_(universe_->getPartitionId(*spec_.partitionName())),
      partition_(universe_->getPartition(partitionId_)),
      limits_(*universe_, *spec_.limit(), scopeId_, partitionId_),
      scopeFilter_(*universe_, *spec_.filter(), scopeId_) {}

ExprPtr GroupCountSpecBuilder::buildObjectPartition(
    ExpressionBuilder& expressionBuilder,
    std::optional<entities::ScopeItemId> scopeItemIdOpt) const {
  auto dimensionId = universe_->getDimensionId(*spec_.dimension());

  auto groupLimitsIndptOfScopeItem =
      limits_.getAllGroupLimitsIndptOfScopeItem();
  processFinalGroupLimits(
      groupLimitsIndptOfScopeItem, scopeItemIdOpt, limits_.getType());

  std::optional<materializer::ExpressionBuilder::ScopeParams> utilScopeParams =
      std::nullopt;
  if (scopeItemIdOpt.has_value()) {
    utilScopeParams = materializer::ExpressionBuilder::ScopeParams{
        .scopeId = scopeId_, .scopeItemId = scopeItemIdOpt.value()};
  }

  return expressionBuilder.getObjectPartition(
      groupLimitsIndptOfScopeItem,
      dimensionId,
      partitionId_,
      *spec_.squares(),
      std::move(utilScopeParams));
}

std::vector<ConstraintInfo>
GroupCountSpecBuilder::buildOptimizedGroupCountExprs(
    ExpressionBuilder& expressionBuilder,
    const std::vector<ScopeItemId>& scopeItemIds,
    GroupCountSpecDefinition definition,
    GroupCountSpecBound bound) const {
  auto dimensionId = universe_->getDimensionId(*spec_.dimension());
  auto& dimension = universe_->getObjects().getDimension(dimensionId);

  if (dimension.isDynamic()) {
    // if the dimension is dynamic, then we need to create one objectPartition
    // per scopeItem, since objectPartition has the dimension values of the
    // objects and also the limits there can depend on the scopeItem (if they
    // are relative)
    return buildOptimizedGroupCountExprsForDynamicDimensions(
        expressionBuilder, scopeItemIds, definition, bound);
  }

  return buildOptimizedGroupCountExprsForStaticDimensions(
      expressionBuilder, scopeItemIds, definition, bound);
}

std::vector<ConstraintInfo>
GroupCountSpecBuilder::buildOptimizedGroupCountExprsForStaticDimensions(
    ExpressionBuilder& expressionBuilder,
    const std::vector<ScopeItemId>& scopeItemIds,
    GroupCountSpecDefinition definition,
    GroupCountSpecBound bound) const {
  auto objectPartition =
      buildObjectPartition(expressionBuilder, std::nullopt /*scopeItemIdOpt*/);

  std::vector<ConstraintInfo> exprs;
  exprs.reserve(scopeItemIds.size());
  for (auto scopeItemId : scopeItemIds) {
    auto groupLimitOverrides = limits_.getGroupsOverride(scopeItemId);
    processFinalGroupLimits(
        groupLimitOverrides, std::nullopt, limits_.getType());

    auto expr = expressionBuilder.getObjectPartitionLookup(
        toUtilMetric(definition),
        scopeId_,
        scopeItemId,
        objectPartition,
        groupLimitOverrides,
        *spec_.squares() ? ObjectPartitionLookupPenaltyTransform::SQUARE
                         : ObjectPartitionLookupPenaltyTransform::IDENTITY,
        0,
        bound == GroupCountSpecBound::MIN);
    exprs.emplace_back(expr);
  }
  return exprs;
}

std::vector<ConstraintInfo>
GroupCountSpecBuilder::buildOptimizedGroupCountExprsForDynamicDimensions(
    ExpressionBuilder& expressionBuilder,
    const std::vector<ScopeItemId>& scopeItemIds,
    GroupCountSpecDefinition definition,
    GroupCountSpecBound bound) const {
  // if the dimension is dynamic, then we need to create one objectPartition
  // per scopeItem, since objectPartition has the dimension values of the
  // objects and also the limits there can depend on the scopeItem (if they
  // are relative)
  std::vector<ConstraintInfo> exprs;
  exprs.reserve(scopeItemIds.size());
  for (auto scopeItemId : scopeItemIds) {
    auto objectPartition = buildObjectPartition(expressionBuilder, scopeItemId);

    auto groupLimitOverrides = limits_.getGroupsOverride(scopeItemId);
    processFinalGroupLimits(
        groupLimitOverrides, scopeItemId, limits_.getType());

    auto expr = expressionBuilder.getObjectPartitionLookup(
        toUtilMetric(definition),
        scopeId_,
        scopeItemId,
        objectPartition,
        groupLimitOverrides,
        *spec_.squares() ? ObjectPartitionLookupPenaltyTransform::SQUARE
                         : ObjectPartitionLookupPenaltyTransform::IDENTITY,
        0,
        bound == GroupCountSpecBound::MIN);
    exprs.emplace_back(expr);
  }
  return exprs;
}

folly::coro::Task<std::vector<ConstraintInfo>>
GroupCountSpecBuilder::buildIndividualGroupRequirement(
    ExpressionBuilder& expressionBuilder,
    GroupCountSpecDefinition definition,
    interface::GroupCountSpecBound bound) const {
  auto dimensionId = universe_->getDimensionId(*spec_.dimension());
  auto& dimension = universe_->getObjects().getDimension(dimensionId);
  if (dimension.size() != 1) {
    throw std::runtime_error(
        "non-scalar dimensions are not supported in GroupCountSpec");
  }

  const bool squares = *spec_.squares();
  auto limitRelativeTo = *spec_.limitRelativeTo();
  const bool zeroAllowed = *spec_.zeroAllowed();

  // Recall that an optimized group count expr uses ObjectPartitionLookup and
  // has a compact representation of with O(|groups| + |scopeItems|) expression
  // as opposed to O(|groups| * |scopeItems|) in traditional implementation.
  // As of now we build this optimized expressions in the cases defined by the
  // following if statement. Note that if using optimal solver, both traditional
  // and optimized models will result in a LP model of similar complexity.
  if ((bound == interface::GroupCountSpecBound::MAX ||
       (bound == interface::GroupCountSpecBound::MIN && !zeroAllowed)) &&
      limitRelativeTo == GroupCountSpecLimitRelativeTo::GROUP_SIZE &&
      definition != GroupCountSpecDefinition::STAYED) {
    co_return buildOptimizedGroupCountExprs(
        expressionBuilder, scopeFilter_.getScopeItemIds(), definition, bound);
  }

  if (squares && bound != interface::GroupCountSpecBound::MAX) {
    throw std::runtime_error(
        "Group requirement with squares and min not yet implemented");
  }

  // However, due to technical reasons (where limit expression is not a constant
  // value), we cannot build the compact model, so we need to use a model with
  // O(|groups| * |scopeItems|) expressions. This happens in following cases:
  // 1. where bound type is MULTIPLE of a pre-specified value
  // 2. if squares is set to true
  // 3. when zero limit value is allowed
  const auto filteredScopeItemIds = scopeFilter_.getScopeItemIds();
  const auto& groupIds = partition_.getGroupIds();
  std::vector<ConstraintInfo> exprs;
  exprs.reserve(filteredScopeItemIds.size() * groupIds.size());
  for (auto scopeItemId : filteredScopeItemIds) {
    for (auto groupId : groupIds) {
      const double limitConstant = limits_.getLimit(scopeItemId, groupId);
      auto expr = co_await buildSingleRequirement(
          expressionBuilder,
          definition,
          bound,
          scopeItemId,
          groupId,
          limitConstant,
          limits_.getType());

      exprs.emplace_back(expr);
    }
  }
  co_return exprs;
}

folly::coro::Task<ExprPtr> GroupCountSpecBuilder::buildSingleRequirement(
    ExpressionBuilder& expressionBuilder,
    GroupCountSpecDefinition definition,
    interface::GroupCountSpecBound bound,
    ScopeItemId scopeItemId,
    GroupId groupId,
    double limitConstant,
    LimitType limitType) const {
  auto dimensionId = universe_->getDimensionId(*spec_.dimension());
  const bool squares = *spec_.squares();
  auto zeroAllowed = *spec_.zeroAllowed();

  auto limitExpr = co_await buildLimitExpr(
      expressionBuilder,
      limitType,
      scopeItemId,
      groupId,
      limitConstant,
      definition);
  auto expr = limitType == LimitType::RELATIVE &&
          *spec_.limitRelativeTo() ==
              GroupCountSpecLimitRelativeTo::SCOPE_ITEM_UTIL
      ? co_await expressionBuilder.getRelativeUtil(
            toUtilMetric(definition),
            dimensionId,
            scopeId_,
            scopeItemId,
            partitionId_,
            groupId)
      : co_await expressionBuilder.getAbsoluteUtil(
            toUtilMetric(definition),
            dimensionId,
            scopeId_,
            scopeItemId,
            partitionId_,
            groupId);

  // if limit is relative to groupSize, we need to convert the relative
  // limitConstant to absolute value using groupWeight
  if (limitType == LimitType::RELATIVE &&
      *spec_.limitRelativeTo() == GroupCountSpecLimitRelativeTo::GROUP_SIZE) {
    limitConstant =
        limitConstant *
        getGroupWeight(
            *universe_, partitionId_, groupId, dimensionId, scopeItemId);
  }

  if (bound == GroupCountSpecBound::MULTIPLE) {
    if (*spec_.limitRelativeTo() != GroupCountSpecLimitRelativeTo::GROUP_SIZE) {
      throw std::runtime_error(
          "only constant limit expression can be used with MULTIPLE bound");
    }
    // only allow if expr = k * limit for some integer k >= 0
    // expr = util(..) is always positive so just check if expr / limit is an
    // integer
    auto temp = expr / limitConstant;
    // return the "distance" from nearest integer multiple of limit
    expr = (ceil(temp) - temp) * limitConstant;
  } else if (bound == GroupCountSpecBound::MAX) {
    expr = expr - limitExpr;
    if (squares) {
      // Squares get normalized, so that improvement comparison
      // across different groups is scalled to be relative.
      // 10% over limit on 1000 sized group and 10 sized group have
      // the same cost.

      expr = square(
          max({const_expr(0, *universe_),
               expr /
                   universe_->getPartition(partitionId_)
                       .getObjectIds(groupId)
                       .size()},
              *universe_));
    }
  } else {
    if (zeroAllowed) {
      // TODO: this likely does not work as expected if limit is non-constant
      // such as when relative to scopeItemUtil. Find an appropriate fix
      expr = rectangle(
          expr,
          *spec_.minimumLimit() + kRectangleTolerance,
          limitConstant - kRectangleTolerance);
    } else {
      expr = limitExpr - expr;
    }
  }
  co_return expr;
}

folly::coro::Task<ExprPtr> GroupCountSpecBuilder::buildLimitExpr(
    ExpressionBuilder& expressionBuilder,
    LimitType limitType,
    ScopeItemId scopeItemId,
    GroupId groupId,
    double limitConstant,
    GroupCountSpecDefinition definition) const {
  auto limitRelativeTo = *spec_.limitRelativeTo();
  auto dimensionId = universe_->getDimensionId(*spec_.dimension());

  switch (limitType) {
    case LimitType::ABSOLUTE:
      co_return const_expr(limitConstant, *universe_);

    case LimitType::RELATIVE:
      switch (limitRelativeTo) {
        case GroupCountSpecLimitRelativeTo::SCOPE_ITEM_UTIL:
          co_return limitConstant* co_await expressionBuilder.getRelativeUtil(
              toUtilMetric(definition), dimensionId, scopeId_, scopeItemId);

        case GroupCountSpecLimitRelativeTo::GROUP_SIZE:
          co_return const_expr(
              limitConstant *
                  getGroupWeight(
                      *universe_,
                      partitionId_,
                      groupId,
                      dimensionId,
                      scopeItemId),
              *universe_);

        case GroupCountSpecLimitRelativeTo::
            GROUP_TO_SCOPE_ITEM_ROUTING_TRAFFIC: {
          if (!spec_.routingConfigForLimit().has_value()) {
            throw std::runtime_error(
                "Expected routingConfigForLimit parameter to be set when using GroupCountSpecLimitRelativeTo::GROUP_TO_SCOPE_ITEM_ROUTING_TRAFFIC");
          }
          if (limitConstant == 0.0) {
            co_return const_expr(0, *universe_);
          }
          co_return limitConstant* expressionBuilder
              .getGroupRoutingTrafficLookup(
                  universe_->getRoutingConfigId(
                      spec_.routingConfigForLimit().value()),
                  partitionId_,
                  groupId,
                  scopeId_,
                  scopeItemId);
        }

        default:
          throw std::runtime_error("unknown relative to");
      }

    default:
      throw std::runtime_error("unknown limit type");
  }
}

folly::coro::Task<std::vector<ConstraintInfo>>
GroupCountSpecBuilder::constraints(ExpressionBuilder& expressionBuilder) const {
  auto bound = *spec_.bound();
  auto definition = *spec_.definition();

  std::vector<GroupCountSpecDefinition> definitionTypes;
  std::vector<GroupCountSpecBound> boundTypes;

  if (definition == GroupCountSpecDefinition::DURING_AND_AFTER) {
    definitionTypes.push_back(GroupCountSpecDefinition::DURING);
    definitionTypes.push_back(GroupCountSpecDefinition::AFTER);
  } else {
    definitionTypes.push_back(definition);
  }

  if (bound == GroupCountSpecBound::EXACT) {
    boundTypes.push_back(GroupCountSpecBound::MIN);
    boundTypes.push_back(GroupCountSpecBound::MAX);
  } else {
    boundTypes.push_back(bound);
  }

  std::vector<ConstraintInfo> exprs;
  for (auto definitionType : definitionTypes) {
    for (auto boundType : boundTypes) {
      auto currExprs = co_await buildIndividualGroupRequirement(
          expressionBuilder, definitionType, boundType);
      exprs.insert(exprs.end(), currExprs.begin(), currExprs.end());
    }
  }

  co_return exprs;
}

folly::coro::Task<ExprPtr> GroupCountSpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  co_return getAggregatedConstraintViolation(
      co_await constraints(expressionBuilder), *universe_);
}

std::string GroupCountSpecBuilder::description() const {
  return fmt::format(
      "Group {} limit {} for {} on scope {} on dimension {}",
      apache::thrift::util::enumNameSafe(*spec_.bound()),
      apache::thrift::util::enumNameSafe(*spec_.definition()),
      *spec_.partitionName(),
      *spec_.scope(),
      *spec_.dimension());
}

void GroupCountSpecBuilder::processFinalGroupLimits(
    Map<GroupId, double>& groupLimits,
    std::optional<entities::ScopeItemId> scopeItemId,
    LimitType limitType) const {
  // processing required only when the limit type is RELATIVE
  if (limitType != LimitType::RELATIVE) {
    return;
  }

  /* Converts the group limit to actual value */
  auto dimensionId = universe_->getDimensionId(*spec_.dimension());
  for (auto& [groupId, groupLimit] : groupLimits) {
    groupLimits[groupId] =
        groupLimit *
        getGroupWeight(
            *universe_, partitionId_, groupId, dimensionId, scopeItemId);
  }
}

SpecParameters GroupCountSpecBuilder::getSpecInfo() const {
  return SpecParameters{
      .name = *spec_.name(),
      .scope = *spec_.scope(),
      .partition = *spec_.partitionName(),
      .dimension = *spec_.dimension(),
      .definition = apache::thrift::util::enumNameSafe(*spec_.definition()),
      .boundType = apache::thrift::util::enumNameSafe(*spec_.bound()),
      .limitType = apache::thrift::util::enumNameSafe(*spec_.limit()->type()),
      .zeroAllowed = *spec_.zeroAllowed() ? "yes" : "no",
      .squares = *spec_.squares() ? "yes" : "no",
      .filterAllowListSize = spec_.filter()->itemsWhitelist()
          ? static_cast<int>(spec_.filter()->itemsWhitelist()->size())
          : 0,
      .filterBlockListSize = spec_.filter()->itemsBlacklist()
          ? static_cast<int>(spec_.filter()->itemsBlacklist()->size())
          : 0};
}

namespace {

// Compute initial utilization per group for a set of containers within
// a scope item. Only groups with non-zero utilization are included.
Map<GroupId, double> getInitialUtilsPerGroupInScopeItem(
    const Universe& universe,
    const Partition& partition,
    const ObjectScalarDimension& scalarDim,
    const Set<ContainerId>& containerIds) {
  Map<GroupId, double> utils;
  const auto& containers = universe.getContainers();
  const auto& objectToGroups = partition.getObjectIdToGroupIds();
  for (const auto& containerId : containerIds) {
    for (const auto& objectId : containers.getInitialObjectIds(containerId)) {
      const auto& value = scalarDim.getValue(objectId);
      if (value == 0.0) {
        continue;
      }
      const auto* groupIds = folly::get_ptr(objectToGroups, objectId);
      if (!groupIds) {
        continue;
      }
      for (const auto& groupId : *groupIds) {
        utils[groupId] += value;
      }
    }
  }
  return utils;
}

} // namespace

void GroupCountSpecBuilder::populateInvalidMoveFilter(
    InvalidMoveFilter& invalidMoveFilter) const {
  const auto& bound = *spec_.bound();
  const auto& definition = *spec_.definition();
  if ((bound != GroupCountSpecBound::MAX &&
       bound != GroupCountSpecBound::EXACT) ||
      (definition != GroupCountSpecDefinition::AFTER &&
       definition != GroupCountSpecDefinition::DURING &&
       definition != GroupCountSpecDefinition::DURING_AND_AFTER)) {
    return;
  }

  const auto& objDim = universe_->getObjects().getDimension(
      universe_->getDimensionId(*spec_.dimension()));
  if (objDim.isDynamic() || objDim.isRoutingConfigBased() ||
      objDim.size() != 1 || objDim.only().hasNegativeValues()) {
    return;
  }

  // Fast path: non-zero global limit with no overrides → no zero-limit pairs.
  if (spec_.limit()->type() != LimitType::ABSOLUTE ||
      (limits_.onlyHasGlobalLimit() && limits_.getGlobalLimit() != 0.0)) {
    return;
  }

  const auto& scalarDim = objDim.only();
  const auto& scope = universe_->getScope(scopeId_);
  const auto isAfter = definition == GroupCountSpecDefinition::AFTER;

  // For AFTER: threshold = initial util L at this (scopeItem, group).
  //   An incoming object with v <= L can be matched by moving existing
  //   objects out, so only block v > L.
  // For DURING / DURING_AND_AFTER: any positive incoming value worsens
  //   the constraint during transit, so block all v > 0 (threshold = 0).
  std::vector<GroupId> zeroLimitGroups;
  std::vector<ObjectId> invalidObjects;
  for (const auto& scopeItemId : scopeFilter_.getScopeItemIds()) {
    zeroLimitGroups.clear();
    invalidObjects.clear();
    for (const auto& groupId : partition_.getGroupIds()) {
      if (limits_.getLimit(scopeItemId, groupId) == 0.0) {
        zeroLimitGroups.push_back(groupId);
      }
    }
    if (zeroLimitGroups.empty()) {
      continue;
    }

    const auto& containerIds = scope.getContainerIds(scopeItemId);
    const Map<GroupId, double> initialUtilByGroup = isAfter
        ? getInitialUtilsPerGroupInScopeItem(
              *universe_, partition_, scalarDim, containerIds)
        : Map<GroupId, double>{};

    for (const auto& groupId : zeroLimitGroups) {
      const auto& threshold =
          folly::get_default(initialUtilByGroup, groupId, 0.0);
      for (const auto& objectId : partition_.getObjectIds(groupId)) {
        if (scalarDim.getValue(objectId) <= threshold) {
          continue;
        }
        invalidObjects.push_back(objectId);
      }
    }
    for (const auto& containerId : containerIds) {
      invalidMoveFilter.markInvalid(invalidObjects, containerId);
    }
  }
}

} // namespace facebook::rebalancer::materializer
