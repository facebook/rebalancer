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

#include "algopt/rebalancer/materializer/spec_builder/MaximizeFreeCapacityUnitsSpecBuilder.h"

#include "algopt/rebalancer/materializer/utils/FilterWrapper.h"
#include "algopt/rebalancer/materializer/utils/LimitWrapper.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"

#include <cmath>

using namespace facebook::rebalancer::entities;

namespace facebook::rebalancer::materializer {
namespace {

// Each scope item's penalty is at most 1, so weighting their average by 0.99
// keeps the total penalty below one discrete objective unit.
constexpr double kPenaltyWeight = 0.99;
constexpr double kPenaltyExponent = 0.9;

// Per scope item penalty is 0 if all slots free, else (fraction
//  needed for next free unit) ^ 0.9. Fraction in (0,1] => penalty in (0,1].
//  Hence a scope item has a lower penalty when it is closer to making
//  another unit available.
ExprPtr buildNextAvailableUnitPenalty(
    ExprPtr scaledRemainingCapacity,
    ExprPtr availableUnits,
    ExprPtr unavailableUnits) {
  // With remaining capacity measured in unit sizes, r - floor(r) is the
  // fraction of another unit already available; its complement is unavailable.
  auto unavailableFractionOfNextAvailableUnit =
      1.0 - (std::move(scaledRemainingCapacity) - std::move(availableUnits));
  auto hasUnavailableUnits = step(std::move(unavailableUnits));
  // With an exponent below 1, the same decrease in unavailable fraction
  // reduces the penalty more for a scope item closer to making another unit
  // available than for one farther away.
  auto penalty = power(
      std::move(unavailableFractionOfNextAvailableUnit), kPenaltyExponent);
  return product(std::move(hasUnavailableUnits), std::move(penalty));
}

} // namespace

MaximizeFreeCapacityUnitsSpecBuilder::MaximizeFreeCapacityUnitsSpecBuilder(
    std::shared_ptr<const Universe> universe,
    interface::MaximizeFreeCapacityUnitsSpec spec,
    bool continuousExpressions)
    : SpecBuilder(std::move(universe)),
      spec_(std::move(spec)),
      continuousExpressions_(continuousExpressions),
      dimensionId_(universe_->getDimensionId(*spec_.dimension())) {
  const auto& objectDimension =
      universe_->getObjects().getDimension(dimensionId_);
  if (objectDimension.size() > 1) {
    throw std::runtime_error(
        "MaximizeFreeCapacityUnitsSpecBuilder is not supported with non-scalar dimensions");
  }
  if (objectDimension.hasNegativeValues()) {
    throw std::runtime_error(
        "MaximizeFreeCapacityUnitsSpecBuilder is not supported with negative dimension values");
  }
}

folly::coro::Task<ExprPtr> MaximizeFreeCapacityUnitsSpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  const auto scopeId = universe_->getScopeId(*spec_.scope());
  const auto& scopeDimension =
      universe_->getScope(scopeId).getDimension(dimensionId_);
  const LimitWrapper unitSizes(*universe_, *spec_.unitSize(), scopeId);
  const ScopeItemFilterWrapper filter(*universe_, *spec_.filter(), scopeId);
  const auto scopeItemIds = filter.getScopeItemIds();

  auto objective = const_expr(0, *universe_);
  auto totalNextAvailableUnitPenalty = const_expr(0, *universe_);
  for (const auto scopeItemId : scopeItemIds) {
    const auto capacity = scopeDimension.getValue(scopeItemId);
    if (capacity < 0) [[unlikely]] {
      throw std::runtime_error(
          fmt::format(
              "MaximizeFreeCapacityUnitsSpecBuilder is not supported with negative capacity {} for scope item '{}'",
              capacity,
              universe_->getEntityName(scopeItemId)));
    }
    const auto unitSize = unitSizes.getLimit(scopeItemId);
    if (!std::isfinite(unitSize) || unitSize <= 0) [[unlikely]] {
      throw std::runtime_error(
          fmt::format(
              "MaximizeFreeCapacityUnitsSpec unit size for scope item '{}' must be finite and positive, but got {}",
              universe_->getEntityName(scopeItemId),
              unitSize));
    }

    const auto maxAvailableUnits = std::floor(capacity / unitSize);
    auto utilization = co_await expressionBuilder.getAbsoluteUtil(
        UtilMetric::AFTER, dimensionId_, scopeId, scopeItemId);
    auto scaledRemainingCapacity =
        max(0.0, (capacity - std::move(utilization)) / unitSize);
    auto availableUnits = floor(scaledRemainingCapacity);
    auto unavailableUnits = maxAvailableUnits - availableUnits;
    inplace_add(objective, unavailableUnits);

    if (continuousExpressions_) {
      inplace_add(
          totalNextAvailableUnitPenalty,
          buildNextAvailableUnitPenalty(
              std::move(scaledRemainingCapacity),
              std::move(availableUnits),
              std::move(unavailableUnits)));
    }
  }

  if (continuousExpressions_ && !scopeItemIds.empty()) {
    inplace_add(
        objective,
        std::move(totalNextAvailableUnitPenalty),
        kPenaltyWeight / scopeItemIds.size());
  }
  co_return objective;
}

[[noreturn]] folly::coro::Task<std::vector<ConstraintInfo>>
MaximizeFreeCapacityUnitsSpecBuilder::constraints(
    ExpressionBuilder& /* expressionBuilder */) const {
  throw std::runtime_error(
      "MaximizeFreeCapacityUnitsSpec is not supported as a constraint");
}

std::string MaximizeFreeCapacityUnitsSpecBuilder::description() const {
  return fmt::format(
      "Maximize available units in each scope item's remaining capacity for dimension '{}' and scope '{}'",
      *spec_.dimension(),
      *spec_.scope());
}

SpecParameters MaximizeFreeCapacityUnitsSpecBuilder::getSpecInfo() const {
  return SpecParameters{
      .name = *spec_.name(),
      .scope = *spec_.scope(),
      .dimension = *spec_.dimension()};
}

} // namespace facebook::rebalancer::materializer
