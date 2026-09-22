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

#include "algopt/rebalancer/materializer/spec_builder/BalanceSpecBuilder.h"

#include "algopt/rebalancer/entities/Set.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/standalone/BackwardCompatabilityUtils.h"
#include "algopt/rebalancer/materializer/utils/FilterWrapper.h"
#include "algopt/rebalancer/materializer/utils/LimitWrapper.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"

#include <folly/container/Foreach.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include <cstddef>
#include <stdexcept>

using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::interface;

namespace facebook::rebalancer::materializer {

namespace {

interface::BalanceSpec migrateDeprecatedFields(interface::BalanceSpec spec) {
  BackwardCompatabilityUtils::possiblyModify(spec);
  return spec;
}

} // namespace

BalanceSpecBuilder::BalanceSpecBuilder(
    std::shared_ptr<const Universe> universe,
    interface::BalanceSpec spec,
    bool continuousExpressions)
    : SpecBuilder(std::move(universe)),
      spec_(migrateDeprecatedFields(std::move(spec))),
      dimensionId_(universe_->getDimensionId(*spec_.dimension())),
      scopeId_(universe_->getScopeId(*spec_.scope())),
      upperBoundLimits_(*universe_, *spec_.upperBounds(), scopeId_),
      continuousExpressions_(continuousExpressions) {
  if (*spec_.balanceMetric() == BalanceSpecMetric::CAPACITY_PER_ITEM &&
      !continuousExpressions_) {
    throw std::runtime_error(
        "BalanceSpec with CAPACITY_PER_ITEM metric is not currently supported with the OptimalSolver (which requires linear expressions); use LocalSearch instead");
  }
}

folly::coro::Task<std::vector<ConstraintInfo>> BalanceSpecBuilder::constraints(
    ExpressionBuilder& /* expressionBuilder */) const {
  throw std::runtime_error("BalanceSpec not supported as a constraint");
}

static UtilMetric parseMetric(BalanceSpecDefinition definition) {
  switch (definition) {
    case BalanceSpecDefinition::AFTER:
      return UtilMetric::AFTER;
    case BalanceSpecDefinition::DURING:
      return UtilMetric::DURING;
    case BalanceSpecDefinition::NEW:
      return UtilMetric::NEW;
    case BalanceSpecDefinition::OLD:
      return UtilMetric::OLD;
    default:
      throw std::runtime_error("Unhandled BalanceSpecDefinition");
  }
}

bool BalanceSpecBuilder::shouldFixAverageToInitial(
    const std::vector<ScopeItemId>& scopeItemIds) const {
  auto fixAverageToInitial = *spec_.fixAverageToInitial();
  auto& scope = universe_->getScope(scopeId_);
  auto& originalScopeItemIds = scope.getScopeItemIds();

  auto totalContainers = universe_->getContainers().getContainerIds().size();
  size_t totalContainersInScope = 0;
  for (auto scopeItemId : originalScopeItemIds) {
    totalContainersInScope += scope.getContainerIds(scopeItemId).size();
  }

  // either the user wants us to fix average to initial
  return fixAverageToInitial ||
      // or average always equals initial average; since the utilization is
      // inherently preserving as objects can only move across containers that
      // are all being considered for average computation
      (*spec_.definition() == BalanceSpecDefinition::AFTER &&
       scopeItemIds.size() == originalScopeItemIds.size() &&
       totalContainersInScope == totalContainers);
}

folly::coro::Task<std::pair<ExprPtr, std::size_t>>
BalanceSpecBuilder::getTotalAbsoluteOrRelativeUtil(
    UtilMetric metric,
    const std::vector<ScopeItemId>& scopeItemIds,
    ExpressionBuilder& expressionBuilder,
    bool computeRelativeUtil) const {
  auto& scope = universe_->getScope(scopeId_);
  auto& scopeDimension = scope.getDimension(dimensionId_);
  auto getScopeItemUtil =
      [&](entities::ScopeItemId scopeItemId) -> folly::coro::Task<ExprPtr> {
    co_return computeRelativeUtil
        ? co_await expressionBuilder.getRelativeUtil(
              metric, dimensionId_, scopeId_, scopeItemId)
        : co_await expressionBuilder.getAbsoluteUtil(
              metric, dimensionId_, scopeId_, scopeItemId);
  };

  ExprPtr sumUtil;
  for (auto scopeItemId : scopeItemIds) {
    sumUtil += co_await getScopeItemUtil(scopeItemId);
  }
  double initialSumUtil = sumUtil->getInitialValue();

  if (shouldFixAverageToInitial(scopeItemIds)) {
    const auto& includeInInitialAverage = *spec_.includeInInitialAverage();
    // compute utilization of other whitelisted scopeItems
    Set<ScopeItemId> scopeItemIdsSet(scopeItemIds.begin(), scopeItemIds.end());
    if (!includeInInitialAverage.empty()) {
      for (const auto& scopeItemName : includeInInitialAverage) {
        auto scopeItemId = universe_->getScopeItemId(scopeId_, scopeItemName);

        // skip scope items with zero capacity
        if (scopeDimension.getValue(scopeItemId) == 0) {
          continue;
        }
        auto [_, insertSuccess] = scopeItemIdsSet.emplace(scopeItemId);
        // do not double count utilization of any scope item
        if (!insertSuccess) {
          continue;
        }
        auto util = co_await getScopeItemUtil(scopeItemId);
        initialSumUtil += util->getInitialValue();
      }
    }
    co_return {const_expr(initialSumUtil, *universe_), scopeItemIdsSet.size()};
  }
  co_return {sumUtil, scopeItemIds.size()};
}

ExprPtr BalanceSpecBuilder::computeMaxPenalty(
    const std::vector<ScopeItemUtil>& scopeItemUtils,
    const ExprPtr& thresholdExpr) const {
  std::vector<ExprPtr> utils;
  utils.reserve(scopeItemUtils.size());
  for (const auto& scopeItemUtil : scopeItemUtils) {
    utils.push_back(scopeItemUtil.util);
  }
  auto excess = max(utils, *universe_) - thresholdExpr;
  return max({std::move(excess), const_expr(0, *universe_)}, *universe_);
}

ExprPtr BalanceSpecBuilder::computeLinearOrSquaresPenalty(
    const std::vector<ScopeItemUtil>& scopeItemUtils,
    const ExprPtr& thresholdExpr,
    BalanceSpecFormula formula) {
  const auto n = scopeItemUtils.size();
  const auto transform = [&formula](const ExprPtr& expr) {
    return formula == BalanceSpecFormula::SQUARES ? power(expr, 1.1) : expr;
  };

  std::vector<ExprPtr> transformedUtils;
  transformedUtils.reserve(n);
  for (const auto& scopeItemUtil : scopeItemUtils) {
    transformedUtils.push_back(transform(scopeItemUtil.util));
  }
  return sum_over_threshold(transform(thresholdExpr), transformedUtils, false) /
      n;
}

ExprPtr BalanceSpecBuilder::computeIdealPenalty(
    const std::vector<ScopeItemUtil>& scopeItemUtils,
    const std::vector<double>& adjustments,
    const std::function<ExprPtr(double)>& boundExpr,
    double upperBound,
    bool applyBound) const {
  const auto n = scopeItemUtils.size();
  auto result = const_expr(0, *universe_);
  for (const auto i : folly::irange(n)) {
    // We want to model: penalty = (absUtil ^ 2) / (capacity * avgCapacity)
    // = power(absUtil/capacity, 2) * (capacity/avgCapacity)
    // = power(relUtil, 2) * adjustment
    auto penalty = power(scopeItemUtils[i].util, 2) * adjustments[i];
    if (applyBound) {
      const auto adjustedBoundSquared =
          power(boundExpr(upperBound), 2) * adjustments[i];
      penalty = max(0, penalty - adjustedBoundSquared);
    }
    result += penalty;
  }
  return result;
}

ExprPtr BalanceSpecBuilder::computeVariancePenalty(
    const std::vector<ScopeItemUtil>& scopeItemUtils,
    const std::function<ExprPtr(double)>& boundExpr) const {
  const auto n = scopeItemUtils.size();
  const bool onlyHasGlobalLimit = upperBoundLimits_.onlyHasGlobalLimit();
  const ExprPtr globalThresholdExpr = onlyHasGlobalLimit
      ? boundExpr(upperBoundLimits_.getGlobalLimit())
      : nullptr;
  ExprPtr sumVal;
  ExprPtr sumValSquared;
  for (const auto& scopeItemUtil : scopeItemUtils) {
    ExprPtr thresholdExpr;
    if (onlyHasGlobalLimit) {
      thresholdExpr = globalThresholdExpr;
    } else {
      thresholdExpr =
          boundExpr(upperBoundLimits_.getLimit(scopeItemUtil.scopeItemId));
    }
    const auto val = max(0, scopeItemUtil.util - thresholdExpr);
    sumVal += val;
    sumValSquared += square(val);
  }
  return std::move(sumValSquared) - square(std::move(sumVal)) / n;
}

ExprPtr BalanceSpecBuilder::computeLegacyPenalty(
    const std::vector<ScopeItemUtil>& scopeItemUtils,
    double initialUtil,
    double sumCapacity,
    double upperBound) const {
  const auto n = scopeItemUtils.size();
  auto result = const_expr(0, *universe_);
  auto maxImbalance = const_expr(0, *universe_);
  const double coefficient = 0.001 / n;
  const double balancedUtil = initialUtil / sumCapacity;
  for (const auto i : folly::irange(n)) {
    const auto imbalance = scopeItemUtils[i].util / balancedUtil - upperBound;
    inplace_max(maxImbalance, imbalance);
    const auto positiveImbalance = max(0, imbalance);
    result += coefficient *
        (continuousExpressions_ ? square(positiveImbalance)
                                : positiveImbalance);
  }
  result += std::move(maxImbalance);
  return result;
}

static std::vector<double> getIdealAdjustments(
    BalanceSpecMetric balanceMetric,
    const std::vector<ScopeItemId>& scopeItemIds,
    double sumCapacity,
    const ScopeDimension& scopeDimension) {
  const auto n = scopeItemIds.size();
  std::vector<double> adjustments;
  adjustments.reserve(n);

  switch (balanceMetric) {
    case BalanceSpecMetric::CAPACITY_PER_ITEM:
      throw std::runtime_error(
          "IDEAL formula is not supported with CAPACITY_PER_ITEM metric");
    case BalanceSpecMetric::RELATIVE_UTIL: {
      const double avgCapacity = sumCapacity / n;
      for (auto scopeItemId : scopeItemIds) {
        adjustments.push_back(
            scopeDimension.getValue(scopeItemId) / avgCapacity);
      }
      break;
    }
  }
  return adjustments;
}

folly::coro::Task<ExprPtr> BalanceSpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  auto boundType = *spec_.boundType();
  auto formula = *spec_.formula();
  auto definition = *spec_.definition();
  auto metric = parseMetric(definition);
  const auto balanceMetric = *spec_.balanceMetric();
  const double upperBound = upperBoundLimits_.getGlobalLimit();
  const ScopeItemFilterWrapper filter(*universe_, *spec_.filter(), scopeId_);

