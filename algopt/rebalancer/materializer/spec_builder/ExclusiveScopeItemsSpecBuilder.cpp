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

#include "algopt/rebalancer/materializer/spec_builder/ExclusiveScopeItemsSpecBuilder.h"

#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/expressions/GroupScopeItemTransformUtil.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"

using namespace facebook::rebalancer::entities;

namespace facebook::rebalancer::materializer {

ExclusiveScopeItemsSpecBuilder::ExclusiveScopeItemsSpecBuilder(
    std::shared_ptr<const Universe> universe,
    interface::ExclusiveScopeItemsSpec spec)
    : SpecBuilder(universe),
      spec_(std::move(spec)),
      dimensionId_(universe_->getDimensionId(*spec_.dimension())),
      scopeId_(universe_->getScopeId(*spec_.scope())) {
  auto& objectDimension = universe->getObjects().getDimension(dimensionId_);
  if (objectDimension.hasNegativeValues()) {
    throw std::runtime_error(
        "ExclusiveScopeItemsSpec is not supported when the object dimension has negative values");
  }
}

folly::coro::Task<ExprPtr> ExclusiveScopeItemsSpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  switch (*spec_.formula()) {
    case interface::ExclusiveScopeItemsFormula::
        MINIMIZE_INVALIDATED_SCOPE_ITEMS_COUNT:
      co_return co_await getMinimizeInvalidatedScopeItemsCountGoal(
          expressionBuilder);

    case interface::ExclusiveScopeItemsFormula::AGGRESSIVE_PACKING:
      co_return co_await getAggressivePackingGoal(expressionBuilder);
  }
}

folly::coro::Task<ExprPtr>
ExclusiveScopeItemsSpecBuilder::getMinimizeInvalidatedScopeItemsCountGoal(
    ExpressionBuilder& expressionBuilder) const {
  folly::F14FastMap<std::string, folly::F14FastSet<std::string>>
      scopeItemToConflictingScopeItems;
  for (const auto& conflictInfo : *spec_.conflictInfoList()) {
    auto& mainScopeItem = *conflictInfo.scopeItem();
    for (const auto& conflictingScopeItemInfo :
         *conflictInfo.conflictingScopeItemsWithOverlap()) {
      auto& conflictingScopeItem =
          *conflictingScopeItemInfo.conflictingScopeItem();
      scopeItemToConflictingScopeItems[mainScopeItem].insert(
          conflictingScopeItem);
      scopeItemToConflictingScopeItems[conflictingScopeItem].insert(
          mainScopeItem);
    }
  }

  // objective is the count of scope items that are invalidated because one of
  // their conflicting scope items is utilized
  auto objective = const_expr(0, *universe_);
  for (const auto& [scopeItem, conflictingScopeItems] :
       scopeItemToConflictingScopeItems) {
    auto conflictSum = const_expr(0, *universe_);
    for (const auto& conflictingScopeItem : conflictingScopeItems) {
      auto conflictingScopeItemId =
          universe_->getScopeItemId(scopeId_, conflictingScopeItem);
      auto conflictingScopeItemUtil =
          co_await expressionBuilder.getAbsoluteUtil(
              UtilMetric::AFTER,
              dimensionId_,
              scopeId_,
              conflictingScopeItemId);
      inplace_add(conflictSum, conflictingScopeItemUtil);
    }
    inplace_add(objective, step(conflictSum));
  }
  co_return objective;
}

folly::coro::Task<ExprPtr>
ExclusiveScopeItemsSpecBuilder::getAggressivePackingGoal(
    ExpressionBuilder& expressionBuilder) const {
  folly::F14FastMap<std::string, std::map<std::string, double>>
      scopeItemToConflictingScopeItems;
  for (const auto& conflictInfo : *spec_.conflictInfoList()) {
    auto& scopeItem = *conflictInfo.scopeItem();
    for (const auto& conflictingScopeItemInfo :
         *conflictInfo.conflictingScopeItemsWithOverlap()) {
      auto& conflictingScopeItem =
          *conflictingScopeItemInfo.conflictingScopeItem();
      const auto overlap = *conflictingScopeItemInfo.overlap();
      scopeItemToConflictingScopeItems[scopeItem][conflictingScopeItem] =
          overlap;
      scopeItemToConflictingScopeItems[conflictingScopeItem][scopeItem] =
          overlap;
    }
  }

  auto scopeItemWeights = *spec_.scopeItemWeights();
  auto objective = const_expr(0, *universe_);
  for (const auto& [scopeItem, conflictingScopeItems] :
       scopeItemToConflictingScopeItems) {
    auto conflictSum = const_expr(0, *universe_);
    const auto scopeItemId = universe_->getScopeItemId(scopeId_, scopeItem);
    auto mainScopeItemUtil = step(
        co_await expressionBuilder.getAbsoluteUtil(
            UtilMetric::AFTER, dimensionId_, scopeId_, scopeItemId));
    const auto scopeItemWeight =
        folly::get_default(scopeItemWeights, scopeItem, 1);
    inplace_add(conflictSum, mainScopeItemUtil, scopeItemWeight);
    for (const auto& [conflictingScopeItem, overlap] : conflictingScopeItems) {
      const auto conflictingScopeItemId =
          universe_->getScopeItemId(scopeId_, conflictingScopeItem);
      auto conflictingScopeItemUtil = step(
          co_await expressionBuilder.getAbsoluteUtil(
              UtilMetric::AFTER,
              dimensionId_,
              scopeId_,
              conflictingScopeItemId));
      inplace_add(conflictSum, conflictingScopeItemUtil, overlap);
    }
    auto conflictSumSquared = power(conflictSum, 2);
    // We make the coefficient negative so that minimizing the expression
    // results in maximizing the sum of the conflictSumSquared * scopeItemWeight
    // values.
    inplace_add(objective, conflictSumSquared, -scopeItemWeight);
  }
  co_return objective;
}

