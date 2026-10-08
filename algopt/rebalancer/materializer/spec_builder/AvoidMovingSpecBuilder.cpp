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

#include "algopt/rebalancer/materializer/spec_builder/AvoidMovingSpecBuilder.h"

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"

using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::interface;

namespace facebook::rebalancer::materializer {

AvoidMovingSpecBuilder::AvoidMovingSpecBuilder(
    std::shared_ptr<const Universe> universe,
    interface::AvoidMovingSpec spec)
    : SpecBuilder(std::move(universe)), spec_(std::move(spec)) {}

folly::coro::Task<ExprPtr> AvoidMovingSpecBuilder::goalCoro(
    ExpressionBuilder& /* expressionBuilder */) const {
  throw std::runtime_error("AvoidMovingSpec not supported as a goal");
}

static ExprPtr getStayedObjectCountExpr(
    ExpressionBuilder& expressionBuilder,
    ContainerId containerId,
    const std::vector<ObjectId>& objectIds,
    const Universe& universe) {
  // When there are most two objects, experiments show it is not worth (both in
  // terms of time and memory) to create an objectLookup. So handling that
  // separately.
  if (objectIds.size() <= 2) {
    auto stayedObjectCount = const_expr(0, universe);
    for (auto objectId : objectIds) {
      inplace_add(
          stayedObjectCount,
          expressionBuilder.isAssigned(containerId, objectId));
    }
    return stayedObjectCount;
  }

  // Create an object vector where every object in objectIds has value 1,
  // everything else has value 0
  const auto numObjects = universe.getNumObjects();
  auto objectIdToValue = std::make_shared<entities::ObjectIdToDoubleMap>(
      numObjects, /*defaultValue=*/0.0, objectIds.size());
  for (auto objectId : objectIds) {
    objectIdToValue->emplace(objectId, 1);
  }
  auto objVector = object_vector(std::move(objectIdToValue), universe);

  return object_lookup(
      objVector,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{containerId}),
      expressionBuilder.getUpdatedInitialAssignment());
}

folly::coro::Task<std::vector<ConstraintInfo>>
AvoidMovingSpecBuilder::constraints(
    ExpressionBuilder& expressionBuilder) const {
  entities::Map<entities::ObjectId, entities::ContainerId> initialAssignment;
  for (auto containerId : universe_->getContainers().getContainerIds()) {
    for (auto objectId :
         universe_->getContainers().getInitialObjectIds(containerId)) {
      initialAssignment.emplace(objectId, containerId);
    }
  }

  // process moveInProgressSpec to allow objects of it to move
  entities::Set<entities::ObjectId> objectsAllowedToMove;
  for (auto constraintId : universe_->getConstraints().getConstraintIds()) {
    auto& constraint = universe_->getConstraints().getConstraint(constraintId);
    auto& spec = constraint.getSpec();
    if (spec.getType() == ConstraintSpecs::Type::movesInProgressSpec) {
      for (const auto& move : *spec.movesInProgressSpec()->moves()) {
        auto objectId = universe_->getObjectId(*move.objName());
        objectsAllowedToMove.insert(objectId);
      }
    }
  }

  int totalObjectCount = 0;
  Map<ContainerId, std::vector<ObjectId>> relevantContainerToObjects;
  for (auto& objectName : *spec_.objects()) {
    auto objectId = universe_->getObjectId(objectName);
    if (objectsAllowedToMove.contains(objectId)) {
      // if an object is mentioned both in "avoid moving" and "moves in
      // progress", the latter takes precedence
      continue;
    }

    auto containerId = initialAssignment.at(objectId);
    relevantContainerToObjects[containerId].push_back(objectId);

    ++totalObjectCount;
  }

  // Final desired constraint expression is N - S <= 0, where N =
  // totalObjectCount and S = the total number of objects of interest that stay
  // in their respective initial containers
  auto constraint = const_expr(totalObjectCount, *universe_);
  const std::vector<ExprPtr> constraints;
  for (auto& [containerId, objectIds] : relevantContainerToObjects) {
    constraint -= getStayedObjectCountExpr(
        expressionBuilder, containerId, objectIds, *universe_);
  }

  co_return std::vector<ConstraintInfo>{ConstraintInfo(constraint)};
}

std::string AvoidMovingSpecBuilder::description() const {
  return fmt::format("Avoid moving {} objects", spec_.objects()->size());
}

entities::Set<entities::ObjectId> AvoidMovingSpecBuilder::fixedObjects() const {
  entities::Set<entities::ObjectId> fixedObjects;

  for (auto& objectName : *spec_.objects()) {
    auto objectId = universe_->getObjectId(objectName);
    fixedObjects.insert(objectId);
  }

  return fixedObjects;
}

SpecParameters AvoidMovingSpecBuilder::getSpecInfo() const {
  return SpecParameters{
      .name = *spec_.name(), .size = static_cast<int>(spec_.objects()->size())};
}

} // namespace facebook::rebalancer::materializer
