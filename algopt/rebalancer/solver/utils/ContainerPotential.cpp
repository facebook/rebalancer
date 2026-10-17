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

#include <algopt/rebalancer/solver/utils/ContainerPotential.h>
#include <algopt/rebalancer/solver/utils/GlobalObjectiveValue.h>

#include <utility>

namespace facebook::rebalancer {

ContainerPotential::ContainerPotential(const Precision& precision)
    : contributionToGoal_(), nObjects_(0), precision_(precision) {}

ContainerPotential::ContainerPotential(
    LocalSearchObjectiveValue contributionToGoal,
    int nObjects,
    const Precision& precision)
    : contributionToGoal_(std::move(contributionToGoal)),
      nObjects_(std::move(nObjects)),
      precision_(precision) {}

// returns -1, 0, 1, respectively, depending on whether potential1 is less
// than, equal to, or greater than potential2
int ContainerPotential::precisionCompare(
    const ContainerPotential& potential1,
    const ContainerPotential& potential2,
    const Precision& precision) {
  const auto contributionCompare = LocalSearchObjectiveValue::precisionCompare(
      potential1.contributionToGoal_,
      potential2.contributionToGoal_,
      precision);
  if (contributionCompare != 0) {
    return contributionCompare;
  }

  // if contributionToGoals are identical in potential1 and potential2, then
  // try to distinguish them based on nObjects in each
  if (potential1.nObjects_ != potential2.nObjects_) {
    return potential1.nObjects_ < potential2.nObjects_ ? -1 : 1;
  }

  return 0;
}

ContainerPotential ContainerPotential::operator+(
    const ContainerPotential& other) const {
  return ContainerPotential(
      LocalSearchObjectiveValue::add(
          contributionToGoal_, other.contributionToGoal_, precision_),
      nObjects_ + other.nObjects_,
      precision_);
}

const LocalSearchObjectiveValue& ContainerPotential::getContributionToGoal()
    const {
  return contributionToGoal_;
}

const Precision& ContainerPotential::getPrecision() const {
  return precision_;
}

} // namespace facebook::rebalancer
