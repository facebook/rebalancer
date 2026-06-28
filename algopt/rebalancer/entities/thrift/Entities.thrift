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
include "configerator/structs/thrift_explorer/annotations.thrift" as thrift_explorer

namespace cpp2 facebook.rebalancer.entities.thrift
namespace py3 rebalancer.entities.thrift

@cpp.Type{template = "folly::F14FastMap"}
typedef map<i32, double> IntToDoubleFastMap

@cpp.Type{template = "folly::F14FastMap"}
typedef map<i32, list<i32>> IntToListOfIntFastMap

@thrift_explorer.Context{
  description = "
Stores the relationship between entity IDs and their corresponding names.
",
}
struct IdStore {
  @thrift_explorer.Context{
    description = "
IDs of all entities of type 'object', in no particular order.
",
  }
  3: list<i32> objectIds;
  @thrift_explorer.Context{
    description = "
IDs of all entities of type 'container', in no particular order.
",
  }
  4: list<i32> containerIds;
  @thrift_explorer.Context{
    description = "
Maps IDs of 'scope' entities into the IDs of 'scope item' entities within each.
",
  }
  5: IntToListOfIntFastMap scopeItemIds;
  @thrift_explorer.Context{
    description = "
Maps IDs of 'partition' entities into the IDs of 'group' entities within each.
",
  }
  6: IntToListOfIntFastMap groupIds;
  @thrift_explorer.Context{
    description = "
IDs of all entities of type 'dimension', in no particular order.
",
  }
  7: list<i32> dimensionIds;
  @thrift_explorer.Context{
    description = "
IDs of all entities of type 'goal', in no particular order.
",
  }
  8: list<i32> goalIds;
  @thrift_explorer.Context{
    description = "
IDs of all entities of type 'constraint', in no particular order.
",
  }
  9: list<i32> constraintIds;
  @thrift_explorer.Context{
    description = "
IDs of all entities of type 'routing config', in no particular order.
",
  }
  10: list<i32> routingConfigIds;

  // Per-type name lists indexed by type-specific ID.
  // New bundles use these instead of the shared 'names' field.
  @thrift_explorer.Context{
    description = "
Names of all 'object' entities ordered by their object ID. An object ID is an
index within this list.
",
  }
  20: list<string> objectNames;

  @thrift_explorer.Context{
    description = "
Names of all 'container' entities ordered by their container ID. A container ID
is an index within this list.
",
  }
  21: list<string> containerNames;

  @thrift_explorer.Context{
    description = "
Names of all 'scope' entities ordered by their scope ID. A scope ID is an index
within this list.
",
  }
  22: list<string> scopeNames;

  @thrift_explorer.Context{
    description = "
Names of all 'scope item' entities ordered by their scope item ID. A scope item
ID is an index within this list.
",
  }
  23: list<string> scopeItemNames;

  @thrift_explorer.Context{
    description = "
Names of all 'partition' entities ordered by their partition ID. A partition ID
is an index within this list.
",
  }
  24: list<string> partitionNames;

  @thrift_explorer.Context{
    description = "
Names of all 'group' entities ordered by their group ID. A group ID is an index
within this list.
",
  }
  25: list<string> groupNames;

  @thrift_explorer.Context{
    description = "
Names of all 'dimension' entities ordered by their dimension ID. A dimension ID
is an index within this list.
",
  }
  26: list<string> dimensionNames;

  @thrift_explorer.Context{
    description = "
Names of all 'goal' entities ordered by their goal ID. A goal ID is an index
within this list.
",
  }
  27: list<string> goalNames;

  @thrift_explorer.Context{
    description = "
Names of all 'constraint' entities ordered by their constraint ID. A constraint
ID is an index within this list.
",
  }
  28: list<string> constraintNames;

  @thrift_explorer.Context{
    description = "
Names of all 'routing config' entities ordered by their routing config ID. A
routing config ID is an index within this list.
",
  }
  29: list<string> routingConfigNames;

  // THE FOLLOWING FIELDS ARE DEPRECATED AND SHOULD NOT BE USED.
  @thrift_explorer.Context{
    description = "
Contains the names of all entities in the universe ordered by ID. An entity ID
represents an index within this list. This can be used to translate the ID of
any entity into its name. Names are not unique.
",
  }
  @thrift.Deprecated{
    message = "Use the per-type name lists (objectNames, containerNames, etc.) instead.",
  }
  1: list<string> names;
}

