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
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"

namespace facebook::rebalancer::materializer {

class GroupDiversitySpecBuilder : public SpecBuilder {
 public:
  GroupDiversitySpecBuilder(
      std::shared_ptr<const entities::Universe> universe,
      interface::GroupDiversitySpec spec,
      bool needContinuousExpressions);

  folly::coro::Task<ExprPtr> goalCoro(
      ExpressionBuilder& expressionBuilder) const override;

  folly::coro::Task<std::vector<ConstraintInfo>> constraints(
      ExpressionBuilder& expressionBuilder) const override;

  std::string description() const override;

  SpecParameters getSpecInfo() const override;

 private:
  // Builds an `ObjectPartitionLookup` for this spec at `scopeItemId`,
  // varying only the penalty `transform` and per-group `coefficient`.
  ExprPtr buildLookup(
      ExpressionBuilder& expressionBuilder,
      entities::ScopeItemId scopeItemId,
      ObjectPartitionLookupPenaltyTransform transform,
      double coefficient = 1.0) const;

  folly::coro::Task<ExprPtr> getContinuousPenaltyExpr(
      ExpressionBuilder& expressionBuilder,
      entities::ScopeItemId scopeItemId,
      interface::GroupDiversityBound bound) const;

  interface::GroupDiversitySpec spec_;
  bool needContinuousExpressions_;
  entities::ScopeId scopeId_;
  entities::PartitionId partitionId_;
  entities::DimensionId dimensionId_;
  const entities::ObjectScalarDimension& dimension_;
};

} // namespace facebook::rebalancer::materializer
