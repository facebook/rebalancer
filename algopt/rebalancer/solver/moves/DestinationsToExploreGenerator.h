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

#include "algopt/rebalancer/algopt_common/Cache.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/solver/utils/Util.h"

namespace facebook::rebalancer {
template <typename T>
using ReferenceList = std::deque<std::reference_wrapper<T>>;

class DestinationsToExploreGenerator {
 public:
  DestinationsToExploreGenerator(
      const PackerSet<entities::ContainerId>& nonAcceptingContainers,
      const entities::Universe& universe);

  ~DestinationsToExploreGenerator() = default;

  ReferenceList<const std::vector<entities::ContainerId>>
  getAcceptingDestinations(
      const interface::MoveToCurrentScopeItemSpec& moveToCurrentScopeItem,
      entities::ContainerId hotContainer);

  ReferenceList<const std::vector<entities::ContainerId>>
  getAcceptingDestinations(
      const interface::MoveToScopeItemsSpec& moveToScopeItems,
      const entities::ContainerId hotContainer,
      const entities::ObjectId hotObject);

  ReferenceList<const std::vector<entities::ContainerId>>
  getAcceptingDestinations(
      const interface::MoveToScopeItemsSpec& moveToScopeItems,
      const entities::ContainerId hotContainer);

 private:
  ReferenceList<const std::vector<entities::ContainerId>>
  getAcceptingContainersList(
      const interface::ScopeItemList& scopeItemList,
      const entities::ContainerId hotContainer,
      std::optional<entities::ObjectId> hotObject);

  const std::vector<entities::ContainerId>& getAcceptingContainers(
      entities::ScopeItemId scopeItemId,
      const entities::Scope& scope);

 private:
  // use folly::F14NodeMap as it is the only F14 map with reference stability
  algopt::Cache<folly::F14NodeMap<
      entities::ScopeItemId,
      std::vector<entities::ContainerId>>>
      scopeItemToAcceptingDestinations_;

  const PackerSet<entities::ContainerId>& nonAcceptingContainers_;
  const entities::Universe& universe_;
};

} // namespace facebook::rebalancer
