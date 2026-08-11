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

#include "algopt/rebalancer/common/log/LogCollector.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/materializer/spec_builder/SpecBuilderFactory.h"
#include "algopt/rebalancer/materializer/utils/ExpressionBuilder.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/utils/MaterializedProblem.h"
#include "algopt/rebalancer/treeprof/ExecutorWrapper.h"

#include <folly/SynchronizedPtr.h>

namespace facebook::rebalancer::materializer {

struct SplitConstraint {
  ExprPtr hardComponent;
  ExprPtr softComponent;
};

class Materializer {
 public:
  // if no logger is provided, uses the default no-op logger
  static const std::shared_ptr<const MaterializedProblem> materialize(
      std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor,
      std::shared_ptr<const entities::Universe> universe,
      bool continuousExpressions,
      std::shared_ptr<LogCollector> logger = std::make_shared<LogCollector>(),
      bool shouldCollectMetrics = false,
      bool enableInvalidMoveFilter = false);

 private:
  explicit Materializer(
      std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor,
      std::shared_ptr<const entities::Universe> universe,
      bool continuousExpressions,
      std::shared_ptr<LogCollector> logger,
      bool shouldCollectMetrics = false,
      bool enableInvalidMoveFilter = false);

  std::shared_ptr<MaterializedProblem> materialize();

  folly::coro::Task<void> materializeConstraintCoro(
      ExpressionBuilder& expressionBuilder,
      entities::ConstraintId constraintId);

  static SplitConstraint splitConstraintComponent(
      ExpressionBuilder& expressionBuilder,
      const entities::Constraint& constraint,
      const ConstraintInfo& constraintInfo,
      std::shared_ptr<const entities::Universe> universe);

  folly::coro::Task<void> materializeGoalCoro(
      ExpressionBuilder& expressionBuilder,
      entities::GoalId goalId);

  static ExprPtr getSoftenedConstraint(
      const ConstraintInfo& constraintInfo,
      const entities::Constraint& constraint);

  static ExprPtr getViolationBeyondInitial(
      ExprPtr constraint,
      double initialValue,
      const Assignment& initialAssignment);

  entities::Map<entities::ObjectId, entities::ContainerId>
  getUpdatesInInitialAssignment();

  std::optional<std::vector<std::vector<entities::ContainerId>>>
  getSimilarContainers();

  void buildLabeledConstraints();

  void buildGlobalObjectiveAndLabeledObjectives();

  void collectFixedContainers();

 private:
  std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor_;
  std::shared_ptr<const entities::Universe> universe_;
  SpecBuilderFactory specBuilderFactory_;
  std::shared_ptr<LogCollector> logger_;
  folly::SynchronizedPtr<std::shared_ptr<MaterializedProblem>> materialized_;
  std::shared_ptr<Metrics::Builder> metricsBuilder_;
};

} // namespace facebook::rebalancer::materializer
