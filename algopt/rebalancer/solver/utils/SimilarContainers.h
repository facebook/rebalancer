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

#include "algopt/rebalancer/algopt_common/ValueSortedMap.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/solver/if/gen-cpp2/packer_types.h"
#include "algopt/rebalancer/solver/utils/ContainerPotential.h"
#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <functional>
#include <iterator>
#include <optional>
#include <random>
#include <utility>
#include <vector>

namespace facebook::rebalancer {

class SimilarContainers {
 public:
  using ContainerPotentials =
      PackerMap<entities::ContainerId, ContainerPotential>;

  explicit SimilarContainers(
      const std::vector<std::vector<entities::ContainerId>>& similarContainers);

  PackerSet<entities::ContainerId> stratifiedSample(
      int size,
      const std::function<bool(entities::ContainerId)>&
          accepting_containers_check);

  PackerSet<entities::ContainerId> stratifiedOrderedSample(
      int size,
      const std::function<bool(entities::ContainerId)>& acceptingContainers,
      bool addEqualSizeRandomSample);

  void updateOrderedGroups(
      const ContainerPotentials& changesInContainerPotentials);

  void setOrderedGroups(
      const ContainerPotentials& containerContainerPotentials);

 private:
  std::vector<int> groupCounts(int size) const;

  std::mt19937 rng;
  std::vector<std::vector<entities::ContainerId>> groups;
  PackerMap<entities::ContainerId, int> container_to_group;

  class Compare {
   public:
    bool operator()(
        const std::pair<entities::ContainerId, ContainerPotential>& c1,
        const std::pair<entities::ContainerId, ContainerPotential>& c2) const {
      const auto& [c1Id, c1Potential] = c1;
      const auto& [c2Id, c2Potential] = c2;

      const auto compare = ContainerPotential::precisionCompare(
          c1Potential, c2Potential, c1Potential.getPrecision());

      if (compare != 0) {
        return compare == -1;
      }

      // this implies c1 and c2 have the same contributionToGoal and also have
      // the same number of objects; so break in favor of one with smaller id
      return c1Id < c2Id;
    }
  };

  // orderedGroups maintains each group of similarContainers as ordered
  // according to their containerPotential. In a group, container c1 is
  // placed before container c2 if c1 has a lower containerPotential than c2
  using OrderedGroup = facebook::algopt::
      ValueSortedMap<entities::ContainerId, ContainerPotential, Compare>;
  std::optional<std::vector<OrderedGroup>> orderedGroups;
};
} // namespace facebook::rebalancer
