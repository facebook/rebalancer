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

#include "algopt/rebalancer/solver/moves/MoveTypeFactory.h"

#include "algopt/rebalancer/solver/moves/ColocateGroupsMoveType.h"
#include "algopt/rebalancer/solver/moves/FixedDestMoveType.h"
#include "algopt/rebalancer/solver/moves/FixedDestMultiMoveType.h"
#include "algopt/rebalancer/solver/moves/FixedDestSwapMultiMoveType.h"
#include "algopt/rebalancer/solver/moves/FixedSourceMoveType.h"
#include "algopt/rebalancer/solver/moves/FixedSourceMultiMoveType.h"
#include "algopt/rebalancer/solver/moves/GreedyGroupToScopeItemMoveType.h"
#include "algopt/rebalancer/solver/moves/GroupMoveWithHintStrategiesMoveType.h"
#include "algopt/rebalancer/solver/moves/GroupRoutingMoveType.h"
#include "algopt/rebalancer/solver/moves/KLSearchMoveType.h"
#include "algopt/rebalancer/solver/moves/ReplicaDropMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleChainFastMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleChainMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleColdestStratifiedMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleEndChainMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleFastMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleGreedyMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleRandomBatchesMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleRandomObjectStratifiedMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleRandomStratifiedMoveType.h"
#include "algopt/rebalancer/solver/moves/SwapFullContainersMoveType.h"
#include "algopt/rebalancer/solver/moves/SwapFullWithEmptyContainersMoveType.h"
#include "algopt/rebalancer/solver/moves/SwapMoveType.h"
#include "algopt/rebalancer/solver/moves/SwapNMoveType.h"
#include "algopt/rebalancer/solver/moves/TripleLoopMoveType.h"

#include <fmt/core.h>

