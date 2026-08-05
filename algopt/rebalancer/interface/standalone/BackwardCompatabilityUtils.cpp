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

#include "algopt/rebalancer/interface/standalone/BackwardCompatabilityUtils.h"

#include "algopt/rebalancer/interface/thrift/ThriftUtils.h"

#include <fmt/core.h>
#include <folly/container/F14Map.h>
#include <folly/Conv.h>
#include <folly/Portability.h>

#include <algorithm>
#include <ranges>

namespace thriftUtils = facebook::rebalancer::interface::thriftUtils;

namespace {

using OldToNewId = folly::F14FastMap<int, int>;

struct OldToNewIds {
  OldToNewId objects;
  OldToNewId containers;
  OldToNewId scopes;
  OldToNewId scopeItems;
  OldToNewId partitions;
  OldToNewId groups;
  OldToNewId dimensions;
  OldToNewId goals;
  OldToNewId constraints;
  OldToNewId routingConfigs;
};

// Builds oldId -> newId, where newId is the position in `oldIdsInOrder`.
template <std::ranges::input_range R>
OldToNewId buildOldToNewId(R&& oldIdsInOrder) {
  OldToNewId oldToNewId;
  if constexpr (std::ranges::sized_range<R>) {
    oldToNewId.reserve(std::ranges::size(oldIdsInOrder));
  }
  int newId = 0;
  for (const auto oldId : oldIdsInOrder) {
    oldToNewId.emplace(oldId, newId++);
  }
  return oldToNewId;
}

// Returns the names corresponding to `oldIdsInOrder`, in order.
template <std::ranges::input_range R>
std::vector<std::string> lookupNames(
    R&& oldIdsInOrder,
    const std::vector<std::string>& allNames) {
  std::vector<std::string> names;
  if constexpr (std::ranges::sized_range<R>) {
    names.reserve(std::ranges::size(oldIdsInOrder));
  }
  for (const auto oldId : oldIdsInOrder) {
    names.push_back(allNames.at(oldId));
  }
  return names;
}

void remapIdList(const OldToNewId& oldToNewId, std::vector<int32_t>& ids) {
  for (auto& id : ids) {
    id = oldToNewId.at(id);
  }
}

// Rebuild a map with keys remapped, optionally transforming each value in place
// before insertion.
template <typename T, typename ValueFn>
void remapKeys(
    folly::F14FastMap<int, T>& map,
    const OldToNewId& keyOldToNewId,
    ValueFn valueFn) {
  folly::F14FastMap<int, T> newMap;
  newMap.reserve(map.size());
  for (auto& [oldKey, value] : map) {
    valueFn(value);
    newMap.emplace(keyOldToNewId.at(oldKey), std::move(value));
  }
  map = std::move(newMap);
}

template <typename T>
void remapKeys(
    folly::F14FastMap<int, T>& map,
    const OldToNewId& keyOldToNewId) {
  remapKeys(map, keyOldToNewId, [](T&) {});
}

// Specialized for map<id, vector<id>> where both keys and inner values are ids.
void remapIdMap(
    folly::F14FastMap<int, std::vector<int>>& map,
    const OldToNewId& keyOldToNewId,
    const OldToNewId& valueOldToNewId) {
  remapKeys(map, keyOldToNewId, [&](std::vector<int>& v) {
    remapIdList(valueOldToNewId, v);
  });
}

// Reading the deprecated `IdStore.names` field is the whole point of this
// file: old-format bundles store all entity names there, and we have to read
// them to migrate to per-type name vectors.
FOLLY_PUSH_WARNING
FOLLY_GNU_DISABLE_WARNING("-Wdeprecated-declarations")

// Remaps the id references inside IdStore. The new per-type name vectors are
// written separately by the caller (which holds `oldNames`).
void remapIds(
    entities::thrift::IdStore& idStore,
    const OldToNewIds& oldToNewIds) {
  remapIdList(oldToNewIds.objects, *idStore.objectIds());
  remapIdList(oldToNewIds.containers, *idStore.containerIds());
  remapIdList(oldToNewIds.dimensions, *idStore.dimensionIds());
  remapIdList(oldToNewIds.goals, *idStore.goalIds());
  remapIdList(oldToNewIds.constraints, *idStore.constraintIds());
  remapIdList(oldToNewIds.routingConfigs, *idStore.routingConfigIds());
  remapIdMap(
      *idStore.scopeItemIds(), oldToNewIds.scopes, oldToNewIds.scopeItems);
  remapIdMap(*idStore.groupIds(), oldToNewIds.partitions, oldToNewIds.groups);
}

void populateScopedValuesFromLegacyValues(
    entities::thrift::ObjectDynamicDimension& dyn) {
  if (!dyn.scopedValues()->empty()) {
    return;
  }

  // NOLINTNEXTLINE(facebook-hte-Deprecated)
  auto& legacyValues = *dyn.values();
  folly::F14FastMap<int, entities::thrift::ObjectValues> scopedValues;
  scopedValues.reserve(legacyValues.size());
  for (auto& [scopeItemId, objectValues] : legacyValues) {
    entities::thrift::ObjectValues values;
    values.objectValues() = std::move(objectValues);
    scopedValues.emplace(scopeItemId, std::move(values));
  }
  dyn.scopedValues() = std::move(scopedValues);
  legacyValues.clear();
}

void remapIds(
    entities::thrift::ObjectScalarDimension& scalar,
    const OldToNewIds& oldToNewIds) {
  using Type = entities::thrift::ObjectScalarDimension::Type;
  switch (scalar.getType()) {
    case Type::objectStaticDimension:
      remapKeys(
          *scalar.mutable_objectStaticDimension().values(),
          oldToNewIds.objects);
      return;
    case Type::objectDynamicDimension: {
      auto& dyn = scalar.mutable_objectDynamicDimension();
      dyn.scopeId() = oldToNewIds.scopes.at(*dyn.scopeId());
      populateScopedValuesFromLegacyValues(dyn);
      remapKeys(
          *dyn.scopedValues(), oldToNewIds.scopeItems, [&](auto& objValues) {
            remapKeys(*objValues.objectValues(), oldToNewIds.objects);
          });
      return;
    }
    case Type::objectPartitionRoutingDimension: {
      auto& oprd = scalar.mutable_objectPartitionRoutingDimension();
      oprd.partitionId() = oldToNewIds.partitions.at(*oprd.partitionId());
      oprd.routingConfigId() =
          oldToNewIds.routingConfigs.at(*oprd.routingConfigId());
      remapKeys(*oprd.groupIdToValue(), oldToNewIds.groups);
      remapKeys(*oprd.groupIdToStaticValue(), oldToNewIds.groups);
      return;
    }
    case Type::__EMPTY__:
      return;
  }
  throw std::runtime_error(
      fmt::format(
          "Unhandled ObjectScalarDimension::Type: {}",
          static_cast<int>(scalar.getType())));
}

void remapIds(
    entities::thrift::Objects& objects,
    const OldToNewIds& oldToNewIds) {
  remapIdList(oldToNewIds.objects, *objects.objectIds());
  remapKeys(*objects.dimensions(), oldToNewIds.dimensions, [&](auto& dim) {
    for (auto& scalar : *dim.scalarDimensions()) {
      remapIds(scalar, oldToNewIds);
    }
  });
}

void remapIds(
    entities::thrift::Containers& containers,
    const OldToNewIds& oldToNewIds) {
  remapIdMap(
      *containers.initialAssignment(),
      oldToNewIds.containers,
      oldToNewIds.objects);
}

void remapIds(entities::thrift::Scope& scope, const OldToNewIds& oldToNewIds) {
  remapIdMap(
      *scope.scopeItems(), oldToNewIds.scopeItems, oldToNewIds.containers);
  remapKeys(*scope.dimensions(), oldToNewIds.dimensions, [&](auto& scopeDim) {
    remapKeys(*scopeDim.values(), oldToNewIds.scopeItems);
  });
}

void remapIds(
    entities::thrift::Scopes& scopes,
    const OldToNewIds& oldToNewIds) {
  remapKeys(*scopes.scopes(), oldToNewIds.scopes, [&](auto& scope) {
    remapIds(scope, oldToNewIds);
  });
}

void remapIds(
    entities::thrift::Partitions& partitions,
    const OldToNewIds& oldToNewIds) {
  remapKeys(
      *partitions.partitions(), oldToNewIds.partitions, [&](auto& partition) {
        remapIdMap(
            *partition.groups(), oldToNewIds.groups, oldToNewIds.objects);
      });
}

void remapIds(
    entities::thrift::RoutingConfig& config,
    const OldToNewIds& oldToNewIds) {
  config.scopeId() = oldToNewIds.scopes.at(*config.scopeId());
  config.partitionId() = oldToNewIds.partitions.at(*config.partitionId());

  remapKeys(
      *config.latencyTable(), oldToNewIds.scopeItems, [&](auto& innerMap) {
        remapKeys(innerMap, oldToNewIds.scopeItems);
      });

  remapKeys(
      *config.groupToRoutingRingsEntities(),
      oldToNewIds.groups,
      [&](auto& rings) {
        for (auto& ring : *rings.routingRings()) {
          ring.originScopeItem() =
              oldToNewIds.scopeItems.at(*ring.originScopeItem());
          if (ring.destinationScopeItemSets().has_value()) {
            for (auto& destSet : *ring.destinationScopeItemSets()) {
              remapIdList(oldToNewIds.scopeItems, destSet);
            }
          }
        }
      });

  if (config.defaultOriginToDestinationScopeItemSets().has_value()) {
    remapKeys(
        *config.defaultOriginToDestinationScopeItemSets(),
        oldToNewIds.scopeItems,
        [&](auto& destSets) {
          for (auto& destSet : destSets) {
            remapIdList(oldToNewIds.scopeItems, destSet);
          }
        });
  }
}

} // namespace

