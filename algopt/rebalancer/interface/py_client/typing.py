# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# pyre-strict
from typing import Union

from rebalancer.interface.thrift.v2.ProblemSpecs.thrift_types import (
    AggregatedGroupSpec,
    AssignmentAffinitiesSpec,
    AvoidAssignmentsSpec,
    AvoidMovingSpec,
    BalanceSpec,
    BalanceV2Spec,
    BipartiteSwapsSpec,
    CapacityRatioSpec,
    CapacitySpec,
    CapacityWithSupplyAndDrSpec,
    ColocateGroupsSpec,
    DisasterRecoveryCapacitySpec,
    DrainCapacitySpec,
    ExclusiveGroupsSpec,
    ExclusiveObjectsSpec,
    ExclusiveScopeItemsSpec,
    ExclusiveSwapsSpec,
    FlowSpec,
    GroupAssignmentAffinitiesSpec,
    GroupCapacitySpec,
    GroupCountSpec,
    GroupDiversitySpec,
    GroupIsolationLimitSpec,
    GroupMoveLimitSpec,
    ItemsAffinitySpec,
    MaximizeAllocationSpec,
    MaximizeFreeCapacityUnitsSpec,
    MinimizeContainersSpec,
    MinimizeMovementSpec,
    MinimizeNthLargestUtilizationSpec,
    MinimizeSquaresSpec,
    MoveGroupSpec,
    MovesInProgressSpec,
    MultipleOrCapacitySpec,
    NestedScopeLimitSpec,
    NonAcceptingSpec,
    ObjectAffinitiesSpec,
    PairAffinitiesSpec,
    RasRebalancingMovementSpec,
    RoutingLatencySpec,
    ScopeAffinitiesSpec,
    SRBufferCapacitySpec,
    SumOfMaxSpec,
    ThrottlingSpec,
    ToFreeSpec,
    UtilIncreaseCostSpec,
    WorkingSetSpec,
)
from rebalancer.interface.thrift.v2.SolverSpecs.thrift_types import (
    LocalSearchSolverSpec,
    LocalSearchStageSolverSpec,
    OptimalSolverSpec,
    OptimalSubsetSolverSpec,
)


Constraint = Union[
    AggregatedGroupSpec,
    AvoidAssignmentsSpec,
    AvoidMovingSpec,
    BipartiteSwapsSpec,
    CapacityRatioSpec,
    CapacitySpec,
    CapacityWithSupplyAndDrSpec,
    ColocateGroupsSpec,
    DisasterRecoveryCapacitySpec,
    DrainCapacitySpec,
    ExclusiveObjectsSpec,
    ExclusiveScopeItemsSpec,
    ExclusiveSwapsSpec,
    FlowSpec,
    GroupCapacitySpec,
    GroupCountSpec,
    GroupIsolationLimitSpec,
    GroupMoveLimitSpec,
    MoveGroupSpec,
    MovesInProgressSpec,
    MultipleOrCapacitySpec,
    NonAcceptingSpec,
    ObjectAffinitiesSpec,
    PairAffinitiesSpec,
    RasRebalancingMovementSpec,
    RoutingLatencySpec,
    SRBufferCapacitySpec,
    ThrottlingSpec,
    ToFreeSpec,
    MinimizeMovementSpec,
    ExclusiveGroupsSpec,
    NestedScopeLimitSpec,
    GroupDiversitySpec,
]

Goal = Union[
    AggregatedGroupSpec,
    AssignmentAffinitiesSpec,
    BalanceSpec,
    BalanceV2Spec,
    CapacityRatioSpec,
    CapacitySpec,
    CapacityWithSupplyAndDrSpec,
    ColocateGroupsSpec,
    DisasterRecoveryCapacitySpec,
    DrainCapacitySpec,
    ExclusiveObjectsSpec,
    FlowSpec,
    GroupCountSpec,
    GroupIsolationLimitSpec,
    MaximizeAllocationSpec,
    MaximizeFreeCapacityUnitsSpec,
    MinimizeContainersSpec,
    MinimizeMovementSpec,
    MinimizeNthLargestUtilizationSpec,
    MinimizeSquaresSpec,
    PairAffinitiesSpec,
    RoutingLatencySpec,
    ScopeAffinitiesSpec,
    SumOfMaxSpec,
    UtilIncreaseCostSpec,
    WorkingSetSpec,
    ExclusiveGroupsSpec,
    GroupAssignmentAffinitiesSpec,
    GroupCapacitySpec,
    ToFreeSpec,
    ItemsAffinitySpec,
    GroupDiversitySpec,
]

Solver = Union[
    LocalSearchSolverSpec,
    LocalSearchStageSolverSpec,
    OptimalSolverSpec,
    OptimalSubsetSolverSpec,
]
