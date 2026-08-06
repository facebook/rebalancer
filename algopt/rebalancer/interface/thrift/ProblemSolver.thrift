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

package "meta.com/algopt/rebalancer"

include "thrift/annotation/thrift.thrift"

namespace cpp2 facebook.rebalancer.interface
namespace py3 rebalancer.interface.thrift.v2
namespace rust problem_solver_types

include "algopt/rebalancer/interface/thrift/ProblemSpecs.thrift"
include "algopt/rebalancer/interface/thrift/SolverSpecs.thrift"

enum ManifoldUploadPolicy {
  // always upload to manifold
  ALWAYS = 1,
  // upload to manifold only on failures
  ON_FAILURE = 2,
  // never upload to manifold
  NEVER = 3,
  // upload to manifold only if current run is an outlier: takes more than
  // expected time to solve, takes more than expected memory, etc.
  OUTLIER = 4,
}

@thrift.ReserveIds{ids = [3]}
struct ManifoldBackupParams {
  1: ManifoldUploadPolicy uploadPolicy = ManifoldUploadPolicy.ON_FAILURE;
  2: optional i64 expectedRuntime;
}

@thrift.ReserveIds{ids = [10, 15, 21, 27]}
union ConstraintSpecs {
  1: ProblemSpecs.AggregatedGroupSpec aggregatedGroupSpec;
  2: ProblemSpecs.AvoidAssignmentsSpec avoidAssignmentsSpec;
  3: ProblemSpecs.AvoidMovingSpec avoidMovingSpec;
  4: ProblemSpecs.BipartiteSwapsSpec bipartiteSwapsSpec;
  5: ProblemSpecs.CapacityRatioSpec capacityRatioSpec;
  6: ProblemSpecs.CapacitySpec capacitySpec;
  7: ProblemSpecs.CapacityWithSupplyAndDrSpec capacityWithSupplyAndDrSpec;
  8: ProblemSpecs.ColocateGroupsSpec colocateGroupsSpec;
  9: ProblemSpecs.DisasterRecoveryCapacitySpec disasterRecoveryCapacitySpec;
  11: ProblemSpecs.DrainCapacitySpec drainCapacitySpec;
  12: ProblemSpecs.ExclusiveObjectsSpec exclusiveObjectsSpec;
  13: ProblemSpecs.ExclusiveScopeItemsSpec exclusiveScopeItemsSpec;
  14: ProblemSpecs.ExclusiveSwapsSpec exclusiveSwapsSpec;
  16: ProblemSpecs.FlowSpec flowSpec;
  17: ProblemSpecs.GroupCapacitySpec groupCapacitySpec;
  18: ProblemSpecs.GroupCountSpec groupCountSpec;
  19: ProblemSpecs.GroupIsolationLimitSpec groupIsolationLimitSpec;
  20: ProblemSpecs.GroupMoveLimitSpec groupMoveLimitSpec;
  22: ProblemSpecs.MoveGroupSpec moveGroupSpec;
  23: ProblemSpecs.MovesInProgressSpec movesInProgressSpec;
  24: ProblemSpecs.MultipleOrCapacitySpec multipleOrCapacitySpec;
  25: ProblemSpecs.NonAcceptingSpec nonAcceptingSpec;
  26: ProblemSpecs.ObjectAffinitiesSpec objectAffinitiesSpec;
  28: ProblemSpecs.PairAffinitiesSpec pairAffinitiesSpec;
  29: ProblemSpecs.RasRebalancingMovementSpec rasRebalancingMovementSpec;
  30: ProblemSpecs.SRBufferCapacitySpec srBufferCapacitySpec;
  31: ProblemSpecs.ThrottlingSpec throttlingSpec;
  32: ProblemSpecs.ToFreeSpec toFreeSpec;
  33: ProblemSpecs.MinimizeMovementSpec minimizeMovementSpec;
  34: ProblemSpecs.ExclusiveGroupsSpec exclusiveGroupsSpec;
  35: ProblemSpecs.NestedScopeLimitSpec nestedScopeLimitSpec;
  36: ProblemSpecs.RoutingLatencySpec routingLatencySpec;
  37: ProblemSpecs.GroupDiversitySpec groupDiversitySpec;
  38: ProblemSpecs.LogicalOrSpec logicalOrSpec;
  39: ProblemSpecs.LogicalAndSpec logicalAndSpec;
  40: ProblemSpecs.CapacityWithGroupPresenceSpec capacityWithGroupPresenceSpec;
}

