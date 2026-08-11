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

#include "algopt/rebalancer/materializer/spec_builder/SpecBuilderFactory.h"

#include "algopt/rebalancer/materializer/spec_builder/AssignmentAffinitiesSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/AvoidAssignmentsSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/AvoidMovingSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/BalanceSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/BipartiteSwapsSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/CapacityRatioSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/CapacitySpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/CapacityWithGroupPresenceSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/CapacityWithSupplyAndDrSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/ColocateGroupsSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/DisasterRecoveryCapacitySpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/DiversifyWithinScopeItemSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/DrainCapacitySpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/ExclusiveGroupsSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/ExclusiveObjectsSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/ExclusiveScopeItemsSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/ExclusiveSwapsSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/FlowSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/GroupAssignmentAffinitiesSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/GroupCapacitySpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/GroupCountSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/GroupDiversitySpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/GroupIsolationLimitSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/GroupMoveLimitSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/ItemsAffinitySpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/LargeShardSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/LogicalAndSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/LogicalOrSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/MaximizeAllocationSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/MaximizeFreeCapacityUnitsSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/MinimizeContainersSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/MinimizeMovementSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/MinimizeNthLargestUtilizationSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/MoveGroupSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/MovesInProgressSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/MultipleOrCapacitySpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/NestedScopeLimitSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/NonAcceptingSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/ObjectAffinitiesSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/RoutingLatencySpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/ScopeAffinitiesSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/SRBufferCapacitySpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/SumOfMaxSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/ToFreeSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/UtilIncreaseCostSpecBuilder.h"
#include "algopt/rebalancer/materializer/spec_builder/WorkingSetSpecBuilder.h"
#include <algopt/rebalancer/materializer/spec_builder/AggregatedGroupSpecBuilder.h>
#include <algopt/rebalancer/materializer/spec_builder/MinimizeSquaresSpecBuilder.h>

#include <memory>
#include <stdexcept>

#ifndef REBALANCER_OSS_BUILD
#include "algopt/rebalancer/materializer/spec_builder/fb/RasRebalancingMovementSpecBuilder.h"
#endif

using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::interface;

