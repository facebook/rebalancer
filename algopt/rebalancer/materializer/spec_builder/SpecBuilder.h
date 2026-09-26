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

#include "algopt/rebalancer/common/CoroUtils.h"
#include "algopt/rebalancer/common/log/RebalancerLog.h"
#include "algopt/rebalancer/common/ValueRequirement.h"
#include "algopt/rebalancer/entities/Set.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/materializer/utils/ExpressionBuilder.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/utils/GoalInfo.h"

#include <thrift/lib/cpp/util/EnumUtils.h>

namespace facebook::rebalancer::materializer {
struct ConstraintInfo {
  explicit ConstraintInfo(ExprPtr constraintExpr)
      : constraintExpr(std::move(constraintExpr)),
        additionalPenaltyExpr(nullptr) {}

  ConstraintInfo(ExprPtr constraintExpr, ExprPtr additionalPenaltyExpr)
      : constraintExpr(std::move(constraintExpr)),
        additionalPenaltyExpr(std::move(additionalPenaltyExpr)) {}

  ExprPtr constraintExpr;
  // additionalPenaltyExpr is to used to specify some additional penalty that
  // needs to be added to max(0, constraintExpr) when the constraint is broken.
  // This is useful when using local search because the additionalPenaltyExpr
  // can be some continuous expression that will help fix the broken constraint.
  // If specified, then the overall penalty that is added is
  // max(0, constraintExpr) + step(constraintExpr) * additionalPenaltyExpr
  // = step(constraintExpr) * (constraintExpr + additionalPenaltyExpr)
  ExprPtr additionalPenaltyExpr = nullptr;
};

class SpecBuilder {
 public:
  explicit SpecBuilder(std::shared_ptr<const entities::Universe> universe);

  SpecBuilder(const SpecBuilder&) = default;
  SpecBuilder(SpecBuilder&&) = default;
  SpecBuilder& operator=(const SpecBuilder&) = default;
  SpecBuilder& operator=(SpecBuilder&&) = default;
  virtual ~SpecBuilder();

  virtual folly::coro::Task<ExprPtr> goalCoro(
      ExpressionBuilder& expressionBuilder) const = 0;

  // Materialization calls either goalCoro() or goal(), never both.
  // Builders with additionalPenaltyExpr must override this to exclude it from
  // objectiveExpr.
  virtual folly::coro::Task<GoalInfo> goal(
      ExpressionBuilder& expressionBuilder) const;

  virtual folly::coro::Task<std::vector<ConstraintInfo>> constraints(
      ExpressionBuilder& expressionBuilder) const = 0;

  virtual std::string description() const = 0;

  virtual SpecParameters getSpecInfo() const = 0;

  virtual entities::Map<entities::ObjectId, entities::ContainerId>
  getUpdatesInInitialAssignment() const;

  // Set of objects which break the constraint if moved from their initial
  // containers.
  virtual entities::Set<entities::ObjectId> fixedObjects() const;

  // Compute a set of containers which break a constraint if any objects are
  // moved in with respect to the initial assignment.
  // since non-accepting containers are superset of fixed containers, we will
  // need to override this for all specs that override fixedContainers
  virtual entities::Set<entities::ContainerId> nonAcceptingContainers() const;

  // Populate invalid (object, container) pairs into the filter.
  // Default: no-op.
  virtual void populateInvalidMoveFilter(InvalidMoveFilter& filter) const;

  GoalInfo getSeparatedConstraintViolation(
      const std::vector<ConstraintInfo>& constraints,
      ExpressionBuilder& expressionBuilder) const;

  static ExprPtr getAggregatedConstraintViolation(
      const std::vector<ConstraintInfo>& constraints,
      const entities::Universe& universe);

  static ExprPtr getConstraintViolation(const ConstraintInfo& constraint);

 protected:
  virtual ValueRequirement getPenaltyValueRequirement() const;

  std::shared_ptr<const entities::Universe> universe_;
};

} // namespace facebook::rebalancer::materializer
