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

#include "algopt/rebalancer/solver/utils/GlobalObjective.h"

#include <optional>

namespace facebook::rebalancer {

struct ObjectiveAndPenalty {
  double objective{};
  std::optional<double> penalty;
};

// Value of a LocalSearchObjective: one entry per objective position, each with
// the objective value and, if present, its penalty.
class LocalSearchObjectiveValue {
 public:
  LocalSearchObjectiveValue() = default;
  // from objective values only, with no penalties
  /* implicit */ LocalSearchObjectiveValue(GlobalObjectiveValue objectiveValue);

  // number of objective positions that have a value
  size_t size() const;
  ObjectiveAndPenalty get(size_t pos) const;

  static bool isStrictlyBetter(
      const LocalSearchObjectiveValue& candidate,
      const LocalSearchObjectiveValue& current,
      const Precision& precision);
  static std::optional<size_t> firstChangedPosition(
      const LocalSearchObjectiveValue& lhs,
      const LocalSearchObjectiveValue& rhs,
      const Precision& precision);
  static int unsafeCompare(
      const LocalSearchObjectiveValue& lhs,
      const LocalSearchObjectiveValue& rhs);
  static int precisionCompare(
      const LocalSearchObjectiveValue& lhs,
      const LocalSearchObjectiveValue& rhs,
      const Precision& precision);
  static LocalSearchObjectiveValue add(
      const LocalSearchObjectiveValue& lhs,
      const LocalSearchObjectiveValue& rhs,
      const Precision& precision);
  static LocalSearchObjectiveValue subtract(
      const LocalSearchObjectiveValue& lhs,
      const LocalSearchObjectiveValue& rhs,
      const Precision& precision);

  std::string toString() const;

 private:
  friend class LocalSearchObjective;

  LocalSearchObjectiveValue(
      GlobalObjectiveValue combinedValue,
      bool hasPenalty);

  static void validateCompatible(
      const LocalSearchObjectiveValue& lhs,
      const LocalSearchObjectiveValue& rhs);
  GlobalObjectiveValue combinedValue_;
  bool hasPenalty_{false};
};

// The objective local search minimizes. Each objective value is followed by
// its penalty, (G0, P0, G1, P1, ...), and values are compared in that order,
// so a penalty only matters when everything before it is equal.
class LocalSearchObjective {
 public:
  LocalSearchObjective(
      const GlobalObjective& globalObjective,
      const GlobalObjective& penaltyObjective,
      const entities::Universe& universe);
  // positions [begin, end), each with its penalty if penaltyObjective is not
  // empty
  LocalSearchObjective(
      const GlobalObjective& globalObjective,
      const GlobalObjective& penaltyObjective,
      int begin,
      int end,
      const entities::Universe& universe);

  LocalSearchObjectiveValue getValue() const;
  LocalSearchObjectiveValue evaluate(
      Context& context,
      const Orchestrator& orchestrator,
      bool evaluateAllTuples = false) const;

 private:
  GlobalObjective combinedObjective_;
  bool hasPenalty_{false};
};

} // namespace facebook::rebalancer
