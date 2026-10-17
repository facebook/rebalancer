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

#include "algopt/rebalancer/solver/utils/LocalSearchObjective.h"

#include <fmt/core.h>

#include <algorithm>

namespace facebook::rebalancer {

namespace {
GlobalObjective buildCombinedObjective(
    const GlobalObjective& globalObjective,
    const GlobalObjective& penaltyObjective,
    int begin,
    int end,
    const entities::Universe& universe) {
  begin = std::max(0, begin);
  end = std::min(end, globalObjective.size());
  if (penaltyObjective.size() == 0) {
    return globalObjective.getRange(begin, end);
  }
  if (globalObjective.size() != penaltyObjective.size()) {
    throw std::runtime_error(
        fmt::format(
            "Global and Penalty objectives have different sizes: {} != {}",
            globalObjective.size(),
            penaltyObjective.size()));
  }
  GlobalObjective::Builder builder;
  for (int pos = begin; pos < end; ++pos) {
    const auto searchPos = 2 * (pos - begin);
    builder.setObjective(searchPos, globalObjective.getObjectiveAt(pos));
    builder.setObjective(searchPos + 1, penaltyObjective.getObjectiveAt(pos));
  }
  return builder.build(universe);
}
} // namespace

LocalSearchObjectiveValue::LocalSearchObjectiveValue(
    GlobalObjectiveValue combinedValue,
    bool hasPenalty)
    : combinedValue_(std::move(combinedValue)), hasPenalty_(hasPenalty) {}

LocalSearchObjectiveValue::LocalSearchObjectiveValue(
    GlobalObjectiveValue objectiveValue)
    : combinedValue_(std::move(objectiveValue)) {}

void LocalSearchObjectiveValue::validateCompatible(
    const LocalSearchObjectiveValue& lhs,
    const LocalSearchObjectiveValue& rhs) {
  if (lhs.combinedValue_.size() != 0 && rhs.combinedValue_.size() != 0 &&
      lhs.hasPenalty_ != rhs.hasPenalty_) {
    throw std::runtime_error(
        fmt::format(
            "Penalty objective presence differs: lhs={}, rhs={}",
            lhs.hasPenalty_,
            rhs.hasPenalty_));
  }
}

std::optional<size_t> LocalSearchObjectiveValue::firstChangedPosition(
    const LocalSearchObjectiveValue& lhs,
    const LocalSearchObjectiveValue& rhs,
    const Precision& precision) {
  validateCompatible(lhs, rhs);
  const auto positions = std::min(lhs.size(), rhs.size());
  for (const auto pos : folly::irange(positions)) {
    const auto lhsPosition = lhs.get(pos);
    const auto rhsPosition = rhs.get(pos);
    if (precision.compare(lhsPosition.objective, rhsPosition.objective) != 0 ||
        precision.compare(
            lhsPosition.penalty.value_or(0), rhsPosition.penalty.value_or(0)) !=
            0) {
      return pos;
    }
  }
  return std::nullopt;
}

size_t LocalSearchObjectiveValue::size() const {
  return hasPenalty_ ? (combinedValue_.size() + 1) / 2 : combinedValue_.size();
}

ObjectiveAndPenalty LocalSearchObjectiveValue::get(size_t pos) const {
  const auto index = static_cast<int>(pos);
  if (!hasPenalty_) {
    return {.objective = combinedValue_.get(index), .penalty = std::nullopt};
  }
  const auto penaltyIndex = 2 * index + 1;
  return {
      .objective = combinedValue_.get(2 * index),
      .penalty = penaltyIndex < static_cast<int>(combinedValue_.size())
          ? std::make_optional(combinedValue_.get(penaltyIndex))
          : std::nullopt};
}

bool LocalSearchObjectiveValue::isStrictlyBetter(
    const LocalSearchObjectiveValue& candidate,
    const LocalSearchObjectiveValue& current,
    const Precision& precision) {
  validateCompatible(candidate, current);
  return GlobalObjectiveValue::isStrictlyBetter(
      candidate.combinedValue_, current.combinedValue_, precision);
}

int LocalSearchObjectiveValue::unsafeCompare(
    const LocalSearchObjectiveValue& lhs,
    const LocalSearchObjectiveValue& rhs) {
  validateCompatible(lhs, rhs);
  return GlobalObjectiveValue::unsafeCompare(
      lhs.combinedValue_, rhs.combinedValue_);
}

int LocalSearchObjectiveValue::precisionCompare(
    const LocalSearchObjectiveValue& lhs,
    const LocalSearchObjectiveValue& rhs,
    const Precision& precision) {
  validateCompatible(lhs, rhs);
  return GlobalObjectiveValue::precisionCompare(
      lhs.combinedValue_, rhs.combinedValue_, precision);
}

LocalSearchObjectiveValue LocalSearchObjectiveValue::add(
    const LocalSearchObjectiveValue& lhs,
    const LocalSearchObjectiveValue& rhs,
    const Precision& precision) {
  validateCompatible(lhs, rhs);
  return LocalSearchObjectiveValue(
      GlobalObjectiveValue::add(
          lhs.combinedValue_, rhs.combinedValue_, precision),
      lhs.hasPenalty_);
}

LocalSearchObjectiveValue LocalSearchObjectiveValue::subtract(
    const LocalSearchObjectiveValue& lhs,
    const LocalSearchObjectiveValue& rhs,
    const Precision& precision) {
  validateCompatible(lhs, rhs);
  return LocalSearchObjectiveValue(
      GlobalObjectiveValue::subtract(
          lhs.combinedValue_, rhs.combinedValue_, precision),
      lhs.hasPenalty_);
}

std::string LocalSearchObjectiveValue::toString() const {
  if (!hasPenalty_) {
    return combinedValue_.toString();
  }

  const auto size = static_cast<int>(combinedValue_.size());
  std::string result = "(";
  for (int pos = 0; pos < size; pos += 2) {
    if (pos != 0) {
      result += ", ";
    }
    result += fmt::format("{}", combinedValue_.get(pos));
    if (pos + 1 < size) {
      result += fmt::format(" ({})", combinedValue_.get(pos + 1));
    }
  }
  if (combinedValue_.isPartial()) {
    result += ", __";
  }
  return result + ")";
}

LocalSearchObjective::LocalSearchObjective(
    const GlobalObjective& globalObjective,
    const GlobalObjective& penaltyObjective,
    const entities::Universe& universe)
    : LocalSearchObjective(
          globalObjective,
          penaltyObjective,
          0,
          globalObjective.size(),
          universe) {}

LocalSearchObjective::LocalSearchObjective(
    const GlobalObjective& globalObjective,
    const GlobalObjective& penaltyObjective,
    int begin,
    int end,
    const entities::Universe& universe)
    : combinedObjective_(buildCombinedObjective(
          globalObjective,
          penaltyObjective,
          begin,
          end,
          universe)),
      hasPenalty_(penaltyObjective.size() != 0) {}

LocalSearchObjectiveValue LocalSearchObjective::getValue() const {
  return LocalSearchObjectiveValue(combinedObjective_.getValue(), hasPenalty_);
}

LocalSearchObjectiveValue LocalSearchObjective::evaluate(
    Context& context,
    const Orchestrator& orchestrator,
    bool evaluateAllTuples) const {
  return LocalSearchObjectiveValue(
      combinedObjective_.evaluate(context, orchestrator, evaluateAllTuples),
      hasPenalty_);
}

} // namespace facebook::rebalancer
