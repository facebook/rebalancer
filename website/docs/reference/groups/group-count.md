---
sidebar_position: 12
---

# GroupCountSpec

**Type**: Goal or Constraint

Control how many groups from a partition can exist on each scope item.

## Overview

`GroupCountSpec` limits or balances the number of distinct groups (from a partition) present on scope items. This is essential for diversity, spreading risk, and ensuring fault tolerance.

**Use this when**: You need to control diversity (e.g., max 1 replica per rack), spread groups across containers, or ensure minimum presence requirements.

## Quick Example

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs groupId="programming-language">
<TabItem value="python" label="Python" default>

```python file=algopt/rebalancer/examples/website/reference/groups/group_count_spec_examples.py start=quick_example_start end=quick_example_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_count_spec_examples.cpp start=quick_example_start end=quick_example_end
```

</TabItem>
</Tabs>

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | Yes | - | Unique identifier for this spec |
| `scope` | string | Yes | - | Scope items to count groups on (e.g., `"rack"`, `"host"`) |
| `partitionName` | string | Yes | - | Partition defining groups (e.g., `"replica"`, `"tenant"`) |
| `definition` | GroupCountSpecDefinition | No | AFTER | When to count groups (see below) |
| `bound` | GroupCountSpecBound | No | MAX | Type of limit (MAX, MIN, EXACT, MULTIPLE) |
| `limit` | Limit | No | ABSOLUTE | The limit value |
| `squares` | bool | No | false | Apply squared penalty (goal only) |
| `zeroAllowed` | bool | No | false | Allow zero groups on scope items |
| `dimension` | string | No | null | Count using dimension instead of group presence |
| `filter` | Filter | No | null | Apply only to filtered objects |
| `limitRelativeTo` | GroupCountSpecLimitRelativeTo | No | GROUP_SIZE | What limit is relative to |
| `minimumLimit` | double | No | 0.0 | Minimum when zeroAllowed=true and bound=MIN |

## Bound Types

| Bound | Behavior | Example |
|-------|----------|---------|
| **MAX** | At most N groups per scope item | Max 1 replica per rack |
| **MIN** | At least N groups per scope item | Min 3 replicas per datacenter |
| **EXACT** | Exactly N groups per scope item | Exactly 5 shards per host |
| **MULTIPLE** | Group count is multiple of N | Groups per host in \{0, N, 2N, 3N, ...\} |

## Definition Types

| Definition | Counts | Use Case |
|-----------|--------|----------|
| **AFTER** | Groups present after rebalancing | Standard diversity (default) |
| **DURING** | Groups present during rebalancing (union of before and after) | Avoid rack failure during migration |
| **DURING_AND_AFTER** | Stricter than DURING | Extra safety during migration |
| **STAYED** | Groups that didn't move | Penalize moving groups off scope items |

## Common Usage Patterns

### 1. Rack Diversity (Max 1 per Rack)

Ensure no two replicas of the same object on the same rack:

```python
# Define replica partition
solver.add_partition("replica", replica_to_objects)

# Max 1 replica per rack
solver.add_constraint(
        ConstraintSpec(
            groupCountSpec=GroupCountSpec(
        name="rack-diversity",
        scope="rack",
        partitionName="replica",
        bound=GroupCountSpecBound.MAX,
        limit=Limit(type=LimitType.ABSOLUTE, globalLimit=1),
    )
        )
)
```

### 2. Minimum Presence (At Least N per Scope Item)

Ensure minimum number of groups on each container:

```python
# Each datacenter must have at least 3 replicas
solver.add_constraint(
        ConstraintSpec(
            groupCountSpec=GroupCountSpec(
        name="min-replicas-per-dc",
        scope="datacenter",
        partitionName="replica",
        bound=GroupCountSpecBound.MIN,
        limit=Limit(type=LimitType.ABSOLUTE, globalLimit=3),
    )
        )
)
```

### 3. Exact Group Count

Ensure exactly N groups per scope item:

```python
# Exactly 10 shards per host
solver.add_constraint(
        ConstraintSpec(
            groupCountSpec=GroupCountSpec(
        name="exact-shards-per-host",
        scope="host",
        partitionName="shard",
        bound=GroupCountSpecBound.EXACT,
        limit=Limit(type=LimitType.ABSOLUTE, globalLimit=10),
    )
        )
)
```

### 4. Multiple of N Groups

Ensure group count is a multiple (useful for even distribution):

```python
# Groups per host must be multiple of 5
solver.add_constraint(
        ConstraintSpec(
            groupCountSpec=GroupCountSpec(
        name="multiple-of-5",
        scope="host",
        partitionName="shard",
        bound=GroupCountSpecBound.MULTIPLE,
        limit=Limit(type=LimitType.ABSOLUTE, globalLimit=5),
    )
        )
)
```

### 5. Relative to Group Size

Limit proportional to group size:

```python
# Large groups can spread more, small groups less
solver.add_constraint(
        ConstraintSpec(
            groupCountSpec=GroupCountSpec(
        name="proportional-spread",
        scope="rack",
        partitionName="replica",
        bound=GroupCountSpecBound.MAX,
        limit=Limit(
            type=LimitType.RELATIVE_TO_GROUP,
            relativeLimitPercentage=0.5,  # Max 50% of group on one rack
        ),
        limitRelativeTo=GroupCountSpecLimitRelativeTo.GROUP_SIZE,
    )
        )
)
```

