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

#include "algopt/rebalancer/solver/utils/Assignment.h"

#include "algopt/rebalancer/entities/Partition.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/utils/Change.h"
#include "algopt/rebalancer/solver/utils/ChangeSet.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/IndexedAssignment.h"
#include "algopt/rebalancer/solver/utils/ObjectStore.h"

#include "multifeed/hash/HeterogeneousAccess.h"
#include "multifeed/hash/QuickHashSet.h"

#include <boost/iterator/iterator_categories.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <fmt/format.h>
#include <folly/container/MapUtil.h>
#include <folly/logging/LogStreamProcessor.h>
#include <folly/logging/xlog.h>

#include <functional>
#include <list>
#include <ostream>
#include <stdexcept>
#include <string>
#include <tuple>

namespace facebook::rebalancer {

namespace {
const static ObjectStore emptyObjectStore =
    ObjectStore::Factory(
        /*scores=*/nullptr,
        /*useDynamicObjectOrdering=*/false)
        .get();
}

Assignment::Assignment()
    : objectStoreFactory_(
          ObjectStore::Factory(
              /*scores=*/nullptr,
              /*useDynamicObjectOrdering=*/false)) {}

Assignment::Assignment(
    const PackerMap<entities::ContainerId, std::vector<entities::ObjectId>>&
        containerToObjects,
    std::shared_ptr<const ObjectScores> objectScores,
    PackerSet<entities::ObjectId> fixedObjects,
    bool useDynamicObjectOrdering)
    : fixedObjects_(std::move(fixedObjects)),
      objectStoreFactory_(
          ObjectStore::Factory(
              std::move(objectScores),
              useDynamicObjectOrdering)) {
  setContainerToObjectsMap(containerToObjects);
}

void Assignment::setOn(
    entities::ObjectId objectId,
    entities::ContainerId containerId) {
  objectToContainer_.insert_or_assign(objectId, containerId);
  getObjects(containerToObjects_, containerId).insert(objectId);

  if (!fixedObjects_.contains(objectId)) {
    getObjects(containerToObjectsDynamic_, containerId).insert(objectId);
    // update indices
    if (containerToEquivSetsToObjects_) {
      containerToEquivSetsToObjects_->setOn(objectId, containerId);
    }
    for (auto& [_, containerToGroupIdToObjects] :
         partitionToPerContainerObjectIndexByGroups_) {
      containerToGroupIdToObjects.setOn(objectId, containerId);
    }
    for (auto& [_, containerToEquivSetAndGroupIdToObjects] :
         partitionToPerContainerIndexByEquivSetAndGroups_) {
      containerToEquivSetAndGroupIdToObjects.setOn(objectId, containerId);
    }
  }
}

void Assignment::removeFrom(
    entities::ObjectId objectId,
    entities::ContainerId containerId) {
  objectToContainer_.erase(objectId);
  getObjects(containerToObjects_, containerId).erase(objectId);

  if (!fixedObjects_.contains(objectId)) {
    getObjects(containerToObjectsDynamic_, containerId).erase(objectId);
    // update indices
    if (containerToEquivSetsToObjects_) {
      containerToEquivSetsToObjects_->removeFrom(objectId, containerId);
    }
    for (auto& [_, containerToGroupIdToObjects] :
         partitionToPerContainerObjectIndexByGroups_) {
      containerToGroupIdToObjects.removeFrom(objectId, containerId);
    }
    for (auto& [_, containerToEquivSetAndGroupIdToObjects] :
         partitionToPerContainerIndexByEquivSetAndGroups_) {
      containerToEquivSetAndGroupIdToObjects.removeFrom(objectId, containerId);
    }
  }
}

void Assignment::moveTo(
    entities::ObjectId objectId,
    entities::ContainerId containerId) {
  removeFrom(objectId, getContainer(objectId));
  setOn(objectId, containerId);
}

const ObjectStore& Assignment::getObjects(
    entities::ContainerId containerId) const {
  return folly::get_ref_default(
      containerToObjects_, containerId, emptyObjectStore);
}

const ObjectStore& Assignment::getDynamicObjects(
    entities::ContainerId containerId) const {
  return folly::get_ref_default(
      containerToObjectsDynamic_, containerId, emptyObjectStore);
}

bool Assignment::isDynamic(entities::ObjectId objectId) const {
  return objectToContainer_.contains(objectId) &&
      !fixedObjects_.contains(objectId);
}

std::vector<entities::ObjectId> Assignment::getObjects() const {
  std::vector<entities::ObjectId> objects;
  for (auto& [objectId, containerId] : objectToContainer_) {
    objects.push_back(objectId);
  }
  return objects;
}

ChangeSet Assignment::getChanges() const {
  ChangeSet changes;
  for (auto& [objectId, containerId] : objectToContainer_) {
    changes.insert(Change(objectId, containerId, 1));
  }
  return changes;
}

bool Assignment::hasContainer(entities::ContainerId containerId) const {
  return containerToObjects_.contains(containerId);
}

bool Assignment::hasObject(entities::ObjectId objectId) const {
  return objectToContainer_.contains(objectId);
}

entities::ContainerId Assignment::getContainer(
    entities::ObjectId objectId) const {
  return objectToContainer_.at(objectId);
}

int Assignment::delta(const Assignment& other) const {
  int sum = 0;
  for (auto& [objectId, containerId] : objectToContainer_) {
    if (other.getContainer(objectId) != containerId) {
      sum++;
    }
  }
  return sum;
}

const PackerMap<entities::ObjectId, entities::ContainerId>&
Assignment::getObjectToContainerMap() const {
  return objectToContainer_;
}

const PackerMap<entities::ContainerId, ObjectStore>&
Assignment::getContainerToObjectsMap() const {
  return containerToObjects_;
}

ObjectStore& Assignment::getObjects(
    PackerMap<entities::ContainerId, ObjectStore>& containerToObjects,
    entities::ContainerId containerId) {
  auto objects = containerToObjects.find(containerId);
  if (objects == containerToObjects.end()) {
    auto result =
        containerToObjects.emplace(containerId, objectStoreFactory_.get());
    return result.first->second;
  }
  return objects->second;
}

void Assignment::buildIndexByEquivalentSets(
    const EquivalenceSets& equivalenceSets) {
  if (containerToEquivSetsToObjects_) {
    XLOG(DBG1) << "Rebuilding assignment index by equivalence sets";
    containerToEquivSetsToObjects_.reset();
  }
  auto getObjectIdxFunc = [&equivalenceSets](entities::ObjectId object) {
    return equivalenceSets.at(object);
  };
  containerToEquivSetsToObjects_ =
      AssignmentIndexedByEquivSet(containerToObjectsDynamic_, getObjectIdxFunc);
}

const AssignmentIndexedByEquivSet&
Assignment::maybeBuildAndGetObjectsIndexedByEquivSets(
    const EquivalenceSets& equivalenceSets) {
  if (!containerToEquivSetsToObjects_) {
    buildIndexByEquivalentSets(equivalenceSets);
  }
  return *containerToEquivSetsToObjects_;
}

const AssignmentIndexedByEquivSet& Assignment::getObjectsIndexedByEquivSets()
    const {
  if (containerToEquivSetsToObjects_) {
    return *containerToEquivSetsToObjects_;
  }
  throw std::runtime_error(
      "Assignment index by equivalence sets is not built yet");
}

std::vector<entities::ObjectId>
Assignment::maybeBuildEquivSetIdxAndGetDistinctObjects(
    const entities::ContainerId& containerId,
    const EquivalenceSets& equivalenceSets) {
  const auto& equivSetsToObjects =
      maybeBuildAndGetObjectsIndexedByEquivSets(equivalenceSets)
          .getContainerObjects(containerId);
  std::vector<entities::ObjectId> distinctObjectIds;
  for (auto& [_, objectIds] : equivSetsToObjects) {
    distinctObjectIds.push_back(*objectIds.begin());
  }
  return distinctObjectIds;
}

void Assignment::buildIndexByPartition(
    const entities::Universe& universe,
    entities::PartitionId partitionId) {
  auto objIndexPtr =
      folly::get_ptr(partitionToPerContainerObjectIndexByGroups_, partitionId);
  if (objIndexPtr) {
    XLOG(DBG1) << fmt::format(
        "Index for partition {} is already built", partitionId);
    return;
  }
  auto& partition = universe.getPartition(partitionId);
  auto getObjectIdxFunc = [&partition](entities::ObjectId object) {
    // assumes that every object belongs to a single group
    auto& groupsForThisObject = partition.getObjectIdToGroupIds().at(object);
    if (groupsForThisObject.size() != 1) {
      throw std::runtime_error(
          "Building assignment index by partition failed: object belongs to multiple groups");
    }
    return groupsForThisObject.at(0);
  };
  partitionToPerContainerObjectIndexByGroups_.emplace(
      partitionId,
      AssignmentIndexedByGroupId(containerToObjectsDynamic_, getObjectIdxFunc));
}

const AssignmentIndexedByGroupId& Assignment::getObjectsIndexedByPartition(
    entities::PartitionId partitionId) const {
  if (auto objIndexPtr = folly::get_ptr(
          partitionToPerContainerObjectIndexByGroups_, partitionId)) {
    return *objIndexPtr;
  }
  throw std::runtime_error(
      fmt::format(
          "Assignment index by partition {} is not built yet", partitionId));
}

void Assignment::buildIndexByEquivalentSetsAndPartition(
    const entities::Universe& universe,
    const EquivalenceSets& equivalenceSets,
    entities::PartitionId partitionId) {
  auto objIndexPtr = folly::get_ptr(
      partitionToPerContainerIndexByEquivSetAndGroups_, partitionId);

  if (objIndexPtr) {
    // because equivalent sets can change, we need to rebuild the index
    XLOG(DBG1)
        << "Rebuilding assignment index by equivalence sets and partition "
        << universe.getEntityName(partitionId);
    partitionToPerContainerIndexByEquivSetAndGroups_.erase(partitionId);
  }

  auto& partition = universe.getPartition(partitionId);
  auto getObjectIdxFunc = [&partition,
                           &equivalenceSets](entities::ObjectId object) {
    // assumes that every object belongs to a single group
    auto& groupsForThisObject = partition.getObjectIdToGroupIds().at(object);
    if (groupsForThisObject.size() != 1) {
      throw std::runtime_error(
          "Building assignment index by partition + equivSets failed: object belongs to multiple groups");
    }
    return std::make_tuple(
        equivalenceSets.at(object), groupsForThisObject.at(0));
  };

  partitionToPerContainerIndexByEquivSetAndGroups_.emplace(
      partitionId,
      AssignmentIndexedByEquivSetAndGroupId(
          containerToObjectsDynamic_, getObjectIdxFunc));

  XLOG(INFO) << "Built assignment index by equivalence sets and partition "
             << universe.getEntityName(partitionId);
}

const AssignmentIndexedByEquivSetAndGroupId&
Assignment::getObjectsIndexedByEquivSetsAndPartition(
    entities::PartitionId partitionId) const {
  if (auto ptr = folly::get_ptr(
          partitionToPerContainerIndexByEquivSetAndGroups_, partitionId)) {
    return *ptr;
  }
  throw std::runtime_error(
      fmt::format(
          "Assignment index by equivalence sets and partition {} is not built yet",
          partitionId));
}

void Assignment::markFixedAndRemoveFromDynamicIndices(
    entities::ContainerId containerId,
    const PackerSet<entities::ObjectId>& containerObjects) {
  for (auto objectId : containerObjects) {
    if (objectToContainer_.at(objectId) != containerId) {
      throw std::runtime_error(
          "Object to fix must belong to provide container");
    }
    auto [_iter, inserted] = fixedObjects_.insert(objectId);
    if (!inserted) {
      // object already fixed, nothing to do
      continue;
    }
    containerToObjectsDynamic_.at(containerId).erase(objectId);
    // Remove from Index#1 : EquivSetId -> Objects
    if (containerToEquivSetsToObjects_) {
      containerToEquivSetsToObjects_->removeFrom(objectId, containerId);
    }
    // Remove from Index#2 : EquivSetId -> GroupId -> Objects
    for (auto& [_partition, indexedByEquivSetAndGroup] :
         partitionToPerContainerIndexByEquivSetAndGroups_) {
      indexedByEquivSetAndGroup.removeFrom(objectId, containerId);
    }
    // Remove from Index#3 : GroupId -> Objects
    for (auto& [_partition, indexedByGroup] :
         partitionToPerContainerObjectIndexByGroups_) {
      indexedByGroup.removeFrom(objectId, containerId);
    }
  }
}

const ObjectStore::Factory& Assignment::getObjectStoreFactory() const {
  return objectStoreFactory_;
}

} // namespace facebook::rebalancer