namespace facebook::rebalancer {

std::unique_ptr<MoveType> MoveTypeFactory::createMoveType(
    const std::string& name,
    const interface::LocalSearchSolverSpec& configs) {
  if (name == kSingleRandomStratifiedMoveTypeName) {
    return std::make_unique<SingleRandomStratifiedMoveType>(configs);
  }
  if (name == kSingleColdestStratifiedMoveTypeName) {
    return std::make_unique<SingleColdestStratifiedMoveType>(configs);
  }
  if (name == kFixedDestMoveTypeName) {
    return std::make_unique<FixedDestMoveType>(configs);
  }
  throw std::runtime_error(
      fmt::format(
          "Move type {} not supported by name; it either does not exist, or you should use its typed move type spec instead",
          name));
}

std::unique_ptr<MoveType> MoveTypeFactory::createMoveType(
    const interface::MoveTypeSpec& spec,
    const interface::LocalSearchSolverSpec& configs,
    const Problem& problem) {
  switch (spec.getType()) {
    case interface::MoveTypeSpec::Type::singleMoveTypeSpec:
      return std::make_unique<SingleMoveType>(
          configs, spec.get_singleMoveTypeSpec());
    case interface::MoveTypeSpec::Type::swapMoveTypeSpec:
      return std::make_unique<SwapMoveType>(
          configs, spec.get_swapMoveTypeSpec());
    case interface::MoveTypeSpec::Type::tripleLoopMoveTypeSpec:
      return std::make_unique<TripleLoopMoveType>(
          configs, spec.get_tripleLoopMoveTypeSpec());
    case interface::MoveTypeSpec::Type::klSearchMoveTypeSpec:
      return std::make_unique<KLSearchMoveType>(
          configs, spec.get_klSearchMoveTypeSpec());
    case interface::MoveTypeSpec::Type::fixedDestMoveTypeSpec:
      return std::make_unique<FixedDestMoveType>(
          configs, spec.get_fixedDestMoveTypeSpec());
    case interface::MoveTypeSpec::Type::fixedSrcMultiMoveTypeSpec:
      return std::make_unique<FixedSourceMultiMoveType>(
          configs, spec.get_fixedSrcMultiMoveTypeSpec());
    case interface::MoveTypeSpec::Type::fixedDestMultiMoveTypeSpec:
      return std::make_unique<FixedDestMultiMoveType>(
          configs, spec.get_fixedDestMultiMoveTypeSpec());
    case interface::MoveTypeSpec::Type::fixedDestSwapMultiMoveTypeSpec:
      return std::make_unique<FixedDestSwapMultiMoveType>(
          configs, spec.get_fixedDestSwapMultiMoveTypeSpec());
    case interface::MoveTypeSpec::Type::
        singleRandomObjectStratifiedMoveTypeSpec:
      return std::make_unique<SingleRandomObjectStratifiedMoveType>(
          configs, spec.get_singleRandomObjectStratifiedMoveTypeSpec());
    case interface::MoveTypeSpec::Type::groupRoutingMoveTypeSpec:
      return std::make_unique<GroupRoutingMoveType>(
          configs, spec.get_groupRoutingMoveTypeSpec());
    case interface::MoveTypeSpec::Type::singleChainMoveTypeSpec:
      return std::make_unique<SingleChainMoveType>(
          configs, spec.get_singleChainMoveTypeSpec());
    case interface::MoveTypeSpec::Type::singleChainFastMoveTypeSpec:
      return std::make_unique<SingleChainFastMoveType>(
          configs, spec.get_singleChainFastMoveTypeSpec());
    case interface::MoveTypeSpec::Type::singleFixedSourceMoveTypeSpec:
      return std::make_unique<FixedSourceMoveType>(
          configs, spec.get_singleFixedSourceMoveTypeSpec());
    case interface::MoveTypeSpec::Type::singleFastMoveTypeSpec:
      return std::make_unique<SingleFastMoveType>(
          configs, spec.get_singleFastMoveTypeSpec());
    case interface::MoveTypeSpec::Type::groupMoveWithHintStrategiesMoveTypeSpec:
      return std::make_unique<GroupMoveWithHintStrategiesMoveType>(
          configs, spec.get_groupMoveWithHintStrategiesMoveTypeSpec());
    case interface::MoveTypeSpec::Type::singleGreedyMoveTypeSpec:
      return std::make_unique<SingleGreedyMoveType>(
          configs, spec.get_singleGreedyMoveTypeSpec());
    case interface::MoveTypeSpec::Type::swapNMoveTypeSpec:
      return std::make_unique<SwapNMoveType>(
          configs, spec.get_swapNMoveTypeSpec());
    case interface::MoveTypeSpec::Type::singleRandomBatchesMoveTypeSpec:
      return std::make_unique<SingleRandomBatchesMoveType>(
          configs, spec.get_singleRandomBatchesMoveTypeSpec());
    case interface::MoveTypeSpec::Type::swapFullContainersMoveTypeSpec:
      return std::make_unique<SwapFullContainersMoveType>(
          configs, spec.get_swapFullContainersMoveTypeSpec());
    case interface::MoveTypeSpec::Type::swapFullWithEmptyContainersMoveTypeSpec:
      return std::make_unique<SwapFullWithEmptyContainersMoveType>(
          configs, spec.get_swapFullWithEmptyContainersMoveTypeSpec());
    case interface::MoveTypeSpec::Type::singleEndChainMoveTypeSpec:
      return std::make_unique<SingleEndChainMoveType>(
          configs, spec.get_singleEndChainMoveTypeSpec());
    case interface::MoveTypeSpec::Type::replicaDropMoveTypeSpec:
      return std::make_unique<ReplicaDropMoveType>(
          configs, spec.get_replicaDropMoveTypeSpec());
    case interface::MoveTypeSpec::Type::colocateGroupsMoveTypeSpec:
      return std::make_unique<ColocateGroupsMoveType>(
          configs, spec.get_colocateGroupsMoveTypeSpec(), problem);
    case interface::MoveTypeSpec::Type::greedyGroupToScopeItemMoveTypeSpec:
      return std::make_unique<GreedyGroupToScopeItemMoveType>(
          configs, spec.get_greedyGroupToScopeItemMoveTypeSpec(), problem);
    case interface::MoveTypeSpec::Type::singleRandomStratifiedMoveTypeSpec:
      return std::make_unique<SingleRandomStratifiedMoveType>(
          configs, spec.get_singleRandomStratifiedMoveTypeSpec());
    case interface::MoveTypeSpec::Type::moveTypeName:
      return createMoveType(spec.get_moveTypeName(), configs);
    case interface::MoveTypeSpec::Type::__EMPTY__:
      throw std::runtime_error(
          "Found uninitialized (i.e., empty) MoveTypeSpec -- initialize it with at least one move type struct");
  }
}

std::vector<std::shared_ptr<MoveType>> MoveTypeFactory::createMoveTypes(
    const interface::LocalSearchSolverSpec& spec,
    const Problem& problem) {
  const auto& moveTypes = *spec.moveTypeList();
  std::vector<std::shared_ptr<MoveType>> moves;
  moves.reserve(moveTypes.size());
  std::transform(
      moveTypes.begin(),
      moveTypes.end(),
      std::back_inserter(moves),
      [&](const auto& move) {
        return MoveTypeFactory::createMoveType(move, spec, problem);
      });
  return moves;
}

void MoveTypeFactory::convertMoveTypesToMoveTypeSpecs(
    interface::LocalSearchSolverSpec& spec) {
  auto& moveNames = *spec.moveTypes();
  if (moveNames.empty()) {
    return;
  }

