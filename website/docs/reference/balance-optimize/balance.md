---
sidebar_position: 2
---

# BalanceSpec

**Type**: Goal only

Balance the utilization of a resource across items in a scope.

## Overview

`BalanceSpec` minimizes the imbalance of a dimension across scope items. This is the most commonly used goal for load balancing problems.

**Use this when**: You want to distribute a resource evenly (e.g., CPU, memory, object count) across containers, racks, or datacenters.

## Quick Example

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/balance_optimize/balance_spec_examples.py start=quick_example_start end=quick_example_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/balance_optimize/balance_spec_examples.cpp start=quick_example_start end=quick_example_end
```

</TabItem>
</Tabs>

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | Yes | - | Descriptive name for logging/debugging |
| `scope` | string | Yes | - | Scope to balance across (e.g., `"host"`, `"rack"`) |
| `dimension` | string | Yes | - | Dimension to balance (e.g., `"cpu"`, `"memory"`) |
| `formula` | BalanceSpecFormula | No | LINEAR | How to measure imbalance |
| `upperBound` | double | No | 1.0 | Upper bound on relative utilization |
| `softUpperBound` | double | No | null | Soft upper bound (less strict) |
| `boundType` | BalanceSpecBoundType | No | RELATIVE | How bounds are interpreted |
| `definition` | BalanceSpecDefinition | No | AFTER | When to measure utilization |
| `fixAverageToInitial` | bool | No | false | Fix average to initial value |
| `includeInInitialAverage` | list&lt;string&gt; | No | [] | Containers to include in average |
| `useLegacyAverage` | bool | No | false | Use legacy average calculation |
| `filter` | Filter | No | null | Apply only to filtered objects |

## Formula Options

The `formula` parameter controls how imbalance is measured:

| Formula | Description | When to Use |
|---------|-------------|-------------|
| `LINEAR` | Sum of absolute deviations from average | Default, good for most cases |
| `SQUARES` | Sum of squared deviations (variance) | Penalizes large imbalances more |
| `MAX` | Maximum deviation from average | Focus on worst-case imbalance |
| `IDEAL` | Minimize distance from ideal uniform distribution | When you want perfect balance |
| `LEGACY` | Combination of MAX and SQUARES | Recommended for general use |

**Recommendation**: Use `LEGACY` for most load balancing problems. It balances the worst-case (MAX) with overall variance (SQUARES).

## Bound Types

The `boundType` parameter controls how `upperBound` is interpreted:

| Type | Interpretation | Example |
|------|----------------|---------|
| `RELATIVE` | Multiplier of average utilization | 1.0 = up to 100% of average |
| `ABSOLUTE` | Absolute offset from average relative util | 0.2 = up to average + 0.2 |
| `RELATIVE_UTIL` | Absolute utilization threshold | 0.8 = up to 80% utilization |

## Definition Options

The `definition` parameter controls when utilization is measured:

| Definition | Measures | Use Case |
|------------|----------|----------|
| `AFTER` | Utilization after moves complete | Standard rebalancing (default) |
| `DURING` | Utilization during moves | Minimize peak during migration |
| `NEW` | Only new objects (moved or added) | Focus on recent changes |
| `OLD` | Only objects that haven't moved | Ignore migration impact |

**Recommendation**: Use `AFTER` (default) for most cases.

## Common Usage Patterns

### Basic Load Balancing

Balance task count across hosts:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/balance_optimize/balance_spec_examples.py start=basic_load_balancing_start end=basic_load_balancing_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/balance_optimize/balance_spec_examples.cpp start=basic_load_balancing_start end=basic_load_balancing_end
```

</TabItem>
</Tabs>

### Balance with Fixed Average

Maintain total load while rebalancing:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/balance_optimize/balance_spec_examples.py start=balance_fixed_average_start end=balance_fixed_average_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/balance_optimize/balance_spec_examples.cpp start=balance_fixed_average_start end=balance_fixed_average_end
```

</TabItem>
</Tabs>

### Multi-Level Balancing

Balance at both host and rack level:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/balance_optimize/balance_spec_examples.py start=multi_level_balancing_start end=multi_level_balancing_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/balance_optimize/balance_spec_examples.cpp start=multi_level_balancing_start end=multi_level_balancing_end
```

