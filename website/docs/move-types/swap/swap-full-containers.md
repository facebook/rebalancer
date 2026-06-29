---
sidebar_position: 2
---

# SwapFullContainers

**Move Type**: Swap
**Complexity**: O(containers) with full container exchange

Exchange ALL objects between two containers in one move. Much simpler than pairwise swaps - useful for container rebalancing and migration scenarios.

## Overview

`SwapFullContainers` evaluates **exchanging all objects** between the hot container and every other container. Unlike regular Swap which tries individual object pairs, this move type swaps **entire container contents** at once.

**Use when**:
- Containers have similar sizes
- Want to relocate entire workload (e.g., migrate server)
- Capacity constraints block individual moves
- Container-level rebalancing more important than object-level

**Avoid when**:
- Containers have very different sizes
- Need fine-grained object placement
- Most objects in containers are well-placed
- Container capacities would be violated

## Quick Example

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_containers_examples.py start=quick_example_start end=quick_example_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_containers_examples.cpp start=quick_example_start end=quick_example_end
```

</TabItem>
</Tabs>

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| (none) | - | - | - | SwapFullContainers has no configuration parameters |

## How It Works

Given a **hot container** (the container contributing most to the objective):

1. **Select cold container**: Pick a different container
2. **Evaluate full swap**: Test exchanging ALL objects between hot and cold containers
3. **Repeat**: Try all other containers as potential swap partners
4. **Apply best**: Apply the full container swap that improves objective most

### Visual Example

```
Before swap:                      After full container swap:
┌─────────────────┐              ┌─────────────────┐
│ Hot Container   │              │ Hot Container   │
│  • obj1         │              │  • objA ←───────┼─┐
│  • obj2         │  <========>  │  • objB         │ │
│  • obj3         │              │  • objC         │ │
└─────────────────┘              └─────────────────┘ │
                                                     │
┌─────────────────┐              ┌─────────────────┐ │
│ Cold Container  │              │ Cold Container  │ │
│  • objA  ───────┼──┐           │  • obj1         │ │
│  • objB         │  │           │  • obj2         │ │
│  • objC         │  │           │  • obj3 ←───────┼─┘
└─────────────────┘  │           └─────────────────┘
                     │
      All objects exchanged in ONE move
```

### Comparison with Regular Swap

| Aspect | Swap | SwapFullContainers |
|--------|------|-------------------|
| **Granularity** | Individual object pairs | Entire containers |
| **Complexity** | O(N² × C) | O(C) |
| **Moves per iteration** | Thousands-millions | Tens-hundreds |
| **Use case** | Fine-grained optimization | Container-level rebalancing |

## Complexity

**Moves evaluated per iteration**: O(C)

Where:
- C = number of containers

**Example**:
- System: 100 containers
- Regular Swap with 1000 objects each: 1000² × 100 = **100 million** pairwise swaps
- SwapFullContainers: **100** full container swaps

**Note**: While fewer moves are evaluated, each full container swap is more expensive to evaluate since all objects in both containers must be considered.

## Usage Patterns

### Server Migration

Move entire server workload:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_containers_examples.py start=server_migration_start end=server_migration_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_containers_examples.cpp start=server_migration_start end=server_migration_end
```

</TabItem>
</Tabs>

### Container Consolidation

Rebalance container-level load:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_containers_examples.py start=consolidation_start end=consolidation_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_containers_examples.cpp start=consolidation_start end=consolidation_end
```

</TabItem>
</Tabs>

### Combined with Object-Level Moves

Use for coarse adjustment, then fine-tune:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_containers_examples.py start=combined_start end=combined_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_containers_examples.cpp start=combined_start end=combined_end
```

</TabItem>
</Tabs>

### Capacity-Constrained Full Swaps

