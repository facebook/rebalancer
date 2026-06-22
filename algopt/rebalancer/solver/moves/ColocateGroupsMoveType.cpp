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

#include "algopt/rebalancer/solver/moves/ColocateGroupsMoveType.h"

#include "algopt/rebalancer/solver/iterators/MultiCollectionCartesianProduct.h"
#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/moves/MoveTypeUtils.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"

#include <folly/container/irange.h>

namespace facebook::rebalancer {

std::string ColocateGroupsMoveType::name() const {
  return kColocateGroupsMoveTypeName.data();
}

ColocateGroupsMoveType::ColocateGroupsMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::ColocateGroupsMoveTypeSpec& spec)
    : MoveType(solverConfigs), spec_(spec) {}

MoveResult ColocateGroupsMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /*hints*/,
    double timeLimit) {
  const algopt::Timer timer(true);
  auto& problem = evaluator.getProblem();
  initializeSpecInfo(problem);

  const auto& universe = problem.getUniverse();
  const auto& precision = universe.getPrecision();
  const auto& colocationScope = universe.getScope(specInfo_->colocationScopeId);
  const auto sourceScopeItemIdOpt =
      colocationScope.getScopeItemId(hotContainer);
  const auto& partition = universe.getPartition(specInfo_->partitionId);
  auto bestResult = MoveResult::makeEmpty();

  entities::Set<entities::GroupId> seenGroupIds;
  const ObjectDeduper dedupedObjs(
      &problem.getEquivalenceSets(), evaluator.getDynamicObjects(hotContainer));
  for (auto hotObjectId : dedupedObjs) {
    if (timer.getSeconds() >= timeLimit) {
      stats.incrNumTimeouts(bestResult);
      return bestResult;
    }

    auto groupIdOpt =
        problem.getOnlyGroupIdIfExists(*spec_.partitionName(), hotObjectId);
    if (!groupIdOpt.has_value()) {
      continue;
    }
    auto hotObjectGroupId = groupIdOpt.value();
    auto relatedGroupsInfoPtr =
        folly::get_ptr(specInfo_->groupIdToRelatedGroups, hotObjectGroupId);
    if (!relatedGroupsInfoPtr || seenGroupIds.contains(hotObjectGroupId)) {
      // not a related group for this move type or we have already seen this
      // group
      continue;
    }

    auto& relatedGroupsInfo = *relatedGroupsInfoPtr;
    auto& relatedGroups = *relatedGroupsInfo.relatedGroups;
    seenGroupIds.insert(relatedGroups.begin(), relatedGroups.end());

    // if hot container is not part of the colocation scope (e.g., it maybe a
    // dummy container), then we only consider that container as part
    // "sourceContainers"; otherwise we need to consider all containers in the
    // scope item
    auto onlySourceContainer =
        entities::Set<entities::ContainerId>{hotContainer};
    const auto& sourceContainers = sourceScopeItemIdOpt
        ? colocationScope.getContainerIds(sourceScopeItemIdOpt.value())
        : onlySourceContainer;
    auto representativeObjectPerGroup = getRepresentativeObjectPerGroup(
        hotObjectGroupId,
        relatedGroups,
        hotObjectId,
        sourceContainers,
        partition,
        problem);
    if (representativeObjectPerGroup.size() != relatedGroups.size()) {
      // if we could not find a representative object for each group, then skip
      continue;
    }

    auto moveSets = getMoveSetsForRelatedGroups(
        relatedGroupsInfo,
        sourceScopeItemIdOpt,
        representativeObjectPerGroup,
        problem,
        timer,
        timeLimit);

    const std::function<MoveResult(MoveSet)> evaluate =
        [&stats, &evaluator](MoveSet moveset) {
          auto result = evaluator.evaluate(std::move(moveset));
          stats.add(result);
          return result;
        };

    bestResult.aggregate(
        MoveHelper::findBest(
            problem.configs.threadPool.get(),
            moveSets,
            evaluate,
            timeLimit - timer.getSeconds(),
            getParallelExecutionConfig()));

    if (bestResult.isBetter(precision)) {
      // return early if we found a better move
      return bestResult;
    }
  }

  return bestResult;
}

