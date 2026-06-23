---
sidebar_position: 1
---

# ColocateGroups

**Move Type**: Group
**Complexity**: O(n × |S| × |G|^k) - can be very large
**Primary Use**: Colocating related groups of objects

Move **related groups of objects** from hot container to the same scope item. Ensures groups that need to be together are placed in the same region/rack/cluster.

## Overview

`ColocateGroups` (also known as `COLOCATE_GROUPS`) evaluates moving a **related set of objects** from different groups to every possible combination of containers in different scope items, ensuring all related groups end up colocated in the same scope item (e.g., same region).

**Use when**:
- Objects have affinity requirements (must be in same region/rack)
- Moving related groups together (e.g., primary + replicas)
- Need to ensure colocation of object groups
- Have partitions defining related object groups
- Know which groups must be colocated

**Avoid when**:
- Objects can move independently (use [Single](../basic/single))
- Don't have partition/group structure
- Colocation not required
- Problem too large (complexity can explode)

## Quick Example

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/colocate_groups_examples.py start=quick_example_start end=quick_example_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/colocate_groups_examples.cpp start=quick_example_start end=quick_example_end
```

</TabItem>
</Tabs>

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `partitionName` | string | **Yes** | null | Partition defining object groups |
| `relatedGroupsList` | list&lt;RelatedGroupsInfo&gt; | **Yes** | null | Sets of related groups (must be disjoint) |
| `colocationScopeName` | string | **Yes** | null | Scope for colocation (e.g., "region") |
| `colocationScopeItemToGroupToContainers` | map | No | null | Valid containers per (group, scope item) |
| `defaultSampleSize` | int | No | null | Sample size to limit move sets |

### Parameter Details

**partitionName**:
- Name of partition defining object groups
- Each object belongs to at most one group in the partition
- Example: "replica_group" partition

**relatedGroupsList**:
- List of `ColocateGroupsMoveTypeRelatedGroupsInfo` structs
- Each struct contains:
  - `relatedGroups`: Set of group names that must be colocated
  - `destinationScopeItems`: Optional specific scope items for this set
- Sets must be disjoint (no group appears in multiple sets)
- Example: `[{relatedGroups: ["primary", "replica1", "replica2"]}]`

**colocationScopeName**:
- Scope in which related groups must be colocated
- Example values: "region", "rack", "cluster"
- Each scope item becomes a potential destination

**colocationScopeItemToGroupToContainers**:
- Optional map: `scope_item → group → containers`
- Restricts valid destination containers per group per scope item
- If omitted, all containers in scope item are considered
- Example: `{"region1": {"primary": {"server1", "server2"}}}`

**defaultSampleSize**:
- Limits containers considered per (group, scope item)
- Critical for controlling complexity
- If omitted, all valid containers considered

## How It Works

Given a **hot container** (most broken):

1. **Select hot object**: Pick object from hot container
2. **Identify hot group**: Determine which group the hot object belongs to
3. **Find related groups**: Identify all groups related to the hot group
4. **Select related objects**: Pick one object from each related group in the same scope item
5. **Choose destination scope**: Pick a different scope item
6. **Select destination containers**: Pick valid containers for each object in the new scope
7. **Evaluate move set**: Test moving all objects together to the new containers
8. **Repeat**: Try all hot objects, all destination scopes, all container combinations
9. **Apply best**: Apply the move set that improves objective most

### Visual Example

```
Before colocation move:                After colocation to region2:
┌──────────────┐                      ┌──────────────┐
│ Region1      │                      │ Region1      │
│  Server1     │                      │  Server1     │
│   • primary1 ┼─┐ Hot object         │   (empty)    │
│  Server2     │ │                    │  Server2     │
│   • replica1 ┼─┼─┐ Related          │   (empty)    │
│  Server3     │ │ │                  │  Server3     │
│   • replica2 ┼─┼─┼─┐ Related        │   (empty)    │
└──────────────┘ │ │ │                └──────────────┘
                 │ │ │
┌──────────────┐ │ │ │                ┌──────────────┐
│ Region2      │ │ │ │                │ Region2      │
│  Server4     │ │ │ │                │  Server4     │
│   (empty) <──┼─┘ │ │                │   • primary1 ┼← Colocated!
│  Server5     │   │ │                │  Server5     │
│   (empty) <──┼───┘ │                │   • replica1 ┼← Colocated!
│  Server6     │     │                │  Server6     │
│   (empty) <──┼─────┘                │   • replica2 ┼← Colocated!
└──────────────┘                      └──────────────┘

All related groups move together to the same scope item
```

## Complexity

**Moves evaluated**: O(n × |S| × |G|^k)

Where:
- n = number of objects in hot container
- |S| = number of colocation scope items
- |G| = number of related groups
- k = valid containers per group per scope item

⚠️ **Warning**: This complexity can become **very large** quickly!

**Example - Replica colocation**:
- Hot container: 100 objects
- Colocation scope items (regions): 3
- Related groups: 3 (primary + 2 replicas)
- Valid containers per group: 10
- **Without sampling**: 100 × 3 × 10³ = **300,000** move sets
- **With defaultSampleSize=5**: 100 × 3 × 5³ = **37,500** move sets

## Usage Patterns

### Basic Replica Colocation

Colocate primary and replicas in same region:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/colocate_groups_examples.py start=replica_colocation_start end=replica_colocation_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/colocate_groups_examples.cpp start=replica_colocation_start end=replica_colocation_end
```

