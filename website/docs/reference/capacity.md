---
sidebar_position: 3
---

# CapacitySpec

**Type**: Both Goal and Constraint

Limit the utilization of a dimension within scope items to not exceed (or fall below) a specified capacity.

## Overview

`CapacitySpec` is one of the most fundamental specs in Rebalancer. It ensures resource utilization doesn't exceed available capacity.

**Use as constraint when**: Capacity must not be exceeded (hard requirement)
**Use as goal when**: Prefer to respect capacity but can violate if necessary (soft objective)

## Quick Example

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=quick_example_start end=quick_example_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=quick_example_start end=quick_example_end
```

</TabItem>
</Tabs>

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | Yes | - | Descriptive name for logging/debugging |
| `scope` | string | Yes | - | Scope to apply capacity limit (e.g., `"host"`, `"rack"`) |
| `dimension` | string | Yes | - | Dimension to constrain (utilization) |
| `limit` | Limit | Yes | - | Capacity limit specification |
| `definition` | CapacitySpecDefinition | No | AFTER | When to measure utilization |
| `bound` | CapacitySpecBound | No | MAX | Maximum or minimum bound |
| `filter` | Filter | No | null | Apply only to filtered objects |
| `zeroAllowed` | bool | No | false | Whether zero utilization is allowed |
| `useLegacyFormula` | bool | No | false | Use legacy normalization |
| `utilizationBound` | UtilizationBound | No | null | Per-group utilization bounds |

## Limit Specification

The `limit` parameter controls how capacity is specified:

### Global Limit

Same limit for all scope items:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=global_limit_start end=global_limit_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=global_limit_start end=global_limit_end
```

</TabItem>
</Tabs>

### Per-Scope-Item Limits

Different limits for different scope items:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=per_scope_item_limits_start end=per_scope_item_limits_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=per_scope_item_limits_start end=per_scope_item_limits_end
```

</TabItem>
</Tabs>

### Relative Limits

Limit as a fraction of a capacity dimension:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=relative_limit_example_start end=relative_limit_example_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=relative_limit_example_start end=relative_limit_example_end
```

</TabItem>
</Tabs>

With relative limits, Rebalancer looks for a matching capacity dimension (e.g., `memory_capacity` for `memory` utilization).

### Limit Types

| LimitType | Meaning | Example |
|-----------|---------|---------|
| `ABSOLUTE` | Absolute values (default) | 64.0 GB |
| `RELATIVE` | Fraction of capacity dimension | 0.8 = 80% of capacity |

## Definition Options

The `definition` parameter controls when utilization is measured:

| Definition | Measures | Use Case |
|------------|----------|----------|
| `AFTER` | Utilization after moves complete (default) | Standard capacity constraint |
| `DURING` | Peak utilization during moves | Ensure capacity during migration |
| `DURING_AND_AFTER` | Both during and after | Conservative |
| `DOUBLE_DURING` | Double-count objects being moved | Very conservative |
| `DOUBLE_DURING_AND_AFTER` | Double-count, both phases | Extremely conservative |
| `NEW` | Only newly placed/moved objects | Focus on changes |
| `OLD` | Only objects that haven't moved | Ignore migration |
| `MOVED_DATA` | Only data being moved | Migration bandwidth |

**Recommendation**: Use `AFTER` (default) for most cases. Use `DURING_AND_AFTER` if you need to ensure capacity during migration.

## Bound Options

The `bound` parameter controls whether this is a maximum or minimum:

| Bound | Meaning | Use Case |
|-------|---------|----------|
| `MAX` | Utilization ≤ limit (default) | Don't exceed capacity |
| `MIN` | Utilization ≥ limit | Ensure minimum utilization |

### Example: Minimum Utilization

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=minimum_utilization_start end=minimum_utilization_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=minimum_utilization_start end=minimum_utilization_end
```

</TabItem>
</Tabs>

## Automatic Capacity Matching

When using `LimitType.RELATIVE`, Rebalancer automatically looks for a matching capacity dimension:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=automatic_capacity_matching_start end=automatic_capacity_matching_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=automatic_capacity_matching_start end=automatic_capacity_matching_end
```

</TabItem>
</Tabs>

Rebalancer looks for `{dimension}_capacity` or just uses container dimension with same name.

## Common Usage Patterns

### Basic Capacity Constraint

Don't exceed host memory:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=basic_capacity_constraint_start end=basic_capacity_constraint_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=basic_capacity_constraint_start end=basic_capacity_constraint_end
```

</TabItem>
</Tabs>

If no `limit` is provided, Rebalancer uses the container dimension as the limit.

### Multi-Resource Capacity

Constrain both CPU and memory:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=multi_resource_capacity_start end=multi_resource_capacity_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=multi_resource_capacity_start end=multi_resource_capacity_end
```

</TabItem>
</Tabs>

Both must be satisfied.

### Hierarchical Capacity

Apply capacity at multiple levels:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=hierarchical_capacity_start end=hierarchical_capacity_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=hierarchical_capacity_start end=hierarchical_capacity_end
```

</TabItem>
</Tabs>

### Soft Capacity (Oversubscription)

Allow temporary capacity violations:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=soft_capacity_oversubscription_start end=soft_capacity_oversubscription_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=soft_capacity_oversubscription_start end=soft_capacity_oversubscription_end
```

