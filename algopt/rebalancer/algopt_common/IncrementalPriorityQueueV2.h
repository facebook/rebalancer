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

#include "algopt/rebalancer/algopt_common/alias.h"

#include <folly/container/Enumerate.h>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <list>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace facebook::algopt {

// Builds a ranking from a sequence of update() calls. To compare two items,
// consider the calls in order. The first call containing one item but not the
// other puts that item first. Items with the same sequence of appearances
// remain tied.
//
// Implementation: The ranking is stored as groups ordered from highest to
// lowest priority. Items within a group are tied. Within each group touched by
// an update, the updated items move into a new group immediately before the
// items left behind. New items form a group at the back. Later updates can
// therefore break ties, but cannot reverse ordering established by earlier
// updates.
//
// For example, starting with an empty queue:
//   update({a, b, c}) -> {a, b, c}
//   update({b, c})    -> {b, c} {a}
//   update({c})       -> {c} {b} {a}
//
// When the highest-priority group is tied, top() returns its smallest item
// according to operator<.
template <class T>
class IncrementalPriorityQueueV2 {
 public:
  IncrementalPriorityQueueV2() = default;
  ~IncrementalPriorityQueueV2() = default;

  IncrementalPriorityQueueV2(const IncrementalPriorityQueueV2& other)
      : priorityGroups_(other.priorityGroups_),
        itemToLocation_(other.itemToLocation_),
        size_(other.size_),
        sortedTopGroupItems_(other.sortedTopGroupItems_) {
    rebuildItemLocations();
  }

  IncrementalPriorityQueueV2& operator=(
      const IncrementalPriorityQueueV2& other) {
    if (this != &other) {
      *this = IncrementalPriorityQueueV2(other);
    }
    return *this;
  }

  IncrementalPriorityQueueV2(IncrementalPriorityQueueV2&&) = default;
  IncrementalPriorityQueueV2& operator=(IncrementalPriorityQueueV2&&) = default;

  size_t size() const {
    return size_;
  }

  bool empty() const {
    return size_ == 0;
  }

  const T& top() const {
    throwIfEmpty();
    return bestItemInTopGroup();
  }

  bool is_top_strict() const {
    throwIfEmpty();
    return priorityGroups_.front().items.size() == 1;
  }

  // Removing an item is permanent. Later updates ignore it.
  void remove(const T& item) {
    const auto removedLocation = markRemoved(item);
    if (!removedLocation) {
      return;
    }

    updateSortedTopGroupItemsAfterRemoval(removedLocation->groupIt, item);
    removeItemFromGroup(*removedLocation);
    eraseGroupIfEmpty(removedLocation->groupIt);
    --size_;
  }

  template <class Items>
  void update(const Items& items) {
    std::optional<PriorityGroupIt> newItemsGroupIt;
    std::vector<PriorityGroupIt> splitSourceGroups;

    for (const auto& item : items) {
      const auto [locationIt, isNewItem] = itemToLocation_.try_emplace(item);
      auto& itemLocation = locationIt->second;
      if (isNewItem) {
        addNewItem(item, itemLocation, newItemsGroupIt);
      } else if (itemLocation) {
        moveToNewGroupBeforeCurrentGroup(
            item, *itemLocation, splitSourceGroups);
      }
    }

    finishGroupSplits(splitSourceGroups);
  }

  void update(std::initializer_list<T> items) {
    update<std::initializer_list<T>>(items);
  }

 private:
  struct PriorityGroup;
  using PriorityGroupList = std::list<PriorityGroup>;
  using PriorityGroupIt = typename PriorityGroupList::iterator;

  struct PriorityGroup {
    std::vector<T> items;
    // Group receiving items split from this group by the current update.
    std::optional<PriorityGroupIt> updatedItemsGroupIt;
  };

  struct ItemLocation {
    PriorityGroupIt groupIt;
    size_t itemIndex;
  };

  void rebuildItemLocations() {
    for (auto groupIt = priorityGroups_.begin();
         groupIt != priorityGroups_.end();
         ++groupIt) {
      groupIt->updatedItemsGroupIt.reset();
      const auto& items = groupIt->items;
      for (auto&& [itemIndex, item] : folly::enumerate(items)) {
        itemToLocation_.at(item) =
            ItemLocation{.groupIt = groupIt, .itemIndex = itemIndex};
      }
    }
  }

  void throwIfEmpty() const {
    if (empty()) [[unlikely]] {
      throw std::runtime_error(
          "IncrementalPriorityQueueV2 is expected to be non-empty");
    }
  }

  const T& bestItemInTopGroup() const {
    const auto& items = priorityGroups_.front().items;
    if (items.size() == 1) {
      return items.front();
    }

    if (!sortedTopGroupItems_) {
      sortedTopGroupItems_ = items;
      std::sort(sortedTopGroupItems_->rbegin(), sortedTopGroupItems_->rend());
    }

    return sortedTopGroupItems_->back();
  }