@thrift_explorer.Context{
  description = "
Describes a static object dimension. This is the most common type of object
dimension. It gives each object a numeric value.
",
}
struct ObjectStaticDimension {
  @thrift_explorer.Context{
    description = "
Maps object names to their respective value.
",
  }
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.objectNames",
      },
    },
  }
  1: IntToDoubleFastMap values;
  @thrift_explorer.Context{
    description = "
Default value given to objects not explicitly mentioned in the 'values' map.
",
  }
  2: double defaultValue;
}

struct ObjectGroupValues {
  @thrift_explorer.Context{
    description = "
ID of the partition used by the group-keyed values.
",
  }
  @thrift_explorer.Transform{
    listIndexToItem = thrift_explorer.ListIndexToItemTransform{
      path = "$.problem.universe.idStore.partitionNames",
    },
  }
  1: i32 partitionId;
  @thrift_explorer.Context{
    description = "
Maps group names to their respective value.
",
  }
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.groupNames",
      },
    },
  }
  2: IntToDoubleFastMap values;
}

union ObjectValues {
  @thrift_explorer.Context{
    description = "
Maps object names to their respective value.
",
  }
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.objectNames",
      },
    },
  }
  1: IntToDoubleFastMap objectValues;
  2: ObjectGroupValues groupValues;
}

@thrift_explorer.Context{
  description = "
Describes a dynamic object dimension. It gives each object a numeric value which
depends on the scope item it is placed in.
",
}
struct ObjectDynamicDimension {
  @thrift_explorer.Context{
    description = "
ID of the scope the object value depends on.
",
  }
  1: i32 scopeId;
  @thrift_explorer.Context{
    description = "
Maps scope item names to object names to value. That's the value given to the
object when placed within that scope item.
",
  }
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.scopeItemNames",
      },
    },
    nestedMapKeyTransform = thrift_explorer.MapKeyTransform{
      transform = thrift_explorer.Transform{
        listIndexToItem = thrift_explorer.ListIndexToItemTransform{
          path = "$.problem.universe.idStore.objectNames",
        },
      },
    },
  }
  @cpp.Type{template = "folly::F14FastMap"}
  @thrift.Deprecated{message = "Use scopedValues instead."}
  2: map<i32, IntToDoubleFastMap> values;
  @thrift_explorer.Context{
    description = "
Default value given to objects when placed in a scope item that doesn't match
any combination mentioned in the 'values' map.
",
  }
  3: double defaultValue;
  @thrift_explorer.Context{
    description = "
Maps scope item names to object values. The nested value can be backed either by
object IDs or by group IDs from a partition.
",
  }
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.scopeItemNames",
      },
    },
  }
  @cpp.Type{template = "folly::F14FastMap"}
  4: map<i32, ObjectValues> scopedValues;
}

struct ObjectPartitionRoutingDimension {
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.groupNames",
      },
    },
  }
  1: IntToDoubleFastMap groupIdToValue;
  2: double defaultValue;
  3: i32 partitionId;
  4: i32 routingConfigId;
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.groupNames",
      },
    },
  }
  5: IntToDoubleFastMap groupIdToStaticValue;
  6: double defaultStaticValue;
}

@thrift_explorer.Context{
  description = "
Describes an object dimension. Only one of the 3 fields can be present:
- objectStaticDimension: the most common, object dimension does not depend on
  placement.
- objectDynamicDimension: dimension of an object depends on where it's placed.
- objectPartitionRoutingDimension: rare; special dimension where each group has
  a value (individual objects do not have values) and depends on a routing
  config.
",
}
union ObjectScalarDimension {
  1: ObjectStaticDimension objectStaticDimension;
  2: ObjectDynamicDimension objectDynamicDimension;
  3: ObjectPartitionRoutingDimension objectPartitionRoutingDimension;
}

@thrift_explorer.Context{
  description = "
Describes an object dimension: a mapping of each object to a scalar value.
",
}
struct ObjectDimension {
  @thrift_explorer.Context{
    description = "
Typically, this list will contain a single scalar dimension. However, in some
cases, a dimension may be a vector of scalar values, each representing the value
at a particular time of the day, for example.
",
  }
  1: list<ObjectScalarDimension> scalarDimensions;
  2: bool isDynamic;
}

