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

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/Set.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/summary/GlobalLabeledObjectives.h"
#include "algopt/rebalancer/solver/summary/LabeledConstraints.h"
#include "algopt/rebalancer/solver/summary/metrics/Metrics.h"
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"

namespace facebook::rebalancer {

struct MaterializedProblem {
  explicit MaterializedProblem(const entities::Universe& universe)
      : globalObjective(GlobalObjective(universe)) {}
  // Context: depending on constraint policy settings, a user-provided
  // constraint may be broken into a pair of soft and hard constraints,
  // typically based on the initial assignment. Components of a constraint which
  // are initially broken will be treated as soft constraints or goals, while
  // initially unbroken components are treated as hard constraints.

  // The goal tuple to minimize. The lexicographically smallest is optimal. It
  // incorporates the user-provided goals and the soft components of
  // user-provided constraints.
  std::vector<ExprPtr> finalGoals;

  // The constraint to satisfy. A positive value indicates a constraint
  // violation. It incorporates the hard components of user-provided
  // constraints.
  ExprPtr finalConstraint;

  // Raw user-provided goals and constraints.
  entities::Map<entities::GoalId, ExprPtr> userGoals;
  entities::Map<entities::ConstraintId, ExprPtr> userConstraints;
  ExprPtr userConstraintSum;

  // User-provided constraints broken into soft and hard components.
  entities::Map<entities::ConstraintId, ExprPtr> softConstraints;
  entities::Map<entities::ConstraintId, ExprPtr> hardConstraints;

  // globalObjective, where the expression at each tuple position i is
  // finalGoals.at(i)
  GlobalObjective globalObjective;
  GlobalLabeledObjectives labeledObjectives;

  LabeledConstraints labeledHardConstraints;
  LabeledConstraints labeledUserConstraints;

  std::shared_ptr<Metrics> metrics = nullptr;

  // Store initial assignment which contains modification from spec
  entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
      updatedInitialAssignment;

  std::optional<std::vector<std::vector<entities::ContainerId>>>
      similarContainers;

  // Set of objects which break a constraint if moved from their initial
  // containers.
  entities::Set<entities::ObjectId> fixedObjects;

  // Set of containers which break a constraint if any objects are moved in or
  // out with respect to the initial assignment.
  entities::Set<entities::ContainerId> fixedContainers;

  // Set of containers which break a constraint if any objects are moved in with
  // respect to the initial assignment. Superset of fixedContainers.
  entities::Set<entities::ContainerId> nonAcceptingContainers;

  // Filter for skipping invalid (object, container) pairs before
  // full expression-tree evaluation. Built from constraint specs during
  // materialization. May be null if no constraints contribute invalid pairs.
  std::unique_ptr<const InvalidMoveFilter> invalidMoveFilter;
};

} // namespace facebook::rebalancer
