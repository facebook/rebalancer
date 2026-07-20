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

#include "algopt/rebalancer/materializer/spec_builder/CapacitySpecBuilder.h"

#include "algopt/rebalancer/common/CoroUtils.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/ScopeDimension.h"
#include "algopt/rebalancer/materializer/utils/ExpressionBuilder.h"
#include "algopt/rebalancer/materializer/utils/FilterWrapper.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"

#include <optional>
#include <stdexcept>

using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::interface;

namespace facebook::rebalancer::materializer {

namespace {

std::optional<GroupUtilizationBound> getGroupUtilizationBound(
    const CapacitySpec& spec) {
  if (spec.utilizationBound() &&
      spec.utilizationBound()->getType() ==
          UtilizationBound::Type::groupUtilizationBound) {
    return spec.utilizationBound()->get_groupUtilizationBound();
  }
  return std::nullopt;
}

} // namespace

CapacitySpecBuilder::CapacitySpecBuilder(
    std::shared_ptr<const Universe> universe,
    CapacitySpec spec)
    : SpecBuilder(std::move(universe)),
      spec_(std::move(spec)),
      scopeId_(universe_->getScopeId(*spec_.scope())),
      dimensionId_(universe_->getDimensionId(*spec_.dimension())),
      limits_(*universe_, *spec_.limit(), scopeId_),
      scopeFilter_(*universe_, *spec_.filter(), scopeId_) {
  if (auto bound = getGroupUtilizationBound(spec_)) {
    initilizePerGroupUtilizationBounds(*bound);
  }
}

UtilMetric CapacitySpecBuilder::getMappedUtilMetric(
    InnerUtilMetric innerMetric) {
  switch (innerMetric) {
    case InnerUtilMetric::AFTER:
      return UtilMetric::AFTER;
    case InnerUtilMetric::DURING:
      return UtilMetric::DURING;
    case InnerUtilMetric::OLD:
      return UtilMetric::OLD;
    case InnerUtilMetric::NEW:
      return UtilMetric::NEW;
    case InnerUtilMetric::MOVED:
      return UtilMetric::MOVED;
    case InnerUtilMetric::DURING_AND_AFTER_IF_BROKEN:
      throw std::runtime_error("Unmapped InnerUtilMetric");
  }
  throw std::runtime_error("Invalid InnerUtilMetric");
}

std::vector<CapacitySpecBuilder::InnerUtilMetric>
CapacitySpecBuilder::getUtilMetrics(
    CapacitySpecDefinition definition,
    bool isGoalSpec) {
  std::vector<InnerUtilMetric> duringAfterExpanded;
  if (isGoalSpec) {
    duringAfterExpanded = {InnerUtilMetric::DURING, InnerUtilMetric::AFTER};
  } else {
    duringAfterExpanded = {InnerUtilMetric::DURING_AND_AFTER_IF_BROKEN};
  }
  switch (definition) {
    case CapacitySpecDefinition::AFTER:
      return {InnerUtilMetric::AFTER};
    case CapacitySpecDefinition::DURING_AND_AFTER:
      return duringAfterExpanded;
    case CapacitySpecDefinition::DURING:
      return {InnerUtilMetric::DURING};
    case CapacitySpecDefinition::DOUBLE_DURING_AND_AFTER: {
      auto rVal = duringAfterExpanded;
      rVal.push_back(InnerUtilMetric::OLD);
      return rVal;
    }
    case CapacitySpecDefinition::DOUBLE_DURING:
      return {InnerUtilMetric::DURING, InnerUtilMetric::OLD};
    case CapacitySpecDefinition::NEW:
      return {InnerUtilMetric::NEW};
    case CapacitySpecDefinition::OLD:
      return {InnerUtilMetric::OLD};
    case CapacitySpecDefinition::MOVED_DATA:
      return {InnerUtilMetric::MOVED};
  }
  throw std::runtime_error("unsupported capacity definition");
}

folly::coro::Task<ExprPtr> CapacitySpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  auto metrics = getUtilMetrics(*spec_.definition(), true);
  auto exprs = co_await getConstraint(expressionBuilder, metrics);
  co_return getAggregatedConstraintViolation(exprs, *universe_);
}

