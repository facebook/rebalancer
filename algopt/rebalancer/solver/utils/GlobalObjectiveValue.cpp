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

#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"

#include <fmt/core.h>
#include <folly/container/irange.h>
#include <folly/gen/Base.h>
#include <folly/gen/String.h>

#include <sstream>
#include <string>
#include <vector>

namespace facebook::rebalancer {

using std::string;
using std::vector;
using namespace folly::gen;

/* static */
GlobalObjectiveValue GlobalObjectiveValue::makeWithFixedSize(size_t size) {
  GlobalObjectiveValue val;
  val.tupleSize_ = size;
  return val;
}

size_t GlobalObjectiveValue::size() const {
  return values_.size();
}

double GlobalObjectiveValue::get(int idx) const {
  return values_.at(idx);
}

void GlobalObjectiveValue::append(double val) {
  if (tupleSize_ == std::nullopt) {
    values_.push_back(val);
    return;
  }

  if (*tupleSize_ <= values_.size()) {
    throw std::runtime_error(
        fmt::format(
            "tupleSize {}, cannot be <= number of values in the tuple {}",
            *tupleSize_,
            values_.size()));
  }
  values_.push_back(val);
}

bool GlobalObjectiveValue::isPartial() const {
  return tupleSize_ != std::nullopt && *tupleSize_ != values_.size();
}

int GlobalObjectiveValue::unsafeCompare(
    const GlobalObjectiveValue& value1,
    const GlobalObjectiveValue& value2) {
  auto size1 = value1.size();
  auto size2 = value2.size();
  // initialized < uninitialized (considered as \inf)
  if (size1 == 0) {
    return size2 == 0 ? 0 : 1;
  }
  if (size2 == 0) {
    return -1;
  }
  if (!(value1.isPartial() || value2.isPartial() || size1 == size2)) {
    throw std::runtime_error(
        fmt::format("size1: {} != size2: {}", size1, size2));
  }

  auto size = std::min(size1, size2);
  for (const auto i : folly::irange(size)) {
    if (value1.get(i) != value2.get(i)) {
      return value1.get(i) < value2.get(i) ? -1 : 1;
    }
  }
  return 0;
}

int GlobalObjectiveValue::precisionCompare(
    const GlobalObjectiveValue& value1,
    const GlobalObjectiveValue& value2,
    const Precision& precision) {
  auto size1 = value1.size();
  auto size2 = value2.size();
  // initialized < uninitialized (considered as \inf)
  if (size1 == 0) {
    return size2 == 0 ? 0 : 1;
  }
  if (size2 == 0) {
    return -1;
  }
  if (!(value1.isPartial() || value2.isPartial() || size1 == size2)) {
    throw std::runtime_error(
        fmt::format("size1: {} != size2: {}", size1, size2));
  }

  const auto size = std::min(size1, size2);
  for (const auto i : folly::irange(size)) {
    auto compare = precision.compare(value1.get(i), value2.get(i));
    if (compare != 0) {
      return compare;
    }
  }
  return 0;
}

bool GlobalObjectiveValue::isStrictlyBetter(
    const GlobalObjectiveValue& candidate,
    const GlobalObjectiveValue& current,
    const Precision& precision) {
  const auto candidateSize = candidate.size();
  const auto currentSize = current.size();
  // Uninitialized is treated as +inf (same convention as precisionCompare).
  if (candidateSize == 0) {
    return false; // +inf is never strictly better
  }
  if (currentSize == 0) {
    return true; // any initialized value beats uninitialized (+inf)
  }
  const auto bothComplete = !candidate.isPartial() && !current.isPartial();
  if (bothComplete && candidateSize != currentSize) [[unlikely]] {
    throw std::runtime_error(
        fmt::format("size1: {} != size2: {}", candidateSize, currentSize));
  }

  const auto size = std::min(candidateSize, currentSize);
  for (const auto i : folly::irange(size)) {
    const auto compare = precision.compare(candidate.get(i), current.get(i));
    if (compare < 0) {
      return true; // significant improvement at this position: better
    }
    if (compare > 0) {
      return false; // significant worsening at this position: not better
    }
    // Within tolerance: forbid 'current' from worsening to ensure that only
    // strictly improving moves are classified as better.
    if (candidate.get(i) > current.get(i)) {
      return false;
    }
  }
  return false; // all positions within tolerance: not strictly better
}

GlobalObjectiveValue GlobalObjectiveValue::add(
    const GlobalObjectiveValue& lhs,
    const GlobalObjectiveValue& rhs,
    const Precision& precision) {
  if (lhs.size() != rhs.size()) {
    throw std::runtime_error(
        "addition of GlobalObjective values can only be done if both have the same length");
  }

  GlobalObjectiveValue summation;
  for (const auto pos : folly::irange(lhs.size())) {
    const double sum = precision.compare(lhs.get(pos), -rhs.get(pos)) == 0
        ? 0.0
        : lhs.get(pos) + rhs.get(pos);
    summation.append(sum);
  }

  return summation;
}

GlobalObjectiveValue GlobalObjectiveValue::subtract(
    const GlobalObjectiveValue& lhs,
    const GlobalObjectiveValue& rhs,
    const Precision& precision) {
  if (lhs.size() != rhs.size()) {
    throw std::runtime_error(
        "subtraction of GlobalObjective values can only be done if both have the same length");
  }

  GlobalObjectiveValue difference;
  for (const auto pos : folly::irange(lhs.size())) {
    const double diff = precision.compare(lhs.get(pos), rhs.get(pos)) == 0
        ? 0.0
        : lhs.get(pos) - rhs.get(pos);
    difference.append(diff);
  }

  return difference;
}

string GlobalObjectiveValue::toString() const {
  const double numUnInitializedPositions =
      tupleSize_ && *tupleSize_ > values_.size() ? *tupleSize_ - values_.size()
                                                 : 0;
  if (values_.size() + numUnInitializedPositions == 1) {
    return fmt::format("{}", values_.at(0));
  }

  std::stringstream ss;
  ss << "("
     << folly::join(", ", from(values_) | eachTo<string>() | as<vector>());
  if (numUnInitializedPositions > 0) {
    ss << ", "
       << folly::join(
              ", ",
              just("__") | cycle(numUnInitializedPositions) | as<vector>());
  }
  ss << ")";
  return ss.str();
}

std::vector<double> GlobalObjectiveValue::toVector() const {
  return {values_.begin(), values_.end()};
}

} // namespace facebook::rebalancer
