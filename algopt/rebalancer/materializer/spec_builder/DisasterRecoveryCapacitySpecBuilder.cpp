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

#include "algopt/rebalancer/materializer/spec_builder/DisasterRecoveryCapacitySpecBuilder.h"

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/ObjectStaticDimension.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"

#include <folly/container/irange.h>

namespace facebook::rebalancer::materializer {

DisasterRecoveryCapacitySpecBuilder::DisasterRecoveryCapacitySpecBuilder(
    std::shared_ptr<const entities::Universe> universe,
    facebook::rebalancer::interface::DisasterRecoveryCapacitySpec spec)
    : SpecBuilder(std::move(universe)),
      spec_(std::move(spec)),
      scopeId_(universe_->getScopeId(*spec_.scope())),
      dimensionId_(universe_->getDimensionId(*spec_.dimension())),
      objectDimension_(universe_->getObjects().getDimension(dimensionId_)) {
  for (const auto dimIndex : folly::irange(objectDimension_.size())) {
    auto& dimension = objectDimension_.at(dimIndex);
    if (dimension.isDynamic() && dimension.getScopeId() != scopeId_) {
      throw std::runtime_error(
          fmt::format(
              "Expected dynamic dimension '{}' to be defined on the same scope as the spec's scope ('{}'), but it is defined on scope '{}'",
              universe_->getEntityName(dimensionId_),
              universe_->getEntityName(scopeId_),
              universe_->getEntityName(dimension.getScopeId())));
    }
  }

  setSharedDisasterGroups();
}

/*
setSharedDisasterGroups() sets 'sharedDisasterGroups_', where each disasterGroup
is a set scopeItems specified by their corresponding scopeItemIDs.

If a scopeItem is not present in any set, then it adds it as a singleton
set {scopeItemId}.
*/
void DisasterRecoveryCapacitySpecBuilder::setSharedDisasterGroups() {
  auto scopeId = universe_->getScopeId(*spec_.scope());
  auto& scopeItemIds = universe_->getScope(scopeId).getScopeItemIds();
  auto& disasterGroups = *spec_.sharedDisasterGroups();

  entities::Set<entities::ScopeItemId> isScopeItemInSomeDisasterGroup;

  // converts each set in disasterGroups to a set with the corresponding
  // ScopeItemIds.
  for (auto& disasterGroup : disasterGroups) {
    entities::Set<entities::ScopeItemId> scopeItemIdSet;
    for (auto& scopeName : disasterGroup) {
      auto scopeItemId = universe_->getScopeItemId(scopeId, scopeName);
      scopeItemIdSet.insert(scopeItemId);
      isScopeItemInSomeDisasterGroup.insert(scopeItemId);
    }
    sharedDisasterGroups_.push_back(std::move(scopeItemIdSet));
  }

  // if there is some scopeItem that is not part of any set, then we add it
  // as a separate set in sharedDisasterGroups
  for (auto scopeItemId : scopeItemIds) {
    if (isScopeItemInSomeDisasterGroup.contains(scopeItemId)) {
      continue;
    }
    sharedDisasterGroups_.push_back({scopeItemId});
  }
}

/*
 getPrimaryDimensionValuesAtDimensionIndex(...) filters out all the objects
 that are "primary" from 'primaryToSetOfSecondaryObjects()' along with their
 dimension values at the given dimensionIndex.
 */
entities::ObjectIdToDoubleMap
DisasterRecoveryCapacitySpecBuilder::getPrimaryDimensionValuesAtDimensionIndex(
    int dimensionIndex,
    std::optional<entities::ScopeItemId> scopeItemId) const {
  auto& primaryToSetOfSecondaryObjects =
      *spec_.primaryToSetOfSecondaryObjects();

  const auto totalObjects = universe_->getNumObjects();
  entities::ObjectIdToDoubleMap primaryDimensionValuesAtDimensionIndex(
      totalObjects,
      /*defaultValue=*/0.0,
      /*expectedNonDefaultSize=*/primaryToSetOfSecondaryObjects.size());
  auto& dimension = objectDimension_.at(dimensionIndex);

  auto objectDimensionScopeScopeItemPair = scopeItemId.has_value()
      ? std::make_optional(
            entities::ScopeScopeItemPair{
                .scopeId = scopeId_, .scopeItemId = *scopeItemId})
      : std::nullopt;

  for (auto& [primaryObject, _] : primaryToSetOfSecondaryObjects) {
    auto primaryObjectId = universe_->getObjectId(primaryObject);
    const double dimValue = dimension.getValueSafe(
        primaryObjectId, objectDimensionScopeScopeItemPair);
    primaryDimensionValuesAtDimensionIndex.emplace(primaryObjectId, dimValue);
  }
  return primaryDimensionValuesAtDimensionIndex;
}

/*
 getObjectsInDisasterGroupExprs(..) creates a vector V of maps, where at each
 position i in V, it stores a map M: objectId -> ExprPtr. The expression
 associated with an object says says if that object is part of any of the
 scopeItems in the disasterGroup
 */
std::vector<entities::Map<entities::ObjectId, ExprPtr>>
DisasterRecoveryCapacitySpecBuilder::getObjectsInDisasterGroupExprs(
    ExpressionBuilder& expressionBuilder) const {
  auto scopeId = universe_->getScopeId(*spec_.scope());
  const auto objectIds = universe_->getObjects().getObjectIds();

  entities::Map<entities::ObjectId, std::shared_ptr<ObjectVector>>
      objectIdToObjectVector;
  std::vector<entities::Map<entities::ObjectId, ExprPtr>>
      objectsInDisasterGroupsExprs;
  for (auto& disasterGroup : sharedDisasterGroups_) {
    auto disasterGroupContainers =
        std::make_shared<entities::Set<entities::ContainerId>>();
    for (auto scopeItemIdInGroup : disasterGroup) {
      auto& scopeItemContainers =
          universe_->getScope(scopeId).getContainerIds(scopeItemIdInGroup);
      disasterGroupContainers->insert(
          scopeItemContainers.begin(), scopeItemContainers.end());
    }

    entities::Map<entities::ObjectId, ExprPtr> objectsInDisasterGroup;
    for (const auto objectId : objectIds) {
      // An object is in 'disasterGroup' if it is in at least one of the
      // scopeItems in 'disasterGroup'
      if (!objectIdToObjectVector.contains(objectId)) {
        auto objectMap = std::make_shared<entities::ObjectIdToDoubleMap>(
            objectIds.size(),
            /*defaultValue=*/0.0,
            /*expectedNonDefaultSize=*/1);
        objectMap->emplace(objectId, 1.0);
        objectIdToObjectVector.emplace(
            objectId, object_vector(std::move(objectMap), *universe_));
      }
      objectsInDisasterGroup.emplace(
          objectId,
          object_lookup(
              objectIdToObjectVector.at(objectId),
              disasterGroupContainers,
              expressionBuilder.getUpdatedInitialAssignment()));
    }
    objectsInDisasterGroupsExprs.push_back(std::move(objectsInDisasterGroup));
  }

  return objectsInDisasterGroupsExprs;
}

/*
Given a dimension index, getPrimaryUsageAtScopeItems(..) computes the
primaryUsage---i.e., sum of loads associated with primaryObjects---for every
scopeItem.
*/
folly::coro::Task<entities::Map<entities::ScopeItemId, ExprPtr>>
DisasterRecoveryCapacitySpecBuilder::getPrimaryUsageAtScopeItems(
    ExpressionBuilder& expressionBuilder,
    int dimensionIndex) const {
  entities::Map<entities::ScopeItemId, ExprPtr> primaryUsage;
  auto scopeId = universe_->getScopeId(*spec_.scope());
  auto& scopeItemIds = universe_->getScope(scopeId).getScopeItemIds();

  // if dimension is static, then just compute primaryDimensionValues
  // once and use it for all scope items
  if (!objectDimension_.at(dimensionIndex).isDynamic()) {
    auto objectIdToStaticValues =
        getPrimaryDimensionValuesAtDimensionIndex(dimensionIndex, std::nullopt);
    auto primaryDimension =
        entities::ObjectStaticDimension(std::move(objectIdToStaticValues));

    for (auto scopeItemId : scopeItemIds) {
      primaryUsage[scopeItemId] = co_await expressionBuilder.getAbsoluteUtil(
          UtilMetric::AFTER, primaryDimension, scopeId, scopeItemId);
    }
  } else {
    for (auto scopeItemId : scopeItemIds) {
      auto primaryDimension = entities::ObjectStaticDimension(
          getPrimaryDimensionValuesAtDimensionIndex(
              dimensionIndex, scopeItemId));
      primaryUsage[scopeItemId] = co_await expressionBuilder.getAbsoluteUtil(
          UtilMetric::AFTER, primaryDimension, scopeId, scopeItemId);
    }
  }

  co_return primaryUsage;
}

/*
Given a dimension index and disasterGroup, getExcessLoadAtScopeItems(..)
computes the excess load that each scopeItem has to handle w.r.t. the given
disasterGroup.
*/
folly::coro::Task<entities::Map<entities::ScopeItemId, ExprPtr>>
DisasterRecoveryCapacitySpecBuilder::getExcessLoadAtScopeItems(
    ExpressionBuilder& expressionBuilder,
    const entities::Map<entities::ObjectId, ExprPtr>&
        objectsInDisasterGroupExprs,
    int disasterGroupIndex,
    int dimensionIndex) const {
  auto& scopeItemIds = universe_->getScope(scopeId_).getScopeItemIds();
  auto& dimension = objectDimension_.at(dimensionIndex);
  auto& primaryToSetOfSecondaryObjects =
      *spec_.primaryToSetOfSecondaryObjects();

  entities::Map<entities::ScopeItemId, ExprPtr> scopeItemToExcessLoad;
  for (auto scopeItemId : scopeItemIds) {
    if (sharedDisasterGroups_.at(disasterGroupIndex).contains(scopeItemId)) {
      // if scopeItem is part of disasterGroup, then there is no
      // excess load on it
      scopeItemToExcessLoad[scopeItemId] = const_expr(0, *universe_);
      continue;
    }

    ExprPtr excessLoad = const_expr(0, *universe_);
    for (auto& [primaryObject, secondaryObjects] :
         primaryToSetOfSecondaryObjects) {
      auto primaryObjectId = universe_->getObjectId(primaryObject);
      auto excessLoadDueToPrimaryObject = const_expr(0, *universe_);

      /*
      A primaryObject, P, contributes to the excessLoad of a
      scopeItem, S, if and only if all the following conditions hold:

      i) P is part of the disasterGroup G under consideration.

      ii) S has some secondary object, R, associated with P, and

      iii) there is no other scope item S' that is outside of G and has a
      secondary object, R', of P that is of higher priority than R (that is in
      S). In other words, all the secondary objects R' of higher priority than
      R are allocated some scopeItem in G.

      (R' is of higher priority than R if R' appears before R in the
      secondaryObjects list associated with P.)
      */

      // 'preConditions' refer to conditions that need to hold for the load of
      // an object to contribute to the excessLoad for the scopeItem under
      // consideration; initially, the condition that needs to hold is that
      // the primaryObject under consideration is part of the disasterGroup;
      // 'preConditions' is updated after looking at ever secondaryObject
      auto preConditions = objectsInDisasterGroupExprs.at(primaryObjectId);

      for (auto& secondaryObject : secondaryObjects) {
        auto secondaryObjectId = universe_->getObjectId(secondaryObject);
        auto isSecondaryObjectInScopeItem = expressionBuilder.isAssigned(
            scopeId_, scopeItemId, secondaryObjectId);
        auto dimValueOfObject = dimension.getValueSafe(
            secondaryObjectId,
            entities::ScopeScopeItemPair{
                .scopeId = scopeId_, .scopeItemId = scopeItemId});

        // if 'preConditions' are met, and the current secondaryObject is in
        // the scopeItem under consideration, then we need to add its load value
        // ('dimValueOfObject') to the excessLoad for this scopeItem; this is
        // computed using the expression below
        excessLoadDueToPrimaryObject +=
            expressionBuilder.binaryMin(
                preConditions, isSecondaryObjectInScopeItem) *
            dimValueOfObject;

        // since we have looked at the current 'secondaryObject' S_i, we will
        // only consider the subsequent secondary objects S_{j} where j > i
        // (i.e., one that appear after S_i in 'secondaryObjects') if S_i is
        // in disasterGroup; hence we update 'preConditions' with the
        // expression 'isSecondaryObjectInDisasterGroup'
        auto isSecondaryObjectInDisasterGroup =
            objectsInDisasterGroupExprs.at(secondaryObjectId);
        preConditions = expressionBuilder.binaryMin(
            preConditions, isSecondaryObjectInDisasterGroup);
      }
      excessLoad += excessLoadDueToPrimaryObject;
    }
    scopeItemToExcessLoad[scopeItemId] = excessLoad;
  }
  co_return scopeItemToExcessLoad;
}

/*
Given a dimension index, getMaxDisasterUsageAtScopeItems(..) computes the
maxDisasterUsage---i.e., the max excess load because of a disaster
scenario---for all the scopeItems.
*/
folly::coro::Task<entities::Map<entities::ScopeItemId, ExprPtr>>
DisasterRecoveryCapacitySpecBuilder::getMaxDisasterUsageAtScopeItems(
    ExpressionBuilder& expressionBuilder,
    int dimensionIndex,
    const std::vector<entities::Map<entities::ObjectId, ExprPtr>>&
        objectsInDisasterGroupsExprs) const {
  /*
  For each disaster scenario, calculate the excess load on each of the
  scopeItems. maxDisasterUsage for a scopeItem is the max excess load on
  that scopeItem across all the disaster scenarios
  */

  // parallelize across disasterScenarios and update maxDisasterUsage
  // synchronously
  entities::Map<entities::ScopeItemId, ExprPtr> maxDisasterUsage;
  co_await CoroUtils::runEachTaskAndUpdate<int>(
      0,
      sharedDisasterGroups_.size(),
      [&](int disasterGroupIndex) {
        return getExcessLoadAtScopeItems(
            expressionBuilder,
            objectsInDisasterGroupsExprs.at(disasterGroupIndex),
            disasterGroupIndex,
            dimensionIndex);
      },
      [&maxDisasterUsage](auto&& coroResult, auto /*unused*/) {
        for (const auto& [scopeItem, excessLoad] : coroResult) {
          inplace_max(maxDisasterUsage[scopeItem], excessLoad);
        }
      });

  co_return maxDisasterUsage;
}

folly::coro::Task<entities::Map<entities::ScopeItemId, ExprPtr>>
DisasterRecoveryCapacitySpecBuilder::getTotalDisasterUsageForDimensionIndex(
    int dimensionIndex,
    ExpressionBuilder& expressionBuilder,
    const std::vector<entities::ScopeItemId>& scopeItemIds,
    const std::vector<entities::Map<entities::ObjectId, ExprPtr>>&
        objectsInDisasterGroupExprs) const {
  auto [primaryUsageAtDimensionIndex, maxDisasterUsageAtDimensionIndex] =
      co_await folly::coro::collectAll(
          getPrimaryUsageAtScopeItems(expressionBuilder, dimensionIndex),
          getMaxDisasterUsageAtScopeItems(
              expressionBuilder, dimensionIndex, objectsInDisasterGroupExprs));

  entities::Map<entities::ScopeItemId, ExprPtr> totalUsageAtDimensionIndex;
  for (auto scopeItemId : scopeItemIds) {
    totalUsageAtDimensionIndex[scopeItemId] =
        primaryUsageAtDimensionIndex.at(scopeItemId) +
        maxDisasterUsageAtDimensionIndex.at(scopeItemId);
  }

  co_return totalUsageAtDimensionIndex;
}

folly::coro::Task<std::vector<ConstraintInfo>>
DisasterRecoveryCapacitySpecBuilder::constraints(
    ExpressionBuilder& expressionBuilder) const {
  auto& scope = universe_->getScope(scopeId_);
  auto& scopeItemIds = scope.getScopeItemIds();

  // precompute these expressions since these are independent of the
  // dimensionIndex
  auto objectsInDisasterGroupExprs =
      getObjectsInDisasterGroupExprs(expressionBuilder);

  // parallelize across each dimensionIndex in objectDimension and update
  // totalUsage synchronously
  entities::Map<entities::ScopeItemId, ExprPtr> totalUsage;
  co_await CoroUtils::runEachTaskAndUpdate<int>(
      0,
      objectDimension_.size(),
      [&](int dimensionIndex) {
        return getTotalDisasterUsageForDimensionIndex(
            dimensionIndex,
            expressionBuilder,
            scopeItemIds,
            objectsInDisasterGroupExprs);
      },
      [&totalUsage](auto&& coroResult, auto /*unused*/) {
        for (const auto& [scopeItem, usageForDimensionIndex] : coroResult) {
          inplace_max(totalUsage[scopeItem], usageForDimensionIndex);
        }
      });

  // If totalScopeItemCapacity is non-zero, then this is used to normalize
  // each constraint expression. If it is zero, then this means it is used a
  // goal, and so the user is expected to provide an appropriate weight
  auto& scopeDimension = scope.getDimension(dimensionId_);
  double totalScopeItemCapacity = 0.0;
  for (auto scopeItemId : scopeItemIds) {
    totalScopeItemCapacity += scopeDimension.getValue(scopeItemId);
  }
  const double averageScopeItemCapacity = totalScopeItemCapacity == 0
      ? 1.0
      : totalScopeItemCapacity / scopeItemIds.size();
  const double normCoeff = 1 / averageScopeItemCapacity;

  std::vector<ConstraintInfo> constraints;
  constraints.reserve(scopeItemIds.size());
  for (auto scopeItemId : scopeItemIds) {
    auto scopeItemCapacity = scopeDimension.getValue(scopeItemId);
    constraints.emplace_back(
        normCoeff * (totalUsage.at(scopeItemId) - scopeItemCapacity));
  }
  co_return constraints;
}

folly::coro::Task<ExprPtr> DisasterRecoveryCapacitySpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  co_return getAggregatedConstraintViolation(
      co_await constraints(expressionBuilder), *universe_);
}

/*
Spec Description
-----------------
Given a scope, a relevant scope dimension `d`, a set of pairs of primary
and secondary objects, and a list of disaster groups (which specifies which
scopeItems fail together), DisasterRecoveryCapacitySpec aims to

 a) minimize the sum of additional value of `d` required at each scopeItem,
when used as a goal

 b) ensure that, for every scopeItem, the existing value of `d` is at least
the peak requirement at that scopeItem, when used a constraint.
*/
std::string DisasterRecoveryCapacitySpecBuilder::description() const {
  return fmt::format(
      "Disaster recovery usage for scope = {} and dimension = {}",
      *spec_.scope(),
      *spec_.dimension());
}

SpecParameters DisasterRecoveryCapacitySpecBuilder::getSpecInfo() const {
  return SpecParameters{
      .name = *spec_.name(),
      .scope = *spec_.scope(),
      .dimension = *spec_.dimension()};
}

} // namespace facebook::rebalancer::materializer
