---
sidebar_position: 16
---

# GroupMoveLimitSpec

**Type**: Constraint only

Limit the number of objects that can move within each partition group.

## Overview

`GroupMoveLimitSpec` constrains how many objects from each group (as defined by a partition) can be moved during rebalancing. This is useful for limiting disruption within logical groups (e.g., don't move more than 5 shards per database, or max 10 VMs per service).

**Use this when**: You want to throttle moves on a per-group basis to limit risk or disruption to each service/database/application.

**Note**: Currently only supports `container` scope.

## Quick Example

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs groupId="programming-language">
<TabItem value="python" label="Python" default>

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=quick_example_start end=quick_example_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=quick_example_start end=quick_example_end
```

</TabItem>
</Tabs>

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | Yes | - | Unique identifier for this constraint |
| `partitionName` | string | Yes | - | Partition defining groups |
| `limit` | Limit | No | `{type: ABSOLUTE}` | Move limit per group |
| `sourceScopeItemsAffectingLimitFilter` | Filter | No | all | Only count moves FROM these containers |
| `destinationScopeItemsAffectingLimitFilter` | Filter | No | all | Only count moves TO these containers |
| `dimension` | string | No | `{object}_count` | Dimension to sum for move counting |

### Limit Structure

See [Limit documentation](../../core-concepts/overview#partitions) for details. Common usage:

| Limit Type | Example | Meaning |
|------------|---------|---------|
| ABSOLUTE | `globalLimit=5` | Max 5 objects move per group |
| ABSOLUTE (per-group) | `groupLimits={"service_a": 10}` | Different limit per group |

## How It Works

For each group in the partition:
1. Count objects that moved (between initial and final assignment)
2. A move counts if it's between a source container (matching filter) and destination container (matching filter)
3. Sum the dimension values for moved objects
4. Constraint: sum ≤ limit

**Default**: If no dimension specified, uses count (each object = 1.0).

## Common Usage Patterns

### 1. Uniform Move Limit Per Service

Limit all services to same max moves:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=uniform_move_limit_start end=uniform_move_limit_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=uniform_move_limit_start end=uniform_move_limit_end
```

</TabItem>
</Tabs>

**Result**: Frontend can move max 3 instances, API max 3, DB max 3.

### 2. Per-Service Custom Limits

Different limits for different services:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=per_service_custom_limits_start end=per_service_custom_limits_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=per_service_custom_limits_start end=per_service_custom_limits_end
```

</TabItem>
</Tabs>

### 3. Weighted Move Limit by Size

Limit based on data volume moved, not object count:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=weighted_move_limit_by_size_start end=weighted_move_limit_by_size_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=weighted_move_limit_by_size_start end=weighted_move_limit_by_size_end
```

</TabItem>
</Tabs>

**Result**: Can move 10 small shards (100GB each) or 1 large shard (1TB), but not more.

### 4. Directional Move Limits

Only count moves FROM certain hosts (e.g., draining):

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=directional_move_limits_start end=directional_move_limits_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=directional_move_limits_start end=directional_move_limits_end
```

</TabItem>
</Tabs>

**Result**: Moves between other hosts don't count toward limit, only moves FROM draining hosts.

### 5. Multi-Round Gradual Rebalancing

Increase limit each round:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=multi_round_gradual_rebalancing_start end=multi_round_gradual_rebalancing_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=multi_round_gradual_rebalancing_start end=multi_round_gradual_rebalancing_end
```

</TabItem>
</Tabs>

### 6. Replica Group Move Limits

Limit moves per replica group for safety:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=replica_group_move_limits_start end=replica_group_move_limits_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=replica_group_move_limits_start end=replica_group_move_limits_end
```

</TabItem>
</Tabs>

### 7. Combined Source/Destination Filters

Count moves only between specific hosts:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=combined_source_destination_filters_start end=combined_source_destination_filters_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=combined_source_destination_filters_start end=combined_source_destination_filters_end
```

</TabItem>
</Tabs>

**Result**: Moves within same DC don't count, only cross-DC migrations.

## Performance Considerations

- **Impact**: Minimal - simple move counting
- **Solver compatibility**: Works with both Local Search and Optimal solvers
- **Scaling**: O(number of groups × number of moves)
- **Current limitation**: Only supports `container` scope

## Common Pitfalls

### 1. Limit Too Restrictive

**Problem**: Limit so low that rebalancing can't make progress.

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=pitfall_restrictive_bad_start end=pitfall_restrictive_bad_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=pitfall_restrictive_bad_start end=pitfall_restrictive_bad_end
```

</TabItem>
</Tabs>

**Solution**: Set limit based on problem size and desired convergence rate:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=pitfall_restrictive_good_start end=pitfall_restrictive_good_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=pitfall_restrictive_good_start end=pitfall_restrictive_good_end
```

</TabItem>
</Tabs>

### 2. Forgetting Partition

**Problem**: Partition not defined.

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=pitfall_forgot_partition_bad_start end=pitfall_forgot_partition_bad_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=pitfall_forgot_partition_bad_start end=pitfall_forgot_partition_bad_end
```

</TabItem>
</Tabs>

**Solution**: Always define partition first:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=pitfall_forgot_partition_good_start end=pitfall_forgot_partition_good_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=pitfall_forgot_partition_good_start end=pitfall_forgot_partition_good_end
```

</TabItem>
</Tabs>

### 3. Wrong Dimension

**Problem**: Dimension doesn't exist on objects.

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=pitfall_wrong_dimension_bad_start end=pitfall_wrong_dimension_bad_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=pitfall_wrong_dimension_bad_start end=pitfall_wrong_dimension_bad_end
```

</TabItem>
</Tabs>

**Solution**: Use existing dimension or omit (defaults to count):

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=pitfall_wrong_dimension_good_start end=pitfall_wrong_dimension_good_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=pitfall_wrong_dimension_good_start end=pitfall_wrong_dimension_good_end
```

</TabItem>
</Tabs>

### 4. Scope Limitation Not Understood

**Problem**: Trying to use non-container scope.

**Note**: This is a current limitation of the spec. It only tracks moves at container level and cannot use rack scope, datacenter scope, etc.

## Combining with Other Specs

### With MinimizeMovementSpec

Soft limit (goal) + hard limit (constraint):

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=combining_minimize_movement_start end=combining_minimize_movement_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=combining_minimize_movement_start end=combining_minimize_movement_end
```

</TabItem>
</Tabs>

### With AvoidMovingSpec

Pin + limit moves for others:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=combining_avoid_moving_start end=combining_avoid_moving_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=combining_avoid_moving_start end=combining_avoid_moving_end
```

</TabItem>
</Tabs>

## Verification Example

Verify move limits respected:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=verification_start end=verification_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=verification_start end=verification_end
```

</TabItem>
</Tabs>

## Move Count Analysis

Analyze moves per group:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.py start=move_count_analysis_start end=move_count_analysis_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/groups/group_move_limit_spec_examples.cpp start=move_count_analysis_start end=move_count_analysis_end
```

</TabItem>
</Tabs>

## Related Specs

- [MoveGroupSpec](move-group) - Move entire groups together
- [MinimizeMovementSpec](../movement/minimize-movement) - Soft movement limit
- [AvoidMovingSpec](../movement/avoid-moving) - Pin specific objects

## Source Code

- Thrift definition: `interface/thrift/ProblemSpecs.thrift:748` (GroupMoveLimitSpec)
- Implementation: `solver/constraints/GroupMoveLimit.cpp`
