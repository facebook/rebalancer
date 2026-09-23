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

#include "algopt/rebalancer/materializer/spec_builder/ColocateGroupsSpecBuilder.h"

#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/expressions/GroupScopeItemTransformUtil.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include <algopt/rebalancer/materializer/utils/FilterWrapper.h>

#include <cmath>

namespace facebook::rebalancer::materializer {
namespace {
static constexpr double kScopeItemDefaultWeight = 1.0;
static constexpr double kQuadraticTermMultiplier = 0.1;
} // namespace

ColocateGroupsSpecBuilder::ColocateGroupsSpecBuilder(
    std::shared_ptr<const entities::Universe> universe,
    interface::ColocateGroupsSpec spec,
    bool needContinuousExpressions)
    : SpecBuilder(universe),
      spec_(std::move(spec)),
      dimensionId_(
          spec_.dimension().has_value()
              ? universe_->getDimensionId(*spec_.dimension())
              : universe_->getDimensionId(
                    fmt::format("{}_count", universe_->getObjectTypeName()))),
      partitionId_(universe_->getPartitionId(*spec_.partitionName())),
      partition_(universe_->getPartition(partitionId_)),
      scopeId_(universe_->getScopeId(*spec_.scope())),
      limits_(*universe_, *spec_.limits(), scopeId_, partitionId_),
      allowedScopeItems_(
          ScopeItemFilterWrapper(*universe_, *spec_.filter(), scopeId_)
              .getScopeItemIds()),
      needContinuousExpressions_(
          needContinuousExpressions ||
          spec_.useContinuousPenaltyWithOptimal().value()) {
  const auto& objectDimension =
      universe_->getObjects().getDimension(dimensionId_);
  if (objectDimension.hasNegativeValues()) {
    throw std::runtime_error(
        "ColocateGroupsSpec is not supported when the object dimension has negative values");
  }

  const auto& scope = universe_->getScope(scopeId_);
  relevantContainersPtr_ =
      std::make_shared<entities::Set<entities::ContainerId>>();
  for (const auto scopeItemId : allowedScopeItems_) {
    const auto scopeContainersPtr = scope.getContainerIdsPtr(scopeItemId);
    relevantContainersPtr_->insert(
        scopeContainersPtr->begin(), scopeContainersPtr->end());
    const auto weightPtr = folly::get_ptr(
        *spec_.scopeItemWeights(), universe_->getEntityName(scopeItemId));
    if (weightPtr) {
      scopeItemWeights_.emplace(scopeItemId, *weightPtr);
    }
  }
}

folly::coro::Task<ExprPtr> ColocateGroupsSpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  const auto& groupIds = partition_.getGroupIds();
  const auto& initialAssignment = expressionBuilder.getInitialAssignment();
  auto aggregatedViolation = const_expr(0, *universe_);
  for (const auto groupId : groupIds) {
    const auto groupWeight = folly::get_default(
        *spec_.groupToWeight(), universe_->getEntityName(groupId), 1);
    const auto constraintViolation = getConstraintViolation(
        getConstraint(groupId, groupWeight, initialAssignment));
    aggregatedViolation += groupWeight * constraintViolation;
  }

  co_return aggregatedViolation;
}

folly::coro::Task<std::vector<ConstraintInfo>>
ColocateGroupsSpecBuilder::constraints(
    ExpressionBuilder& expressionBuilder) const {
  std::vector<ConstraintInfo> result;
  const auto& initialAssignment = expressionBuilder.getInitialAssignment();
  for (const auto groupId : partition_.getGroupIds()) {
    const auto groupWeight = folly::get_default(
        *spec_.groupToWeight(), universe_->getEntityName(groupId), 1);
    auto [constraintExpr, additionalPenaltyExpr] =
        getConstraint(groupId, groupWeight, initialAssignment);
    constraintExpr *= groupWeight;
    if (additionalPenaltyExpr) {
      additionalPenaltyExpr *= std::abs(groupWeight);
    }
    result.emplace_back(constraintExpr, additionalPenaltyExpr);
  }
  co_return result;
}

ConstraintInfo ColocateGroupsSpecBuilder::getConstraint(
    entities::GroupId groupId,
    double groupWeight,
    const Assignment& initialAssignment) const {
  ExprPtr howManyWeightedItems;
  // optimized linear size expression
  howManyWeightedItems += std::make_shared<GroupScopeItemTransformUtil>(
      *universe_,
      partitionId_,
      groupId,
      dimensionId_,
      scopeId_,
      allowedScopeItems_,
      relevantContainersPtr_,
      initialAssignment,
      scopeItemWeights_,
      kScopeItemDefaultWeight,
      GroupScopeItemTransformUtil::TransformFunctionType::STEP);

  howManyWeightedItems -= limits_.getLimit(groupId);
  if (*spec_.bound() == interface::ColocateGroupsSpecBound::MIN) {
    howManyWeightedItems *= -1;
  }

  return {
      *spec_.squares() ? power(std::move(howManyWeightedItems), 1.1)
                       : std::move(howManyWeightedItems),
      getContinuousPenaltyExpr(groupId, groupWeight, initialAssignment)};
}

std::string ColocateGroupsSpecBuilder::description() const {
  return fmt::format(
      "colocate groups in partition '{}' across scope items in scope '{}' w.r.t. dimension '{}' and when using bound = {}",
      *spec_.partitionName(),
      *spec_.scope(),
      universe_->getEntityName(dimensionId_),
      apache::thrift::util::enumNameSafe(*spec_.bound()));
}

