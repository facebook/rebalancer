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

include "thrift/annotation/cpp.thrift"
include "thrift/annotation/thrift.thrift"
include "algopt/rebalancer/algopt_common/thrift/Types.thrift"
include "algopt/rebalancer/interface/thrift/ProblemSolver.thrift"
include "algopt/rebalancer/interface/thrift/Types.thrift"
include "algopt/rebalancer/interface/thrift/SolverSpecs.thrift"

namespace cpp2 facebook.rebalancer.entities.thrift
namespace py3 rebalancer.entities.thrift

@cpp.Type{template = "folly::F14FastMap"}
typedef map<i32, double> IntToDoubleFastMap

@cpp.Type{template = "folly::F14FastMap"}
typedef map<i32, list<i32>> IntToListOfIntFastMap

struct IdStore {
  3: list<i32> objectIds;
  4: list<i32> containerIds;
  5: IntToListOfIntFastMap scopeItemIds;
  6: IntToListOfIntFastMap groupIds;
  7: list<i32> dimensionIds;
  8: list<i32> goalIds;
  9: list<i32> constraintIds;
  10: list<i32> routingConfigIds;

  // Per-type name lists indexed by type-specific ID.
  // New bundles use these instead of the shared 'names' field.
  20: list<string> objectNames;

  21: list<string> containerNames;

  22: list<string> scopeNames;

  23: list<string> scopeItemNames;

  24: list<string> partitionNames;

  25: list<string> groupNames;

  26: list<string> dimensionNames;

  27: list<string> goalNames;

  28: list<string> constraintNames;

  29: list<string> routingConfigNames;

  // THE FOLLOWING FIELDS ARE DEPRECATED AND SHOULD NOT BE USED.
  @thrift.Deprecated{
    message = "Use the per-type name lists (objectNames, containerNames, etc.) instead.",
  }
  1: list<string> names;
}

struct ObjectStaticDimension {
  1: IntToDoubleFastMap values;
  2: double defaultValue;
}

struct ObjectGroupValues {
  1: i32 partitionId;
  2: IntToDoubleFastMap values;
}

union ObjectValues {
  1: IntToDoubleFastMap objectValues;
  2: ObjectGroupValues groupValues;
}

struct ObjectDynamicDimension {
  1: i32 scopeId;
  @cpp.Type{template = "folly::F14FastMap"}
  @thrift.Deprecated{message = "Use scopedValues instead."}
  2: map<i32, IntToDoubleFastMap> values;
  3: double defaultValue;
  @cpp.Type{template = "folly::F14FastMap"}
  4: map<i32, ObjectValues> scopedValues;
}

struct ObjectPartitionRoutingDimension {
  1: IntToDoubleFastMap groupIdToValue;
  2: double defaultValue;
  3: i32 partitionId;
  4: i32 routingConfigId;
  5: IntToDoubleFastMap groupIdToStaticValue;
  6: double defaultStaticValue;
}

union ObjectScalarDimension {
  1: ObjectStaticDimension objectStaticDimension;
  2: ObjectDynamicDimension objectDynamicDimension;
  3: ObjectPartitionRoutingDimension objectPartitionRoutingDimension;
}

struct ObjectDimension {
  1: list<ObjectScalarDimension> scalarDimensions;
  2: bool isDynamic;
}

struct Objects {
  @cpp.Type{template = "folly::F14FastMap"}
  3: map<i32, ObjectDimension> dimensions;
  // TODO: deprecate this field
  4: list<i32> objectIds;

  5: i32 numObjects;
}

struct Containers {
  1: IntToListOfIntFastMap initialAssignment;
}

struct ScopeDimension {
  1: IntToDoubleFastMap values;
  2: double defaultValue;
}

struct Scope {
  1: IntToListOfIntFastMap scopeItems;
  @cpp.Type{template = "folly::F14FastMap"}
  2: map<i32, ScopeDimension> dimensions;
}

struct Scopes {
  @cpp.Type{template = "folly::F14FastMap"}
  1: map<i32, Scope> scopes;
}

struct Partition {
  1: IntToListOfIntFastMap groups;
}

struct Partitions {
  @cpp.Type{template = "folly::F14FastMap"}
  1: map<i32, Partition> partitions;
}

struct Goal {
  1: ProblemSolver.GoalSpecs spec;
  2: double weight;
  3: i32 tupleIndex;
}

struct Goals {
  @cpp.Type{template = "folly::F14FastMap"}
  1: map<i32, Goal> goals;
}

struct Constraint {
  1: ProblemSolver.ConstraintSpecs spec;
  2: Types.ConstraintPolicy policy;
  3: double invalidCost;
  4: double invalidState;
  5: i32 tuplePosIfBroken;
}

struct Constraints {
  @cpp.Type{template = "folly::F14FastMap"}
  1: map<i32, Constraint> constraints;
}

struct RoutingRing {
  1: i32 originScopeItem;
  2: double originTraffic;
  3: optional list<list<i32>> destinationScopeItemSets;
}

struct GroupRoutingRings {
  1: list<RoutingRing> routingRings;
}

struct RoutingConfig {
  @cpp.Type{template = "folly::F14FastMap"}
  2: map<i32, IntToDoubleFastMap> latencyTable;
  3: i32 scopeId;
  4: i32 partitionId;
  @cpp.Type{template = "folly::F14FastMap"}
  5: map<i32, GroupRoutingRings> groupToRoutingRingsEntities;
  @cpp.Type{template = "folly::F14FastMap"}
  6: optional map<i32, list<list<i32>>> defaultOriginToDestinationScopeItemSets;
}

@thrift.ReserveIds{ids = [13]} //deprecated fields
struct Universe {
  1: IdStore idStore;
  2: string objectTypeName;
  3: string containerTypeName;
  4: Objects objects;
  5: Containers containers;
  6: optional list<list<i32>> similarContainerIds;
  7: Scopes scopes;
  8: Partitions partitions;
  9: Goals goals;
  10: Constraints constraints;
  11: bool stableOptimization;
  12: bool moveObjectsOnce;
  14: list<i32> descendingHotnessContainers;
  15: optional i32 objectOrderingDimensionId;
  @cpp.Type{template = "folly::F14FastMap"}
  16: map<i32, RoutingConfig> routingConfigIdToRoutingConfig;
  @cpp.Type{template = "folly::F14FastMap"}
  17: map<
    string,
    SolverSpecs.DestinationsToExploreOptions
  > idToDestinationToExploreOptions;
  18: Types.PrecisionTolerances precisionTolerances;
}