  auto& scope = universe_->getScope(scopeId_);
  auto& scopeDimension = scope.getDimension(dimensionId_);

  auto scopeItemIds = filter.getScopeItemIds();

  // Filter scope items that would produce undefined values for the metric.
  switch (balanceMetric) {
    case BalanceSpecMetric::RELATIVE_UTIL:
      // relUtil = absUtil/capacity is undefined when capacity is 0.
      scopeItemIds.erase(
          std::remove_if(
              scopeItemIds.begin(),
              scopeItemIds.end(),
              [&scopeDimension](ScopeItemId scopeItemId) {
                return scopeDimension.getValue(scopeItemId) == 0;
              }),
          scopeItemIds.end());
      break;
    case BalanceSpecMetric::CAPACITY_PER_ITEM:
      // Capacity is not used, so zero-capacity scope items are kept.
      break;
  }

  if (scopeItemIds.empty()) {
    co_return const_expr(0, *universe_);
  }

  const auto n = scopeItemIds.size();
  // Compute per-scope-item values based on the metric.
  std::vector<ScopeItemUtil> scopeItemUtils;
  scopeItemUtils.reserve(n);
  double sumCapacity = 0;
  const auto objectCountDimensionId =
      spec_.capacityPerItemCountDimension().has_value()
      ? universe_->getDimensionId(*spec_.capacityPerItemCountDimension())
      : universe_->getDimensionId(
            fmt::format("{}_count", universe_->getObjectTypeName()));

