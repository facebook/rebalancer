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

#include "algopt/rebalancer/materializer/Materializer.h"

#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/treeprof/EventRecorder.h"

#include <folly/container/irange.h>
#include <folly/coro/BlockingWait.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include <algorithm>
#include <stdexcept>

using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::interface;

namespace facebook::rebalancer::materializer {

namespace {
void expandVecToIndex(
    int index,
    std::vector<ExprPtr>& vec,
    const entities::Universe& universe) {
  auto fromIndex = vec.size();
  for (int j = fromIndex; j < index + 1; ++j) {
    vec.emplace_back(const_expr(0, universe));
  }
}
void inplaceAddToVecIndex(
    int index,
    std::vector<ExprPtr>& vec,
    const ExprPtr& expr,
    const entities::Universe& universe) {
  expandVecToIndex(index, vec, universe);
  inplace_add(vec.at(index), expr);
}
} // namespace

const std::shared_ptr<const MaterializedProblem> Materializer::materialize(
    std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor,
    std::shared_ptr<const Universe> universe,
    bool continuousExpressions,
    std::shared_ptr<LogCollector> logger,
    bool shouldCollectMetrics,
    bool enableInvalidMoveFilter) {
  Materializer materializer(
      std::move(executor),
      std::move(universe),
      continuousExpressions,
      std::move(logger),
      shouldCollectMetrics,
      enableInvalidMoveFilter);
  return materializer.materialize();
}

Materializer::Materializer(
    std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor,
    std::shared_ptr<const entities::Universe> universe,
    bool continuousExpressions,
    std::shared_ptr<LogCollector> logger,
    bool shouldCollectMetrics,
    bool enableInvalidMoveFilter)
    : executor_(std::move(executor)),
      universe_(std::move(universe)),
      specBuilderFactory_(universe_, continuousExpressions, logger),
      logger_(std::move(logger)),
      materialized_(std::make_shared<MaterializedProblem>(*universe_)),
      metricsBuilder_(
          shouldCollectMetrics ? std::make_shared<Metrics::Builder>()
                               : nullptr) {
  if (enableInvalidMoveFilter) {
    materialized_.wlock()->invalidMoveFilter =
        std::make_unique<InvalidMoveFilter>(
            universe_->getObjects().getObjectIds().size(),
            universe_->getContainers().getContainerIds().size());
  }
}

std::shared_ptr<MaterializedProblem> Materializer::materialize() {
  auto updatesInInitialAssignment = getUpdatesInInitialAssignment();

  materialized_.withWLock([&](auto& wlockedMaterialized) {
    // Create updatedInitialAssignment using updates from spec(s)
    auto& containers = universe_->getContainers();
    for (auto containerId : containers.getContainerIds()) {
      // initialize updatedInitialAssignment for each containerId
      if (!wlockedMaterialized.updatedInitialAssignment.contains(containerId)) {
        wlockedMaterialized.updatedInitialAssignment[containerId] = {};
      }

      for (auto objectId : containers.getInitialObjectIds(containerId)) {
        auto dstContainerId = folly::get_default(
            updatesInInitialAssignment, objectId, containerId);
        wlockedMaterialized.updatedInitialAssignment[dstContainerId].push_back(
            objectId);
      }
    }

    wlockedMaterialized.similarContainers = getSimilarContainers();

    // Initialize the constraint.
    wlockedMaterialized.finalConstraint = any_positive({}, *universe_);
    wlockedMaterialized.userConstraintSum = const_expr(0, *universe_);
  });

  ExpressionBuilder expressionBuilder(
      universe_,
      materialized_.rlock()->updatedInitialAssignment,
      executor_,
      metricsBuilder_);

  algopt::treeprof::EventRecorder ctrMaterialization(
      "Materialize Constraints & Goals");
  const auto constraintIds = universe_->getConstraints().getConstraintIds();
  const auto goalIds = universe_->getGoals().getGoalIds();

  std::vector<folly::coro::Task<void>> tasks;
  tasks.reserve(constraintIds.size() + goalIds.size());
  for (auto constraintId : constraintIds) {
    tasks.emplace_back(
        materializeConstraintCoro(expressionBuilder, constraintId));
  }
  for (auto goalId : goalIds) {
    tasks.emplace_back(materializeGoalCoro(expressionBuilder, goalId));
  }
  folly::coro::blockingWait(
      CoroUtils::runEachTask(
          tasks.begin(),
          tasks.end(),
          [&](auto iter) { return std::move(*iter); },
          executor_));

  ctrMaterialization.stop();

  // The computation of fixed containers depends on fixed objects being known.
  // Since fixed objects are computed as part of materializeConstraints, it's
  // important for collectFixedContainers to be invoked after.
  collectFixedContainers();

  buildLabeledConstraints();

  buildGlobalObjectiveAndLabeledObjectives();

  if (metricsBuilder_) {
    materialized_.wlock()->metrics =
        std::make_shared<Metrics>(metricsBuilder_->build(universe_));
  }

  // Reset empty filter to null so hot-loop pointer checks short-circuit.
  materialized_.withWLock([](auto& wlockedMaterialized) {
    if (wlockedMaterialized.invalidMoveFilter &&
        wlockedMaterialized.invalidMoveFilter->empty()) {
      wlockedMaterialized.invalidMoveFilter.reset();
    }
  });

  return *materialized_.wlockPointer();
}

Map<ObjectId, ContainerId> Materializer::getUpdatesInInitialAssignment() {
  /* Parse specs and check if any updates if required in initial assignment */
  Map<ObjectId, ContainerId> updatesInInitialAssignment;
  auto& constraints = universe_->getConstraints();
  for (auto constraintId : constraints.getConstraintIds()) {
    const auto& constraint =
        universe_->getConstraints().getConstraint(constraintId);
    auto specBuilder = specBuilderFactory_.getBuilder(constraint.getSpec());
    auto updates = specBuilder->getUpdatesInInitialAssignment();

    for (auto& [objectId, containerId] : updates) {
      auto existingOverride = folly::get_ptr(updates, objectId);
      if (existingOverride && *existingOverride != containerId) {
        throw std::runtime_error(
            fmt::format(
                "object {} cannot be fixed to {} as it is already fixed to {}. Object can be fixed to only one container. Please fix required constraints",
                universe_->getEntityName(objectId),
                universe_->getEntityName(containerId),
                universe_->getEntityName(*existingOverride)));
      } else {
        updatesInInitialAssignment.emplace(objectId, containerId);
      }
    }
  }
  return updatesInInitialAssignment;
}

folly::coro::Task<void> Materializer::materializeConstraintCoro(
    ExpressionBuilder& expressionBuilder,
    entities::ConstraintId constraintId) {
  auto& constraint = universe_->getConstraints().getConstraint(constraintId);
  auto specBuilder = specBuilderFactory_.getBuilder(constraint.getSpec());
  auto specType =
      apache::thrift::util::enumNameSafe(constraint.getSpec().getType());
  auto description =
      fmt::format("{}: {}", specType, specBuilder->description());

  algopt::treeprof::EventRecorder materializeCtr(
      fmt::format("[constraint] {}", description));
  const algopt::Timer timer(true);

  auto userConstraint = const_expr(0, *universe_);
  ExprPtr softConstraint;
  auto hardConstraint = any_positive({}, *universe_);

  auto constraints = co_await specBuilder->constraints(expressionBuilder);

  // Sort by expression memory address for better cache locality
  // when expressions were allocated from multiple threads
  std::sort(
      constraints.begin(),
      constraints.end(),
      [](const ConstraintInfo& a, const ConstraintInfo& b) {
        return a.constraintExpr.get() < b.constraintExpr.get();
      });

  // TODO: The loop below is quite expensive when there are many expressions in
  // components (since they take significant time to initialize, etc.).
  // Parellelize it after making Context thread-safe.
  for (auto& constraintInfo : constraints) {
    userConstraint += max(0, constraintInfo.constraintExpr);

    auto [hardComponent, softComponent] = splitConstraintComponent(
        expressionBuilder, constraint, constraintInfo, universe_);

    if (softComponent != nullptr) {
      if (softConstraint == nullptr) {
        softConstraint = const_expr(0, *universe_);
      }
      inplace_add(softConstraint, softComponent);
    }

    if (hardComponent != nullptr) {
      inplace_any_positive(hardConstraint, hardComponent);
    }
  }

  // Initially broken constraints are softened and minimized as a goal in
  // the first item in the goals tuple.
  if (softConstraint) {
    softConstraint->description =
        fmt::format("initially broken {}", description);
    materialized_.withWLock([&](auto& wlockedMaterialized) {
      inplaceAddToVecIndex(
          constraint.getTupleIndex(),
          wlockedMaterialized.finalGoals,
          softConstraint,
          *universe_);
      wlockedMaterialized.softConstraints.emplace(constraintId, softConstraint);
    });

    softConstraint->setSpecAnnotation(specType);
  }

  hardConstraint->setSpecAnnotation(specType);
  userConstraint->setSpecAnnotation(specType);
  userConstraint->description = description;
  hardConstraint->description = description;
  auto fixedObjects = specBuilder->fixedObjects();
  auto nonAcceptingContainers = specBuilder->nonAcceptingContainers();

  materialized_.withWLock([&](auto& wlockedMaterialized) {
    any_positive_add(wlockedMaterialized.finalConstraint, hardConstraint);
    wlockedMaterialized.userConstraints.emplace(constraintId, userConstraint);
    inplace_add(
        wlockedMaterialized.userConstraintSum, std::move(userConstraint));
    wlockedMaterialized.hardConstraints.emplace(constraintId, hardConstraint);
    wlockedMaterialized.fixedObjects.insert(
        std::move_iterator(fixedObjects.begin()),
        std::move_iterator(fixedObjects.end()));
    wlockedMaterialized.nonAcceptingContainers.insert(
        std::move_iterator(nonAcceptingContainers.begin()),
        std::move_iterator(nonAcceptingContainers.end()));
    if (wlockedMaterialized.invalidMoveFilter &&
        constraint.getPolicy() != interface::ConstraintPolicy::SOFT) {
      specBuilder->populateInvalidMoveFilter(
          *wlockedMaterialized.invalidMoveFilter);
    }
  });

  logger_->log(
      SpecUsageInfo{
          .specType = std::move(specType),
          .specParameters = specBuilder->getSpecInfo(),
          .usedAs = "constraint",
          .materializationTime = timer.getSeconds(),
      });
  materializeCtr.stop();
  co_return;
}

SplitConstraint Materializer::splitConstraintComponent(
    ExpressionBuilder& expressionBuilder,
    const entities::Constraint& constraint,
    const ConstraintInfo& constraintInfo,
    std::shared_ptr<const entities::Universe> universe) {
  auto& constraintExpr = constraintInfo.constraintExpr;
  switch (constraint.getPolicy()) {
    case ConstraintPolicy::DEFAULT: {
      const double initialValue = constraintExpr->getInitialValue();
      const bool initiallyBroken =
          universe->getPrecision().compare(initialValue, 0) == 1;
      return !initiallyBroken
          ? SplitConstraint{.hardComponent = constraintExpr, .softComponent = nullptr}
          : SplitConstraint{
                .hardComponent = getViolationBeyondInitial(
                    constraintExpr,
                    initialValue,
                    expressionBuilder.getInitialAssignment()),
                .softComponent =
                    getSoftenedConstraint(constraintInfo, constraint)};
    }
    case ConstraintPolicy::HARD: {
      return {.hardComponent = constraintExpr, .softComponent = nullptr};
    }
    case ConstraintPolicy::SOFT:
      return {
          .hardComponent = nullptr,
          .softComponent = getSoftenedConstraint(constraintInfo, constraint)};
    default:
      throw std::runtime_error("Unhandled ConstraintPolicy");
  }
}

folly::coro::Task<void> Materializer::materializeGoalCoro(
    ExpressionBuilder& expressionBuilder,
    entities::GoalId goalId) {
  auto& goal = universe_->getGoals().getGoal(goalId);

  auto specBuilder = specBuilderFactory_.getBuilder(goal.getSpec());
  auto specType = apache::thrift::util::enumNameSafe(goal.getSpec().getType());
  auto description =
      fmt::format("{}: {}", specType, specBuilder->description());

  algopt::treeprof::EventRecorder materializeGoal(
      fmt::format("[goal] {}", description));
  const algopt::Timer timer(true);

  auto expression = co_await specBuilder->goalCoro(expressionBuilder);

  expression->setSpecAnnotation(specType);

  double weight = goal.getWeight();
  if (weight == 1) {
    expression->description = description;
  } else {
    expression *= weight;
    expression->description = fmt::format("{} * ({})", weight, description);
  }

  materialized_.withWLock([&](auto& wlockedMaterialized) {
    inplaceAddToVecIndex(
        goal.getTupleIndex(),
        wlockedMaterialized.finalGoals,
        expression,
        *universe_);
    wlockedMaterialized.userGoals.emplace(goalId, expression);
  });

  logger_->log(
      SpecUsageInfo{
          .specType = std::move(specType),
          .specParameters = specBuilder->getSpecInfo(),
          .usedAs = "goal",
          .materializationTime = timer.getSeconds(),
      });
  materializeGoal.stop();
  co_return;
}

ExprPtr Materializer::getSoftenedConstraint(
    const ConstraintInfo& constraintInfo,
    const entities::Constraint& constraint) {
  auto invalidCost = constraint.getInvalidCost();
  auto invalidState = constraint.getInvalidState();
  auto weightedPenalty =
      invalidCost * SpecBuilder::getConstraintViolation(constraintInfo);

  if (invalidState == 0) {
    // TODO: it should be trivial to implement this optimization at the
    // expression level
    return weightedPenalty;
  }

  return invalidState * step(constraintInfo.constraintExpr) +
      std::move(weightedPenalty);
}

ExprPtr Materializer::getViolationBeyondInitial(
    ExprPtr constraint,
    double initialValue,
    const Assignment& initialAssignment) {
  // ObjectPartitionLookup requires complex handling in order to treat groups
  // independently as they are all conflated under the same expression.
  auto objectPartitionLookup =
      std::dynamic_pointer_cast<ObjectPartitionLookupDefault>(constraint);
  if (objectPartitionLookup != nullptr) {
    return objectPartitionLookup->get_do_not_make_worse_copy(initialAssignment);
  }

  return std::move(constraint) - initialValue;
}

std::optional<std::vector<std::vector<ContainerId>>>
Materializer::getSimilarContainers() {
  return universe_->getSimilarContainers();
}

void Materializer::buildLabeledConstraints() {
  materialized_.withWLock([&](auto& wlockedMaterialized) {
    // build labeledHardConstraints
    wlockedMaterialized.labeledHardConstraints.setRoot(
        wlockedMaterialized.finalConstraint);
    for (auto& [constraintId, expr] : wlockedMaterialized.hardConstraints) {
      wlockedMaterialized.labeledHardConstraints.addSingle(
          universe_->getEntityName(constraintId), expr);
    }

    // build labeledUserConstraints
    wlockedMaterialized.labeledUserConstraints.setRoot(
        wlockedMaterialized.userConstraintSum);
    for (auto& [constraintId, expr] : wlockedMaterialized.userConstraints) {
      wlockedMaterialized.labeledUserConstraints.addSingle(
          universe_->getEntityName(constraintId), expr);
    }
  });
}

void Materializer::buildGlobalObjectiveAndLabeledObjectives() {
  // build globalObjective
  GlobalObjective::Builder globalObjectiveBuilder;
  // build labeled objectives
  GlobalLabeledObjectives::Builder globalLabeledObjectivesBuilder;
  materialized_.withWLock([&](auto& wlockedMaterialized) {
    for (const auto pos :
         folly::irange(wlockedMaterialized.finalGoals.size())) {
      auto& goalExpr = wlockedMaterialized.finalGoals.at(pos);
      globalObjectiveBuilder.setObjective(pos, goalExpr);
    }
    wlockedMaterialized.globalObjective =
        globalObjectiveBuilder.build(*universe_);

    // if there are no userGoals or softConstraints, then just set a
    // labeledObjective with root to be const_expr(0, *universe_) as
    // finalGoals(0) still exists with expr = const_expr(0, *universe_)
    if (wlockedMaterialized.softConstraints.size() == 0 &&
        wlockedMaterialized.userGoals.size() == 0) {
      globalLabeledObjectivesBuilder.setRoot(0, const_expr(0, *universe_));
    }
  });

  auto& userGoals = materialized_.rlock()->userGoals;
  auto& softConstraints = materialized_.rlock()->softConstraints;
  for (auto& [goalId, expr] : userGoals) {
    auto& goal = universe_->getGoals().getGoal(goalId);
    auto& name = universe_->getEntityName(goalId);
    globalLabeledObjectivesBuilder.addSingle(
        goal.getTupleIndex(), name, expr, goal.getWeight());
  }

  for (auto& [constraintId, expr] : softConstraints) {
    const int tuplePos =
        universe_->getConstraints().getConstraint(constraintId).getTupleIndex();
    auto& name = universe_->getEntityName(constraintId);
    globalLabeledObjectivesBuilder.addSingle(
        tuplePos, name, expr, 1 /* weight */);
  }

  materialized_.withWLock([&](auto& wlockedMaterialized) {
    wlockedMaterialized.labeledObjectives =
        globalLabeledObjectivesBuilder.build(
            wlockedMaterialized.globalObjective);
  });
}

void Materializer::collectFixedContainers() {
  // Given a set of fixed objects, compute a set of containers which break a
  // constraint if any objects are moved in or out with respect to the initial
  // assignment. This is determined by looking at the set of nonAcceting
  // containers and seeing if all the initial objects in it are fixed.
  auto& containers = universe_->getContainers();
  materialized_.withWLock([&](auto& wlockedMaterialized) {
    auto& fixedObjects = wlockedMaterialized.fixedObjects;
    auto& nonAcceptingContainers = wlockedMaterialized.nonAcceptingContainers;

    for (auto containerId : nonAcceptingContainers) {
      bool allObjectsFixed = true;
      auto& objectIds = containers.getInitialObjectIds(containerId);
      for (auto objectId : objectIds) {
        if (!fixedObjects.contains(objectId)) {
          allObjectsFixed = false;
          break;
        }
      }

      if (allObjectsFixed) {
        wlockedMaterialized.fixedContainers.insert(containerId);
      }
    }
  });
}

} // namespace facebook::rebalancer::materializer