  std::optional<ItemLocation> markRemoved(const T& item) {
    const auto [locationIt, isNewItem] = itemToLocation_.try_emplace(item);
    auto& itemLocation = locationIt->second;
    if (isNewItem || !itemLocation) {
      return std::nullopt;
    }

    return std::exchange(itemLocation, std::nullopt);
  }

  ItemLocation appendItemToGroup(const PriorityGroupIt groupIt, const T& item) {
    auto& items = groupIt->items;
    items.push_back(item);
    return ItemLocation{.groupIt = groupIt, .itemIndex = items.size() - 1};
  }

  void addNewItem(
      const T& item,
      std::optional<ItemLocation>& itemLocation,
      std::optional<PriorityGroupIt>& newItemsGroupIt) {
    if (!newItemsGroupIt) {
      newItemsGroupIt = insertGroupBefore(priorityGroups_.end());
    }
    itemLocation = appendItemToGroup(*newItemsGroupIt, item);
    ++size_;
  }

  void moveToNewGroupBeforeCurrentGroup(
      const T& item,
      ItemLocation& itemLocation,
      std::vector<PriorityGroupIt>& splitSourceGroups) {
    invalidateSortedTopGroupItemsIfNeeded(itemLocation.groupIt);
    removeItemFromGroup(itemLocation);
    itemLocation = appendItemToGroup(
        getOrCreateNewGroupBefore(itemLocation.groupIt, splitSourceGroups),
        item);
  }

  void removeItemFromGroup(const ItemLocation location) {
    auto& items = location.groupIt->items;
    const auto lastItemIndex = items.size() - 1;
    // Use swap-and-pop because items in a group are tied.
    if (location.itemIndex != lastItemIndex) {
      items[location.itemIndex] = std::move(items[lastItemIndex]);
      auto& movedItemLocation = itemToLocation_.at(items[location.itemIndex]);
      movedItemLocation->itemIndex = location.itemIndex;
    }
    items.pop_back();
  }

  void updateSortedTopGroupItemsAfterRemoval(
      const PriorityGroupIt groupIt,
      const T& item) {
    if (!sortedTopGroupItems_ || groupIt != priorityGroups_.begin()) {
      return;
    }
    if (sortedTopGroupItems_->back() == item) {
      sortedTopGroupItems_->pop_back();
    } else {
      sortedTopGroupItems_.reset();
    }
  }

  void invalidateSortedTopGroupItemsIfNeeded(const PriorityGroupIt groupIt) {
    if (sortedTopGroupItems_ && groupIt == priorityGroups_.begin()) {
      sortedTopGroupItems_.reset();
    }
  }

  PriorityGroupIt getOrCreateNewGroupBefore(
      const PriorityGroupIt sourceGroupIt,
      std::vector<PriorityGroupIt>& splitSourceGroups) {
    if (sourceGroupIt->updatedItemsGroupIt) {
      return *sourceGroupIt->updatedItemsGroupIt;
    }

    sourceGroupIt->updatedItemsGroupIt = insertGroupBefore(sourceGroupIt);
    splitSourceGroups.push_back(sourceGroupIt);
    return *sourceGroupIt->updatedItemsGroupIt;
  }

  void finishGroupSplits(
      const std::vector<PriorityGroupIt>& splitSourceGroups) {
    for (const auto sourceGroupIt : splitSourceGroups) {
      sourceGroupIt->updatedItemsGroupIt.reset();
      eraseGroupIfEmpty(sourceGroupIt);
    }
  }

  void eraseGroup(const PriorityGroupIt groupIt) {
    invalidateSortedTopGroupItemsIfNeeded(groupIt);
    recycledPriorityGroups_.splice(
        recycledPriorityGroups_.end(), priorityGroups_, groupIt);
  }

  void eraseGroupIfEmpty(const PriorityGroupIt groupIt) {
    if (groupIt->items.empty()) {
      eraseGroup(groupIt);
    }
  }

  PriorityGroupIt insertGroupBefore(const PriorityGroupIt nextGroupIt) {
    if (recycledPriorityGroups_.empty()) {
      return priorityGroups_.emplace(nextGroupIt);
    }

    const auto groupIt = recycledPriorityGroups_.begin();
    priorityGroups_.splice(nextGroupIt, recycledPriorityGroups_, groupIt);
    return groupIt;
  }

  PriorityGroupList priorityGroups_;
  // Reuse empty groups to preserve their list nodes and item-vector capacity
  // across repeated splits.
  PriorityGroupList recycledPriorityGroups_;
  // Removed items remain in the map with a null location so updates ignore
  // them.
  MapImpl<T, std::optional<ItemLocation>> itemToLocation_;
  size_t size_ = 0;

  // Sorting a copy preserves the item positions stored in itemToLocation_.
  // Items are kept worst-to-best so removing the best item is efficient.
  mutable std::optional<std::vector<T>> sortedTopGroupItems_;
};

} // namespace facebook::algopt