  switch (balanceMetric) {
    case BalanceSpecMetric::CAPACITY_PER_ITEM:
      for (auto scopeItemId : scopeItemIds) {
        auto absUtil = co_await expressionBuilder.getAbsoluteUtil(
            metric, dimensionId_, scopeId_, scopeItemId);
        auto numObjects = co_await expressionBuilder.getAbsoluteUtil(
            metric, objectCountDimensionId, scopeId_, scopeItemId);
        auto capPerItem = quotient(absUtil, max(1, numObjects));
        scopeItemUtils.push_back({scopeItemId, std::move(capPerItem)});
        sumCapacity += scopeDimension.getValue(scopeItemId);
      }
      break;
    case BalanceSpecMetric::RELATIVE_UTIL:
      for (auto scopeItemId : scopeItemIds) {
        scopeItemUtils.push_back(
            {scopeItemId,
             co_await expressionBuilder.getRelativeUtil(
                 metric, dimensionId_, scopeId_, scopeItemId)});
        sumCapacity += scopeDimension.getValue(scopeItemId);
      }
      break;
  }

  // Compute the scope-wide average used as the balance threshold.
  ExprPtr avgUtil;
  switch (balanceMetric) {
    case BalanceSpecMetric::CAPACITY_PER_ITEM: {
      // TODO: getTotalAbsoluteOrRelativeUtil handles shouldFixAverageToInitial
      // for RELATIVE_UTIL by fixing the threshold to the initial value. For
      // CAPACITY_PER_ITEM the threshold is sum(capPerItem)/n which changes as
      // objects move. Consider supporting fixAverageToInitial here if needed.
      ExprPtr sumUtil;
      for (const auto& scopeItemUtil : scopeItemUtils) {
        sumUtil += scopeItemUtil.util;
      }
      avgUtil = sumUtil / n;
      break;
    }
    case BalanceSpecMetric::RELATIVE_UTIL:
      if (!*spec_.useLegacyAverage()) {
        auto [totalUtil, numScopeItems] =
            co_await getTotalAbsoluteOrRelativeUtil(
                metric,
                scopeItemIds,
                expressionBuilder,
                /*computeRelativeUtil=*/true);
        avgUtil = totalUtil / numScopeItems;
      } else {
        auto [totalAbsoluteUtil, _] = co_await getTotalAbsoluteOrRelativeUtil(
            metric,
            scopeItemIds,
            expressionBuilder,
            /*computeRelativeUtil=*/false);
        avgUtil = totalAbsoluteUtil / sumCapacity;
      }
      break;
  }

