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

#include "algopt/rebalancer/solver/utils/Precision.h"

#include <folly/small_vector.h>

#include <optional>
#include <string>

namespace facebook::rebalancer {

const size_t expected_tuple_size = 10;

// encapsulates the objective values of an ObjectiveTuple as vec<double>
class GlobalObjectiveValue {
 public:
  using objective_values = folly::small_vector<double, expected_tuple_size>;

 public:
  static GlobalObjectiveValue makeWithFixedSize(size_t size);

  GlobalObjectiveValue() : values_({}) {}
  // mainly used in tests
  explicit GlobalObjectiveValue(objective_values&& values)
      : values_(std::move(values)) {}

  // get size of the objective tuple value
  size_t size() const;
  // get value for objective at position idx in objective tuple value
  double get(int idx) const;
  // add a value to objective tuple value
  void append(double value);
  // is the vector only partially populated because objective value is only
  // partially evaluated. GlobalObjective::evaluate decided that populating the
  // remaining values was not necessary
  bool isPartial() const;

  // Returns true iff `candidate` is strictly better than `current` for the
  // purpose of accepting a local-search move. Lexicographic: the first position
  // that differs beyond tolerance decides; at earlier within-tolerance ("tied")
  // positions any raw worsening (candidate > current) makes the move
  // not-better. This guarantees every accepted move strictly decreases the
  // objective tuple, so local search cannot cycle. A purely within-tolerance
  // change is not strictly better.
  static bool isStrictlyBetter(
      const GlobalObjectiveValue& candidate,
      const GlobalObjectiveValue& current,
      const Precision& precision);

  static GlobalObjectiveValue add(
      const GlobalObjectiveValue& lhs,
      const GlobalObjectiveValue& rhs,
      const Precision& precision);
  static GlobalObjectiveValue subtract(
      const GlobalObjectiveValue& lhs,
      const GlobalObjectiveValue& rhs,
      const Precision& precision);

  // mainly used for logging
  std::string toString() const;

  std::vector<double> toVector() const;

  // returns a -1, 0, 1, when the first argument is less than, equal to, or
  // greater than the second respectively (based on default floating-point
  // comparision)
  static int unsafeCompare(
      const GlobalObjectiveValue& value1,
      const GlobalObjectiveValue& value2);

  // returns a -1, 0, 1, when the first argument is less than, equal to,
  // or greater than the second respectively using code from Precision helpers
  static int precisionCompare(
      const GlobalObjectiveValue& value1,
      const GlobalObjectiveValue& value2,
      const Precision& precision);

 private:
  std::optional<size_t> tupleSize_;
  objective_values values_;
};

} // namespace facebook::rebalancer
