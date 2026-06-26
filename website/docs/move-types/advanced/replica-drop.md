---
sidebar_position: 2
---

# ReplicaDrop

**Move Type**: Advanced
**Complexity**: O(G × R) where G = groups, R = replicas per group
**Primary Use**: Intelligent replica reduction when over-replicated

Intelligently select which replicas to drop when reducing replication levels. Compares all replicas in a group and chooses the best ones to remove.

## Overview

`ReplicaDrop` (also known as `REPLICA_DROP`) is a specialized move type for scenarios where you have **more replicas than needed** and must decide which ones to drop. Instead of randomly removing replicas, it:

1. Identifies all replicas in the same group (e.g., all tasks in a job)
2. Evaluates moving **each replica** out of the specified scope
3. Compares the objective for each option
4. Selects the **best replica to drop** based on objectives

This ensures that when reducing replication, you keep the best replicas and drop the worst ones.

**Use when**:
- Over-replicated shards/jobs need reduction
- Must intelligently choose which replicas to drop
- Have objectives that distinguish replica quality (lag, load, etc.)
- Downsizing while maintaining quality

**Avoid when**:
- No over-replication
- All replicas are equivalent
- Don't have quality metrics to compare
- Need to add replicas (not remove them)

## Quick Example

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/replica_drop_examples.py start=quick_example_start end=quick_example_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/replica_drop_examples.cpp start=quick_example_start end=quick_example_end
```

</TabItem>
</Tabs>

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `replicaDropPartition` | string | **Yes** | null | Partition defining replica groups |
| `replicaDropScope` | string | **Yes** | null | Scope to move replicas OUT OF |

### Parameter Details

**replicaDropPartition**:
- The partition name defining replica groups
- Example: "job" partition groups tasks by job
- Example: "shard" partition groups replicas by shard
- Each group represents a set of replicas to compare

**replicaDropScope**:
- The scope from which replicas should be moved OUT
- Example: "assigned" scope (move to outside, e.g., "unassigned")
- Replicas are moved from inside this scope to containers outside it
- Typically used with special unassigned/dropped container

## How It Works

For each **hot container** in the scope:

1. **Identify objects**: Find objects in hot container within the drop scope
2. **Group by partition**: Determine which replica group each object belongs to
3. **For each group with object in hot container**:
   - **Evaluate all replicas**: Try moving each replica in the group out of scope
   - **Compare objectives**: Calculate objective change for each replica drop
   - **Select best**: Choose replica whose removal improves objective most
4. **Return best move**: Return the single best replica drop found

### Visual Example

```
Job "job0" has 4 tasks (over-replicated, need only 2):
  - task0: lag = 0.00 (good)
  - task1: lag = 0.01 (okay)
  - task2: lag = 0.03 (bad)
  - task3: lag = 0.02 (not great)

Goal: Minimize total lag, keep exactly 2 tasks assigned

ReplicaDrop evaluation:
  Option 1: Drop task0 → Remaining lag: 0.01 + 0.03 + 0.02 = 0.06
  Option 2: Drop task1 → Remaining lag: 0.00 + 0.03 + 0.02 = 0.05
  Option 3: Drop task2 → Remaining lag: 0.00 + 0.01 + 0.02 = 0.03 ✓ Best!
  Option 4: Drop task3 → Remaining lag: 0.00 + 0.01 + 0.03 = 0.04

Choose to drop task2 (highest lag)

After 2 drops:
  Keeps: task0 (lag 0.00), task1 (lag 0.01)
  Dropped: task2 (lag 0.03), task3 (lag 0.02)
```

## Complexity

**Per iteration**: O(G × R)

Where:
- G = number of groups with objects in hot container
- R = average replicas per group

**Example calculation**:
- Groups in hot container: 10
- Replicas per group: 4
- **Evaluations**: 10 × 4 = **40**

**Typical scenario**:
- 100 shards (groups)
- 5 replicas per shard
- Want to reduce to 3 replicas per shard
- Iterations needed: 2 drops per shard × 100 shards = 200 iterations
- Evaluations per iteration: ~5 (compare all replicas)
- **Total**: ~1,000 evaluations

## Usage Patterns

### Job Downsizing

Reduce job task count while minimizing lag:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/replica_drop_examples.py start=job_downsizing_start end=job_downsizing_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/replica_drop_examples.cpp start=job_downsizing_start end=job_downsizing_end
```

</TabItem>
</Tabs>

### Shard Replica Reduction