std::vector<MoveSet> ColocateGroupsMoveType::getMoveSetsForRelatedGroups(
    const RelatedGroupsInfoId& relatedGroupsInfo,
    std::optional<entities::ScopeItemId> sourceScopeItemIdOpt,
    const std::vector<entities::ObjectId>& representativeObjectPerGroup,
    const Problem& problem,
    const algopt::Timer& timer,
    double timeLimit) const {
  const auto& universe = problem.getUniverse();
  const auto& colocationScope = universe.getScope(specInfo_->colocationScopeId);
  // if the group has a specific set of colocation scope items, then only those
  // are considered as potential destinations; else consider all scope items in
  // colocationScope
  const auto& destinationScopeItemIds =
      relatedGroupsInfo.destinationScopeItemIds
      ? *relatedGroupsInfo.destinationScopeItemIds
      : colocationScope.getScopeItemIds();

  std::vector<MoveSet> moveSets;
  for (auto destinationScopeItem : destinationScopeItemIds) {
    if (timer.getSeconds() >= timeLimit) {
      // Out of time before enumerating the remaining destination scope items;
      // return whatever was collected so far.
      break;
    }
    if (sourceScopeItemIdOpt &&
        sourceScopeItemIdOpt.value() == destinationScopeItem) {
      continue;
    }

    std::vector<entities::Set<entities::ContainerId>>
        destinationContainersPerGroup;
    auto groupToContainersPtr = folly::get_ptr(
        specInfo_->colocationScopeItemToGroupToContainers,
        destinationScopeItem);
    auto& relatedGroups = *relatedGroupsInfo.relatedGroups;
    for (auto group : relatedGroups) {
      auto containersPtr = groupToContainersPtr
          ? folly::get_ptr(*groupToContainersPtr, group)
          : nullptr;
      auto& unsampledContainers = containersPtr
          ? *containersPtr
          : colocationScope.getContainerIds(destinationScopeItem);

      destinationContainersPerGroup.emplace_back(
          specInfo_->defaultSampleSize
              ? getRandomSample(
                    unsampledContainers, *specInfo_->defaultSampleSize, rng_)
              : folly::copy(unsampledContainers));
    }

    auto moveSetsToDestinationScopeItem = getMoveSetsToDestinationScopeItem(
        destinationContainersPerGroup,
        representativeObjectPerGroup,
        problem,
        timer,
        timeLimit);

    moveSets.insert(
        moveSets.end(),
        std::make_move_iterator(moveSetsToDestinationScopeItem.begin()),
        std::make_move_iterator(moveSetsToDestinationScopeItem.end()));
  }

  return moveSets;
}

std::vector<entities::ObjectId>
ColocateGroupsMoveType::getRepresentativeObjectPerGroup(
    entities::GroupId hotObjectGroupId,
    const std::vector<entities::GroupId>& relatedGroups,
    entities::ObjectId hotObjectId,
    const entities::Set<entities::ContainerId>& sourceContainers,
    const entities::Partition& partition,
    const Problem& problem) {
  // for each group in relatedGroups, take a representative object from
  // sourceScopeItem; if no such object exists, we will skip the group
  // TODO: make this more efficient by perhaps using IndexedAssignment?
  std::vector<entities::ObjectId> representativeObjectPerGroup;
  representativeObjectPerGroup.reserve(relatedGroups.size());
  for (auto relatedGroupId : relatedGroups) {
    auto repObjectOpt = (relatedGroupId == hotObjectGroupId)
        ? hotObjectId
        : getRepresentativeObjectFromSourceScopeItem(
              sourceContainers, relatedGroupId, partition, problem);
    if (!repObjectOpt.has_value()) {
      break;
    } else {
      representativeObjectPerGroup.emplace_back(repObjectOpt.value());
    }
  }

  return representativeObjectPerGroup;
}

std::optional<entities::ObjectId>
ColocateGroupsMoveType::getRepresentativeObjectFromSourceScopeItem(
    const entities::Set<entities::ContainerId>& sourceContainers,
    entities::GroupId groupId,
    const entities::Partition& partition,
    const Problem& problem) {
  // for each group, take an object from the given sourceScopeItem
  for (auto repObject : partition.getObjectIds(groupId)) {
    auto currContainer = problem.assignment.getContainer(repObject);
    if (sourceContainers.contains(currContainer)) {
      return repObject;
    }
  }

  return std::nullopt;
}

std::vector<MoveSet> ColocateGroupsMoveType::getMoveSetsToDestinationScopeItem(
    const std::vector<entities::Set<entities::ContainerId>>&
        destinationContainersPerGroup,
    const std::vector<entities::ObjectId>& representativeObjectPerGroup,
    const Problem& problem,
    const algopt::Timer& timer,
    double timeLimit) {
  std::vector<MoveSet> moveSets;
  auto cartestianProduct =
      MultiCollectionCartesianProduct(destinationContainersPerGroup);
  // Sample the clock every kTimeCheckInterval iterations rather than on every
  // one: the candidate space can be ~1B, so reading the clock per iteration
  // adds measurable overhead. This still bounds the overrun to at most
  // kTimeCheckInterval extra move sets.
  constexpr size_t kTimeCheckInterval = 1024;
  size_t numProcessed = 0;
  for (const auto& destinationContainers : cartestianProduct) {
    if ((numProcessed++ & (kTimeCheckInterval - 1)) == 0 &&
        timer.getSeconds() >= timeLimit) {
      return moveSets;
    }
    MoveSet moveSet;
    for (const auto j : folly::irange(destinationContainers.size())) {
      auto objectId = representativeObjectPerGroup.at(j);
      auto destinationContainer = destinationContainers[j];
      auto sourceContainer = problem.assignment.getContainer(objectId);
      if (problem.not_accepting_containers.contains(destinationContainer) ||
          sourceContainer == destinationContainer) {
        continue;
      }

      moveSet.insert(Move(objectId, sourceContainer, destinationContainer));
    }
    moveSets.emplace_back(std::move(moveSet));
  }

  return moveSets;
}