folly::coro::Task<std::vector<ConstraintInfo>> CapacitySpecBuilder::constraints(
    ExpressionBuilder& expressionBuilder) const {
  // initialize scopeItemsInitiallyAtOrAboveDuringLimit_ when used as a
  // constraint
  scopeItemsInitiallyAtOrAboveDuringLimit_.emplace();
  auto metrics = getUtilMetrics(*spec_.definition(), false);
  co_return co_await getConstraint(expressionBuilder, metrics);
}

folly::coro::Task<std::vector<ConstraintInfo>>
CapacitySpecBuilder::getConstraint(
    ExpressionBuilder& expressionBuilder,
    const std::vector<InnerUtilMetric>& metrics) const {
  std::vector<ConstraintInfo> result;
  for (auto metric : metrics) {
    auto exprs = co_await getConstraint(expressionBuilder, metric);
    result.insert(result.end(), exprs.begin(), exprs.end());
  }
  co_return result;
}

folly::coro::Task<ExprPtr> CapacitySpecBuilder::getMaybeBoundedUtil(
    ExpressionBuilder& expressionBuilder,
    UtilMetric metric,
    entities::ScopeItemId scopeItemId) const {
  if (auto bound = getGroupUtilizationBound(spec_)) {
    co_return co_await getBoundedUtil(
        expressionBuilder, metric, scopeItemId, *bound);
  }
  // if no bounds were specified, return the util as is
  co_return co_await expressionBuilder.getAbsoluteUtil(
      metric, dimensionId_, scopeId_, scopeItemId);
}

folly::coro::Task<std::optional<ExprPtr>> CapacitySpecBuilder::getExpression(
    ExpressionBuilder& expressionBuilder,
    UtilMetric metric,
    entities::ScopeItemId scopeItemId,
    double threshold,
    double normCoef) const {
  // scale util and threshold using normCoef
  auto util = normCoef *
      co_await getMaybeBoundedUtil(expressionBuilder, metric, scopeItemId);
  threshold = threshold * normCoef;

  ExprPtr expr;
  auto bound = *spec_.bound();
  if (bound == CapacitySpecBound::MAX) {
    expr = util - threshold;
  } else if (bound == CapacitySpecBound::MIN) {
    if (threshold == 0) {
      // Min capacity is always satisfied when threshold is zero.
      co_return std::nullopt;
    }

    const bool zeroAllowed = *spec_.zeroAllowed();
    if (zeroAllowed) {
      double ub = expressionBuilder.getUpperBound(*util);
      // the piecewise function simply transforms util to
      // the following function:
      //          { 0                    util = 0         }
      //   expr = { threshold - util     util > 0         }
      //          { 0                    util > threshold }
      if (ub > threshold) {
        expr = piecewise(
            {{0, 0}, {0, threshold}, {threshold, 0}, {ub, 0}}, util, false);
      } else {
        expr = piecewise({{0, 0}, {0, threshold}, {threshold, 0}}, util, false);
      }
    } else {
      expr = threshold - util;
    }
  } else {
    throw std::runtime_error("unsupported capacity bound");
  }

  co_return expr;
}

