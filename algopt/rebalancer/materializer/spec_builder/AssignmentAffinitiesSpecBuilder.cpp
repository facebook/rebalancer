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

#include "algopt/rebalancer/materializer/spec_builder/AssignmentAffinitiesSpecBuilder.h"

#include "algopt/rebalancer/entities/ObjectStaticDimension.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"

using namespace facebook::rebalancer::entities;

namespace facebook::rebalancer::materializer {
namespace {

const ObjectScalarDimension* getAffinityDimension(
    const Universe& universe,
    const interface::AssignmentAffinitiesSpec& spec,
    ScopeId scopeId) {
  if (!spec.dimension().has_value()) {
    return nullptr;
  }

  if (!spec.affinities()->empty()) {
    throw std::runtime_error(
        "AssignmentAffinitiesSpec cannot specify both affinities and a dimension");
  }
  const auto dimensionId = universe.getDimensionId(*spec.dimension());
  const auto& objectDimension = universe.getObjects().getDimension(dimensionId);
  if (objectDimension.size() != 1) {
    throw std::runtime_error(
        fmt::format(
            "AssignmentAffinitiesSpec dimension '{}' must be scalar, but has size {}",
            *spec.dimension(),
            objectDimension.size()));
  }

  const auto& dimension = objectDimension.at(0);
  if (dimension.isRoutingConfigBased()) {
    throw std::runtime_error(
        fmt::format(
            "AssignmentAffinitiesSpec dimension '{}' cannot be routing-config-based",
            *spec.dimension()));
  }
  if (dimension.isDynamic() && dimension.getScopeId() != scopeId) {
    throw std::runtime_error(
        fmt::format(
            "AssignmentAffinitiesSpec dimension '{}' must use the same scope '{}' as the spec",
            *spec.dimension(),
            universe.getEntityName(scopeId)));
  }
  if (dimension.getDefaultValue() != 0) {
    throw std::runtime_error(
        fmt::format(
            "AssignmentAffinitiesSpec dimension '{}' must have default value 0, but has {}",
            *spec.dimension(),
            dimension.getDefaultValue()));
  }
  return &dimension;
}

} // namespace

struct AssignmentAffinitiesSpecBuilder::AffinityPenalties {
  double maxAffinitySum;
  ObjectIdToDoubleMap objectIdToMaxAffinity;
  Map<ScopeItemId, ObjectIdToDoubleMap> scopeItemIdToObjectIdToPenalty;
};

AssignmentAffinitiesSpecBuilder::AssignmentAffinitiesSpecBuilder(
    std::shared_ptr<const Universe> universe,
    interface::AssignmentAffinitiesSpec spec)
    : SpecBuilder(std::move(universe)),
      spec_(std::move(spec)),
      scopeId_(universe_->getScopeId(
          spec_.scope()->empty() ? universe_->getContainerTypeName()
                                 : *spec_.scope())),
      dimension_(getAffinityDimension(*universe_, spec_, scopeId_)) {}

folly::coro::Task<ExprPtr> AssignmentAffinitiesSpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  auto penalties = dimension_ ? buildPenaltiesFromDimension()
                              : buildPenaltiesFromAffinityList();

  // Choice of implementation: assignment affinities are implemented as a
  // penalty contributed to the formula by objects assigned to sub-optimal
  // containers. With this implementation, containers with unhappy objects
  // will have a non-zero potential (value minus lower bound), and the
  // local search heuristic to select source containers will pick them first.
  auto result = const_expr(0, *universe_);
  for (auto& [scopeItemId, objectIdToPenalty] :
       penalties.scopeItemIdToObjectIdToPenalty) {
    const ObjectStaticDimension objectDimension(std::move(objectIdToPenalty));
    result += co_await expressionBuilder.getAbsoluteUtil(
        UtilMetric::AFTER, objectDimension, scopeId_, scopeItemId);
  }

  // Objects have an affinity of zero to being placed outside of the scope.
  // Following the formula above (penalty = maxAffinity - affinity), the
  // penalty paid by objects outside of the scope is maxAffinity.
  const ObjectStaticDimension outOfScopeObjectPenalties(
      std::move(penalties.objectIdToMaxAffinity));
  auto outOfScopePenalty = expressionBuilder.getAbsoluteUtilOutOfScope(
      UtilMetric::AFTER, outOfScopeObjectPenalties, scopeId_);
  result += std::move(outOfScopePenalty);

  // Legacy constant adjustment.
  result -= penalties.maxAffinitySum;
  co_return result;
}

