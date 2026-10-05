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
#include <algopt/rebalancer/solver/utils/GlobalObjectiveValue.h>
#include <algopt/rebalancer/solver/utils/Precision.h>

namespace facebook::rebalancer {

// The ContainerPotential of a container C is the tuple (contributionToGoal,
// nObjects), where, given an assignment A, contributionToGoal is the change in
// goal value(s) that results from removing C (along with the objects
// in it) from A, and nObjects is the number of objects in C. It can be thought
// of as a measure of "hotness" of a container, where higher the potential, the
// "hotter" the container is.
class ContainerPotential {
 public:
  explicit ContainerPotential(const Precision& precision);

  explicit ContainerPotential(
      GlobalObjectiveValue contributionToGoal,
      int nObjects,
      const Precision& precision);

  // returns -1, 0, 1, respectively, depending on whether potential1 is less
  // than, equal to, or greater than potential2
  static int precisionCompare(
      const ContainerPotential& potential1,
      const ContainerPotential& potential2,
      const Precision& precision);

  ContainerPotential operator+(const ContainerPotential& other) const;

  const GlobalObjectiveValue& getContributionToGoal() const;

  const Precision& getPrecision() const;

 private:
  GlobalObjectiveValue contributionToGoal_;
  int nObjects_ = 0;
  Precision precision_;
};

} // namespace facebook::rebalancer