folly::coro::Task<std::pair<std::vector<ConstraintInfo>, bool>>
CapacitySpecBuilder::getConstraintsForScopeItem(
    ExpressionBuilder& expressionBuilder,
    InnerUtilMetric innerMetric,
    entities::ScopeItemId scopeItemId,
    double threshold,
    double normCoef) const {
  bool isAtOrAboveDuringLimit = false;

  auto isDuringBrokenAndMaybeTrackViolation =
      [this, &isAtOrAboveDuringLimit](
          Expression& duringExpr,
          [[maybe_unused]] entities::ScopeItemId scopeItemId) {
        auto duringValue = duringExpr.getInitialValue();
        auto compare = universe_->getPrecision().compare(duringValue, 0);
        auto duringBelowLimit = (compare == -1);
        if (!duringBelowLimit &&
            scopeItemsInitiallyAtOrAboveDuringLimit_.has_value()) {
          isAtOrAboveDuringLimit = true;
        }

        return (compare == 1); // during is broken, i.e., above limit
      };

  std::vector<ConstraintInfo> rVal;
  if (innerMetric == InnerUtilMetric::DURING_AND_AFTER_IF_BROKEN) {
    // Check if DURING is initially broken
    if (auto expr = co_await getExpression(
            expressionBuilder,
            UtilMetric::DURING,
            scopeItemId,
            threshold,
            normCoef)) {
      auto duringExpr = *expr;
      rVal.emplace_back(duringExpr);
      auto duringBroken =
          isDuringBrokenAndMaybeTrackViolation(*duringExpr, scopeItemId);
      if (duringBroken) {
        // DURING was initially broken, AFTER is needed
        if (auto after = co_await getExpression(
                expressionBuilder,
                UtilMetric::AFTER,
                scopeItemId,
                threshold,
                normCoef)) {
          rVal.emplace_back(*after);
        }
      }
    }
  } else {
    auto metric = getMappedUtilMetric(innerMetric);
    if (auto expr = co_await getExpression(
            expressionBuilder, metric, scopeItemId, threshold, normCoef)) {
      if (metric == UtilMetric::DURING) {
        isDuringBrokenAndMaybeTrackViolation(*expr.value(), scopeItemId);
      }

      rVal.emplace_back(*expr);
    }
  }
  co_return std::make_pair(std::move(rVal), isAtOrAboveDuringLimit);
}

std::string CapacitySpecBuilder::description() const {
  std::string definition;
  switch (*spec_.definition()) {
    case CapacitySpecDefinition::AFTER:
      definition = "after";
      break;
    case CapacitySpecDefinition::DURING_AND_AFTER:
      definition = "during + after";
      break;
    case CapacitySpecDefinition::DURING:
      definition = "during";
      break;
    case CapacitySpecDefinition::DOUBLE_DURING_AND_AFTER:
      definition = "during + old + after";
      break;
    case CapacitySpecDefinition::DOUBLE_DURING:
      definition = "during + old";
      break;
    case CapacitySpecDefinition::NEW:
      definition = "new";
      break;
    case CapacitySpecDefinition::OLD:
      definition = "old";
      break;
    case CapacitySpecDefinition::MOVED_DATA:
      definition = "new + old";
      break;
  }

  std::string bound;
  switch (*spec_.bound()) {
    case CapacitySpecBound::MAX:
      bound = "<=";
      break;
    case CapacitySpecBound::MIN:
      bound = ">=";
      break;
  }

  return fmt::format(
      "{}({}) {} {} for scope {}",
      definition,
      *spec_.dimension(),
      bound,
      *spec_.limit()->globalLimit(),
      *spec_.scope());
}

folly::coro::Task<std::vector<ConstraintInfo>>
CapacitySpecBuilder::getConstraint(
    ExpressionBuilder& expressionBuilder,
    InnerUtilMetric metric) const {
  auto& scopeDimension =
      universe_->getScope(scopeId_).getDimension(dimensionId_);

  auto scopeItemIds = scopeFilter_.getScopeItemIds();

  if (scopeItemIds.empty()) {
    co_return std::vector<ConstraintInfo>{};
  }

  double totalCapacity = 0;
  for (auto scopeItemId : scopeItemIds) {
    totalCapacity += scopeDimension.getValue(scopeItemId);
  }
  const double averageCapacity = totalCapacity / scopeItemIds.size();

  // {scopeItemId, threshold, normCoef}
  using ScopeItemParams = std::tuple<entities::ScopeItemId, double, double>;
  std::vector<ScopeItemParams> params;
  params.reserve(scopeItemIds.size());

  for (auto scopeItemId : scopeItemIds) {
    double threshold = limits_.getLimit(scopeItemId);
    if (limits_.getType() == LimitType::RELATIVE) {
      threshold *= scopeDimension.getValue(scopeItemId);
    }

    // We use average capacity to normalize absolute expressions, which
    // makes tuning the weights straightforward. While the average can
    // legitimately be zero, it makes it impossible for us to normalize
    // the expression. In that particular case, we leave normalization
    // up to the user by using the weight. Also note that in the most
    // common case where capacity is used as a constraint that's
    // initially not broken, normalization is completely irrelevant.
    double normCoef = averageCapacity == 0 ? 1 : 1 / averageCapacity;

    if (*spec_.useLegacyFormula()) {
      const double capacity = scopeDimension.getValue(scopeItemId);
      if (capacity == 0) {
        throw std::runtime_error(
            fmt::format(
                "{} {} has zero capacity which is not compatible with legacy formula",
                universe_->getEntityName(scopeId_),
                universe_->getEntityName(scopeItemId)));
      }
      normCoef = 1 / capacity;
    }

    params.emplace_back(scopeItemId, threshold, normCoef);
  }

  std::vector<ConstraintInfo> result;
  result.reserve(scopeItemIds.size());

  co_await CoroUtils::runEachTaskAndUpdate<size_t>(
      0,
      params.size(),
      [&](size_t i) {
        const auto& [scopeItemId, threshold, normCoef] = params[i];
        return getConstraintsForScopeItem(
            expressionBuilder, metric, scopeItemId, threshold, normCoef);
      },
      [&](auto&& taskResult, size_t i) {
        auto& [constraints, isAtOrAboveDuringLimit] = taskResult;
        result.insert(result.end(), constraints.begin(), constraints.end());
        if (isAtOrAboveDuringLimit &&
            scopeItemsInitiallyAtOrAboveDuringLimit_.has_value()) {
          const auto& [scopeItemId, threshold, normCoef] = params[i];
          scopeItemsInitiallyAtOrAboveDuringLimit_->emplace(scopeItemId);
        }
      });
  co_return result;
}