  const auto boundExpr = [avgUtil, boundType, this](const double bound) {
    switch (boundType) {
      case (BalanceSpecBoundType::ABSOLUTE):
        return avgUtil + bound;
      case BalanceSpecBoundType::RELATIVE:
        return bound * avgUtil;
      case BalanceSpecBoundType::RELATIVE_UTIL:
        return const_expr(bound, *universe_);
      default:
        throw std::runtime_error("Unhandled BalanceSpecBoundType");
    }
  };

  auto result = const_expr(0, *universe_);
  auto thresholdExpr = spec_.softUpperBound()
      ? max(*spec_.softUpperBound(), boundExpr(upperBound))
      : boundExpr(upperBound);

  if (formula == BalanceSpecFormula::MAX) {
    result = computeMaxPenalty(scopeItemUtils, thresholdExpr);
  } else if (formula == BalanceSpecFormula::IDEAL) {
    const bool applyBound = boundType == BalanceSpecBoundType::RELATIVE_UTIL ||
        !*spec_.ignoreUpperBoundForIdealWithAbsOrRelBoundTypes();

    auto adjustments = getIdealAdjustments(
        balanceMetric, scopeItemIds, sumCapacity, scopeDimension);
    result = computeIdealPenalty(
        scopeItemUtils, adjustments, boundExpr, upperBound, applyBound);
  } else if (formula == BalanceSpecFormula::RELATIVE_UTIL_VARIANCE) {
    result = computeVariancePenalty(scopeItemUtils, boundExpr);
  } else if (formula == BalanceSpecFormula::LEGACY) {
    switch (balanceMetric) {
      case BalanceSpecMetric::CAPACITY_PER_ITEM:
        throw std::runtime_error(
            "LEGACY formula is not supported with CAPACITY_PER_ITEM metric");
      case BalanceSpecMetric::RELATIVE_UTIL:
        break;
    }
    auto [totalUtil, numScopeItems] = co_await getTotalAbsoluteOrRelativeUtil(
        metric, scopeItemIds, expressionBuilder, /*computeRelativeUtil=*/false);
    auto initialUtil = totalUtil->getInitialValue();
    if (universe_->getPrecision().compare(initialUtil, 0.0) == 0) {
      co_return const_expr(-upperBound, *universe_);
    }
    result = computeLegacyPenalty(
        scopeItemUtils, initialUtil, sumCapacity, upperBound);
  } else {
    result =
        computeLinearOrSquaresPenalty(scopeItemUtils, thresholdExpr, formula);
  }
  co_return result;
}