void BackwardCompatabilityUtils::densifyEntityIds(
    entities::thrift::Universe& universe) {
  auto& idStore = *universe.idStore();
  // deprecated 'names' field is non-empty only for old-format bundles. So, if
  // empty, we're done.
  // NOLINTNEXTLINE(facebook-hte-Deprecated)
  const auto& oldNames = *idStore.names();
  if (oldNames.empty()) {
    return;
  }

  // Phase 1: collect old ids per entity type.
  const auto& oldObjectIds = *idStore.objectIds();
  const auto& oldContainerIds = *idStore.containerIds();
  const auto oldScopeIds = *idStore.scopeItemIds() | std::views::keys;
  const auto oldScopeItemIds =
      *idStore.scopeItemIds() | std::views::values | std::views::join;
  const auto oldPartitionIds = *idStore.groupIds() | std::views::keys;
  const auto oldGroupIds =
      *idStore.groupIds() | std::views::values | std::views::join;
  const auto& oldDimensionIds = *idStore.dimensionIds();
  const auto& oldGoalIds = *idStore.goalIds();
  const auto& oldConstraintIds = *idStore.constraintIds();
  const auto& oldRoutingConfigIds = *idStore.routingConfigIds();

  // Phase 2a: build oldId -> newId remaps.
  const OldToNewIds oldToNewIds{
      .objects = buildOldToNewId(oldObjectIds),
      .containers = buildOldToNewId(oldContainerIds),
      .scopes = buildOldToNewId(oldScopeIds),
      .scopeItems = buildOldToNewId(oldScopeItemIds),
      .partitions = buildOldToNewId(oldPartitionIds),
      .groups = buildOldToNewId(oldGroupIds),
      .dimensions = buildOldToNewId(oldDimensionIds),
      .goals = buildOldToNewId(oldGoalIds),
      .constraints = buildOldToNewId(oldConstraintIds),
      .routingConfigs = buildOldToNewId(oldRoutingConfigIds),
  };

  // Phase 2b: write the new per-type name vectors.
  idStore.objectNames() = lookupNames(oldObjectIds, oldNames);
  idStore.containerNames() = lookupNames(oldContainerIds, oldNames);
  idStore.scopeNames() = lookupNames(oldScopeIds, oldNames);
  idStore.scopeItemNames() = lookupNames(oldScopeItemIds, oldNames);
  idStore.partitionNames() = lookupNames(oldPartitionIds, oldNames);
  idStore.groupNames() = lookupNames(oldGroupIds, oldNames);
  idStore.dimensionNames() = lookupNames(oldDimensionIds, oldNames);
  idStore.goalNames() = lookupNames(oldGoalIds, oldNames);
  idStore.constraintNames() = lookupNames(oldConstraintIds, oldNames);
  idStore.routingConfigNames() = lookupNames(oldRoutingConfigIds, oldNames);

  // clear the old names field
  // NOLINTNEXTLINE(facebook-hte-Deprecated)
  idStore.names()->clear();

  // Phase 3: apply the remaps to every id-bearing field in the universe.
  remapIds(idStore, oldToNewIds);
  remapIds(*universe.objects(), oldToNewIds);
  remapIds(*universe.containers(), oldToNewIds);
  remapIds(*universe.scopes(), oldToNewIds);
  remapIds(*universe.partitions(), oldToNewIds);
  remapKeys(*universe.goals()->goals(), oldToNewIds.goals);
  remapKeys(*universe.constraints()->constraints(), oldToNewIds.constraints);

  if (universe.similarContainerIds().has_value()) {
    for (auto& containerIds : *universe.similarContainerIds()) {
      remapIdList(oldToNewIds.containers, containerIds);
    }
  }

  remapIdList(oldToNewIds.containers, *universe.descendingHotnessContainers());

  if (universe.objectOrderingDimensionId().has_value()) {
    universe.objectOrderingDimensionId() =
        oldToNewIds.dimensions.at(*universe.objectOrderingDimensionId());
  }

  remapKeys(
      *universe.routingConfigIdToRoutingConfig(),
      oldToNewIds.routingConfigs,
      [&](auto& config) { remapIds(config, oldToNewIds); });
}

