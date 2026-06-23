---
sidebar_position: 13
---

# ColocateGroupsSpec

**Type**: Goal or Constraint

Control how many scope items each group spans (colocation vs. spread).

## Overview

`ColocateGroupsSpec` controls whether groups should be colocated on few scope items (tight grouping) or spread across many scope items (high diversity). Use MAX bound to enforce colocation, MIN bound to enforce spreading.

**Use this when**: You want to colocate related objects together, or ensure groups spread across sufficient scope items for fault tolerance.

## Quick Example

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs groupId="programming-language">
<TabItem value="python" label="Python" default>

```python file=algopt/rebalancer/examples/website/reference/groups/colocate_groups_spec_examples.py start=quick_example_start end=quick_example_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/colocate_groups_spec_examples.cpp start=quick_example_start end=quick_example_end
```

</TabItem>
</Tabs>

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | Yes | - | Unique identifier for this spec |
| `scope` | string | Yes | - | Scope items to count across (e.g., `"host"`, `"rack"`) |
| `partitionName` | string | Yes | - | Partition defining groups |
| `bound` | ColocateGroupsSpecBound | No | MAX | MAX (colocate) or MIN (spread) |
| `limits` | Limit | No | ABSOLUTE(1) | Maximum/minimum scope items per group |
| `scopeItemWeights` | map&lt;string, double&gt; | No | {} | Custom weights per scope item (default 1.0) |
| `filter` | Filter | No | null | Apply only to filtered objects |
| `dimension` | string | No | null | Use dimension for counting instead of presence |
| `squares` | bool | No | false | Apply squared penalty (goal only) |

## Bound Types

| Bound | Behavior | Use Case |
|-------|----------|----------|
| **MAX** | Each group on **at most** N scope items | Colocation - keep group together |
| **MIN** | Each group on **at least** N scope items | Spreading - ensure diversity |

## Common Usage Patterns

### 1. Colocate Related Objects (MAX)

Keep tenant's objects together on few hosts:

```python
# Define tenant partition
solver.add_partition("tenant", {
    "tenant_a": ["obj_1", "obj_2", "obj_3", "obj_4"],
    "tenant_b": ["obj_5", "obj_6", "obj_7"],
})

# Each tenant on at most 2 hosts
solver.add_constraint(
        ConstraintSpec(
            colocateGroupsSpec=ColocateGroupsSpec(
        name="colocate-tenants",
        scope="host",
        partitionName="tenant",
        bound=ColocateGroupsSpecBound.MAX,
        limits=Limit(type=LimitType.ABSOLUTE, globalLimit=2),
    )
        )
)
```

### 2. Spread for Fault Tolerance (MIN)

Ensure groups spread across sufficient racks:

```python
# Each replica group on at least 3 racks
solver.add_constraint(
        ConstraintSpec(
            colocateGroupsSpec=ColocateGroupsSpec(
        name="spread-replicas",
        scope="rack",
        partitionName="replica",
        bound=ColocateGroupsSpecBound.MIN,
        limits=Limit(type=LimitType.ABSOLUTE, globalLimit=3),
    )
        )
)
```

### 3. Prefer Colocation (Goal, not Constraint)

Soft preference for colocation:

```python
# Try to colocate, but don't fail if can't
solver.add_goal(
        GoalSpec(
            colocateGroupsSpec=ColocateGroupsSpec(
        name="prefer-colocation",
        scope="host",
        partitionName="tenant",
        bound=ColocateGroupsSpecBound.MAX,
        limits=Limit(type=LimitType.ABSOLUTE, globalLimit=1),
        squares=True,  # Stronger penalty for spread
    )
        ),
    weight=0.5
)
```

### 4. Weighted Scope Items

Prefer colocation on certain scope items:

```python
# Prefer spreading to high-capacity hosts (lower weight = better for colocation)
solver.add_constraint(
        ConstraintSpec(
            colocateGroupsSpec=ColocateGroupsSpec(
        name="weighted-colocation",
        scope="host",
        partitionName="tenant",
        bound=ColocateGroupsSpecBound.MAX,
        limits=Limit(type=LimitType.ABSOLUTE, globalLimit=5),
        scopeItemWeights={
            "high_capacity_host_1": 0.5,  # Prefer these
            "high_capacity_host_2": 0.5,
            "low_capacity_host_3": 1.0,   # Default
        }
    )
        )
)
```

### 5. Relative to Group Size

Limit spread proportionally:

```python
# Large groups can spread more, small groups must colocate
solver.add_constraint(
        ConstraintSpec(
            colocateGroupsSpec=ColocateGroupsSpec(
        name="proportional-spread",
        scope="host",
        partitionName="shard",
        bound=ColocateGroupsSpecBound.MAX,
        limits=Limit(
            type=LimitType.RELATIVE_TO_GROUP,
            relativeLimitPercentage=0.3,  # Max 30% of hosts
        ),
    )
        )
)
```

### 6. Dimension-Based Counting

Count using dimension instead of binary presence:

```python
# Total data size per group on max 3 hosts
solver.add_constraint(
        ConstraintSpec(
            colocateGroupsSpec=ColocateGroupsSpec(
        name="data-size-colocation",
        scope="host",
        partitionName="tenant",
        bound=ColocateGroupsSpecBound.MAX,
        limits=Limit(type=LimitType.ABSOLUTE, globalLimit=3),
        dimension="data_size",
    )
        )
)
```

## How It Works

For each group in the partition:

**MAX bound**: Count how many scope items the group appears on, penalize if > limit
```
penalty = max(0, count_scope_items_with_group - limit)
```