namespace facebook::rebalancer::materializer {

SpecBuilderFactory::SpecBuilderFactory(
    std::shared_ptr<const Universe> universe,
    bool continuousExpressions,
    std::shared_ptr<LogCollector> logger)
    : universe_(std::move(universe)),
      continuousExpressions_(continuousExpressions),
      logger_(std::move(logger)) {}

std::unique_ptr<SpecBuilder> SpecBuilderFactory::getBuilder(
    const ConstraintSpecs& spec) const {
  switch (spec.getType()) {
    case ConstraintSpecs::Type::capacitySpec:
      return std::make_unique<CapacitySpecBuilder>(
          universe_, spec.get_capacitySpec());
    case ConstraintSpecs::Type::bipartiteSwapsSpec:
      return std::make_unique<BipartiteSwapsSpecBuilder>(
          universe_, spec.get_bipartiteSwapsSpec());
    case ConstraintSpecs::Type::colocateGroupsSpec:
      return std::make_unique<ColocateGroupsSpecBuilder>(
          universe_, spec.get_colocateGroupsSpec(), continuousExpressions_);
    case ConstraintSpecs::Type::exclusiveObjectsSpec:
      return std::make_unique<ExclusiveObjectsSpecBuilder>(
          universe_, spec.get_exclusiveObjectsSpec());
    case ConstraintSpecs::Type::exclusiveGroupsSpec:
      return std::make_unique<ExclusiveGroupsSpecBuilder>(
          universe_, spec.get_exclusiveGroupsSpec(), logger_);
    case ConstraintSpecs::Type::exclusiveScopeItemsSpec:
      return std::make_unique<ExclusiveScopeItemsSpecBuilder>(
          universe_, spec.get_exclusiveScopeItemsSpec());
    case ConstraintSpecs::Type::flowSpec:
      return std::make_unique<FlowSpecBuilder>(universe_, spec.get_flowSpec());
    case ConstraintSpecs::Type::avoidAssignmentsSpec:
      return std::make_unique<AvoidAssignmentsSpecBuilder>(
          universe_, spec.get_avoidAssignmentsSpec());
    case ConstraintSpecs::Type::moveGroupSpec:
      return std::make_unique<MoveGroupSpecBuilder>(
          universe_, spec.get_moveGroupSpec());
    case ConstraintSpecs::Type::capacityRatioSpec:
      return std::make_unique<CapacityRatioSpecBuilder>(
          universe_, spec.get_capacityRatioSpec());
    case ConstraintSpecs::Type::toFreeSpec:
      return std::make_unique<ToFreeSpecBuilder>(
          universe_, spec.get_toFreeSpec(), continuousExpressions_);
    case ConstraintSpecs::Type::minimizeMovementSpec:
      return std::make_unique<MinimizeMovementSpecBuilder>(
          universe_, spec.get_minimizeMovementSpec());
    case ConstraintSpecs::Type::srBufferCapacitySpec:
      return std::make_unique<SRBufferCapacitySpecBuilder>(
          universe_, spec.get_srBufferCapacitySpec());
    case ConstraintSpecs::Type::rasRebalancingMovementSpec:
#ifndef REBALANCER_OSS_BUILD
      return std::make_unique<RasRebalancingMovementSpecBuilder>(
          universe_, spec.get_rasRebalancingMovementSpec());
#else
      throw std::runtime_error("Not available in OSS.");
#endif
    case ConstraintSpecs::Type::avoidMovingSpec:
      return std::make_unique<AvoidMovingSpecBuilder>(
          universe_, spec.get_avoidMovingSpec());
    case ConstraintSpecs::Type::movesInProgressSpec:
      return std::make_unique<MovesInProgressSpecBuilder>(
          universe_, spec.get_movesInProgressSpec());
    case ConstraintSpecs::Type::groupCountSpec:
      return std::make_unique<GroupCountSpecBuilder>(
          universe_, spec.get_groupCountSpec());
    case ConstraintSpecs::Type::groupMoveLimitSpec:
      return std::make_unique<GroupMoveLimitSpecBuilder>(
          universe_, spec.get_groupMoveLimitSpec());
    case ConstraintSpecs::Type::groupIsolationLimitSpec:
      return std::make_unique<GroupIsolationLimitSpecBuilder>(
          universe_, spec.get_groupIsolationLimitSpec());
    case ConstraintSpecs::Type::groupCapacitySpec:
      return std::make_unique<GroupCapacitySpecBuilder>(
          universe_, spec.get_groupCapacitySpec());
    case ConstraintSpecs::Type::objectAffinitiesSpec:
      return std::make_unique<ObjectAffinitiesSpecBuilder>(
          universe_, spec.get_objectAffinitiesSpec());
    case ConstraintSpecs::Type::capacityWithSupplyAndDrSpec:
      return std::make_unique<CapacityWithSupplyAndDrSpecBuilder>(
          universe_, spec.get_capacityWithSupplyAndDrSpec());
    case ConstraintSpecs::Type::disasterRecoveryCapacitySpec:
      return std::make_unique<DisasterRecoveryCapacitySpecBuilder>(
          universe_, spec.get_disasterRecoveryCapacitySpec());
    case ConstraintSpecs::Type::aggregatedGroupSpec:
      return std::make_unique<AggregatedGroupSpecBuilder>(
          universe_, spec.get_aggregatedGroupSpec());
    case ConstraintSpecs::Type::nestedScopeLimitSpec:
      return std::make_unique<NestedScopeLimitSpecBuilder>(
          universe_, spec.get_nestedScopeLimitSpec());
    case ConstraintSpecs::Type::drainCapacitySpec:
      return std::make_unique<DrainCapacitySpecBuilder>(
          universe_, spec.get_drainCapacitySpec());
    case ConstraintSpecs::Type::nonAcceptingSpec:
      return std::make_unique<NonAcceptingSpecBuilder>(
          universe_, spec.get_nonAcceptingSpec());
    case ConstraintSpecs::Type::exclusiveSwapsSpec:
      return std::make_unique<ExclusiveSwapsSpecBuilder>(
          universe_, spec.get_exclusiveSwapsSpec());
    case ConstraintSpecs::Type::multipleOrCapacitySpec:
      return std::make_unique<MultipleOrCapacitySpecBuilder>(
          universe_, spec.get_multipleOrCapacitySpec());
    case ConstraintSpecs::Type::logicalOrSpec:
      return std::make_unique<LogicalOrSpecBuilder>(
          universe_, spec.get_logicalOrSpec());
    case ConstraintSpecs::Type::logicalAndSpec:
      return std::make_unique<LogicalAndSpecBuilder>(
          universe_, spec.get_logicalAndSpec());
    case ConstraintSpecs::Type::routingLatencySpec:
      return std::make_unique<RoutingLatencySpecBuilder>(
          universe_, spec.get_routingLatencySpec());
    case ConstraintSpecs::Type::groupDiversitySpec:
      return std::make_unique<GroupDiversitySpecBuilder>(
          universe_, spec.get_groupDiversitySpec(), continuousExpressions_);
    case ConstraintSpecs::Type::capacityWithGroupPresenceSpec:
      return std::make_unique<CapacityWithGroupPresenceSpecBuilder>(
          universe_,
          spec.get_capacityWithGroupPresenceSpec(),
          continuousExpressions_);
    case ConstraintSpecs::Type::throttlingSpec:
      throw std::runtime_error(
          "Did not expect to be called with ThrottlingSpec; it is expected to be converted to CapacitySpec");
    case ConstraintSpecs::Type::pairAffinitiesSpec:
      throw std::runtime_error(
          "Did not expect to be called with PairAffinitiesSpec; it is expected to be converted to ColocateGroupsSpec");
    case ConstraintSpecs::Type::__EMPTY__:
      throw std::runtime_error(
          "ConstraintSpecs union is empty. You need to properly initialize it");
    default:
      throw std::runtime_error("Unhandled ConstraintSpecs::Type");
  }
}

std::unique_ptr<SpecBuilder> SpecBuilderFactory::getBuilder(
    const interface::GoalSpecs& spec) const {
  switch (spec.getType()) {
    case GoalSpecs::Type::assignmentAffinitiesSpec:
      return std::make_unique<AssignmentAffinitiesSpecBuilder>(
          universe_, spec.get_assignmentAffinitiesSpec());
    case GoalSpecs::Type::capacitySpec:
      return std::make_unique<CapacitySpecBuilder>(
          universe_, spec.get_capacitySpec());
    case GoalSpecs::Type::colocateGroupsSpec:
      return std::make_unique<ColocateGroupsSpecBuilder>(
          universe_, spec.get_colocateGroupsSpec(), continuousExpressions_);
    case GoalSpecs::Type::exclusiveObjectsSpec:
      return std::make_unique<ExclusiveObjectsSpecBuilder>(
          universe_, spec.get_exclusiveObjectsSpec());
    case GoalSpecs::Type::exclusiveGroupsSpec:
      return std::make_unique<ExclusiveGroupsSpecBuilder>(
          universe_, spec.get_exclusiveGroupsSpec(), logger_);
    case GoalSpecs::Type::flowSpec:
      return std::make_unique<FlowSpecBuilder>(universe_, spec.get_flowSpec());
    case GoalSpecs::Type::groupAssignmentAffinitiesSpec:
      return std::make_unique<GroupAssignmentAffinitiesSpecBuilder>(
          universe_, spec.get_groupAssignmentAffinitiesSpec());
    case GoalSpecs::Type::maximizeAllocationSpec:
      return std::make_unique<MaximizeAllocationSpecBuilder>(
          universe_, spec.get_maximizeAllocationSpec());
    case GoalSpecs::Type::maximizeFreeCapacityUnitsSpec:
      return std::make_unique<MaximizeFreeCapacityUnitsSpecBuilder>(
          universe_,
          spec.get_maximizeFreeCapacityUnitsSpec(),
          continuousExpressions_);
    case GoalSpecs::Type::capacityRatioSpec:
      return std::make_unique<CapacityRatioSpecBuilder>(
          universe_, spec.get_capacityRatioSpec());
    case GoalSpecs::Type::minimizeSquaresSpec:
      return std::make_unique<MinimizeSquaresSpecBuilder>(
          universe_, spec.get_minimizeSquaresSpec());
    case GoalSpecs::Type::minimizeMovementSpec:
      return std::make_unique<MinimizeMovementSpecBuilder>(
          universe_, spec.get_minimizeMovementSpec());
    case GoalSpecs::Type::sumOfMaxSpec:
      return std::make_unique<SumOfMaxSpecBuilder>(
          universe_, spec.get_sumOfMaxSpec());
    case GoalSpecs::Type::balanceSpec:
      return std::make_unique<BalanceSpecBuilder>(
          universe_, spec.get_balanceSpec(), continuousExpressions_);
    case GoalSpecs::Type::groupCapacitySpec:
      return std::make_unique<GroupCapacitySpecBuilder>(
          universe_, spec.get_groupCapacitySpec());
    case GoalSpecs::Type::groupCountSpec:
      return std::make_unique<GroupCountSpecBuilder>(
          universe_, spec.get_groupCountSpec());
    case GoalSpecs::Type::minimizeContainersSpec:
      return std::make_unique<MinimizeContainersSpecBuilder>(
          universe_, spec.get_minimizeContainersSpec(), continuousExpressions_);
    case GoalSpecs::Type::utilIncreaseCostSpec:
      return std::make_unique<UtilIncreaseCostSpecBuilder>(
          universe_, spec.get_utilIncreaseCostSpec());
    case GoalSpecs::Type::groupIsolationLimitSpec:
      return std::make_unique<GroupIsolationLimitSpecBuilder>(
          universe_, spec.get_groupIsolationLimitSpec());
    case GoalSpecs::Type::capacityWithSupplyAndDrSpec:
      return std::make_unique<CapacityWithSupplyAndDrSpecBuilder>(
          universe_, spec.get_capacityWithSupplyAndDrSpec());
    case GoalSpecs::Type::workingSetSpec:
      return std::make_unique<WorkingSetSpecBuilder>(
          universe_, spec.get_workingSetSpec());
    case GoalSpecs::Type::disasterRecoveryCapacitySpec:
      return std::make_unique<DisasterRecoveryCapacitySpecBuilder>(
          universe_, spec.get_disasterRecoveryCapacitySpec());
    case GoalSpecs::Type::aggregatedGroupSpec:
      return std::make_unique<AggregatedGroupSpecBuilder>(
          universe_, spec.get_aggregatedGroupSpec());
    case GoalSpecs::Type::drainCapacitySpec:
      return std::make_unique<DrainCapacitySpecBuilder>(
          universe_, spec.get_drainCapacitySpec());
    case GoalSpecs::Type::toFreeSpec:
      return std::make_unique<ToFreeSpecBuilder>(
          universe_, spec.get_toFreeSpec(), continuousExpressions_);
    case GoalSpecs::Type::minimizeNthLargestUtilizationSpec:
      return std::make_unique<MinimizeNthLargestUtilizationSpecBuilder>(
          universe_, spec.get_minimizeNthLargestUtilizationSpec());
    case GoalSpecs::Type::scopeAffinitiesSpec:
      return std::make_unique<ScopeAffinitiesSpecBuilder>(
          universe_, spec.get_scopeAffinitiesSpec());
    case GoalSpecs::Type::itemsAffinitySpec:
      return std::make_unique<ItemsAffinitySpecBuilder>(
          universe_, spec.get_itemsAffinitySpec());
    case GoalSpecs::Type::largeShardSpec:
      return std::make_unique<LargeShardSpecBuilder>(
          universe_, spec.get_largeShardSpec());
    case GoalSpecs::Type::routingLatencySpec:
      return std::make_unique<RoutingLatencySpecBuilder>(
          universe_, spec.get_routingLatencySpec());
    case GoalSpecs::Type::groupDiversitySpec:
      return std::make_unique<GroupDiversitySpecBuilder>(
          universe_, spec.get_groupDiversitySpec(), continuousExpressions_);
    case GoalSpecs::Type::srBufferCapacitySpec:
      return std::make_unique<SRBufferCapacitySpecBuilder>(
          universe_, spec.get_srBufferCapacitySpec());
    case GoalSpecs::Type::capacityWithGroupPresenceSpec:
      return std::make_unique<CapacityWithGroupPresenceSpecBuilder>(
          universe_,
          spec.get_capacityWithGroupPresenceSpec(),
          continuousExpressions_);
    case GoalSpecs::Type::diversifyWithinScopeItemSpec:
      return std::make_unique<DiversifyWithinScopeItemSpec>(
          universe_, spec.get_diversifyWithinScopeItemSpec());
    case GoalSpecs::Type::exclusiveScopeItemsSpec:
      return std::make_unique<ExclusiveScopeItemsSpecBuilder>(
          universe_, spec.get_exclusiveScopeItemsSpec());
    case GoalSpecs::Type::balanceV2Spec:
      throw std::runtime_error(
          "Did not expect to be called with balanceV2Spec as it is deprecated");
    case GoalSpecs::Type::pairAffinitiesSpec:
      throw std::runtime_error(
          "Did not expect to be called with PairAffinitiesSpec; it is expected to be converted to ColocateGroupsSpec");
    case GoalSpecs::Type::__EMPTY__:
      throw std::runtime_error(
          "GoalSpecs union is empty. You need to properly initialize it");
    default:
      throw std::runtime_error("Unhandled GoalSpecs::Type");
  }
}

} // namespace facebook::rebalancer::materializer
// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.