When individual moves blocked by capacity:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_containers_examples.py start=capacity_constrained_start end=capacity_constrained_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/swap_full_containers_examples.cpp start=capacity_constrained_start end=capacity_constrained_end
```

</TabItem>
</Tabs>

## Performance Characteristics

### Scalability

| Containers | Objects per Container | Full Swaps Evaluated | Evaluation Time |
|------------|----------------------|---------------------|----------------|
| 10 | 1K | 10 | &lt;1s |
| 100 | 1K | 100 | 1-5s |
| 1000 | 1K | 1000 | 10-60s |
| 10K | 1K | 10K | 2-10 min |

**Note**: Evaluation time depends heavily on objective function complexity.

### When Does It Help?

SwapFullContainers helps when:
- **Similar container sizes**: Swapping makes sense
- **Container-level imbalance**: Distribution across containers is wrong
- **Capacity-blocked**: Individual moves violate capacity
- **Migration scenarios**: Moving entire workloads
- **Coarse rebalancing**: Need big changes quickly

SwapFullContainers does NOT help when:
- **Variable container sizes**: Swapping creates more imbalance
- **Object-level issues**: Problem is specific object placement
- **Most objects well-placed**: Full swap undoes good placements
- **Capacity highly varied**: Full swaps likely to violate constraints

## Comparison with Variants

| Move Type | Granularity | Complexity | Capacity Handling | Use Case |
|-----------|-------------|------------|------------------|----------|
| [Swap](swap.md) | Object pairs | O(N²) | Capacity-neutral | General swapping |
| [SwapSampled](swap-sampled) | Sampled pairs | O(S²) | Capacity-neutral | Large-scale swapping |
| **SwapFullContainers** | Full containers | O(C) | May violate | Container migration |
| [SwapFullWithEmpty](swap-full-with-empty) | Full to empty | O(C_empty) | Consolidation-friendly | Emptying containers |

**Decision tree**:
1. **Need to empty containers?** → [SwapFullWithEmpty](swap-full-with-empty)
2. **Container-level rebalancing?** → **SwapFullContainers**
3. **Object-level optimization?** → [Swap](swap.md) or [SwapSampled](swap-sampled)
4. **Capacity-constrained?** → [Swap](swap.md) (capacity-neutral by definition)

## Troubleshooting

### Problem: SwapFullContainers not finding good moves

**Diagnosis**: Container sizes too different or no beneficial full swaps exist

**Solutions**:
- Check container size distribution
- Use [Swap](swap.md) for fine-grained object swaps instead
- Combine with [Single](../basic/single) to move objects first
- May already be at good container-level balance

### Problem: Capacity violations after full swap

**Diagnosis**: Full container swap violated capacity constraints

**Solutions**:
- This is expected - SwapFullContainers doesn't guarantee capacity respect
- Set tight capacity constraints to prevent violations
- Use [Swap](swap.md) instead (capacity-neutral)
- Follow up with [Single](../basic/single) to fix violations

### Problem: Undoing good object placements

**Diagnosis**: Full swap moves well-placed objects

**Solutions**:
- Use SwapFullContainers only for initial coarse rebalancing
- Follow with object-level moves ([Single](../basic/single), [Swap](swap.md))
- Consider if full container swap is appropriate for this problem
- Use `avoidMoving` constraints for critical objects

### Problem: Too few improving moves found

**Diagnosis**: Only C moves evaluated, may miss opportunities

**Solutions**:
- This is expected - trade-off for simplicity
- Combine with [Swap](swap.md) or [SwapSampled](swap-sampled)
- Use multi-stage: full swap first, then object swaps
- Consider if problem needs container-level or object-level rebalancing

## When to Use SwapFullContainers

**DO use when**:
- Migrating entire server/container workloads
- Container-level rebalancing (similar-sized containers)
- Coarse initial rebalancing before fine-tuning
- Capacity constraints block most individual moves
- Testing "what if we swapped these two servers"

**DO NOT use when**:
- Containers have very different sizes
- Need fine-grained object placement
- Capacity constraints must be strictly respected
- Most objects are already well-placed

## Related Move Types

**Swap variants**:
- [Swap](swap.md) - Fine-grained object pair swaps
- [SwapSampled](swap-sampled) - Sampled object swaps for scale
- [SwapFullWithEmpty](swap-full-with-empty) - Move all to empty container

**Complementary**:
- [Single](../basic/single) - Use before/after for object-level tuning
- [SingleEndChain](../chain/single-end-chain) - Alternative for capacity issues

**Use together**:
1. SwapFullContainers for coarse container rebalancing
2. Swap or Single for fine object placement
3. Achieves both container and object level optimization

## Source Code

- Thrift definition: [`interface/thrift/SolverSpecs.thrift:533`](https://github.com/facebookincubator/rebalancer/blob/main/interface/thrift/SolverSpecs.thrift#L533)
- Implementation: [`solver/moves/SwapFullContainersMoveType.h`](https://github.com/facebookincubator/rebalancer/blob/main/solver/moves/SwapFullContainersMoveType.h)
- Tests: [`solver/moves/tests/`](https://github.com/facebookincubator/rebalancer/tree/main/solver/moves/tests/)

## Next Steps

- Learn about [SwapFullWithEmpty](swap-full-with-empty) for container consolidation
- Try [Swap](swap.md) for fine-grained object swaps
- Review [Move Types Overview](../) for choosing move types
- See [Capacity](../../reference/specs/capacity) for capacity constraints