FOLLY_POP_WARNING

void BackwardCompatabilityUtils::possiblyModify(
    entities::thrift::Universe& universe) {
  densifyEntityIds(universe);

  // Bundles serialized before `numObjects` was introduced will have
  // objects.numObjects()=0, so populate `numObjects` if it's missing.
  auto& objects = *universe.objects();
  if (objects.numObjects() == 0) {
    objects.numObjects() =
        folly::to<int32_t>(universe.idStore()->objectIds()->size());
  }
  for (auto& [_, dimension] : *objects.dimensions()) {
    for (auto& scalar : *dimension.scalarDimensions()) {
      if (scalar.getType() ==
          entities::thrift::ObjectScalarDimension::Type::
              objectDynamicDimension) {
        populateScopedValuesFromLegacyValues(
            scalar.mutable_objectDynamicDimension());
      }
    }
  }

  possiblyModify(*universe.goals());
  possiblyModify(*universe.constraints());
}

void BackwardCompatabilityUtils::possiblyModify(
    entities::thrift::Goals& goals) {
  for (auto& [_, goal] : *goals.goals()) {
    possiblyModify(*goal.spec());
  }
}

void BackwardCompatabilityUtils::possiblyModify(
    entities::thrift::Constraints& constraints) {
  for (auto& [_, constraint] : *constraints.constraints()) {
    possiblyModify(*constraint.spec());
  }
}

