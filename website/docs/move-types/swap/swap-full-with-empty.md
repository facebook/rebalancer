---
sidebar_position: 3
---

# SwapFullWithEmpty

**Move Type**: Swap
**Complexity**: O(empty_containers) with full container move

Move ALL objects from hot container to an empty container. Ideal for consolidation, decommissioning, and bin-packing scenarios.

## Overview

`SwapFullWithEmpty` evaluates moving **all objects** from the hot container to every empty container in the system. This is a specialized move type for **consolidation** - reducing the number of active containers by emptying overloaded or problematic containers.

**Use when**:
- Consolidating workload (reducing active containers)
- Decommissioning servers/containers
- Bin-packing optimization
- Have spare capacity in empty containers
- Want to empty specific containers

**Avoid when**:
- No empty containers available
- Objects can't all fit in one container
- Need fine-grained object placement
- Container capacities would be violated

## Quick Example

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_with_empty_examples.py start=quick_example_start end=quick_example_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_with_empty_examples.cpp start=quick_example_start end=quick_example_end
```

</TabItem>
</Tabs>

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| (none) | - | - | - | SwapFullWithEmpty has no configuration parameters |

## How It Works

Given a **hot container** (the container contributing most to the objective):

1. **Find empty containers**: Identify all containers with zero objects
2. **Try each empty**: For each empty container:
   - Move ALL objects from hot container to empty container
   - Evaluate resulting objective
3. **Parallel evaluation**: All empty containers evaluated in parallel
4. **Apply best**: Apply the move to empty container that improves objective most

### Visual Example

```
Before move:                      After full move to empty:
┌─────────────────┐              ┌─────────────────┐
│ Hot Container   │              │ Hot Container   │
│  • obj1  ───────┼──┐           │  (empty)        │
│  • obj2         │  │           │                 │
│  • obj3         │  │           │                 │
└─────────────────┘  │           └─────────────────┘
                     │
                     │           ┌─────────────────┐
┌─────────────────┐  │           │ Empty Container │
│ Empty Container │  │           │  • obj1 ←───────┼─┘
│  (empty)        │  │           │  • obj2         │
│                 │  │           │  • obj3         │
└─────────────────┘  │           └─────────────────┘
      All objects moved in ONE move
```

### Comparison with Alternatives

| Move Type | Source | Destination | Use Case |
|-----------|--------|-------------|----------|
| [Single](../basic/single) | Any object | Any container | General placement |
| [Swap](swap.md) | Object pairs | Occupied containers | Capacity-constrained |
| [SwapFullContainers](swap-full-containers) | All objects | Any container (swap) | Container migration |
| **SwapFullWithEmpty** | All objects | Empty containers only | Consolidation |

## Complexity

**Moves evaluated per iteration**: O(E)

Where:
- E = number of empty containers

**Example**:
- System: 1,000 containers total
- Empty containers: 100
- Hot container: 1,000 objects
- Moves evaluated: **100** (one per empty container)

**Note**: Much faster than regular moves since only empty containers are considered.

## Usage Patterns

### Container Consolidation

Reduce number of active containers:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_with_empty_examples.py start=consolidation_start end=consolidation_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_with_empty_examples.cpp start=consolidation_start end=consolidation_end
```

</TabItem>
</Tabs>

### Server Decommissioning

Empty servers for maintenance:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_with_empty_examples.py start=decommission_start end=decommission_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_with_empty_examples.cpp start=decommission_start end=decommission_end
```

</TabItem>
</Tabs>

### Bin-Packing Optimization

Minimize number of active bins:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_with_empty_examples.py start=bin_packing_start end=bin_packing_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_with_empty_examples.cpp start=bin_packing_start end=bin_packing_end
```

</TabItem>
</Tabs>

### Combined Strategy