@thrift.ReserveIds{ids = [11, 14]}
union GoalSpecs {
  1: ProblemSpecs.AggregatedGroupSpec aggregatedGroupSpec;
  2: ProblemSpecs.AssignmentAffinitiesSpec assignmentAffinitiesSpec;
  5: ProblemSpecs.BalanceV2Spec balanceV2Spec;
  6: ProblemSpecs.CapacityRatioSpec capacityRatioSpec;
  7: ProblemSpecs.CapacitySpec capacitySpec;
  8: ProblemSpecs.CapacityWithSupplyAndDrSpec capacityWithSupplyAndDrSpec;
  9: ProblemSpecs.ColocateGroupsSpec colocateGroupsSpec;
  10: ProblemSpecs.DisasterRecoveryCapacitySpec disasterRecoveryCapacitySpec;
  12: ProblemSpecs.DrainCapacitySpec drainCapacitySpec;
  13: ProblemSpecs.ExclusiveObjectsSpec exclusiveObjectsSpec;
  15: ProblemSpecs.FlowSpec flowSpec;
  16: ProblemSpecs.GroupCountSpec groupCountSpec;
  17: ProblemSpecs.GroupIsolationLimitSpec groupIsolationLimitSpec;
  18: ProblemSpecs.MaximizeAllocationSpec maximizeAllocationSpec;
  19: ProblemSpecs.MinimizeContainersSpec minimizeContainersSpec;
  20: ProblemSpecs.MinimizeMovementSpec minimizeMovementSpec;
  21: ProblemSpecs.MinimizeNthLargestUtilizationSpec minimizeNthLargestUtilizationSpec;
  23: ProblemSpecs.MinimizeSquaresSpec minimizeSquaresSpec;
  25: ProblemSpecs.PairAffinitiesSpec pairAffinitiesSpec;
  26: ProblemSpecs.ScopeAffinitiesSpec scopeAffinitiesSpec;
  27: ProblemSpecs.SumOfMaxSpec sumOfMaxSpec;
  28: ProblemSpecs.UtilIncreaseCostSpec utilIncreaseCostSpec;
  29: ProblemSpecs.WorkingSetSpec workingSetSpec;
  30: ProblemSpecs.ExclusiveGroupsSpec exclusiveGroupsSpec;
  31: ProblemSpecs.BalanceSpec balanceSpec;
  33: ProblemSpecs.GroupCapacitySpec groupCapacitySpec;
  34: ProblemSpecs.ToFreeSpec toFreeSpec;
  35: ProblemSpecs.ItemsAffinitySpec itemsAffinitySpec;
  36: ProblemSpecs.LargeShardSpec largeShardSpec;
  37: ProblemSpecs.GroupAssignmentAffinitiesSpec groupAssignmentAffinitiesSpec;
  38: ProblemSpecs.RoutingLatencySpec routingLatencySpec;
  39: ProblemSpecs.GroupDiversitySpec groupDiversitySpec;
  40: ProblemSpecs.SRBufferCapacitySpec srBufferCapacitySpec;
  41: ProblemSpecs.CapacityWithGroupPresenceSpec capacityWithGroupPresenceSpec;
  42: ProblemSpecs.DiversifyWithinScopeItemSpec diversifyWithinScopeItemSpec;
  43: ProblemSpecs.ExclusiveScopeItemsSpec exclusiveScopeItemsSpec;
  44: ProblemSpecs.MaximizeFreeCapacityUnitsSpec maximizeFreeCapacityUnitsSpec;
}

@thrift.ReserveIds{ids = [1, 2]}
union SolverSpecs {
  3: SolverSpecs.LocalSearchSolverSpec localSearchSolverSpec;
  4: SolverSpecs.LocalSearchStageSolverSpec localSearchStageSolverSpec;
  5: SolverSpecs.OptimalSolverSpec optimalSolverSpec;
  6: SolverSpecs.OptimalSubsetSolverSpec optimalSubsetSolverSpec;
}