</TabItem>
</Tabs>

### With Sampling

Limit move sets with sampling:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/colocate_groups_examples.py start=sampling_start end=sampling_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/colocate_groups_examples.cpp start=sampling_start end=sampling_end
```

</TabItem>
</Tabs>

### Restricted Destinations

Restrict valid containers per group:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/colocate_groups_examples.py start=restricted_start end=restricted_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/colocate_groups_examples.cpp start=restricted_start end=restricted_end
```

</TabItem>
</Tabs>

### Multiple Related Sets

Multiple independent sets of related groups:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/colocate_groups_examples.py start=multiple_sets_start end=multiple_sets_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/colocate_groups_examples.cpp start=multiple_sets_start end=multiple_sets_end
```

</TabItem>
</Tabs>

## Performance Characteristics

### Complexity Analysis

| Related Groups | Containers/Group | Scopes | Objects | Move Sets |
|----------------|------------------|--------|---------|-----------|
| 2 | 10 | 3 | 100 | 30K |
| 3 | 10 | 3 | 100 | 300K |
| 3 | 5 (sampled) | 3 | 100 | 37.5K |
| 4 | 10 | 5 | 100 | 5M |

**Critical observations**:
- Exponential in number of related groups
- Linear in number of objects
- Linear in number of scopes
- **Sampling is essential** for reasonable performance

### When Does It Help?

ColocateGroups helps when:
- **Colocation requirements**: Objects must be in same scope item
- **Group affinity**: Primary + replicas must be nearby
- **Region/rack constraints**: Network locality requirements
- **Disaster recovery**: Groups spread across failure domains

ColocateGroups does NOT help when:
- **No colocation needs**: Objects can be anywhere
- **Too many groups**: Complexity explodes
- **Independent objects**: No group relationships

## Comparison with Alternatives

| Move Type | Colocation | Complexity | Use Case |
|-----------|------------|------------|----------|
| [Single](../basic/single) | No | O(N × C) | Independent objects |
| [GroupRouting](group-routing) | Partial | Varies | Group-aware routing |
| **ColocateGroups** | Yes (strict) | O(n×S×G^k) | Related groups together |

## Troubleshooting

### Problem: Too slow / too many move sets

**Diagnosis**: Complexity explosion from many groups or containers

**Solutions**:
- **Critical**: Set `defaultSampleSize` (start with 5-10)
- Reduce number of related groups if possible
- Restrict valid containers with `colocationScopeItemToGroupToContainers`
- Use fewer destination scope items
- Consider if all groups really need colocation

### Problem: No improving moves found

**Diagnosis**: Cannot find beneficial colocation

**Solutions**:
- Check partition and group definitions are correct
- Verify `relatedGroupsList` specifies correct groups
- Check capacity constraints on destination containers
- May already be optimally colocated
- Review objective function

### Problem: Groups not moving together

**Diagnosis**: Related groups configuration issue

**Solutions**:
- Verify `relatedGroupsList` includes all groups that should move together
- Check partition assigns objects to correct groups
- Ensure related group sets are disjoint
- Review scope item configuration

### Problem: Memory issues

**Diagnosis**: Too many move sets generated

**Solutions**:
- Set aggressive `defaultSampleSize` (e.g., 2-3)
- Reduce related group sets
- Limit destination scope items
- May need to break into smaller problems

## When to Use ColocateGroups

**DO use when**:
- Objects have strict colocation requirements
- Moving related groups together (primary + replicas)
- Need to ensure groups in same region/rack
- Have well-defined partition structure
- Willing to use sampling to control complexity

**DO NOT use when**:
- Objects can move independently
- No partition/group structure
- Colocation not required
- Problem scale is too large (complexity explosion)

## Related Move Types

**Group-based alternatives**:
- [GroupRouting](group-routing) - Group-aware routing
- [GroupMoveWithHintStrategies](group-move-with-hint-strategies) - Group moves with hints
- [GreedyGroupToScopeItem](greedy-group-to-scope-item) - Greedy group placement

**General alternatives**:
- [Single](../basic/single) - For independent objects
- [SingleChain](../chain/single-chain) - For 2-object chains

## Source Code

- Thrift definition: [`interface/thrift/SolverSpecs.thrift:721`](https://github.com/facebookincubator/rebalancer/blob/main/interface/thrift/SolverSpecs.thrift#L721)
- Implementation: [`solver/moves/ColocateGroupsMoveType.h`](https://github.com/facebookincubator/rebalancer/blob/main/solver/moves/ColocateGroupsMoveType.h)
- Tests: [`solver/moves/tests/`](https://github.com/facebookincubator/rebalancer/tree/main/solver/moves/tests/)

## Next Steps

- Learn about [GroupMoveWithHintStrategies](group-move-with-hint-strategies) for hint-based group moves
- Try [GroupRouting](group-routing) for group-aware routing
- Review [Move Types Overview](../) for choosing move types
- See core concepts on [Groups](../../core-concepts/overview#partitions) for partition setup
