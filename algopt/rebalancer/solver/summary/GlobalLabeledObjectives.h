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

#include "algopt/rebalancer/common/log/RebalancerLog.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/summary/LabeledExpressions.h"
#include "algopt/rebalancer/solver/summary/LabeledObjectives.h"
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"
#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"

#include <folly/small_vector.h>

#include <initializer_list>
#include <map>
#include <stddef.h>
#include <string>
#include <utility>

namespace facebook::rebalancer {

class GlobalLabeledObjectives {
 public:
  using labeled_objectives_tuple =
      folly::small_vector<LabeledObjectives, expected_tuple_size>;
  class Builder;

 public:
  explicit GlobalLabeledObjectives() : labeled_objectives_({}) {}

  GlobalLabeledObjectives getLabeledObjectivesTuple(size_t begin, size_t end)
      const;

  labeled_objectives_tuple::const_iterator begin() const;
  labeled_objectives_tuple::const_iterator end() const;

  const LabeledObjectives& getFirstObjective() const;
  const LabeledObjectives& getObjectiveAt(int pos) const;

  size_t size() const;

 private:
  explicit GlobalLabeledObjectives(
      labeled_objectives_tuple&& labeled_objectives)
      : labeled_objectives_(std::move(labeled_objectives)) {}

 private:
  labeled_objectives_tuple labeled_objectives_;
};

class GlobalLabeledObjectives::Builder {
 public:
  explicit Builder() : labeled_objectives_({}) {}

  Builder& setRoot(int pos, ExprPtr root);
  Builder&
  addSingle(int pos, std::string name, ExprPtr expr, double weight = 1);
  GlobalLabeledObjectives build(const GlobalObjective& objective);

 private:
  LabeledObjectives& getOrCreateLabeledObjectivesAt(int pos);

 private:
  labeled_objectives_tuple labeled_objectives_;
  bool built_ = false;
};

} // namespace facebook::rebalancer
