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

#include "algopt/rebalancer/solver/moves/DestinationsToExploreGenerator.h"

namespace facebook::rebalancer {

namespace {
// Keeps only the scope items that compare better than the moving object's
// current one on a chosen dimension. See ScopeItemList.filter.
class ObjectDimensionBasedFilter {
 public:
  ObjectDimensionBasedFilter(
      const interface::ObjectDimensionBasedFilter& filter,
      const entities::ObjectScalarDimension& dimension,
      entities::ScopeId scopeId,
      entities::ObjectId object,
      std::optional<entities::ScopeItemId> currentScopeItem)
      : filter_{filter},
        dimension_{dimension},
        scopeId_{scopeId},
        object_{object} {
    if (currentScopeItem.has_value()) {
      currentValue_ = valueAt(*currentScopeItem);
    }
  }

  bool includes(entities::ScopeItemId scopeItemId) const {
    // An object outside the scope has no current value to beat, so keep all.
    if (!currentValue_.has_value()) {
      return true;
    }
    const auto candidate = valueAt(scopeItemId);
    if (candidate == *currentValue_) {
      return *filter_.includeEqual();
    }
    return *filter_.preferLower() ? candidate < *currentValue_
                                  : candidate > *currentValue_;
  }

 private:
  double valueAt(entities::ScopeItemId scopeItemId) const {
    return dimension_.getValueSafe(
        object_,
        entities::ScopeScopeItemPair{
            .scopeId = scopeId_, .scopeItemId = scopeItemId});
  }

