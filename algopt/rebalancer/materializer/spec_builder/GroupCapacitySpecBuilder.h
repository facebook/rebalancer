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
#include <algopt/rebalancer/entities/Identifiers.h>
#include <algopt/rebalancer/entities/Map.h>
#include <algopt/rebalancer/entities/Partition.h>
#include <algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h>
#include <algopt/rebalancer/materializer/utils/LimitWrapper.h>

namespace facebook::rebalancer::materializer {

class GroupCapacitySpecBuilder : public SpecBuilder {
 public:
  GroupCapacitySpecBuilder(
      std::shared_ptr<const entities::Universe> universe,
      interface::GroupCapacitySpec spec);

  folly::coro::Task<ExprPtr> goalCoro(
      ExpressionBuilder& expressionBuilder) const override;

  folly::coro::Task<std::vector<ConstraintInfo>> constraints(
      ExpressionBuilder& expressionBuilder) const override;

  std::string description() const override;

  SpecParameters getSpecInfo() const override;

 private:
  entities::Map<entities::GroupId, std::vector<entities::GroupId>>
  getMainGroupIdToContributionGroupIds() const;

  folly::coro::Task<std::vector<ConstraintInfo>> getConstraint(
      ExpressionBuilder& expressionBuilder,
      interface::GroupCapacitySpecDefinition definition,
      const std::vector<entities::ScopeItemId>& scopeItemIds,
      std::shared_ptr<const entities::Set<entities::ContainerId>>
          relevantContainersPtr) const;

  ExprPtr getAfterUtilForMainGroup(
      const std::vector<entities::GroupId>& contributionGroupIds,
      const std::vector<entities::ScopeItemId>& scopeItemIds,
      std::shared_ptr<const entities::Set<entities::ContainerId>>
          relevantContainersPtr,
      const Assignment& updatedInitialAssignment) const;

  folly::coro::Task<ExprPtr> getDuringUtilForMainGroup(
      ExpressionBuilder& expressionBuilder,
      const std::vector<entities::GroupId>& contributionGroupIds,
      const std::vector<entities::ScopeItemId>& scopeItemIds) const;

 private:
  interface::GroupCapacitySpec spec_;
  entities::PartitionId mainPartitionId_;
  entities::PartitionId contributionPartitionId_;
  entities::ScopeId scopeId_;
  entities::DimensionId objectCountDimensionId_;
  LimitWrapper groupToCapacityLimit_;
  LimitWrapper contributionMultipliers_;
  entities::Map<entities::GroupId, std::vector<entities::GroupId>>
      mainGroupToContributionGroups_;
  std::optional<LimitWrapper> perScopeItemBundleSize_ = std::nullopt;
};

} // namespace facebook::rebalancer::materializer
