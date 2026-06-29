---
sidebar_position: 1
---

# SingleChain

**Move Type**: Chain
**Complexity**: O(objects² × containers)

Evaluate 2-object chain moves where hot container gives one object and receives another. More expensive than [SingleEndChain](single-end-chain) but explores different neighborhood.

## Overview

`SingleChain` evaluates **2-object chain moves** where the hot container is in the **middle** of the chain:
1. Hot object moves from hot container → other container 2
2. Other object moves from other container 1 → hot container (fills the gap)

This differs from [SingleEndChain](single-end-chain) where the hot container is at the **end** and doesn't receive an object back.

**Use when**:
- Hot container has capacity constraints (must receive object back)
- Need balanced exchanges (one out, one in)
- [SingleEndChain](single-end-chain) not finding good moves
- Capacity-neutral chain moves required

**Avoid when**:
- Hot container should empty (use [SingleEndChain](single-end-chain))
- Problem is large (&gt;10K objects, too expensive)
- Simple moves work (try [Single](../basic/single) first)
- [SingleEndChain](single-end-chain) works (it's faster)

## Quick Example

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_chain_examples.py start=quick_example_start end=quick_example_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_chain_examples.cpp start=quick_example_start end=quick_example_end
```

</TabItem>
</Tabs>

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `partitionNameToExploreChainsWithinObjectGroup` | string | No | null | Restrict replacement objects to same partition |
| `specialColdContainer` | string | No | null | Fixed destination for hot object |

### Parameter Details

**partitionNameToExploreChainsWithinObjectGroup**:
- Restricts which objects can replace the hot object
- Only objects in same partition group will be considered
- Useful for type-based constraints (e.g., only replace task with same type task)

**specialColdContainer**:
- Forces hot object to move to specific container
- Useful when you know the destination (e.g., new server)
- Reduces search space significantly

## How It Works

Given a **hot container** (most broken):

1. **Select hot object**: Pick object from hot container
2. **Select destination**: Pick other container 2 for hot object
3. **Select source**: Pick other container 1 (source of replacement)
4. **Select replacement**: Pick object from other container 1
5. **Evaluate chain**: Test 2-move chain:
   - Move hot object: hot container → other container 2
   - Move replacement: other container 1 → hot container
6. **Repeat**: Try all combinations
7. **Apply best**: Apply the 2-move chain improving objective most

### Visual Example

```
Before chain:                          After chain:
┌──────────────┐                      ┌──────────────┐
│ Hot          │                      │ Hot          │
│ Container    │  ─────(1)────>       │ Container    │
│  • hotObj ──┐│                      │  • replObj ←─┼─┐
│  • obj2     ││                      │  • obj2      │ │
└─────────────┼┘                      └──────────────┘ │
              │                                        │
┌─────────────┼┐                      ┌────────────────┼┐
│ Other       ││                      │ Other          ││
│ Container 1 ││  <────(2)────        │ Container 1    ││
│  • replObj ─┼┘                      │  (gave replObj)│ │
│  • objX     │                       │  • objX        │ │
└─────────────┘                       └────────────────┘ │
┌─────────────┐                       ┌────────────────┐ │
│ Other       │                       │ Other          │ │
│ Container 2 │                       │ Container 2    │ │
│  • objY     │                       │  • hotObj ←────┼─┘
│  • objZ     │                       │  • objY        │
└─────────────┘                       │  • objZ        │
                                      └────────────────┘

Chain: hotObj (Hot→Other2), replObj (Other1→Hot)
Hot container: gives hotObj, receives replObj (balanced)
```

### Comparison with SingleEndChain

| Aspect | SingleChain | SingleEndChain |
|--------|-------------|----------------|
| **Hot container role** | Middle (gives + receives) | End (gives only) |
| **Hot container change** | Object swapped | Object removed |
| **Complexity** | O(N² × C) | O(N × C²) |
| **Use case** | Capacity-constrained hot | Empty hot container |
| **Default choice** | No | **Yes** |

**Recommendation**: Try [SingleEndChain](single-end-chain) first - it's more commonly useful.

## Complexity

**Moves evaluated per iteration**: O(N² × C)

Where:
- N = number of objects in hot container
- C = number of other containers

**Example - Medium problem**:
- Hot container: 1,000 objects
- System: 100 containers
- Moves evaluated ≈ 1,000² × 100 = **100 million** moves per iteration

**Warning**: Very expensive for large problems. Use [SingleEndChain](single-end-chain) or restrict parameters.

## Usage Patterns

### Basic Chain Moves

Default usage for capacity-constrained scenarios:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_chain_examples.py start=basic_start end=basic_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_chain_examples.cpp start=basic_start end=basic_end
```

</TabItem>
</Tabs>

### Restricted by Object Type

Only replace with same type objects:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_chain_examples.py start=restricted_start end=restricted_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_chain_examples.cpp start=restricted_start end=restricted_end
```

</TabItem>
</Tabs>

### Fixed Destination

When you know where hot object should go:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_chain_examples.py start=fixed_dest_start end=fixed_dest_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_chain_examples.cpp start=fixed_dest_start end=fixed_dest_end
```

</TabItem>
</Tabs>

### Combined Strategy

Use with other move types:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_chain_examples.py start=combined_start end=combined_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_chain_examples.cpp start=combined_start end=combined_end
```

</TabItem>
</Tabs>

## Performance Characteristics

### Scalability

| Objects | Containers | Chain Moves Evaluated | Time | Practical? |
|---------|------------|----------------------|------|------------|
| 100 | 50 | 500K | &lt;1s | ✓ Yes |
| 1K | 100 | 100M | 10-60s | △ Marginal |
| 10K | 100 | 10B | Hours | ✗ No |
| &gt;10K | &gt;100 | Too many | N/A | ✗ Absolutely not |

**Hard limit**: Do not use SingleChain with &gt;1K objects per container.

### When Does It Help?

SingleChain helps when:
- **Hot container capacity-constrained**: Must receive object back
- **Balanced exchanges needed**: One out, one in
- **Type constraints**: Replacement must match hot object type
- [SingleEndChain](single-end-chain) not applicable (hot can't empty)

SingleChain does NOT help when:
- **Hot should empty**: Use [SingleEndChain](single-end-chain) instead
- **Problem too large**: O(N²) too expensive
- **Simple moves work**: Use [Single](../basic/single) first
- **No capacity constraints**: [Single](../basic/single) is faster

## Comparison with Alternatives

| Move Type | Hot Container Role | Complexity | Capacity | Use Case |
|-----------|-------------------|------------|----------|----------|
| [Single](../basic/single) | Gives only | O(N × C) | Hot empties | General moves |
| [SingleEndChain](single-end-chain) | End (gives) | O(N × C²) | Hot empties | 2-object chains |
| **SingleChain** | Middle (gives+receives) | O(N² × C) | Balanced | Capacity-constrained |
| [SingleChainFast](single-chain-fast) | Middle (early exit) | O(N² × C) | Balanced | Faster SingleChain |
| [Swap](../swap/) | Paired exchange | O(N²) | Balanced | Direct swaps |

**Decision tree**:
1. **Hot should empty?** → [SingleEndChain](single-end-chain)
2. **Hot must stay balanced?** → **SingleChain**
3. **Need speed?** → [SingleChainFast](single-chain-fast)
4. **Direct swaps?** → [Swap](../swap/)

## Troubleshooting

### Problem: SingleChain too slow

**Diagnosis**: O(N²) too expensive for problem size

**Solutions**:
- Use [SingleChainFast](single-chain-fast) instead (early exit)
- Set `specialColdContainer` to reduce search space
- Use `partitionNameToExploreChainsWithinObjectGroup` to filter
- Consider if [SingleEndChain](single-end-chain) works instead
- Problem may be too large for chain moves

### Problem: Not finding good chain moves

**Diagnosis**: Wrong move type OR constraints too restrictive

**Solutions**:
- Try [SingleEndChain](single-end-chain) (different neighborhood)
- Remove `partitionNameToExploreChainsWithinObjectGroup` restriction
- Check if simple [Single](../basic/single) moves work
- May already be at local optimum
- Verify capacity constraints aren't blocking all moves

### Problem: Hot container not staying balanced

**Diagnosis**: This is expected - SingleChain should balance

**Solutions**:
- This move type is designed for balance (one in, one out)
- If seeing imbalance, check implementation
- Verify objective function rewards balance
- May need capacity constraints

### Problem: Getting worse results than expected

**Diagnosis**: Wrong move type for problem

**Solutions**:
- Try [SingleEndChain](single-end-chain) first (usually better)
- Check if [Single](../basic/single) + [Swap](../swap/) sufficient
- May need different objective function
- Verify hot container selection is correct

## When to Use SingleChain

**DO use when**:
- Hot container capacity-constrained (must receive back)
- Need balanced exchanges (one out, one in)
- Type-based replacement constraints
- [SingleEndChain](single-end-chain) doesn't apply
- Problem is small-medium (&lt;1K objects)

**DO NOT use when**:
- Hot container should empty → Use [SingleEndChain](single-end-chain)
- Problem is large (&gt;1K objects) → Too expensive
- Simple moves work → Use [Single](../basic/single)
- [SingleEndChain](single-end-chain) works → It's the better default

## Related Move Types

**Chain variants**:
- [SingleEndChain](single-end-chain) - **Default choice** for chains
- **SingleChain** - Capacity-constrained variant (this)
- [SingleChainFast](single-chain-fast) - Faster with early exit

**Simpler alternatives**:
- [Single](../basic/single) - Try first
- [Swap](../swap/) - Direct object swaps

**When to use each**:
1. **First**: [Single](../basic/single)
2. **If stuck**: [SingleEndChain](single-end-chain) (not SingleChain!)
3. **If hot must balance**: **SingleChain**
4. **Need speed**: [SingleChainFast](single-chain-fast)

## Source Code

- Thrift definition: [`interface/thrift/SolverSpecs.thrift:568`](https://github.com/facebookincubator/rebalancer/blob/main/interface/thrift/SolverSpecs.thrift#L568)
- Implementation: [`solver/moves/SingleChainMoveType.h`](https://github.com/facebookincubator/rebalancer/blob/main/solver/moves/SingleChainMoveType.h)
- Tests: [`solver/moves/tests/`](https://github.com/facebookincubator/rebalancer/tree/main/solver/moves/tests/)

## Next Steps

- **Try [SingleEndChain](single-end-chain) first** - better default choice for chains
- Learn about [SingleChainFast](single-chain-fast) for faster chain moves
- Review [Move Types Overview](../) for choosing move types
- See [Capacity](../../reference/specs/capacity) for capacity constraints