void ColocateGroupsMoveType::initializeSpecInfo(const Problem& problem) {
  if (specInfo_.has_value()) {
    // previously initialized, nothing to do
    return;
  }

  const auto& universe = problem.getUniverse();
  auto& partitionName = *spec_.partitionName();
  auto partitionId = universe.getPartitionId(partitionName);
  auto& partition = universe.getPartition(partitionId);
  auto& colocationScopeName = *spec_.colocationScopeName();
  auto colocationScopeId = universe.getScopeId(colocationScopeName);
  auto& relatedGroupsList = *spec_.relatedGroupsList();

  // check that partition is disjoint
  if (!partition.isDisjoint()) {
    throw std::runtime_error(
        fmt::format(
            "Partition '{}' used in ColocateGroupsMoveTyope is not disjoint, but it is expected to be so",
            partitionName));
  }

  entities::Map<entities::GroupId, RelatedGroupsInfoId> groupIdToRelatedGroups;
  for (auto& relatedGroupsInfo : relatedGroupsList) {
    // convert each group in relatedGroups to ids
    auto relatedGroupsIds = std::make_shared<std::vector<entities::GroupId>>();
    relatedGroupsIds->reserve(relatedGroupsInfo.relatedGroups()->size());
    for (auto& groupName : *relatedGroupsInfo.relatedGroups()) {
      auto groupId = universe.getGroupId(partitionId, groupName);
      relatedGroupsIds->push_back(groupId);
    }

    // convert each colocation scope item in relatedGroups to ids
    std::shared_ptr<std::vector<entities::ScopeItemId>>
        destinationScopeItemIds = nullptr;
    if (relatedGroupsInfo.destinationScopeItems().has_value()) {
      destinationScopeItemIds =
          std::make_shared<std::vector<entities::ScopeItemId>>();
      destinationScopeItemIds->reserve(
          relatedGroupsInfo.destinationScopeItems()->size());
      for (auto& scopeItemName : *relatedGroupsInfo.destinationScopeItems()) {
        auto scopeItemId =
            universe.getScopeItemId(colocationScopeId, scopeItemName);
        destinationScopeItemIds->push_back(scopeItemId);
      }
    }

    for (auto groupId : *relatedGroupsIds) {
      groupIdToRelatedGroups.emplace(
          groupId,
          RelatedGroupsInfoId{
              .relatedGroups = relatedGroupsIds,
              .destinationScopeItemIds = destinationScopeItemIds,
          });
    }
  }

  auto& colocationScopeItemToGroupToContainers =
      *spec_.colocationScopeItemToGroupToContainers();
  entities::Map<
      entities::ScopeItemId,
      entities::Map<entities::GroupId, entities::Set<entities::ContainerId>>>
      colocationScopeItemIdToGroupIdToContainerIds;

  for (auto& [scopeItemName, groupToContainers] :
       colocationScopeItemToGroupToContainers) {
    auto scopeItemId =
        universe.getScopeItemId(colocationScopeId, scopeItemName);
    for (auto& [groupName, containers] : groupToContainers) {
      auto groupId = universe.getGroupId(partitionId, groupName);

      // convert containers to ids
      entities::Set<entities::ContainerId> containerIds;
      for (auto& containerName : containers) {
        containerIds.insert(universe.getContainerId(containerName));
      }
      colocationScopeItemIdToGroupIdToContainerIds[scopeItemId][groupId] =
          std::move(containerIds);
    }
  }

  specInfo_.emplace(
      SpecInfo{
          .partitionId = partitionId,
          .colocationScopeId = colocationScopeId,
          .groupIdToRelatedGroups = std::move(groupIdToRelatedGroups),
          .colocationScopeItemToGroupToContainers =
              std::move(colocationScopeItemIdToGroupIdToContainerIds),
          .defaultSampleSize = spec_.defaultSampleSize().to_optional(),
      });
}

} // namespace facebook::rebalancer
