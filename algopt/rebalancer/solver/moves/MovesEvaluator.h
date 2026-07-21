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

#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/moves/MoveResult.h"
#include "algopt/rebalancer/solver/utils/ContainerPotential.h"
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/solver/utils/Util.h"

namespace facebook::rebalancer {

class MovesEvaluator {
 public:
  MovesEvaluator(
      Problem& problem,
      int objectiveTupleIndexBegin,
      int objectiveTupleIndexEnd,
      const std::string& stageName,
      const std::optional<
          algopt::common::thrift::HigherPriorityObjectivesConfig>&
          higherPriorityObjConfig = std::nullopt);

  MoveResult evaluate(MoveSet&& moves) const;

  // Checks only hard constraints (skips objective evaluation).
  // Cheaper than evaluate() when only validity matters.
  bool satisfiesConstraints(const MoveSet& moves) const;

  // Whether `move` violates any of `constraints` (a positive value means
  // violated).
  bool violatesAny(
      const std::vector<const Expression*>& constraints,
      const Move& move) const;

  // NOTE: apply changes to problem and also update internal state
  // needed since what we evaluate depends on what the intermediate 'applied'
  // state is
  void apply(const ChangeSet& changes) const;

  const GlobalObjective& getObjective() const;

  // NOTE: currently used in all places except 'evaluate' in MoveTypes
  // which had access to problems
  Problem& getProblem() const;

  const PackerSet<entities::ContainerId>& getContainers() const;

  const ObjectStore& getDynamicObjects(entities::ContainerId containerId) const;

  PackerMap<entities::ContainerId, ContainerPotential>
  computeContainerPotentials() const;

  PackerMap<entities::ContainerId, ContainerPotential>
  updateContainerPotentialsAfterMove(const MoveResult& appliedMoveResult) const;

  const entities::ContainerId getContainer(entities::ObjectId objectId) const;

 private:
  // TODO: underneath methods are copies of corresponding methods in Problem
  // clean them up in Problem once we deprecate Problem::evaluate
  void checkMoves(const MoveSet& moves) const;
  bool shouldCollectMoveStats() const;
  std::optional<ObjectiveDeltaSets> getObjectiveDeltas(
      Context& context,
      int objIdxEvaluatedUpto) const;
  std::optional<LabeledExpressionSet> getInvalidConstraints(
      Context& context,
      bool getOnlyFirstInvalidConstraint = false) const;

  bool isPositive(
      const LabeledConstraints& labeledConstraints,
      Context& context) const;

  bool isPositive(const Expression* expression, Context& context) const;

 protected:
  // By default, all prior goals in [0, objTupleBegin) cannot be worsened by any
  // amount. One can specify custom allowed `violation` for specific goals using
  // higherPriorityObjConfig
  struct DoNotWorsenGoalConfig {
    DoNotWorsenGoalConfig(
        const Problem& problem,
        int objTupleEnd,
        const std::string& stageName,
        const std::optional<
            algopt::common::thrift::HigherPriorityObjectivesConfig>&
            higherPriorityObjConfig);

    bool worseningAllowed() const;
    const double* getAllowedWorsenUntilValue(int pos) const;
    std::optional<int> getFirstWorseTuplePos(
        Context& context,
        Orchestrator& orchestrator) const;

    GlobalObjective goal;
    std::map<int, double> tuplePosToAllowedWorsenUntilValue;
    std::vector<LabeledExpressionPtr> doNotWorsenLabels;
    const entities::Universe& universe_;
  };

 private:
  Problem& problem_;
  const int objTupleBegin_;
  const int objTupleEnd_;
  const Precision& precision_;

  DoNotWorsenGoalConfig doNotWorsenGoalConfig_;
  GlobalObjective minimizingGoal_;
  std::optional<GlobalObjective> arbiterGoal_;
};

} // namespace facebook::rebalancer
