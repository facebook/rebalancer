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

#include "algopt/rebalancer/solver/moves/MoveResult.h"

#include <sstream>

namespace facebook::rebalancer {

MoveResult MoveResult::makeEmpty() {
  MoveResult result;
  result.empty_ = true;
  return result;
}

MoveResult MoveResult::makeValid(
    MoveSet moveSet,
    LocalSearchObjectiveValue oldValue,
    LocalSearchObjectiveValue newValue,
    std::optional<GlobalObjectiveValue> arbiterValue,
    std::optional<ObjectiveDeltaSets> objectiveDeltas,
    int objectiveDeltaIndexOffset) {
  MoveResult result;
  result.valid_ = true;
  result.oldValue_ = std::move(oldValue);
  result.newValue_ = std::move(newValue);
  result.arbiterValue_ = std::move(arbiterValue);
  result.evalsCount_ = 1;
  result.moveSet_ = std::move(moveSet);
  result.objectiveDeltas_ = std::move(objectiveDeltas);
  result.objectiveDeltaIndexOffset_ = objectiveDeltaIndexOffset;
  return result;
}

MoveResult MoveResult::makeInvalid(
    MoveSet moveSet,
    std::optional<LabeledExpressionSet> invalidConstraints) {
  MoveResult result;
  result.evalsCount_ = 1;
  result.moveSet_ = std::move(moveSet);
  result.invalidConstraints_ = std::move(invalidConstraints);
  return result;
}

bool MoveResult::isEmpty() const {
  return empty_;
}

bool MoveResult::isValid() const {
  return valid_;
}

bool MoveResult::isBetter(const Precision& precision) const {
  return valid_ &&
      LocalSearchObjectiveValue::isStrictlyBetter(
             newValue_, oldValue_, precision);
}

bool MoveResult::isNeutral(const Precision& precision) const {
  // Neither strictly better nor strictly worse.
  return valid_ && !isBetter(precision) && !isWorse(precision);
}

bool MoveResult::isWorse(const Precision& precision) const {
  return valid_ &&
      LocalSearchObjectiveValue::isStrictlyBetter(
             oldValue_, newValue_, precision);
}

const MoveSet& MoveResult::getMoveSet() const {
  return moveSet_;
}

int64_t MoveResult::getEvalsCount() const {
  return evalsCount_;
}

bool MoveResult::hasObjectiveDeltas() const {
  return objectiveDeltas_.has_value();
}

const ObjectiveDeltaSet& MoveResult::getFirstChangedObjectiveDelta(
    const Precision& precision) const {
  // Select the objective that explains this stage's move classification.
  // showAllChangedObjectivesInMovesSummary may collect deltas outside the
  // stage for reporting, but those objectives do not drive stage acceptance.
  if (const auto pos = LocalSearchObjectiveValue::firstChangedPosition(
          newValue_, oldValue_, precision)) {
    return objectiveDeltas_->at(objectiveDeltaIndexOffset_ + *pos);
  }
  // Numerical differences below precision do not identify a position. Keep
  // the legacy fallback to the first objective in the stage.
  return objectiveDeltas_->at(objectiveDeltaIndexOffset_);
}

const ObjectiveDeltaSets& MoveResult::getObjectiveDeltas() const {
  return objectiveDeltas_.value();
}

std::string MoveResult::toString(const entities::Universe& universe) const {
  std::stringstream ss;
  ss << fmt::format(
      "Objective: {}, eval count: {}\nMoveSet ({} moves):",
      newValue_.toString(),
      evalsCount_,
      moveSet_.size());
  for (const auto& move : moveSet_) {
    ss << fmt::format("\n{}", move.toString(universe));
  }
  return ss.str();
}

LabeledExpressionPtr MoveResult::getSmallestDeltaObjective(
    const Precision& precision) const {
  double smallestDelta = std::numeric_limits<double>::infinity();
  LabeledExpressionPtr smallestObjective = nullptr;
  if (objectiveDeltas_) {
    for (auto& objectiveDelta : getFirstChangedObjectiveDelta(precision)) {
      const double delta = objectiveDelta.newValue - objectiveDelta.oldValue;
      if (delta < smallestDelta) {
        smallestDelta = delta;
        smallestObjective = objectiveDelta.objective;
      }
    }
  }
  return smallestObjective;
}

LabeledExpressionPtr MoveResult::getLargestDeltaObjective(
    const Precision& precision) const {
  double largestDelta = -std::numeric_limits<double>::infinity();
  LabeledExpressionPtr largestObjective = nullptr;
  if (objectiveDeltas_) {
    for (auto& objectiveDelta : getFirstChangedObjectiveDelta(precision)) {
      const double delta = objectiveDelta.newValue - objectiveDelta.oldValue;
      if (delta > largestDelta) {
        largestDelta = delta;
        largestObjective = objectiveDelta.objective;
      }
    }
  }
  return largestObjective;
}

LabeledExpressionPtr MoveResult::getFirstInvalidConstraint() const {
  if (invalidConstraints_ && !invalidConstraints_->empty()) {
    return invalidConstraints_->front();
  }
  return nullptr;
}

void MoveResult::aggregate(MoveResult&& other) {
  evalsCount_ += other.evalsCount_;
  const auto compare =
      LocalSearchObjectiveValue::unsafeCompare(other.newValue_, newValue_);
  auto arbitersImproved = false;
  auto arbiterCompare = 0;
  const auto hasArbiterValue =
      arbiterValue_.has_value() && other.arbiterValue_.has_value();
  if (hasArbiterValue) {
    arbiterCompare = GlobalObjectiveValue::unsafeCompare(
        *other.arbiterValue_, *arbiterValue_);
    arbitersImproved = arbiterCompare < 0;
  }
  const auto needsTieBreak = [&]() {
    if (compare != 0) {
      return false;
    }
    return !hasArbiterValue || arbiterCompare == 0;
  };
  if (compare < 0 || (compare == 0 && arbitersImproved) ||
      (compare == 0 && needsTieBreak() &&
       MoveSet::compare(moveSet_, other.moveSet_) < 0)) {
    empty_ = other.empty_;
    valid_ = other.valid_;
    oldValue_ = std::move(other.oldValue_);
    newValue_ = std::move(other.newValue_);
    arbiterValue_ = std::move(other.arbiterValue_);
    moveSet_ = std::move(other.moveSet_);
    objectiveDeltas_ = std::move(other.objectiveDeltas_);
    objectiveDeltaIndexOffset_ = other.objectiveDeltaIndexOffset_;
    invalidConstraints_ = std::move(other.invalidConstraints_);
  }
}

const LocalSearchObjectiveValue& MoveResult::getOldValue() const {
  return oldValue_;
}

const LocalSearchObjectiveValue& MoveResult::getValue() const {
  return newValue_;
}

MoveResult::MoveResult() = default;

} // namespace facebook::rebalancer
