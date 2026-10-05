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

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer {

class Assignment;

// This class is used to support "stable" assignment of objects to containers.
// especially when using optimal solver. As an example, consider we have only
// one equivalence set consting of four objects (o1,.., o4), and two containers
// c1 and c2. Suppose the initial assignment is {c1: o1, o2} {c2: o3, o4}.
// Now, for scalability reasons, optimal solver works with "equivalence set"
// comes up with a final assignment of {c1: 2 objects of e1} {c2: 2 objects of
// e1} If we are not careful, Rebalancer may translate that as {c1: o3, o4} {c2:
// o1, o2} which flips the initial assignment completely for no reason. The
// following util class tries to ensure that the final assignment from optimal
// solver is not too different from initial assignment (and is more or less
// stable)
//
// NOTE: This is only relevant to Optimal Solver and is totally irrelvant to
// local search. This implementation was initially added in D23299522. The
// implementation is arguably quite complicated and poorly understood. A
// refactor and rewrite of this is long overdue so that we properly understand
// how it is used. T225081622
//
class ObjectToContainerAssignmentUtils {
 public:
  // If shuffleSeed is passed, the objects are randomly shuffled based
  // on the seed chosen
  PackerMap<entities::ObjectId, entities::ContainerId> getObjectToContainer(
      const EquivalenceSets& equivalenceSets,
      const PackerMap<
          entities::EquivalenceSetId,
          PackerMap<entities::ContainerId, int>>&
          equivSetToContainerToObjectCount,
      const Assignment& initialAssignment,
      const std::optional<std::string>& shuffleSeed = std::nullopt);
  std::vector<entities::ContainerId> getPreferredContainers(
      entities::ContainerId container) const;

  // we want objects in this group try to stay in this group
  void addStableContainerGroup(
      std::shared_ptr<const PackerSet<entities::ContainerId>> containersPtr);

 private:
  static PackerMap<
      entities::EquivalenceSetId,
      PackerMap<entities::ContainerId, std::vector<entities::ObjectId>>>
  getEquivSetToContainerToObjects(
      const EquivalenceSets& equivalenceSets,
      const Assignment& assignment,
      const std::optional<std::string>& shuffleSeed);

  static std::optional<entities::ContainerId> pickContainerToPlaceObjects(
      const PackerMap<entities::ContainerId, int>& containerToPreferredCount,
      const PackerSet<entities::ContainerId>& sourceContainerOrder);

  void assignObjectToContainerForOneSet(
      PackerMap<entities::ObjectId, entities::ContainerId>& objectToContainer,
      PackerMap<entities::ContainerId, int> containerToObjectCount,
      PackerMap<entities::ContainerId, std::vector<entities::ObjectId>>
          containerToInitialObjects,
      const std::vector<PackerSet<entities::ContainerId>>& sourceContainerOrder)
      const;

  static void sanityCheckOnEquivSetToContainerToObjectCount(
      const PackerMap<
          entities::EquivalenceSetId,
          PackerMap<entities::ContainerId, std::vector<entities::ObjectId>>>&
          equivSetToContainerToInitialObjects,
      const PackerMap<
          entities::EquivalenceSetId,
          PackerMap<entities::ContainerId, int>>&
          equivSetToContainerToObjectCount);

  void getContainerToPreferredContainers(const auto& containers);

  std::vector<PackerSet<entities::ContainerId>> getSourceContainerOrder();

  void updatePreferredCount(
      const PackerSet<entities::ContainerId>& emptyContainers,
      PackerMap<entities::ContainerId, int>& containerToPreferredCount) const;

  // these are groups, which specified by users, to indicate how to
  // translate equivalent set count on container to inidividual objects
  // if a stable group is set, we prefer to pick object
  // initially on this container
  std::vector<std::shared_ptr<const PackerSet<entities::ContainerId>>>
      stableContainerGroups_;
  std::optional<
      PackerMap<entities::ContainerId, std::vector<entities::ContainerId>>>
      containerToPreferredContainers_;
  PackerMap<entities::ContainerId, PackerSet<entities::ContainerId>>
      preferredContainerToContainers_;
  std::optional<std::vector<PackerSet<entities::ContainerId>>>
      sourceContainerOrder_;
};

} // namespace facebook::rebalancer