@thrift_explorer.Context{description = "
Describes all objects in the universe.
"}
struct Objects {
  @thrift_explorer.Context{
    description = "
Maps a dimension name to a data structure describing how it maps objects to
values.
",
  }
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.dimensionNames",
      },
    },
  }
  @cpp.Type{template = "folly::F14FastMap"}
  3: map<i32, ObjectDimension> dimensions;
  // TODO: deprecate this field
  @thrift_explorer.Context{
    description = "
List of all object IDs in the universe, in no particular order.
",
  }
  4: list<i32> objectIds;

  @thrift_explorer.Context{
    description = "
Total number of objects in the universe. Object IDs are always dense and
contiguous from 0, so this count fully determines the set of object IDs as
[0, 1, ..., numObjects - 1].
",
  }
  5: i32 numObjects;
}

@thrift_explorer.Context{
  description = "
Describes all containers in the universe.
",
}
struct Containers {
  @thrift_explorer.Context{
    description = "
Maps each container ID to the list of object IDs initially assigned to it.
",
  }
  1: IntToListOfIntFastMap initialAssignment;
}

@thrift_explorer.Context{
  description = "
Describes a scope dimension. It gives each scope item a numeric value.
",
}
struct ScopeDimension {
  @thrift_explorer.Context{
    description = "
Maps scope item names to their respective value.
",
  }
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.scopeItemNames",
      },
    },
  }
  1: IntToDoubleFastMap values;
  @thrift_explorer.Context{
    description = "
Default value given to scope items not explicitly mentioned in the 'values' map.
",
  }
  2: double defaultValue;
}

@thrift_explorer.Context{
  description = "
Describes a single scope. A scope is a partition of containers into groups
called scope items.
",
}
struct Scope {
  @thrift_explorer.Context{
    description = "
Maps each scope item ID to the list of container IDs composing the scope item.
",
  }
  1: IntToListOfIntFastMap scopeItems;
  @thrift_explorer.Context{
    description = "
Maps a dimension name to a data structure describing how scope items map to
values.
",
  }
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.dimensionNames",
      },
    },
  }
  @cpp.Type{template = "folly::F14FastMap"}
  2: map<i32, ScopeDimension> dimensions;
}

@thrift_explorer.Context{
  description = "
Describes all scopes in the universe. Scopes are disjoint groupings of
containers.
",
}
struct Scopes {
  @thrift_explorer.Context{
    description = "
Maps each scope name to a data structure describing the scope.
",
  }
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.scopeNames",
      },
    },
  }
  @cpp.Type{template = "folly::F14FastMap"}
  1: map<i32, Scope> scopes;
}

@thrift_explorer.Context{
  description = "
Describes a partition of objects into groups. Groups may be overlapping: an
object may belong to any number of groups.
",
}
struct Partition {
  @thrift_explorer.Context{
    description = "
Maps each group ID to the list of object IDs composing the group.
",
  }
  1: IntToListOfIntFastMap groups;
}

@thrift_explorer.Context{
  description = "
Describes all partitions in the universe. If unspecified, 'partition' always
refers to 'object partition'.
",
}
struct Partitions {
  @thrift_explorer.Context{
    description = "
Maps each partition name to a data structure describing the partition.
",
  }
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.partitionNames",
      },
    },
  }
  @cpp.Type{template = "folly::F14FastMap"}
  1: map<i32, Partition> partitions;
}

@thrift_explorer.Context{description = "
Describes a single goal.
"}
struct Goal {
  @thrift_explorer.Context{
    description = "
Data structure describing the type of goal and its configuration.
  ",
  }
  1: ProblemSolver.GoalSpecs spec;
  @thrift_explorer.Context{
    description = "
Weight of this particular goal when aggregated with other competing goals.
  ",
  }
  2: double weight;
  @thrift_explorer.Context{
    description = "
Index within the tuple of goals this particular goal should be placed in. Goals
within the same tuple index get summed up using weights. The solver will aim to
find the solution with the lexicographically smallest goal tuple.
  ",
  }
  3: i32 tupleIndex;
}

@thrift_explorer.Context{description = "
Describes all goals in the universe.
"}
struct Goals {
  @thrift_explorer.Context{
    description = "
Maps each goal name to a data structure describing the goal.
  ",
  }
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.goalNames",
      },
    },
  }
  @cpp.Type{template = "folly::F14FastMap"}
  1: map<i32, Goal> goals;
}

