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

#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Orchestrator.h"
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"
#include "algopt/rebalancer/solver/utils/Context.h"
#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"

#include <folly/container/small_vector.h>

#include <initializer_list>
#include <utility>

namespace facebook::rebalancer {

class GlobalObjective {
 public:
  using objectives_tuple = folly::small_vector<ExprPtr, expected_tuple_size>;
  class Builder;
  class View;

 public:
  explicit GlobalObjective(const entities::Universe& universe)
      : objectives_({}), universe_(&universe) {}

  objectives_tuple::const_iterator begin() const;
  objectives_tuple::const_iterator end() const;

  ExprPtr getOnlyObjective() const;
  ExprPtr getFirstObjective() const;
  ExprPtr getObjectiveAt(int pos) const;

  void initUnconstrainedBounds();

  GlobalObjectiveValue lowerBound(
      const BoundConstraints& bc = BoundConstraints()) const;

  double evaluateObjectiveAt(
      int i,
      Context& context,
      const Orchestrator& orchestrator) const;

  GlobalObjectiveValue evaluate(
      Context& context,
      const Orchestrator& orchestrator,
      bool evaluateAllTuples = false) const;

  GlobalObjectiveValue fullApply(
      const TopToBottomEvaluator& evaluator,
      const Assignment& assignment) const;
  GlobalObjectiveValue getValue() const;
  double getValueAt(int pos) const;

  View getView() const;
  View getView(int start, int end) const;
  // create a global objective with objectives from positions [start, end)
  GlobalObjective getRange(int start, int end) const;
  int size() const;

 private:
  explicit GlobalObjective(
      objectives_tuple&& objective,
      const entities::Universe& universe)
      : objectives_(std::move(objective)), universe_(&universe) {}

 private:
  objectives_tuple objectives_;
  const entities::Universe* universe_;
};

class GlobalObjective::Builder {
 public:
  explicit Builder() : objectives_({}), built_(false) {}
  GlobalObjective::Builder& addToObjective(
      int pos,
      ExprPtr objective,
      const entities::Universe& universe);
  GlobalObjective::Builder& setObjective(int pos, ExprPtr objective);

  GlobalObjective build(const entities::Universe& universe);

 private:
  objectives_tuple objectives_;
  bool built_;
};

class GlobalObjective::View {
 public:
  // provides a view of the globalobjective with objectives starting at start
  // and ending at end: [start, end)
  explicit View(const GlobalObjective& objective, int start, int end);
  objectives_tuple::const_iterator begin() const;
  objectives_tuple::const_iterator end() const;
  int size() const;

 private:
  const GlobalObjective& objective_;
  int start_;
  int end_;
};
} // namespace facebook::rebalancer
