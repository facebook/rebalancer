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

#include "algopt/rebalancer/materializer/spec_builder/DrainCapacitySpecBuilder.h"

#include "algopt/rebalancer/solver/expressions/Operators.h"

#include <folly/container/irange.h>

namespace facebook::rebalancer::materializer {

DrainCapacitySpecBuilder::DrainCapacitySpecBuilder(
    std::shared_ptr<const entities::Universe> universe,
    facebook::rebalancer::interface::DrainCapacitySpec spec)
    : SpecBuilder(std::move(universe)), spec_(std::move(spec)) {}

folly::coro::Task<ExprPtr> DrainCapacitySpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  co_return getAggregatedConstraintViolation(
      co_await constraints(expressionBuilder), *universe_);
}

folly::coro::Task<std::vector<ConstraintInfo>>
DrainCapacitySpecBuilder::constraints(
    ExpressionBuilder& expressionBuilder) const {
  std::vector<ConstraintInfo> result;

  auto scopeId = universe_->getScopeId(*spec_.scope());
  auto& scope = universe_->getScope(scopeId);
  auto& scopeItemIds = scope.getScopeItemIds();

  auto dimensionId = universe_->getDimensionId(*spec_.dimension());
  auto& dimension = scope.getDimension(dimensionId);

  double totalCapacity = 0.0;
  for (auto scopeItemId : scopeItemIds) {
    totalCapacity += dimension.getValue(scopeItemId);
  }

  const double normCoeff =
      totalCapacity == 0.0 ? 1.0 : 1.0 / (totalCapacity / scopeItemIds.size());

  for (auto& [srcItemName, proportions] : *spec_.spillDistribution()) {
    auto srcItemId = universe_->getScopeItemId(scopeId, srcItemName);
    auto srcUsages = co_await getUsagePerDimensionIndex(
        expressionBuilder, dimensionId, scopeId, srcItemId);

    for (auto& [dstItemName, proportion] : proportions) {
      auto dstItemId = universe_->getScopeItemId(scopeId, dstItemName);
      auto dstUsages = co_await getUsagePerDimensionIndex(
          expressionBuilder, dimensionId, scopeId, dstItemId);
      double dstCapacity = dimension.getValue(dstItemId);

      // Constraint formula, at the worst index of a vector dimension:
      // max_i({dst usage}_i + {proportion} * {src usage}_i) <= {dst capacity}
      std::vector<ExprPtr> drainUsages;
      drainUsages.reserve(dstUsages.size());
      for (const auto i : folly::irange(dstUsages.size())) {
        drainUsages.push_back(dstUsages[i] + proportion * srcUsages[i]);
      }
      auto drainUsage = drainUsages.size() == 1 ? drainUsages.front()
                                                : max(drainUsages, *universe_);

      auto expr = (drainUsage - dstCapacity) * normCoeff;
      expr->description = fmt::format(
          "usage of {} + {} * usage of {} <= {}",
          dstItemName,
          proportion,
          srcItemName,
          dstCapacity);
      result.emplace_back(expr);
    }
  }

  co_return result;
}

folly::coro::Task<std::vector<ExprPtr>>
DrainCapacitySpecBuilder::getUsagePerDimensionIndex(
    ExpressionBuilder& expressionBuilder,
    entities::DimensionId dimensionId,
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId) const {
  // Only the aggregated util registers the scope item's util in metrics.
  auto aggregatedUsage = co_await expressionBuilder.getAbsoluteUtil(
      UtilMetric::AFTER, dimensionId, scopeId, scopeItemId);

  const int dimensionSize =
      universe_->getObjects().getDimension(dimensionId).size();
  if (dimensionSize == 1) {
    co_return std::vector<ExprPtr>{std::move(aggregatedUsage)};
  }

  std::vector<ExprPtr> usages;
  usages.reserve(dimensionSize);
  for (const auto dimensionIndex : folly::irange(dimensionSize)) {
    usages.push_back(
        co_await expressionBuilder.getAbsoluteUtil(
            UtilMetric::AFTER,
            dimensionId,
            scopeId,
            scopeItemId,
            dimensionIndex));
  }
  co_return usages;
}

std::string DrainCapacitySpecBuilder::description() const {
  return fmt::format(
      "limit drain capacity ({}) on scope {}",
      *spec_.dimension(),
      *spec_.scope());
}

SpecParameters DrainCapacitySpecBuilder::getSpecInfo() const {
  return SpecParameters{
      .name = *spec_.name(),
      .scope = *spec_.scope(),
      .dimension = *spec_.dimension()};
}

} // namespace facebook::rebalancer::materializer