@thrift_explorer.Context{description = "
Describes a single constraint.
"}
struct Constraint {
  @thrift_explorer.Context{
    description = "
Data structure describing the type of constraint and its configuration.
  ",
  }
  1: ProblemSolver.ConstraintSpecs spec;
  @thrift_explorer.Context{
    description = "
Decides how how the solver should treat the constraint if initially broken.
  ",
  }
  2: Types.ConstraintPolicy policy;
  @thrift_explorer.Context{
    description = "
If the constraint is broken, then its penalty formula is added as a goal using
invalidCost as weight. This creates an incentive for the solver to minimize the
constraint's brokenness or even satisfy it completely.
  ",
  }
  3: double invalidCost;
  @thrift_explorer.Context{
    description = "
The value of invalidState is conditionally added to the goal function if the
constraint is not fully satisfied.
  ",
  }
  4: double invalidState;
  @thrift_explorer.Context{
    description = "
Index within the goal tuple the broken constraint penalties get added to.
  ",
  }
  5: i32 tuplePosIfBroken;
}

@thrift_explorer.Context{
  description = "
Describes all constraints in the universe.
",
}
struct Constraints {
  @thrift_explorer.Context{
    description = "
Maps each constraint name to a data structure describing the constraint.
",
  }
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.constraintNames",
      },
    },
  }
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
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.scopeItemNames",
      },
    },
  }
  @cpp.Type{template = "folly::F14FastMap"}
  2: map<i32, IntToDoubleFastMap> latencyTable;
  3: i32 scopeId;
  4: i32 partitionId;
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.groupNames",
      },
    },
  }
  @cpp.Type{template = "folly::F14FastMap"}
  5: map<i32, GroupRoutingRings> groupToRoutingRingsEntities;
  @cpp.Type{template = "folly::F14FastMap"}
  6: optional map<i32, list<list<i32>>> defaultOriginToDestinationScopeItemSets;
}

@thrift_explorer.Context{
  description = "
Describes a Rebalancer problem fully, including a description of all objects,
containers, scopes, partitions, dimensions, goals, constraints, etc. It does not
include the solution generated by the solver.
",
}
@thrift.ReserveIds{ids = [13]} //deprecated fields
struct Universe {
  @thrift_explorer.Context{
    description = "
Stores the relationship between entity IDs and names.
",
  }
  1: IdStore idStore;
  @thrift_explorer.Context{
    description = "
Name of the real world entity an object represents. For example, in a
shard-to-server assignment problem, the object type would be 'shard'.
",
  }
  2: string objectTypeName;
  @thrift_explorer.Context{
    description = "
Name of the real world entity a container represents. For example, in a
shard-to-server assignment problem, the container type would be 'server'.
",
  }
  3: string containerTypeName;
  @thrift_explorer.Context{
    description = "
Information about objects and object dimensions.
",
  }
  4: Objects objects;
  @thrift_explorer.Context{
    description = "
Information about containers and container dimensions.
",
  }
  5: Containers containers;
  @thrift_explorer.Context{
    description = "
Divides containers into classes of similarity. The outer list represents a
collection of similarity classes. Each inner list describes a single similarity
class, and it is a list of container IDs.
",
  }
  6: optional list<list<i32>> similarContainerIds;
  @thrift_explorer.Context{
    description = "
Information about scopes and scope dimensions. Scopes are groupings of
containers. For example, in a shard-to-server assignment problem, servers may be
organized into racks, and 'rack' would be defined as a scope.
",
  }
  7: Scopes scopes;
  @thrift_explorer.Context{
    description = "
Information about object partitions. Partitions are groupings of objects. For
example, in a shard-to-server assignment problem, shards may be organized into
replicas, and 'replica' would be defined as a partition.
",
  }
  8: Partitions partitions;
  9: Goals goals;
  10: Constraints constraints;
  11: bool stableOptimization;
  12: bool moveObjectsOnce;
  14: list<i32> descendingHotnessContainers;
  15: optional i32 objectOrderingDimensionId;
  @thrift_explorer.MapKeyTransform{
    transform = thrift_explorer.Transform{
      listIndexToItem = thrift_explorer.ListIndexToItemTransform{
        path = "$.problem.universe.idStore.routingConfigNames",
      },
    },
  }
  @cpp.Type{template = "folly::F14FastMap"}
  16: map<i32, RoutingConfig> routingConfigIdToRoutingConfig;
  @cpp.Type{template = "folly::F14FastMap"}
  17: map<
    string,
    SolverSpecs.DestinationsToExploreOptions
  > idToDestinationToExploreOptions;
  18: Types.PrecisionTolerances precisionTolerances;
}