### 6. Allow Zero with Minimum

Allow some scope items to have zero groups:

```python
# Min 2 groups per host, but zero is okay
solver.add_constraint(
        ConstraintSpec(
            groupCountSpec=GroupCountSpec(
        name="min-with-zero-allowed",
        scope="host",
        partitionName="shard",
        bound=GroupCountSpecBound.MIN,
        limit=Limit(type=LimitType.ABSOLUTE, globalLimit=2),
        zeroAllowed=True,
        minimumLimit=0,
    )
        )
)
```

### 7. Goal (Soft) Instead of Constraint

Balance group count as a goal rather than hard constraint:

```python
# Prefer even group distribution
solver.add_goal(
        GoalSpec(
            groupCountSpec=GroupCountSpec(
        name="balance-groups",
        scope="host",
        partitionName="shard",
        bound=GroupCountSpecBound.MAX,
        limit=Limit(type=LimitType.ABSOLUTE, globalLimit=10),
        squares=True,  # Penalize imbalance quadratically
    )
        ),
    weight=0.5
)
```

## Disaster Recovery Example

Maintain diversity during and after migration:

```python
# Define replica partition
solver.add_partition("replica", {
    "replica_0": ["shard_0", "shard_1", "shard_2"],
    "replica_1": ["shard_0", "shard_1", "shard_2"],
    "replica_2": ["shard_0", "shard_1", "shard_2"],
})

# Max 1 replica per rack, even during migration
solver.add_constraint(
        ConstraintSpec(
            groupCountSpec=GroupCountSpec(
        name="rack-diversity-during-migration",
        scope="rack",
        partitionName="replica",
        bound=GroupCountSpecBound.MAX,
        limit=Limit(type=LimitType.ABSOLUTE, globalLimit=1),
        definition=GroupCountSpecDefinition.DURING,  # Strict!
    )
        )
)
```

## Performance Considerations

- **Impact**: Moderate - requires group presence tracking
- **Solver compatibility**: Works with both Local Search and Optimal solvers
- **Scaling**: O(number of groups × number of scope items)
- **Complexity**: More complex than simple capacity constraints

## Common Pitfalls

### 1. Infeasible Min/Exact Constraints

**Problem**: Impossible to satisfy with available resources.

```python
# BAD: Only 5 hosts, but need min 3 groups per host for 20 groups
solver.add_constraint(
        ConstraintSpec(
            groupCountSpec=GroupCountSpec(
        scope="host",  # Only 5 hosts
        partitionName="replica",  # 20 groups
        bound=GroupCountSpecBound.MIN,
        limit=Limit(type=LimitType.ABSOLUTE, globalLimit=3),
    )
        )
)
# Impossible: 5 hosts × 3 groups/host = 15 < 20 groups
```

**Solution**: Verify math before adding constraint.

### 2. Conflicting with Colocation

**Problem**: GroupCount MAX conflicts with colocation requirements.

```python
# Conflicts: Can't colocate and enforce diversity
solver.add_constraint(
        ConstraintSpec(
            colocateGroupsSpec=ColocateGroupsSpec(partitionName="replica", ...)
        )
)
solver.add_constraint(
        ConstraintSpec(
            groupCountSpec=GroupCountSpec(
        partitionName="replica",
        bound=GroupCountSpecBound.MAX,
        limit=Limit(type=LimitType.ABSOLUTE, globalLimit=1),
    )
        )
)
```

**Solution**: Check that specs aren't contradictory.

### 3. Wrong Definition Type

**Problem**: Using AFTER when you need DURING for safety.

```python
# BAD: Rack can have 2 replicas during migration (unsafe!)
solver.add_constraint(
        ConstraintSpec(
            groupCountSpec=GroupCountSpec(
        scope="rack",
        partitionName="replica",
        bound=GroupCountSpecBound.MAX,
        limit=Limit(type=LimitType.ABSOLUTE, globalLimit=1),
        definition=GroupCountSpecDefinition.AFTER,  # Too lenient!
    )
        )
)
```

**Solution**: Use DURING for critical diversity:

```python
# GOOD: Never have 2 replicas on same rack, even mid-migration
solver.add_constraint(
        ConstraintSpec(
            groupCountSpec=GroupCountSpec(
        definition=GroupCountSpecDefinition.DURING,
    )
        )
)
```

### 4. Forgetting Partition Definition

**Problem**: Referencing partition that wasn't added.

```python
# BAD: Partition "replica" never defined
solver.add_constraint(
        ConstraintSpec(
            groupCountSpec=GroupCountSpec(partitionName="replica", ...)
        )
)
# Error: Unknown partition
```

**Solution**: Always define partition first:

```python
solver.add_partition("replica", replica_to_objects)
solver.add_constraint(
        ConstraintSpec(
            groupCountSpec=GroupCountSpec(partitionName="replica", ...)
        ))
```

## Related Specs

- [ColocateGroupsSpec](colocate-groups) - Control group spread/colocation
- [GroupIsolationLimitSpec](group-isolation-limit) - Prevent groups from sharing scope items
- [Partitions](../../core-concepts/overview#partitions) - How to define groups

## Source Code

- Thrift definition: `interface/thrift/ProblemSpecs.thrift:716`
- Implementation: `solver/constraints/GroupCount.cpp`