  auto& moveSpecs = *spec.moveTypeList();
  if (!moveSpecs.empty()) {
    throw std::runtime_error(
        fmt::format(
            "Will not convert non-empty moveTypes ({} elements) to non-empty moveTypeList ({} elements)",
            moveNames.size(),
            moveSpecs.size()));
  }

  moveSpecs.clear();
  moveSpecs.reserve(moveNames.size());
  for (const auto& name : moveNames) {
    interface::MoveTypeSpec moveSpec;
    if (name == kSingleMoveTypeName) {
      moveSpec.set_singleMoveTypeSpec(interface::SingleMoveTypeSpec());
    } else if (name == kSwapMoveTypeName) {
      throw std::runtime_error("SwapMoveType is no longer supported by name.");
    } else if (name == kTripleLoopMoveTypeName) {
      moveSpec.set_tripleLoopMoveTypeSpec(interface::TripleLoopMoveTypeSpec());
    } else if (name == kKLSearchMoveTypeName) {
      moveSpec.set_klSearchMoveTypeSpec(interface::KLSearchMoveTypeSpec());
    } else {
      moveSpec.set_moveTypeName(name);
    }
    moveSpecs.push_back(moveSpec);
  }

  moveNames.clear();
}

void MoveTypeFactory::transformMoveTypeSpecs(
    interface::LocalSearchSolverSpec& spec) {
  auto& moveSpecs = *spec.moveTypeList();
  if (moveSpecs.empty()) {
    return;
  }

  std::vector<interface::MoveTypeSpec> transformedMoveSpecs;
  transformedMoveSpecs.reserve(moveSpecs.size());