void BackwardCompatabilityUtils::possiblyModify(
    interface::GoalSpecs& specUnion) {
  if (specUnion.getType() == interface::GoalSpecs::Type::routingLatencySpec) {
    possiblyModify(specUnion.mutable_routingLatencySpec());
  } else if (
      specUnion.getType() ==
      interface::GoalSpecs::Type::exclusiveScopeItemsSpec) {
    possiblyModify(specUnion.mutable_exclusiveScopeItemsSpec());
  } else if (
      specUnion.getType() ==
      interface::GoalSpecs::Type::minimizeContainersSpec) {
    possiblyModify(specUnion.mutable_minimizeContainersSpec());
  }
}

void BackwardCompatabilityUtils::possiblyModify(
    interface::ConstraintSpecs& specUnion) {
  if (specUnion.getType() ==
      interface::ConstraintSpecs::Type::routingLatencySpec) {
    possiblyModify(specUnion.mutable_routingLatencySpec());
  } else if (
      specUnion.getType() ==
      interface::ConstraintSpecs::Type::exclusiveScopeItemsSpec) {
    possiblyModify(specUnion.mutable_exclusiveScopeItemsSpec());
  }
}

void BackwardCompatabilityUtils::possiblyModify(
    interface::RoutingLatencySpec& spec) {
  if (*spec.metric() == interface::RoutingLatencyMetric::P99) {
    spec.latencyMetric() = thriftUtils::makeRoutingLatencyMetric(
        interface::RoutingLatencyMetric::PERCENTILE, 99);
  } else if (*spec.metric() == interface::RoutingLatencyMetric::MAX) {
    spec.latencyMetric() = thriftUtils::makeRoutingLatencyMetric(
        interface::RoutingLatencyMetric::PERCENTILE, 100);
  }
}