folly::coro::Task<std::vector<ConstraintInfo>>
ExclusiveScopeItemsSpecBuilder::buildConstraintPerGroup(
    ExpressionBuilder& expressionBuilder,
    entities::PartitionId partitionId) const {
  std::vector<ConstraintInfo> result;
  auto& scope = universe_->getScope(scopeId_);
  const auto& partition = universe_->getPartition(partitionId);
  for (auto groupId : partition.getGroupIds()) {
    auto exprForThisGroup =
        any_positive({const_expr(0, *universe_)}, *universe_);
    bool atLeastOneConstraint = false;
    for (auto& conflictInfo : *spec_.conflictInfoList()) {
      if (conflictInfo.conflictingScopeItemsWithOverlap()->empty()) {
        continue;
      }

      atLeastOneConstraint = true;
      auto scopeItemId =
          universe_->getScopeItemId(scopeId_, *conflictInfo.scopeItem());
      auto mainScopeItemUtil = co_await expressionBuilder.getAbsoluteUtil(
          UtilMetric::AFTER,
          dimensionId_,
          scopeId_,
          scopeItemId,
          partitionId,
          groupId);

      auto conflictingScopeItemIds = std::vector<entities::ScopeItemId>();
      auto relevantContainersPtr =
          std::make_shared<entities::Set<entities::ContainerId>>();
      for (auto& conflictingScopeItemInfo :
           *conflictInfo.conflictingScopeItemsWithOverlap()) {
        auto conflictingScopeItemId = universe_->getScopeItemId(
            scopeId_, *conflictingScopeItemInfo.conflictingScopeItem());
        conflictingScopeItemIds.push_back(conflictingScopeItemId);
        auto& scopeContainersIds =
            scope.getContainerIds(conflictingScopeItemId);
        relevantContainersPtr->insert(
            scopeContainersIds.begin(), scopeContainersIds.end());
      }

      auto conflictScopeItemsUtilSum =
          std::make_shared<GroupScopeItemTransformUtil>(
              *universe_,
              partitionId,
              groupId,
              dimensionId_,
              scopeId_,
              conflictingScopeItemIds,
              relevantContainersPtr,
              expressionBuilder.getUpdatedInitialAssignment(),
              folly::F14FastMap<entities::ScopeItemId, double>{},
              1 /* scopeItemDefaultWeight */,
              GroupScopeItemTransformUtil::TransformFunctionType::IDENTITY);

      inplace_any_positive(
          exprForThisGroup,
          step(mainScopeItemUtil) + step(conflictScopeItemsUtilSum) - 1);
    }

    if (atLeastOneConstraint) {
      result.emplace_back(exprForThisGroup);
    }
  }
  co_return result;
}

folly::coro::Task<std::vector<ConstraintInfo>>
ExclusiveScopeItemsSpecBuilder::constraints(
    ExpressionBuilder& expressionBuilder) const {
  if (auto partitionName = spec_.partitionName()) {
    // if partitionName is set, enforce this constraint on each group of
    // objects of the partition
    auto partitionId = universe_->getPartitionId(*partitionName);
    co_return co_await buildConstraintPerGroup(expressionBuilder, partitionId);
  }

  // otherwise enforce this constraint on all objects at once
  auto result = any_positive({const_expr(0, *universe_)}, *universe_);
  bool atLeastOneConstraint = false;
  for (auto& conflictInfo : *spec_.conflictInfoList()) {
    if (conflictInfo.conflictingScopeItemsWithOverlap()->empty()) {
      continue;
    }
    atLeastOneConstraint = true;
    auto scopeItemId =
        universe_->getScopeItemId(scopeId_, *conflictInfo.scopeItem());
    auto mainScopeItemUtil = co_await expressionBuilder.getAbsoluteUtil(
        UtilMetric::AFTER, dimensionId_, scopeId_, scopeItemId);
    auto conflictingScopeItemsUtilSum = const_expr(0, *universe_);
    for (auto& conflictingScopeItemInfo :
         *conflictInfo.conflictingScopeItemsWithOverlap()) {
      auto conflictingScopeItemId = universe_->getScopeItemId(
          scopeId_, *conflictingScopeItemInfo.conflictingScopeItem());
      inplace_add(
          conflictingScopeItemsUtilSum,
          co_await expressionBuilder.getAbsoluteUtil(
              UtilMetric::AFTER,
              dimensionId_,
              scopeId_,
              conflictingScopeItemId));
    }
    inplace_any_positive(
        result,
        step(mainScopeItemUtil) + step(conflictingScopeItemsUtilSum) - 1);
  }
  co_return atLeastOneConstraint
      ? std::vector<ConstraintInfo>{ConstraintInfo(std::move(result))}
      : std::vector<ConstraintInfo>{};
}

std::string ExclusiveScopeItemsSpecBuilder::description() const {
  std::string additionalText;
  if (auto partitionName = spec_.partitionName()) {
    additionalText = fmt::format(
        " by objects that belong to same group of partition {}",
        *partitionName);
  }
  return fmt::format(
      "Spec {} : specified conflicts of scope items cannot be utilized concurrently{}",
      *spec_.name(),
      additionalText);
}

SpecParameters ExclusiveScopeItemsSpecBuilder::getSpecInfo() const {
  return SpecParameters{
      .name = *spec_.name(),
      .scope = *spec_.scope(),
      .dimension = *spec_.dimension(),
      .size = static_cast<int>(spec_.conflictInfoList()->size())};
}

} // namespace facebook::rebalancer::materializer