  const interface::ObjectDimensionBasedFilter& filter_;
  const entities::ObjectScalarDimension& dimension_;
  entities::ScopeId scopeId_;
  entities::ObjectId object_;
  std::optional<double> currentValue_;
};
} // namespace

DestinationsToExploreGenerator::DestinationsToExploreGenerator(
    const PackerSet<entities::ContainerId>& nonAcceptingContainers,
    const entities::Universe& universe)
    : nonAcceptingContainers_(nonAcceptingContainers), universe_(universe) {}

ReferenceList<const std::vector<entities::ContainerId>>
DestinationsToExploreGenerator::getAcceptingDestinations(
    const interface::MoveToCurrentScopeItemSpec& moveToCurrentScopeItem,
    entities::ContainerId hotContainerId) {
  auto& scopeName =
      *moveToCurrentScopeItem.scopeNameForExploringMovesToCurrentScopeItem();

  auto scopeId = universe_.getScopeId(scopeName);
  auto& scope = universe_.getScope(scopeId);
  auto scopeItemId = scope.getScopeItemId(hotContainerId);
  if (!scopeItemId.has_value()) {
    // if hotContainer is not in the given scope, we explore all containers
    // in the given scope
    interface::ScopeItemList scopeItemList;
    scopeItemList.scopeName() = scopeName;
    return getAcceptingContainersList(
        scopeItemList, hotContainerId, /*hotObject=*/std::nullopt);
  }

  return ReferenceList<const std::vector<entities::ContainerId>>{
      getAcceptingContainers(scopeItemId.value(), scope)};
}

ReferenceList<const std::vector<entities::ContainerId>>
DestinationsToExploreGenerator::getAcceptingDestinations(
    const interface::MoveToScopeItemsSpec& moveToScopeItems,
    const entities::ContainerId hotContainer,
    const entities::ObjectId hotObject) {
  auto& objectToScopeItems = *moveToScopeItems.objectToScopeItems();
  auto& hotObjectName = universe_.getEntityName(hotObject);

  // if an object is specified in objectToScopeItems, use that.
  auto scopeItemListPtr = folly::get_ptr(objectToScopeItems, hotObjectName);
  if (scopeItemListPtr) {
    return getAcceptingContainersList(
        *scopeItemListPtr, hotContainer, hotObject);
  }

  // if a group is specified in groupToScopeItems and hotObject belongs to that
  // group, then use that.
  auto& defaultScopeItems = *moveToScopeItems.defaultScopeItems();
  auto& scopeItemsPerGroup = *moveToScopeItems.scopeItemsPerGroups();
  auto& groupToScopeItem = *scopeItemsPerGroup.groupToScopeItemList();
  if (!groupToScopeItem.empty()) {
    auto& partitionName = *scopeItemsPerGroup.partitionName();
    auto partitionId = universe_.getPartitionId(partitionName);
    auto& partition = universe_.getPartition(partitionId);
    auto& objectIdToGroupIds = partition.getObjectIdToGroupIds();
    auto groupIdsPtr = folly::get_ptr(objectIdToGroupIds, hotObject);
    if (groupIdsPtr && groupIdsPtr->size() > 0) {
      if (groupIdsPtr->size() > 1) {
        throw std::runtime_error(
            fmt::format(
                "groupToScopeItemList in MoveToScopeItemsSpec is not supported when objects (e.g., '{}') belong to multiple groups in partition '{}'",
                hotObjectName,
                partitionName));
      }

      auto& onlyGroupId = groupIdsPtr->at(0);
      auto& scopeItemList = folly::get_ref_default(
          groupToScopeItem,
          universe_.getEntityName(onlyGroupId),
          defaultScopeItems);
      return getAcceptingContainersList(scopeItemList, hotContainer, hotObject);
    }
  }

  // use defaultScopeItems if there is no specialization for hotObject
  return getAcceptingContainersList(defaultScopeItems, hotContainer, hotObject);
}

ReferenceList<const std::vector<entities::ContainerId>>
DestinationsToExploreGenerator::getAcceptingDestinations(
    const interface::MoveToScopeItemsSpec& moveToScopeItems,
    const entities::ContainerId hotContainer) {
  if (!moveToScopeItems.objectToScopeItems()->empty()) {
    throw std::runtime_error(
        "this function requires that objectToScopeItems is empty");
  }
  return getAcceptingContainersList(
      *moveToScopeItems.defaultScopeItems(),
      hotContainer,
      /*hotObject=*/std::nullopt);
}

ReferenceList<const std::vector<entities::ContainerId>>
DestinationsToExploreGenerator::getAcceptingContainersList(
    const interface::ScopeItemList& scopeItemList,
    const entities::ContainerId hotContainer,
    std::optional<entities::ObjectId> hotObject) {
  auto& scopeName = *scopeItemList.scopeName();
  auto scopeId = universe_.getScopeId(scopeName);
  auto& scope = universe_.getScope(scopeId);

  std::optional<ObjectDimensionBasedFilter> objectDimensionFilter;
  if (scopeItemList.filter().has_value()) {
    if (!hotObject.has_value()) {
      throw std::runtime_error(
          "ScopeItemList.filter requires the moving object, but this move type does not provide one");
    }
    const auto& filter = *scopeItemList.filter();
    switch (filter.getType()) {
      case interface::ScopeItemFilter::Type::objectDimensionBased: {
        const auto& filterSpec = filter.get_objectDimensionBased();
        const auto& dimension = universe_.getObjects()
                                    .getDimension(universe_.getDimensionId(
                                        *filterSpec.dimensionName()))
                                    .only();
        if (!dimension.isDynamic() || dimension.getScopeId() != scopeId)
            [[unlikely]] {
          throw std::runtime_error(
              fmt::format(
                  "ObjectDimensionBasedFilter dimension '{}' is expected to be dynamic on scope '{}'",
                  *filterSpec.dimensionName(),
                  scopeName));
        }
        const auto hotContainerScopeItem = scope.getScopeItemId(hotContainer);
        objectDimensionFilter.emplace(
            filterSpec, dimension, scopeId, *hotObject, hotContainerScopeItem);
        break;
      }
      case interface::ScopeItemFilter::Type::__EMPTY__:
        throw std::runtime_error("ScopeItemFilter is not set");
    }
  }

  ReferenceList<const std::vector<entities::ContainerId>> destinations;
  const auto addToDestinations = [&](entities::ScopeItemId scopeItemId) {
    if (!objectDimensionFilter.has_value() ||
        objectDimensionFilter->includes(scopeItemId)) {
      destinations.emplace_back(getAcceptingContainers(scopeItemId, scope));
    }
  };

  if (scopeItemList.scopeItems().has_value()) {
    const auto& scopeItemNames = scopeItemList.scopeItems().value();
    for (const auto& scopeItemName : scopeItemNames) {
      addToDestinations(universe_.getScopeItemId(scopeId, scopeItemName));
    }
  } else {
    // if scopeItems are not explicitly listed, all scopeItems in the specified
    // scopeName are taken
    auto& scopeItemIds = scope.getScopeItemIds();
    for (auto scopeItemId : scopeItemIds) {
      addToDestinations(scopeItemId);
    }
  }
  return destinations;
}

const std::vector<entities::ContainerId>&
DestinationsToExploreGenerator::getAcceptingContainers(
    entities::ScopeItemId scopeItemId,
    const entities::Scope& scope) {
  return scopeItemToAcceptingDestinations_.getSavedOrCompute(
      scopeItemId, [&]() {
        auto& containerIds = scope.getContainerIds(scopeItemId);
        std::vector<entities::ContainerId> destinations;
        std::copy_if(
            containerIds.begin(),
            containerIds.end(),
            std::back_inserter(destinations),
            [this](entities::ContainerId containerId) {
              return !nonAcceptingContainers_.contains(containerId);
            });

        return destinations;
      });
}

} // namespace facebook::rebalancer