</TabItem>
</Tabs>

### Balance with Upper Bound

Allow some imbalance, but not too much:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/balance_optimize/balance_spec_examples.py start=balance_upper_bound_start end=balance_upper_bound_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/balance_optimize/balance_spec_examples.cpp start=balance_upper_bound_start end=balance_upper_bound_end
```

</TabItem>
</Tabs>

### Balance Across Datacenters (With Drain)

When draining a datacenter, include it in average calculation:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/balance_optimize/balance_spec_examples.py start=balance_datacenter_drain_start end=balance_datacenter_drain_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/balance_optimize/balance_spec_examples.cpp start=balance_datacenter_drain_start end=balance_datacenter_drain_end
```

</TabItem>
</Tabs>

## Combining with Constraints

Common pattern: balance while respecting capacity:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/reference/balance_optimize/balance_spec_examples.py start=combining_capacity_start end=combining_capacity_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/reference/balance_optimize/balance_spec_examples.cpp start=combining_capacity_start end=combining_capacity_end
```

</TabItem>
</Tabs>

## Performance Considerations

- **Complexity**: O(number of scope items × number of objects)
- **Scales well** up to 10,000+ scope items
- `LEGACY` formula is slightly more expensive than `LINEAR` but usually worth it
- `fixAverageToInitial` adds minimal overhead

## Troubleshooting

### Problem: Balance not achieved

**Possible causes**:
1. Competing goals with higher weights
2. Constraints preventing balance
3. Not enough containers to achieve balance
4. Wrong scope or dimension

**Solution**:
- Increase weight of BalanceSpec
- Check constraint satisfaction
- Verify scope and dimension names

### Problem: Unexpected average calculation

**Possible causes**:
1. `fixAverageToInitial=false` (default) recalculates average dynamically
2. Wrong containers in `includeInInitialAverage`
3. `useLegacyAverage` changes calculation method

**Solution**:
- Set `fixAverageToInitial=true` to maintain initial average
- Double-check `includeInInitialAverage` list

## Related Specs

- [CapacitySpec](../capacity) - Often used together to enforce capacity while balancing
- [UtilIncreaseCostSpec](../util-increase-cost) - Alternative that prefers less-utilized containers
- [MinimizeSquaresSpec](minimize-squares) - Similar to SQUARES formula

## Examples in Repository

See these complete examples using BalanceSpec:

- [Load Balancing Cookbook](../../cookbook/load-balancing) - Basic load balancing patterns
- [Multi-Objective Cookbook](../../cookbook/multi-objective) - Balance with multiple goals
- [Multi-Tenant Cookbook](../../cookbook/multi-tenant) - Balanced multi-tenant placement
- [Shard Placement Cookbook](../../cookbook/shard-placement) - Balanced shard distribution
- [Colocation Cookbook](../../cookbook/colocation) - Balance with colocation constraints
- [Disaster Recovery Cookbook](../../cookbook/disaster-recovery) - Balance with DR requirements

## Source Code

- Thrift definition: [`interface/thrift/ProblemSpecs.thrift:183`](https://github.com/facebookincubator/rebalancer/blob/main/interface/thrift/ProblemSpecs.thrift#L183)
- Builder: [`materializer/spec_builder/BalanceSpecBuilder.h`](https://github.com/facebookincubator/rebalancer/tree/main/materializer/spec_builder/)
- Tests: [`interface/tests/`](https://github.com/facebookincubator/rebalancer/tree/main/interface/tests/)

## Next Steps

- See [Load Balancing Cookbook](../../cookbook/load-balancing) for complete example
- Learn about [Scopes](../../core-concepts/overview#scopes) to use BalanceSpec effectively
- Understand [Goal Priorities](../../core-concepts/overview#goals-and-constraints) for multi-goal problems