entities::Set<entities::ContainerId>
CapacitySpecBuilder::nonAcceptingContainers() const {
  if (!scopeItemsInitiallyAtOrAboveDuringLimit_.has_value() ||
      scopeItemsInitiallyAtOrAboveDuringLimit_->empty()) {
    return {};
  }

  // Check if the dimension is NOT dynamic and has objects with zero value or is
  // routingConfigBased; if yes, then we cannot use
  // scopeItemsInitiallyAtOrAboveDuringLimit_ to figure out the non-accepting
  // containers. Dynamic dimensions are handled below
  auto& objectDimension = universe_->getObjects().getDimension(dimensionId_);
  if (objectDimension.isRoutingConfigBased() ||
      (!objectDimension.isDynamic() &&
       objectDimension.hasZeroValuedObjects())) {
    return {};
  }

  entities::Set<entities::ContainerId> nonAcceptingContainers;
  for (auto scopeItemId : *scopeItemsInitiallyAtOrAboveDuringLimit_) {
    auto& containerIds =
        universe_->getScope(scopeId_).getContainerIds(scopeItemId);
    if (containerIds.size() > 1 ||
        objectDimension.hasZeroValuedObjects(scopeItemId)) {
      // note that we cannot mark all containers in the scopeItem as
      // non-accepting since objects can move between containers of the same
      // scopeItem without changing the during utilization
      // also, if some object has zero dimension value w.r.t. this scopeitem,
      // then it cannot be marked as non-accepting
      continue;
    }
    nonAcceptingContainers.insert(containerIds.begin(), containerIds.end());
  }

  return nonAcceptingContainers;
}

SpecParameters CapacitySpecBuilder::getSpecInfo() const {
  return SpecParameters{
      .name = *spec_.name(),
      .scope = *spec_.scope(),
      .dimension = *spec_.dimension(),
      .definition = apache::thrift::util::enumNameSafe(*spec_.definition()),
      .boundType = apache::thrift::util::enumNameSafe(*spec_.bound()),
      .limitType = apache::thrift::util::enumNameSafe(*spec_.limit()->type()),
      .zeroAllowed = *spec_.zeroAllowed() ? "yes" : "no",
      .filterAllowListSize = spec_.filter()->itemsWhitelist()
          ? static_cast<int>(spec_.filter()->itemsWhitelist()->size())
          : 0,
      .filterBlockListSize = spec_.filter()->itemsBlacklist()
          ? static_cast<int>(spec_.filter()->itemsBlacklist()->size())
          : 0};
}

void CapacitySpecBuilder::initilizePerGroupUtilizationBounds(
    const interface::GroupUtilizationBound& bound) {
  auto partitionId = universe_->getPartitionId(*bound.partitionName());
  perGroupUtilizationBounds_ =
      LimitWrapper(*universe_, *bound.perGroupValues(), scopeId_, partitionId);
}

