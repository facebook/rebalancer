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
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/utils/EntityAttributes.h"

#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/lang/Hint.h>

namespace facebook::rebalancer {

// Forward declaration to avoid circular dependency
class Assignment;

/**
 * OneObjectPerGroup enforces that only one object from each group can be
 * selected from the source container when groups may overlap.
 *
 * This class tracks object counts per group in a partition where groups can
 * overlap (objects may belong to multiple groups) and ensures mutual
 * exclusion: an object can only be picked if all groups it belongs to are
 * untouched (have all their objects still in the source container).
 *
 * Key constraints and assumptions:
 * - The partition may have overlapping groups: an object can belong to
 *   multiple groups within the partition
 * - The class only tracks whether objects are in the source container or
 *   outside of it (binary state)
 * - Relies on LocalSearch invariants:
 *   * Only valid moves are performed between containers
 *   * No duplicate move operations are performed back-to-back
 *   * The class assumes these invariants hold and does not validate them
 *
 * Example Usage: GPU-Topology-Aware Adaptive Allotment
 * ------------------------------------------------
 * Problem: Servers have multiple server parts that share physical GPUs.
 * When allocating parts from "unassigned", we want to ensure only one part
 * per server is picked at a time to avoid GPU conflicts. A single overlapping
 * partition groups server parts by server, where parts may appear in multiple
 * groups due to shared GPU resources.
 *
 *   // Setup tracker with single overlapping partition name
 *   auto tracker = std::make_shared<OneObjectPerGroup>(
 *       universe, "server_gpu_topology", unassignedContainer,
 * initialAssignment); attributeStore.registerAttributes("gpu_topology",
 * tracker);
 *
 *   // When selecting candidates from "unassigned" for allocation:
 *   for (auto candidatePart : unassignedParts) {
 *     if (tracker->canPick(candidatePart)) {
 *       // Safe to allocate: no other part from same server using shared GPU
 *       // has been picked yet
 *       candidateMove.insert(candidatePart);
 *     }
 *   }
 *
 */
class OneObjectPerGroup : public EntityAttributes {
 public:
  /**
   * Constructs the tracker with a single overlapping partition.
   *
   * @param universe The universe containing partition definitions
   * @param overlapPartitionId The partition ID containing overlapping
   *        groups (objects may belong to multiple groups)
   * @param sourceContainer The source container (typically "unassigned")
   * @param initialAssignment The initial assignment to determine which
   *        container each object is placed in
   */
  explicit OneObjectPerGroup(
      const entities::Universe& universe,
      entities::PartitionId overlapPartitionId,
      entities::ContainerId sourceContainer,
      const Assignment& initialAssignment);

  /**
   * Checks if an object can be picked from the source container without
   * violating the one-object-per-group constraint.
   *
   * An object can be picked if and only if all groups it belongs to are
   * untouched (all their objects are still in the source container).
   *
   * @param candidateObject The object being considered for selection
   * @return true if the object can be picked, false if picking would violate
   *         the constraint
   */
  bool canPick(entities::ObjectId candidateObject) const;

  /**
   * Gets the number of objects assigned (moved out of source container) in a
   * group. Used for testing and debugging.
   *
   * @return Count of objects that have been moved out of the source container
   */
  int getAssignedCount(entities::GroupId groupId) const;

 protected:
  void setOn(entities::ObjectId objectId, entities::ContainerId dstContainer)
      override;

  void removeFrom(
      entities::ObjectId objectId,
      entities::ContainerId srcContainer) override;

 private:
  enum class MoveDirection {
    ToSource, // Object moved into source container
    FromSource, // Object moved out of source container
  };

  void initializeAssignedCounts(const Assignment& initialAssignment);
  void recordObjectMove(entities::ObjectId objectId, MoveDirection direction);

  const entities::Universe& universe_;
  entities::PartitionId overlapPartitionId_;
  const entities::ContainerId sourceContainer_;

  // Tracks count of objects moved out of source container per group
  folly::F14FastMap<entities::GroupId, int> assignedCounts_;
};

} // namespace facebook::rebalancer