std::string BalanceSpecBuilder::description() const {
  return fmt::format(
      "Balance {} on {} (definition {}, bound type {}, upper bound {}, formula {}, metric {})",
      *spec_.dimension(),
      *spec_.name(),
      apache::thrift::util::enumNameSafe(*spec_.definition()),
      apache::thrift::util::enumNameSafe(*spec_.boundType()),
      upperBoundLimits_.getGlobalLimit(),
      apache::thrift::util::enumNameSafe(*spec_.formula()),
      apache::thrift::util::enumNameSafe(*spec_.balanceMetric()));
}

SpecParameters BalanceSpecBuilder::getSpecInfo() const {
  return SpecParameters{
      .name = *spec_.name(),
      .scope = *spec_.scope(),
      .dimension = *spec_.dimension(),
      .definition = apache::thrift::util::enumNameSafe(*spec_.definition()),
      .boundType = apache::thrift::util::enumNameSafe(*spec_.boundType()),
      .formula = apache::thrift::util::enumNameSafe(*spec_.formula()),
      .filterAllowListSize = spec_.filter()->itemsWhitelist()
          ? static_cast<int>(spec_.filter()->itemsWhitelist()->size())
          : 0,
      .filterBlockListSize = spec_.filter()->itemsBlacklist()
          ? static_cast<int>(spec_.filter()->itemsBlacklist()->size())
          : 0};
}

} // namespace facebook::rebalancer::materializer