folly::coro::Task<ExprPtr> CapacitySpecBuilder::getBoundedUtil(
    ExpressionBuilder& expressionBuilder,
    UtilMetric metric,
    entities::ScopeItemId scopeItemId,
    const interface::GroupUtilizationBound& bound) const {
  const bool isUpperBound = *bound.boundType() == UtilizationBoundType::UPPER;
  auto partitionId = universe_->getPartitionId(*bound.partitionName());
  assert(perGroupUtilizationBounds_);
  std::optional<entities::ScopeId> aggregationScopeId = std::nullopt;
  if (auto scopeNameRef = bound.aggregationScope()) {
    aggregationScopeId = universe_->getScopeId(*scopeNameRef);
  }
  const auto defaultLimit = perGroupUtilizationBounds_->getLimit(scopeItemId);
  co_return co_await expressionBuilder.getBoundedAbsoluteUtil(
      metric,
      dimensionId_,
      scopeId_,
      scopeItemId,
      partitionId,
      perGroupUtilizationBounds_->getGroupsOverride(scopeItemId),
      isUpperBound,
      defaultLimit,
      aggregationScopeId);
}

void CapacitySpecBuilder::populateInvalidMoveFilter(
    InvalidMoveFilter& invalidMoveFilter) const {
  const auto def = *spec_.definition();
  if (*spec_.bound() != CapacitySpecBound::MAX ||
      (def != CapacitySpecDefinition::AFTER &&
       def != CapacitySpecDefinition::DURING &&
       def != CapacitySpecDefinition::DURING_AND_AFTER)) {
    return;
  }
  const auto& objDim = universe_->getObjects().getDimension(dimensionId_);
  if (objDim.isDynamic() || objDim.isRoutingConfigBased() ||
      objDim.size() != 1 || spec_.utilizationBound().has_value() ||
      objDim.only().hasNegativeValues()) {
    return;
  }

  const auto& scope = universe_->getScope(scopeId_);
  const auto& scalarDim = objDim.only();
  const auto& containers = universe_->getContainers();

  // Fast path for ABSOLUTE limits: non-zero global limit with no overrides
  // means no scope item can have a zero threshold.
  if (limits_.getType() == LimitType::ABSOLUTE &&
      limits_.onlyHasGlobalLimit() && limits_.getGlobalLimit() != 0.0) {
    return;
  }

  const auto isRelative = limits_.getType() == LimitType::RELATIVE;
  const auto* scopeDimension =
      isRelative ? &scope.getDimension(dimensionId_) : nullptr;
  const auto isAfter = def == CapacitySpecDefinition::AFTER;

  // For AFTER: threshold = the scope item's initial util L. An incoming object
  // with v <= L can be offset by moving existing objects out, so block only
  // v > L.
  // For DURING / DURING_AND_AFTER: any positive incoming value worsens the
  // constraint during transit, so block all v > 0 (threshold = 0). Objects
  // initially assigned to the scope item are exempt: they can move within the
  // scope item's containers without changing the DURING util.
  for (const auto& scopeItemId : scopeFilter_.getScopeItemIds()) {
    auto limit = limits_.getLimit(scopeItemId);
    if (isRelative) {
      limit *= scopeDimension->getValue(scopeItemId);
    }
    if (limit != 0.0) {
      continue;
    }

    const auto& containerIds = scope.getContainerIds(scopeItemId);
    double initialUtil = 0.0;
    entities::Set<entities::ObjectId> initiallyAssigned;
    for (const auto& cid : containerIds) {
      for (const auto& objectId : containers.getInitialObjectIds(cid)) {
        initialUtil += scalarDim.getValue(objectId);
        initiallyAssigned.insert(objectId);
      }
    }
    const double threshold = isAfter ? initialUtil : 0.0;

    for (const auto objectId : universe_->getObjects().getObjectIds()) {
      if (scalarDim.getValue(objectId) <= threshold ||
          initiallyAssigned.contains(objectId)) {
        continue;
      }
      for (const auto& cid : containerIds) {
        invalidMoveFilter.markInvalid(objectId, cid);
      }
    }
  }
}

} // namespace facebook::rebalancer::materializer