  for (const auto& moveSpec : moveSpecs) {
    auto newSpec = interface::MoveTypeSpec();
    if (moveSpec.getType() != interface::MoveTypeSpec::Type::moveTypeName) {
      newSpec = moveSpec;
    } else if (
        moveSpec.get_moveTypeName() == kGroupRoutingMoveTypeName &&
        spec.groupRoutingMoveTypeSpec()) {
      newSpec.set_groupRoutingMoveTypeSpec(*spec.groupRoutingMoveTypeSpec());
    } else if (moveSpec.get_moveTypeName() == kSingleChainMoveTypeName) {
      newSpec.set_singleChainMoveTypeSpec(
          spec.singleChainMoveTypeSpec().value_or(
              interface::SingleChainMoveTypeSpec()));
    } else if (moveSpec.get_moveTypeName() == kSingleChainFastMoveTypeName) {
      auto singleChainFastMoveTypeSpec =
          interface::SingleChainFastMoveTypeSpec();
      auto currentSpec = spec.singleChainFastMoveTypeSpec().value_or(
          interface::SingleChainMoveTypeSpec());
      singleChainFastMoveTypeSpec
          .partitionNameToExploreFastChainsWithinObjectGroup()
          .copy_from(
              currentSpec.partitionNameToExploreChainsWithinObjectGroup());
      singleChainFastMoveTypeSpec.specialFastColdContainer().copy_from(
          currentSpec.specialColdContainer());
      newSpec.set_singleChainFastMoveTypeSpec(singleChainFastMoveTypeSpec);
    } else if (
        moveSpec.get_moveTypeName() == kSingleFixedSourceMoveTypeName &&
        spec.singleFixedSourceMoveTypeSpec()) {
      newSpec.set_singleFixedSourceMoveTypeSpec(
          *spec.singleFixedSourceMoveTypeSpec());
    } else if (moveSpec.get_moveTypeName() == kSingleGreedyMoveTypeName) {
      newSpec.set_singleGreedyMoveTypeSpec(
          interface::SingleGreedyMoveTypeSpec());
    } else if (moveSpec.get_moveTypeName() == kSingleFastMoveTypeName) {
      newSpec.set_singleFastMoveTypeSpec(interface::SingleFastMoveTypeSpec());
    } else if (moveSpec.get_moveTypeName() == kSingleEndChainMoveTypeName) {
      newSpec.set_singleEndChainMoveTypeSpec(
          interface::SingleEndChainMoveTypeSpec());
    } else if (
        moveSpec.get_moveTypeName() == kGreedyGroupToScopeItemMoveTypeName) {
      auto greedyGroupToScopeItemMoveTypeSpec =
          interface::GreedyGroupToScopeItemMoveTypeSpec();
      if (spec.scopeItemMovesScope().has_value()) {
        greedyGroupToScopeItemMoveTypeSpec.scopeItemMovesScope() =
            spec.scopeItemMovesScope().value();
      }
      if (spec.groupMovesPartition().has_value()) {
        greedyGroupToScopeItemMoveTypeSpec.groupMovesPartition() =
            *spec.groupMovesPartition();
      }
      greedyGroupToScopeItemMoveTypeSpec.nSampleSetsToExplore().copy_from(
          spec.nSampleSetsToExplore());
      newSpec.set_greedyGroupToScopeItemMoveTypeSpec(
          greedyGroupToScopeItemMoveTypeSpec);
    } else if (
        moveSpec.get_moveTypeName() == kSingleRandomBatchesMoveTypeName) {
      auto singleRandomBatchesMoveTypeSpec =
          interface::SingleRandomBatchesMoveTypeSpec();
      singleRandomBatchesMoveTypeSpec.randomContainerBatchSize().copy_from(
          spec.randomContainerBatchSize());
      newSpec.set_singleRandomBatchesMoveTypeSpec(
          singleRandomBatchesMoveTypeSpec);
    } else if (moveSpec.get_moveTypeName() == kReplicaDropMoveTypeName) {
      auto replicaDropMoveTypeSpec = interface::ReplicaDropMoveTypeSpec();
      if (spec.replicaDropPartition().has_value()) {
        replicaDropMoveTypeSpec.replicaDropPartition() =
            spec.replicaDropPartition().value();
      }
      if (spec.replicaDropScope().has_value()) {
        replicaDropMoveTypeSpec.replicaDropScope() =
            spec.replicaDropScope().value();
      }
      newSpec.set_replicaDropMoveTypeSpec(replicaDropMoveTypeSpec);
    } else if (moveSpec.get_moveTypeName() == kSwapFullContainersTypeName) {
      newSpec.set_swapFullContainersMoveTypeSpec(
          interface::SwapFullContainersMoveTypeSpec());
    } else if (
        moveSpec.get_moveTypeName() == kSwapFullWithEmptyContainersTypeName) {
      newSpec.set_swapFullWithEmptyContainersMoveTypeSpec(
          interface::SwapFullWithEmptyContainersMoveTypeSpec());
    } else if (moveSpec.get_moveTypeName() == kSwapNMoveTypeName) {
      auto swapNMoveTypeSpec = interface::SwapNMoveTypeSpec();
      swapNMoveTypeSpec.swapNConcurrentObjects().copy_from(
          spec.swapNConcurrentObjects());
      swapNMoveTypeSpec.swapNSourceObjects().copy_from(
          spec.swapNSourceObjects());
      swapNMoveTypeSpec.swapNDestinationScope().copy_from(
          spec.swapNDestinationScope());
      swapNMoveTypeSpec.swapNIterations().copy_from(spec.swapNIterations());
      newSpec.set_swapNMoveTypeSpec(swapNMoveTypeSpec);
    } else {
      // In case of a move type name that is not supported by the typed spec
      // yet,
      newSpec = moveSpec;
    }
    transformedMoveSpecs.push_back(newSpec);
  }

  spec.moveTypeList() = transformedMoveSpecs;
}

void MoveTypeFactory::transformMoveTypesForReplayingSavedInstances(
    interface::LocalSearchSolverSpec& spec) {
  convertMoveTypesToMoveTypeSpecs(spec);
  transformMoveTypeSpecs(spec);
}

} // namespace facebook::rebalancer