AssignmentAffinitiesSpecBuilder::AffinityPenalties
AssignmentAffinitiesSpecBuilder::buildPenaltiesFromAffinityList() const {
  Map<ObjectId, Map<ScopeItemId, double>> objectItemAffinity;

  for (const auto& affinity : *spec_.affinities()) {
    const auto objectId = universe_->getObjectId(*affinity.objectName());
    const auto scopeItemId =
        universe_->getScopeItemId(scopeId_, *affinity.scopeItemName());
    objectItemAffinity[objectId][scopeItemId] += *affinity.affinity();
  }

  const auto numObjects = universe_->getNumObjects();
  double maxAffinitySum = 0;
  ObjectIdToDoubleMap objectMaxAffinity(
      numObjects,
      /*defaultValue=*/0.0,
      /*expectedNonDefaultSize=*/objectItemAffinity.size());

  const auto& scopeItemIds = universe_->getScope(scopeId_).getScopeItemIds();
  Map<ScopeItemId, ObjectIdToDoubleMap> itemObjectPenalty;
  itemObjectPenalty.reserve(
      static_cast<decltype(itemObjectPenalty)::size_type>(scopeItemIds.size()));
  for (const auto& [objectId, itemAffinity] : objectItemAffinity) {
    double maxAffinity = 0;
    for (const auto& [_, affinity] : itemAffinity) {
      maxAffinity = std::max(maxAffinity, affinity);
    }

    maxAffinitySum += maxAffinity;

    for (const auto scopeItemId : scopeItemIds) {
      const double affinity = folly::get_default(itemAffinity, scopeItemId, 0);
      auto [it, _] = itemObjectPenalty.try_emplace(
          scopeItemId,
          numObjects,
          /*defaultValue=*/0.0,
          /*expectedNonDefaultSize=*/objectItemAffinity.size());
      it->second.emplace(objectId, maxAffinity - affinity);
    }

    objectMaxAffinity.emplace(objectId, maxAffinity);
  }

  return AffinityPenalties{
      .maxAffinitySum = maxAffinitySum,
      .objectIdToMaxAffinity = std::move(objectMaxAffinity),
      .scopeItemIdToObjectIdToPenalty = std::move(itemObjectPenalty)};
}

AssignmentAffinitiesSpecBuilder::AffinityPenalties
AssignmentAffinitiesSpecBuilder::buildPenaltiesFromDimension() const {
  const auto numObjects = universe_->getNumObjects();
  const auto& scopeItemIds = universe_->getScope(scopeId_).getScopeItemIds();
  Map<ObjectId, double> objectIdToMaxAffinity;
  for (const auto scopeItemId : scopeItemIds) {
    dimension_->values(scopeItemId)
        .forEachNonDefault([&](ObjectId objectId, double affinity) {
          auto& maxAffinity = objectIdToMaxAffinity[objectId];
          maxAffinity = std::max(maxAffinity, affinity);
        });
  }

  const auto numAffinityObjects = objectIdToMaxAffinity.size();
  double maxAffinitySum = 0;
  for (const auto& [_, maxAffinity] : objectIdToMaxAffinity) {
    maxAffinitySum += maxAffinity;
  }

  Map<ScopeItemId, ObjectIdToDoubleMap> scopeItemIdToObjectIdToPenalty;
  scopeItemIdToObjectIdToPenalty.reserve(
      static_cast<decltype(scopeItemIdToObjectIdToPenalty)::size_type>(
          scopeItemIds.size()));
  if (!objectIdToMaxAffinity.empty()) {
    for (const auto scopeItemId : scopeItemIds) {
      const auto& objectIdToAffinity = dimension_->values(scopeItemId);
      ObjectIdToDoubleMap objectIdToPenalty(
          numObjects,
          /*defaultValue=*/0.0,
          /*expectedNonDefaultSize=*/numAffinityObjects);
      for (const auto& [objectId, maxAffinity] : objectIdToMaxAffinity) {
        objectIdToPenalty.emplace(
            objectId,
            maxAffinity - objectIdToAffinity.getObjectValue(objectId));
      }
      scopeItemIdToObjectIdToPenalty.emplace(
          scopeItemId, std::move(objectIdToPenalty));
    }
  }

  return AffinityPenalties{
      .maxAffinitySum = maxAffinitySum,
      .objectIdToMaxAffinity = ObjectIdToDoubleMap(
          std::move(objectIdToMaxAffinity),
          /*defaultValue=*/0.0,
          numObjects),
      .scopeItemIdToObjectIdToPenalty =
          std::move(scopeItemIdToObjectIdToPenalty)};
}

folly::coro::Task<std::vector<ConstraintInfo>>
AssignmentAffinitiesSpecBuilder::constraints(
    ExpressionBuilder& /* expressionBuilder */) const {
  throw std::runtime_error("not supported as a constraint");
}

std::string AssignmentAffinitiesSpecBuilder::description() const {
  return fmt::format(
      "Assignment affinities of {} to {}",
      universe_->getObjectTypeName(),
      *spec_.scope());
}

SpecParameters AssignmentAffinitiesSpecBuilder::getSpecInfo() const {
  std::size_t affinityCount = spec_.affinities()->size();
  if (dimension_) {
    const auto& scopeItemIds = universe_->getScope(scopeId_).getScopeItemIds();
    if (dimension_->isDynamic()) {
      affinityCount = 0;
      for (const auto scopeItemId : scopeItemIds) {
        affinityCount += dimension_->values(scopeItemId).nonDefaultCount();
      }
    } else {
      affinityCount =
          dimension_->values().nonDefaultCount() * scopeItemIds.size();
    }
  }
  return SpecParameters{
      .name = *spec_.name(),
      .scope = *spec_.scope(),
      .size = static_cast<int>(affinityCount)};
}

} // namespace facebook::rebalancer::materializer
