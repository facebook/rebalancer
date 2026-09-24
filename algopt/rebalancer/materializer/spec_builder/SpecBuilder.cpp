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

#include "algopt/rebalancer/materializer/spec_builder/SpecBuilder.h"

#include "algopt/rebalancer/solver/expressions/Operators.h"

#include <fmt/core.h>

namespace facebook::rebalancer::materializer {

SpecBuilder::SpecBuilder(std::shared_ptr<const entities::Universe> universe)
    : universe_(std::move(universe)) {}

SpecBuilder::~SpecBuilder() = default;

entities::Map<entities::ObjectId, entities::ContainerId>
SpecBuilder::getUpdatesInInitialAssignment() const {
  /* Return the updates in initial assignment */

  // all spec dont have to override this, hence default there
  // are no updates to initial assignment.
  return {};
}

entities::Set<entities::ObjectId> SpecBuilder::fixedObjects() const {
  return {};
}

entities::Set<entities::ContainerId> SpecBuilder::nonAcceptingContainers()
    const {
  return {};
}

void SpecBuilder::populateInvalidMoveFilter(
    InvalidMoveFilter& /*filter*/) const {}

folly::coro::Task<GoalInfo> SpecBuilder::goal(
    ExpressionBuilder& expressionBuilder) const {
  co_return GoalInfo{
      .objectiveExpr = co_await goalCoro(expressionBuilder),
      .penaltyExpr = nullptr};
}

/*static*/
ExprPtr SpecBuilder::getAggregatedConstraintViolation(
    const std::vector<ConstraintInfo>& constraints,
    const entities::Universe& universe) {
  auto aggregatedViolation = const_expr(0, universe);
  for (auto& constraint : constraints) {
    inplace_add(aggregatedViolation, getConstraintViolation(constraint));
  }

  return aggregatedViolation;
}

/*static*/
ExprPtr SpecBuilder::getConstraintViolation(const ConstraintInfo& constraint) {
  auto& [constraintExpr, additionalPenaltyExpr] = constraint;
  if (constraintExpr == nullptr) {
    throw std::runtime_error("Constraint expression is not set");
  }
  const auto& universe = constraintExpr->getUniverse();
  const auto kZero = const_expr(0, universe);
  if (!additionalPenaltyExpr) {
    return max(kZero, constraintExpr);
  }
  // If additionalPenalty is specified, then total penalty is
  // = max(0, constraintExpr) + step(constraintExpr) * additionalPenalty
  // Also, while we don't expect additionalPenalty to be used when using optimal
  // solver, even if we do, it is fine because the product below can be
  // converted to its lp form since one of the children is binary
  return max(kZero, constraintExpr) +
      product(step(constraintExpr), additionalPenaltyExpr);
}

GoalInfo SpecBuilder::getSeparatedConstraintViolation(
    const std::vector<ConstraintInfo>& constraints,
    ExpressionBuilder& expressionBuilder,
    const entities::Universe& universe,
    ValueRequirement penaltyValueRequirement) {
  ExprPtr objectiveExpr;
  ExprPtr penaltyExpr;
  for (const auto& constraint : constraints) {
    if (constraint.constraintExpr == nullptr) {
      throw std::runtime_error("Constraint expression is not set");
    }
    const auto violation = max(0, constraint.constraintExpr);
    inplace_add(objectiveExpr, violation);

    if (constraint.additionalPenaltyExpr != nullptr) {
      if (penaltyValueRequirement != ValueRequirement::NONE) {
        const auto penaltyLowerBound =
            expressionBuilder.getLowerBound(*constraint.additionalPenaltyExpr);
        if ((penaltyValueRequirement == ValueRequirement::NON_NEGATIVE &&
             penaltyLowerBound < 0) ||
            (penaltyValueRequirement == ValueRequirement::POSITIVE &&
             penaltyLowerBound <= 0)) {
          throw std::runtime_error(
              fmt::format(
                  "Additional penalty expression has invalid lower bound {}",
                  penaltyLowerBound));
        }
      }
      const auto violationLowerBound = std::max(
          0.0, expressionBuilder.getLowerBound(*constraint.constraintExpr));
      // Stop applying the penalty once the violation reaches its best possible
      // value, including when that value is above zero.
      inplace_add(
          penaltyExpr,
          product(
              universe.getPrecision().isZero(violationLowerBound)
                  ? step(violation)
                  : step(violation - violationLowerBound),
              constraint.additionalPenaltyExpr));
    }
  }
  return GoalInfo{
      .objectiveExpr =
          objectiveExpr ? std::move(objectiveExpr) : const_expr(0, universe),
      .penaltyExpr = std::move(penaltyExpr)};
}

} // namespace facebook::rebalancer::materializer