SpecParameters ColocateGroupsSpecBuilder::getSpecInfo() const {
  return SpecParameters{
      .name = *spec_.name(),
      .scope = *spec_.scope(),
      .partition = *spec_.partitionName(),
      .dimension = universe_->getEntityName(dimensionId_),
      .boundType = apache::thrift::util::enumNameSafe(*spec_.bound()),
      .filterAllowListSize = spec_.filter()->itemsWhitelist()
          ? static_cast<int>(spec_.filter()->itemsWhitelist()->size())
          : 0,
      .filterBlockListSize = spec_.filter()->itemsBlacklist()
          ? static_cast<int>(spec_.filter()->itemsBlacklist()->size())
          : 0};
}

std::shared_ptr<Expression> ColocateGroupsSpecBuilder::getContinuousPenaltyExpr(
    entities::GroupId groupId,
    double groupWeight,
    const Assignment& initialAssignment) const {
  if (!needContinuousExpressions_) {
    // continuous expressions are not needed for example when solving using MIPs
    return nullptr;
  }

  const bool usingMinBound =
      (*spec_.bound() == interface::ColocateGroupsSpecBound::MIN &&
       groupWeight >= 0) ||
      (*spec_.bound() == interface::ColocateGroupsSpecBound::MAX &&
       groupWeight < 0);
  if (usingMinBound) {
    // For a given group, the default formulation is:
    // SUM_{s in S}  STEP(UTIL(g, s)) >= L
    // Penalty will be added when STEP(UTIL(g, s)) < L.
    // The goal of the penalty is to make sure we make every local progress is
    // positively accounted.
    // Observe, that in this case, we only make local progress if an object of
    // this group moves to the scopeItem S which initially has no objects from
    // this group. This will be reflected by STEP(UTIL(g, s)). so adding an
    // additional penalty is not needed.
    return nullptr;
  }

  const auto& dimension =
      universe_->getObjects().getDimension(dimensionId_).only();
  // max util possible is when all objects of the group contribute to the scope
  // item utilization
  double maxUtilPossible = 0;
  for (auto objectId : partition_.getObjectIds(groupId)) {
    if (!dimension.isDynamic()) {
      maxUtilPossible += dimension.getValue(objectId);
    } else {
      double maxUtilForObject = 0;
      for (auto scopeItemId : allowedScopeItems_) {
        maxUtilForObject = std::max(
            maxUtilForObject,
            dimension.getValueSafe(
                objectId,
                entities::ScopeScopeItemPair{
                    .scopeId = scopeId_, .scopeItemId = scopeItemId}));
      }
      maxUtilPossible += maxUtilForObject;
    }
  }

  if (maxUtilPossible == 0) {
    // if max possible util is zero, this expression can never improve
    return nullptr;
  }

  // Now, we are in the MAX case : SUM_{s in S}  STEP(UTIL(g, s)) <= L
  // Penalty will be added SUM_{s in S}  STEP(UTIL(g, s)) > L and local progress
  // happens when we "work toward" removing objects from scopeItems with
  // positive utilization (call then "used" scopeItems). Indeed, STEP(UTIL(g,
  // s)) will not account for such progress as long as `s` contains more than
  // one object. So, we need to add an additional penalty which improves more
  // for underuitilized scopeItems (UTIL(g, s) is small) and less for
  // overutilized scopeItems. We choose this penalty to be (UTIL(g, s) - C *
  // UTIL(g, s)^2), where UTIL(g, s) is the normalized utilization value <= 1
  // and C is a constant < 0.5. The quadratic component ensures that objects
  // from under-utilized scopeItems are moved first. The linear component
  // ensures that increasing the utilization across all the scopeItems is
  // penalized, and reducing the utilization across all the scopeItems is
  // prioritized. Concretely, when the number of used scopeItems is over the
  // limit, we have three cases involving used group objects:

  // Case 1: SINGLE move-out of an object of g
  //   * linear term will decrease and encourage move-out, quadratic term will
  //   decrease the least for an underutilized group
  // Case 2: SINGLE move-in of an object of g
  //   * linear term will increase and discourage move-in and dominate quadratic
  //   term which will decrease and encourage move-in
  // Case 3: SWAP of two objects of group g
  //   * Linear term will remain unchanged but quadratic term will encourage
  //   that we move out of the most underutilized scopeItem to another scopeItem

  // normalize each util so that it is within [0, 1]
  auto normalizedLinearTerm = std::make_shared<GroupScopeItemTransformUtil>(
      *universe_,
      partitionId_,
      groupId,
      dimensionId_,
      scopeId_,
      allowedScopeItems_,
      relevantContainersPtr_,
      initialAssignment,
      scopeItemWeights_,
      kScopeItemDefaultWeight,
      GroupScopeItemTransformUtil::TransformFunctionType::IDENTITY,
      maxUtilPossible);

  auto normalizedQuadraticTerm = std::make_shared<GroupScopeItemTransformUtil>(
      *universe_,
      partitionId_,
      groupId,
      dimensionId_,
      scopeId_,
      allowedScopeItems_,
      relevantContainersPtr_,
      initialAssignment,
      scopeItemWeights_,
      kScopeItemDefaultWeight,
      GroupScopeItemTransformUtil::TransformFunctionType::SQUARE,
      maxUtilPossible);
  return normalizedLinearTerm -
      kQuadraticTermMultiplier * normalizedQuadraticTerm;
}

} // namespace facebook::rebalancer::materializer