</TabItem>
</Tabs>

### Reserve Capacity

Keep some capacity free:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=reserve_capacity_start end=reserve_capacity_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=reserve_capacity_start end=reserve_capacity_end
```

</TabItem>
</Tabs>

### Disaster Recovery Capacity

Ensure capacity remains after failure:

```python
# Use DisasterRecoveryCapacitySpec for DR scenarios
# See: DisasterRecoveryCapacitySpec documentation
```

For disaster recovery scenarios, see [DisasterRecoveryCapacitySpec](disaster-recovery-capacity) which provides specialized DR capacity enforcement.

## Advanced: Group Utilization Bounds

Limit utilization contribution from specific groups:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=group_utilization_bounds_start end=group_utilization_bounds_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=group_utilization_bounds_start end=group_utilization_bounds_end
```

</TabItem>
</Tabs>

**Use case**: Multi-tenant fair sharing, noisy neighbor prevention.

## Combining with Other Specs

### Capacity + Balance

Common pattern: respect capacity while balancing load:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=combining_capacity_balance_start end=combining_capacity_balance_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=combining_capacity_balance_start end=combining_capacity_balance_end
```

</TabItem>
</Tabs>

### Capacity + Minimize Movement

Respect capacity with minimal disruption:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=combining_capacity_minimize_movement_start end=combining_capacity_minimize_movement_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=combining_capacity_minimize_movement_start end=combining_capacity_minimize_movement_end
```

</TabItem>
</Tabs>

## Performance Considerations

- **Complexity**: O(number of scope items × number of objects)
- **Scales well** to 10,000+ scope items
- **Relative limits** slightly more expensive than absolute limits (requires capacity lookup)
- **Group utilization bounds** add overhead proportional to number of groups

## Troubleshooting

### Problem: Capacity exceeded in solution

**Possible causes**:
1. Used as goal instead of constraint
2. Initial assignment violates capacity (treated as broken constraint)
3. Other constraints make it impossible to satisfy

**Solutions**:
- Use `addConstraint()` not `addGoal()` for hard limits
- Check initial assignment capacity usage
- Review other constraints for conflicts
- Check solver output for feasibility warnings

### Problem: Can't find capacity dimension

**Error**: "Cannot find capacity dimension 'cpu_capacity'"

**Cause**: Container dimension not defined or name doesn't match

**Solution**: Ensure container dimension exists:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=troubleshooting_capacity_dimension_start end=troubleshooting_capacity_dimension_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=troubleshooting_capacity_dimension_start end=troubleshooting_capacity_dimension_end
```

</TabItem>
</Tabs>

### Problem: Relative limit not working

**Cause**: Missing capacity dimension for relative computation

**Solution**: When using `LimitType.RELATIVE`, provide matching capacity dimension:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=troubleshooting_relative_limit_start end=troubleshooting_relative_limit_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=troubleshooting_relative_limit_start end=troubleshooting_relative_limit_end
```

</TabItem>
</Tabs>

### Problem: Zero utilization not allowed

**Error**: "Zero utilization not allowed for scope item 'host0'"

**Cause**: `zeroAllowed=false` and solver trying to empty a container

**Solution**: Set `zeroAllowed=true` if empty containers are acceptable:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.py start=troubleshooting_zero_utilization_start end=troubleshooting_zero_utilization_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/capacity/capacity_spec_examples.cpp start=troubleshooting_zero_utilization_start end=troubleshooting_zero_utilization_end
```

</TabItem>
</Tabs>

## Related Specs

- [BalanceSpec](balance-optimize/balance) - Balance utilization across scope items
- [ToFreeSpec](to-free) - Free specific containers (reduce to zero)
- [DisasterRecoveryCapacitySpec](disaster-recovery-capacity) - Ensure post-failure capacity
- [CapacityWithGroupPresenceSpec](groups/capacity-with-group-presence) - Group-aware capacity
- [MinimizeContainersSpec](balance-optimize/minimize-containers) - Minimize number of used containers

## Examples in Repository

See these complete examples using CapacitySpec:

- [Load Balancing Cookbook](../cookbook/load-balancing) - Capacity constraints with balancing
- [Bin Packing Cookbook](../cookbook/bin-packing) - Capacity with container minimization
- [Multi-Tenant Cookbook](../cookbook/multi-tenant) - Per-tenant capacity limits
- [Disaster Recovery Cookbook](../cookbook/disaster-recovery) - Capacity with DR constraints
- [Gradual Migration Cookbook](../cookbook/gradual-migration) - Capacity during migrations

## Source Code

- Thrift definition: [`interface/thrift/ProblemSpecs.thrift:332`](https://github.com/facebookincubator/rebalancer/blob/main/interface/thrift/ProblemSpecs.thrift#L332)
- Builder: [`materializer/spec_builder/CapacitySpecBuilder.h`](https://github.com/facebookincubator/rebalancer/tree/main/materializer/spec_builder/)
- Tests: [`interface/tests/`](https://github.com/facebookincubator/rebalancer/tree/main/interface/tests/)

## Next Steps

- Learn about [Dimensions](../core-concepts/overview#dimensions) to understand capacity modeling
- See [Scopes](../core-concepts/overview#scopes) for applying capacity at different levels
- Check [Shard Placement Cookbook](../cookbook/shard-placement) for real-world capacity constraints
