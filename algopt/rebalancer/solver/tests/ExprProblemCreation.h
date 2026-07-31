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

#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/summary/GlobalLabeledObjectives.h"
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"
#include "algopt/rebalancer/solver/utils/Problem.h"

#include <folly/container/irange.h>

#include <utility>

namespace facebook::rebalancer::packer::tests {

inline LabeledConstraints makeLabeledHardConstraints(
    const ExprPtr& constraint) {
  LabeledConstraints labeledHardConstraints;
  labeledHardConstraints.setRoot(constraint);
  labeledHardConstraints.addSingle("Constraint", constraint);
  return labeledHardConstraints;
}

inline GlobalLabeledObjectives makeGlobalLabeledObjectives(
    const GlobalObjective& globalObjective) {
  GlobalLabeledObjectives::Builder builder;
  builder.setRoot(0, globalObjective.getObjectiveAt(0));
  for (const auto i : folly::irange(globalObjective.size())) {
    auto obj = globalObjective.getObjectiveAt(i);
    builder.addSingle(i, fmt::format("Objective {}", i), obj);
  }
  return builder.build(globalObjective);
}

inline GlobalObjective makeGlobalObjective(
    const std::vector<ExprPtr>& objectiveTuple,
    const entities::Universe& universe) {
  GlobalObjective::Builder builder;
  for (const auto i : folly::irange(objectiveTuple.size())) {
    builder.addToObjective((int)i, objectiveTuple[i], universe);
  }
  return builder.build(universe);
}

inline std::unique_ptr<Problem> createTestProblem(
    std::shared_ptr<const entities::Universe> universe,
    const std::vector<ExprPtr>& objectiveTuple,
    ExprPtr constraint,
    PackerSet<entities::ContainerId> nonAcceptingContainers = {},
    ProblemConfigs config = {},
    bool performInitialFullApply = true,
    bool enableParallelizedBoundsComputing = false,
    std::unique_ptr<InvalidMoveFilter> invalidMoveFilter = nullptr) {
  auto materializedProblem = std::make_shared<MaterializedProblem>(*universe);
  materializedProblem->globalObjective =
      makeGlobalObjective(objectiveTuple, *universe);
  materializedProblem->labeledObjectives =
      makeGlobalLabeledObjectives(materializedProblem->globalObjective);
  materializedProblem->finalConstraint = std::move(constraint);
  materializedProblem->labeledHardConstraints =
      makeLabeledHardConstraints(materializedProblem->finalConstraint);

  const auto& assignment = universe->getContainers().getInitialAssignment();

  materializedProblem->updatedInitialAssignment = assignment;
  materializedProblem->nonAcceptingContainers =
      std::move(nonAcceptingContainers);
  materializedProblem->invalidMoveFilter = std::move(invalidMoveFilter);

  // by default none of the containers are similar
  std::vector<std::vector<entities::ContainerId>> defaultSimilarContainers;
  for (auto& [containerId, _] : assignment) {
    defaultSimilarContainers.push_back({containerId});
  }
  materializedProblem->similarContainers = defaultSimilarContainers;

  if (performInitialFullApply) {
    Context context;
    const TopToBottomEvaluator evaluator(context);
    const Assignment initialAssignment(assignment);
    materializedProblem->globalObjective.fullApply(
        evaluator, initialAssignment);
    materializedProblem->finalConstraint->fullApply(
        evaluator, initialAssignment);

    for (auto& labeled_constraint :
         materializedProblem->labeledHardConstraints) {
      labeled_constraint->expression->fullApply(evaluator, initialAssignment);
    }
    for (auto& tupleItem : materializedProblem->labeledObjectives) {
      for (auto& labeledObjective : tupleItem) {
        labeledObjective->expression->fullApply(evaluator, initialAssignment);
      }
    }
  }

  // use dynamicObjectOrdering
  config.useDynamicObjectOrdering = true;
  config.validateAppliedMoves = true;
  config.enableParallelizedBoundsComputing = enableParallelizedBoundsComputing;
  return std::make_unique<Problem>(universe, materializedProblem, config);
}

} // namespace facebook::rebalancer::packer::tests