**MIN bound**: Penalize if group appears on < limit scope items
```
penalty = max(0, limit - count_scope_items_with_group)
```

With `scopeItemWeights`:
```
weighted_count = sum(weight for each scope_item where group_exists)
```

## Real-World Examples

### Microservices Colocation

Colocate service instances of the same microservice:

```python
# Define service partition
solver.add_partition("service", {
    "auth_service": ["auth_1", "auth_2", "auth_3"],
    "api_service": ["api_1", "api_2", "api_3", "api_4"],
})

# Each service on at most 2 hosts (for locality)
solver.add_constraint(
        ConstraintSpec(
            colocateGroupsSpec=ColocateGroupsSpec(
        name="service-locality",
        scope="host",
        partitionName="service",
        bound=ColocateGroupsSpecBound.MAX,
        limits=Limit(type=LimitType.ABSOLUTE, globalLimit=2),
    )
        )
)
```

### Database Shard Spreading

Ensure database shards spread for availability:

```python
# Each shard's replicas on at least 3 datacenters
solver.add_partition("shard", shard_to_replicas)

solver.add_constraint(
        ConstraintSpec(
            colocateGroupsSpec=ColocateGroupsSpec(
        name="shard-dc-diversity",
        scope="datacenter",
        partitionName="shard",
        bound=ColocateGroupsSpecBound.MIN,
        limits=Limit(type=LimitType.ABSOLUTE, globalLimit=3),
    )
        )
)
```

## Performance Considerations

- **Impact**: Moderate - requires group membership tracking
- **Solver compatibility**: Works with both Local Search and Optimal solvers
- **Scaling**: O(number of groups × number of scope items)
- **Complexity**: More complex than simple counting

## Common Pitfalls

### 1. Infeasible Colocation Limit

**Problem**: Not enough scope items to satisfy MIN bound.

```python
# BAD: Only 2 racks, but require min 3
solver.add_constraint(
        ConstraintSpec(
            colocateGroupsSpec=ColocateGroupsSpec(
        scope="rack",  # Only 2 racks exist
        partitionName="replica",
        bound=ColocateGroupsSpecBound.MIN,
        limits=Limit(type=LimitType.ABSOLUTE, globalLimit=3),  # Impossible!
    )
        )
)
```

**Solution**: Verify sufficient scope items exist.

### 2. Conflicting with GroupCount

**Problem**: ColocateGroups MAX + GroupCount MAX can conflict.

```python
# Can't both colocate AND limit per scope item
solver.add_constraint(
        ConstraintSpec(
            colocateGroupsSpec=ColocateGroupsSpec(
        scope="host",
        partitionName="replica",
        bound=ColocateGroupsSpecBound.MAX,
        limits=Limit(type=LimitType.ABSOLUTE, globalLimit=1),  # All on 1 host
    )
        )
)
solver.add_constraint(
        ConstraintSpec(
            groupCountSpec=GroupCountSpec(
        scope="host",
        partitionName="replica",
        bound=GroupCountSpecBound.MAX,
        limit=Limit(type=LimitType.ABSOLUTE, globalLimit=1),  # Only 1 per host
    )
        )
)
# Contradiction!
```

**Solution**: Ensure specs are compatible.

### 3. Wrong Bound Direction

**Problem**: Using MAX when you want spreading (or vice versa).

```python
# BAD: Using MAX but want diversity
solver.add_constraint(
        ConstraintSpec(
            colocateGroupsSpec=ColocateGroupsSpec(
        scope="rack",
        partitionName="replica",
        bound=ColocateGroupsSpecBound.MAX,  # Wrong! Allows colocating
        limits=Limit(type=LimitType.ABSOLUTE, globalLimit=1),
    )
        )
)
```

**Solution**: Use MIN for spreading:

```python
# GOOD: MIN enforces spreading
solver.add_constraint(
        ConstraintSpec(
            colocateGroupsSpec=ColocateGroupsSpec(
        bound=ColocateGroupsSpecBound.MIN,
        limits=Limit(type=LimitType.ABSOLUTE, globalLimit=3),
    )
        )
)
```

### 4. Forgetting Partition

**Problem**: Referencing undefined partition.

```python
# BAD: Partition never defined
solver.add_constraint(
        ConstraintSpec(
            colocateGroupsSpec=ColocateGroupsSpec(partitionName="replica", ...)
        )
)
```

**Solution**: Define partition first:

```python
solver.add_partition("replica", replica_to_objects)
solver.add_constraint(
        ConstraintSpec(
            colocateGroupsSpec=ColocateGroupsSpec(partitionName="replica", ...)
        ))
```

## Comparison with GroupCountSpec

| Aspect | ColocateGroupsSpec | GroupCountSpec |
|--------|-------------------|----------------|
| **Perspective** | Per group | Per scope item |
| **Counts** | Scope items per group | Groups per scope item |
| **Colocation (MAX)** | Group on ≤ N scope items | Scope item has ≤ N groups |
| **Spreading (MIN)** | Group on ≥ N scope items | Scope item has ≥ N groups |

**Both can be needed**: Use ColocateGroups to control group spread, GroupCount to control per-scope-item load.

## Related Specs

- [GroupCountSpec](group-count) - Control groups per scope item (complementary)
- [GroupIsolationLimitSpec](group-isolation-limit) - Prevent groups from sharing scope items
- [Partitions](../../core-concepts/overview#partitions) - How to define groups

## Source Code

- Thrift definition: `interface/thrift/ProblemSpecs.thrift:404`
- Implementation: `solver/constraints/ColocateGroups.cpp`
