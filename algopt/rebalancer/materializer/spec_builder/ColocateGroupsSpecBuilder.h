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
#include "algopt/rebalancer/materializer/utils/ExpressionBuilder.h"
#include <algopt/rebalancer/materializer/utils/LimitWrapper.h>

namespace facebook::rebalancer::materializer {

class ColocateGroupsSpecBuilder : public SpecBuilder {
 public:
  ColocateGroupsSpecBuilder(
      std::shared_ptr<const entities::Universe> universe,
      interface::ColocateGroupsSpec spec,
      bool needContinuousExpressions = false);

  folly::coro::Task<ExprPtr> goalCoro(
      ExpressionBuilder& expressionBuilder) const override;

  folly::coro::Task<GoalInfo> goal(
      ExpressionBuilder& expressionBuilder) const override;

  folly::coro::Task<std::vector<ConstraintInfo>> constraints(
      ExpressionBuilder& expressionBuilder) const override;

  std::string description() const override;

  SpecParameters getSpecInfo() const override;

 private:
  ValueRequirement getPenaltyValueRequirement() const override;

  std::shared_ptr<Expression> getContinuousPenaltyExpr(
      entities::GroupId groupId,
      double groupWeight,
      const Assignment& initialAssignment) const;

  ConstraintInfo getConstraint(
      entities::GroupId groupId,
      double groupWeight,
      const Assignment& initialAssignment) const;

  const interface::ColocateGroupsSpec spec_;
  const entities::DimensionId dimensionId_;
  const entities::PartitionId partitionId_;
  const entities::Partition& partition_;
  const entities::ScopeId scopeId_;
  const LimitWrapper limits_;
  const std::vector<entities::ScopeItemId> allowedScopeItems_;
  const bool needContinuousExpressions_;
  folly::F14FastMap<entities::ScopeItemId, double> scopeItemWeights_;
  std::shared_ptr<entities::Set<entities::ContainerId>> relevantContainersPtr_ =
      nullptr;
};

} // namespace facebook::rebalancer::materializer
