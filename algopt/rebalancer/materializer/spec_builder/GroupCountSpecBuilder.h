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

#include "algopt/rebalancer/entities/Partition.h"
#include "algopt/rebalancer/materializer/spec_builder/SpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/ExpressionBuilder.h"
#include "algopt/rebalancer/materializer/utils/FilterWrapper.h"
#include "algopt/rebalancer/materializer/utils/LimitWrapper.h"

#include <map>
#include <set>

namespace facebook::rebalancer::entities {
class ObjectScalarDimension;
} // namespace facebook::rebalancer::entities

namespace facebook::rebalancer::materializer {

class GroupCountSpecBuilder : public SpecBuilder {
 public:
  GroupCountSpecBuilder(
      std::shared_ptr<const entities::Universe> universe,
      interface::GroupCountSpec spec);

  folly::coro::Task<ExprPtr> goalCoro(
      ExpressionBuilder& expressionBuilder) const override;

  folly::coro::Task<std::vector<ConstraintInfo>> constraints(
      ExpressionBuilder& expressionBuilder) const override;

  std::string description() const override;

  SpecParameters getSpecInfo() const override;

  void populateInvalidMoveFilter(
      InvalidMoveFilter& filter,
      const Assignment& updatedInitialAssignment) const override;

 private:
  std::vector<ConstraintInfo> buildOptimizedGroupCountExprs(
      ExpressionBuilder& expressionBuilder,
      const std::vector<entities::ScopeItemId>& scopeItemIds,
      interface::GroupCountSpecDefinition definition,
      interface::GroupCountSpecBound bound) const;

  std::vector<ConstraintInfo> buildOptimizedGroupCountExprsForStaticDimensions(
      ExpressionBuilder& expressionBuilder,
      const std::vector<entities::ScopeItemId>& scopeItemIds,
      interface::GroupCountSpecDefinition definition,
      interface::GroupCountSpecBound bound) const;

  std::vector<ConstraintInfo> buildOptimizedGroupCountExprsForDynamicDimensions(
      ExpressionBuilder& expressionBuilder,
      const std::vector<entities::ScopeItemId>& scopeItemIds,
      interface::GroupCountSpecDefinition definition,
      interface::GroupCountSpecBound bound) const;

  folly::coro::Task<std::vector<ConstraintInfo>>
  buildIndividualGroupRequirement(
      ExpressionBuilder& expressionBuilder,
      interface::GroupCountSpecDefinition definition,
      interface::GroupCountSpecBound bound) const;

  ExprPtr buildObjectPartition(
      ExpressionBuilder& expressionBuilder,
      std::optional<entities::ScopeItemId> scopeItemId) const;

  folly::coro::Task<ExprPtr> buildLimitExpr(
      ExpressionBuilder& expressionBuilder,
      interface::LimitType limitType,
      entities::ScopeItemId scopeItemId,
      entities::GroupId groupId,
      double limitConstant,
      interface::GroupCountSpecDefinition definition) const;

  folly::coro::Task<ExprPtr> buildSingleRequirement(
      ExpressionBuilder& expressionBuilder,
      interface::GroupCountSpecDefinition definition,
      interface::GroupCountSpecBound bound,
      entities::ScopeItemId scopeItemId,
      entities::GroupId groupId,
      double limitConstant,
      interface::LimitType limitType) const;

  void processFinalGroupLimits(
      entities::Map<entities::GroupId, double>& groupLimits,
      std::optional<entities::ScopeItemId> scopeItemId,
      interface::LimitType limitType) const;

 private:
  interface::GroupCountSpec spec_;
  entities::ScopeId scopeId_;
  entities::PartitionId partitionId_;
  const entities::Partition& partition_;
  LimitWrapper limits_;
  ScopeItemFilterWrapper scopeFilter_;
};

} // namespace facebook::rebalancer::materializer