// Migrate the deprecated `maxFreeLimit` field into the `target` union. Reading
// the deprecated field is the whole point here, so the deprecation warning is
// suppressed (mirrors the IdStore.names migration above).
FOLLY_PUSH_WARNING
FOLLY_GNU_DISABLE_WARNING("-Wdeprecated-declarations")
void BackwardCompatabilityUtils::possiblyModify(
    interface::MinimizeContainersSpec& spec) {
  // NOLINTNEXTLINE(facebook-hte-Deprecated)
  if (spec.maxFreeLimit() && !spec.target()) {
    interface::MinimizeContainersTarget target;
    // NOLINTNEXTLINE(facebook-hte-Deprecated)
    target.set_maxFreeLimit(*spec.maxFreeLimit());
    spec.target() = std::move(target);
  }
  // NOLINTNEXTLINE(facebook-hte-Deprecated)
  spec.maxFreeLimit().reset();
}
FOLLY_POP_WARNING

void BackwardCompatabilityUtils::possiblyModify(
    interface::ExclusiveScopeItemsSpec& spec) {
  if (spec.conflictInfoList()->empty() && !spec.pairs()->empty()) {
    std::vector<interface::ScopeItemConflictInfo> conflictInfoList;
    conflictInfoList.reserve(spec.pairs()->size());
    for (const auto& pair : *spec.pairs()) {
      interface::ScopeItemConflictInfo info;
      info.scopeItem() = *pair.scopeItem1();
      interface::ConflictingScopeItemInfo conflictingInfo;
      conflictingInfo.conflictingScopeItem() = *pair.scopeItem2();
      info.conflictingScopeItemsWithOverlap() = {conflictingInfo};
      conflictInfoList.push_back(std::move(info));
    }
    spec.conflictInfoList() = std::move(conflictInfoList);
  } else {
    for (auto& conflictInfo : *spec.conflictInfoList()) {
      if (conflictInfo.conflictingScopeItemsWithOverlap()->empty() &&
          !conflictInfo.conflictingScopeItems()->empty()) {
        std::vector<interface::ConflictingScopeItemInfo> conflictingInfoList;
        conflictingInfoList.reserve(
            conflictInfo.conflictingScopeItems()->size());
        for (const auto& conflictingScopeItem :
             *conflictInfo.conflictingScopeItems()) {
          interface::ConflictingScopeItemInfo conflictingInfo;
          conflictingInfo.conflictingScopeItem() = conflictingScopeItem;
          conflictingInfoList.push_back(std::move(conflictingInfo));
        }
        conflictInfo.conflictingScopeItemsWithOverlap() =
            std::move(conflictingInfoList);
      }
    }
  }
}