Use with other moves for complete consolidation:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_with_empty_examples.py start=combined_start end=combined_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_with_empty_examples.cpp start=combined_start end=combined_end
```

</TabItem>
</Tabs>

## Performance Characteristics

### Scalability

| Empty Containers | Objects to Move | Evaluation Time | Practical? |
|-----------------|----------------|----------------|------------|
| 10 | 1K | &lt;1s | ✓ Yes |
| 100 | 10K | 1-5s | ✓ Yes |
| 1000 | 10K | 10-60s | ✓ Yes |
| 10K | 100K | 2-10 min | △ Depends on objective |

**Note**: Complexity is O(empty_containers), independent of total containers.

### When Does It Help?

SwapFullWithEmpty helps when:
- **Consolidation goal**: Want to reduce active containers
- **Spare capacity**: Have empty containers available
- **Decommissioning**: Need to empty specific containers
- **Bin-packing**: Minimize container usage
- **Hot container is problematic**: Want it completely empty

SwapFullWithEmpty does NOT help when:
- **No empty containers**: Can't move anywhere
- **Capacity issues**: Objects won't fit in one container
- **Need fine-grained placement**: Full move too coarse
- **Hot container should stay active**: Just needs rebalancing

## Comparison with Variants

| Move Type | Destination Type | Granularity | Primary Use Case |
|-----------|-----------------|-------------|------------------|
| [Single](../basic/single) | Any container | Individual objects | General optimization |
| [Swap](swap.md) | Occupied containers | Object pairs | Capacity-constrained |
| [SwapFullContainers](swap-full-containers) | Any container | Full swap | Container migration |
| **SwapFullWithEmpty** | Empty only | All objects | Consolidation |

**Consolidation strategy order**:
1. **SwapFullWithEmpty** - Empty problematic containers
2. [Single](../basic/single) - Pack remaining objects efficiently
3. `MinimizeContainers` goal - Drive toward fewer containers

## Troubleshooting

### Problem: No improving moves found

**Diagnosis**: No empty containers OR moving to empty doesn't improve objective

**Solutions**:
- Check if empty containers exist (`containers - used_containers`)
- Verify objective function rewards consolidation
- May need `MinimizeContainers` goal
- Use [Single](../basic/single) to create empty containers first

### Problem: Capacity violations when moving to empty

**Diagnosis**: All objects don't fit in empty container

**Solutions**:
- Check container capacity constraints
- Objects may exceed single container capacity
- Use [Single](../basic/single) to move objects individually
- Split hot container load across multiple destinations

### Problem: Empties wrong containers

**Diagnosis**: Hot container selection not aligned with decommissioning goals

**Solutions**:
- Use `avoidMoving` to protect containers that should stay active
- Use `nonAccepting` to mark containers for decommissioning
- Set `toFree` constraints on containers to empty
- Adjust objective to penalize specific containers

### Problem: Too slow despite few empty containers

**Diagnosis**: Objective evaluation is expensive for full container moves

**Solutions**:
- Profile objective function evaluation
- Each move evaluates all objects moving together
- Consider if full move is necessary
- May be inherently expensive for this problem

## When to Use SwapFullWithEmpty

**DO use when**:
- Consolidating workload to reduce container count
- Decommissioning servers or containers
- Bin-packing optimization
- Have empty containers available
- Want to completely empty specific containers

**DO NOT use when**:
- No empty containers in system
- Need fine-grained object placement
- Objects won't fit in single container
- Hot container should remain active

## Related Move Types

**Consolidation moves**:
- **SwapFullWithEmpty** - Move all to empty (this)
- `MinimizeContainers` - Goal to reduce containers
- [Single](../basic/single) - Move individual objects

**Full container moves**:
- [SwapFullContainers](swap-full-containers) - Swap entire containers
- **SwapFullWithEmpty** - Move to empty only (this)

**Use together**:
1. SwapFullWithEmpty to empty problematic containers
2. [Single](../basic/single) to pack efficiently
3. `MinimizeContainers` goal to drive consolidation

## Source Code

- Thrift definition: [`interface/thrift/SolverSpecs.thrift:520`](https://github.com/facebookincubator/rebalancer/blob/main/interface/thrift/SolverSpecs.thrift#L520)
- Implementation: [`solver/moves/SwapFullWithEmptyContainersMoveType.h`](https://github.com/facebookincubator/rebalancer/blob/main/solver/moves/SwapFullWithEmptyContainersMoveType.h)
- Tests: [`solver/moves/tests/`](https://github.com/facebookincubator/rebalancer/tree/main/solver/moves/tests/)

## Next Steps

- Learn about [SwapFullContainers](swap-full-containers) for full container swaps
- Try `MinimizeContainers` goal for consolidation
- See [Move Types Overview](../) for choosing move types