Reduce shard replication from 5x to 3x:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/replica_drop_examples.py start=shard_reduction_start end=shard_reduction_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/replica_drop_examples.cpp start=shard_reduction_start end=shard_reduction_end
```

</TabItem>
</Tabs>

### Quality-Based Selection

Drop replicas based on quality metrics:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/replica_drop_examples.py start=quality_based_start end=quality_based_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/replica_drop_examples.cpp start=quality_based_start end=quality_based_end
```

</TabItem>
</Tabs>

## Performance Characteristics

### When Does It Help?

ReplicaDrop helps when:
- **Over-replication**: More replicas than needed
- **Intelligent selection**: Quality/metrics distinguish replicas
- **Downsizing**: Reducing replication levels
- **Cost optimization**: Removing least valuable replicas
- **Resource constraints**: Must reduce replica count

ReplicaDrop does NOT help when:
- **Not over-replicated**: Have correct number of replicas
- **All replicas equivalent**: No basis to choose which to drop
- **Adding replicas**: Need to increase replication (not decrease)
- **No quality metrics**: Can't distinguish good from bad replicas
- **Random drops acceptable**: Don't need intelligent selection

### Comparison with Manual Approaches

| Approach | Selection | Quality | Use Case |
|----------|-----------|---------|----------|
| Random drop | Random | Variable | No quality metric |
| Round-robin drop | Deterministic | Variable | Equal replicas |
| **ReplicaDrop** | **Intelligent** | **Optimal** | **Quality-aware downsizing** |

## Comparison with Alternatives

| Move Type | Purpose | Replica Awareness | Use Case |
|-----------|---------|-------------------|----------|
| [Single](../basic/single) | General moves | No | Standard optimization |
| [FixedSource](../fixed/fixed-source) | Move from specific | No | Targeted moves |
| **ReplicaDrop** | **Reduce replicas** | **Yes** | **Intelligent downsizing** |

## Troubleshooting

### Problem: No moves found

**Diagnosis**: May not have over-replication or all replicas outside scope

**Solutions**:
- Check replica counts per group
- Verify replicas are in the drop scope
- Ensure some replicas can be moved out
- Review constraints blocking moves

### Problem: Wrong replicas being dropped

**Diagnosis**: Objectives not reflecting replica quality

**Solutions**:
- Add dimension capturing replica quality (lag, staleness, etc.)
- Add goal minimizing that dimension
- Check goal weights and priorities
- Verify dimension values are correct

### Problem: Drops too slow

**Diagnosis**: Many groups or large groups

**Solutions**:
- Check number of groups and replicas per group
- May need to batch drops
- Consider if all groups need reduction simultaneously
- Review time limits

### Problem: Constraint violations

**Diagnosis**: Dropping replicas violates constraints

**Solutions**:
- Check GroupCount constraints are correct
- Verify capacity constraints allow reduction
- Review anti-affinity or colocation constraints
- May need to adjust constraints for downsizing

## When to Use ReplicaDrop

**DO use when**:
- Reducing replication levels
- Have quality metrics for replicas
- Need intelligent replica selection
- Over-replicated and must downsize
- Want to keep best replicas

**DO NOT use when**:
- Not over-replicated
- All replicas are equivalent
- No quality metrics available
- Increasing replication (not decreasing)
- Random selection is acceptable

## Related Move Types

**Complementary move types**:
- [Single](../basic/single) - General optimization after downsizing
- [FixedSource](../fixed/fixed-source) - Targeted moves from specific sources

**Related concepts**:
- GroupCount constraint - Control replica counts
- Scope-based placement - Define where replicas can go

## Source Code

- Thrift definition: [`interface/thrift/SolverSpecs.thrift:678`](https://github.com/facebookincubator/rebalancer/blob/main/interface/thrift/SolverSpecs.thrift#L678)
- Implementation: [`solver/moves/ReplicaDropMoveType.h`](https://github.com/facebookincubator/rebalancer/blob/main/solver/moves/ReplicaDropMoveType.h)
- Tests: [`interface/tests/ReplicaDropTest.cpp`](https://github.com/facebookincubator/rebalancer/blob/main/interface/tests/ReplicaDropTest.cpp)

## Next Steps

- Learn about `GroupCount` constraint for controlling replica counts
- Try [Single](../basic/single) for general optimization
- Review [Move Types Overview](../) for choosing move types

## Notes

⚠️ **Requires Quality Metrics**: ReplicaDrop only helps if you have objectives/dimensions that distinguish replica quality. Without quality metrics, all replicas look the same and selection is arbitrary.

💡 **Typical Pattern**: Use with GroupCount constraint specifying target replica counts, and goals minimizing quality dimensions (lag, staleness, load, etc.).
