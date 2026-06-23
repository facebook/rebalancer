---
sidebar_position: 7
---

# SingleRandomObjectStratified

**Move Type**: Basic
**Complexity**: O(object_strata × sample_size) instead of O(objects)

Sample objects from stratified groups for more intelligent source selection. Reduces search space while maintaining coverage across different object types.

## Overview

`SingleRandomObjectStratified` evaluates single object moves but samples **source objects** in a stratified manner - taking samples from different object groups rather than uniformly. This is the **inverse** of `SingleRandomStratified` which stratifies destination containers.

**Use when**:
- Objects naturally group into strata (e.g., by size, type, priority)
- Want balanced exploration across object types
- Problem has many objects (millions)
- Random object selection gives poor coverage

**Avoid when**:
- Objects are homogeneous (no natural grouping)
- Problem has few objects (&lt;10K, use Single)
- Don't have meaningful object partitions
- Need exhaustive object exploration

## Quick Example

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_random_object_stratified_examples.py start=quick_example_start end=quick_example_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_random_object_stratified_examples.cpp start=quick_example_start end=quick_example_end
```

</TabItem>
</Tabs>

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `stratifiedSampleSize` | SampleSize | **Yes** | - | Sample size per object stratum |
| `objectsToExploreOptions` | ObjectsToExploreOptions | **Yes** | - | Defines object strata (groups/partitions) |

### Parameter Details

**stratifiedSampleSize**:
- Number of objects to sample **per object group**
- Uses `SampleSize` struct with `defaultSampleSize`
- Total objects sampled = `sample_size × number_of_object_groups`

**objectsToExploreOptions**:
- Defines the **object stratification** - how to group objects
- Typically uses `objectsFromGroupSpec` with partition name
- Example: `group` partition creates strata by object group

## How It Works

Given a **cold container** (underutilized destination):

1. **Define object strata**: Group objects by partition (e.g., size, type)
2. **Sample per stratum**: Take `sample_size` random objects from each group
3. **Evaluate samples**: Test moving each sampled object to cold container
4. **Apply best**: Apply the move that improves objective most

### Visual Example

```
System with 3 million objects stratified into 5 groups by size:

Group 1 (Very Small) Group 2 (Small)    Group 3 (Medium)   Group 4 (Large)    Group 5 (Very Large)
+------------------+ +------------------+ +------------------+ +------------------+ +------------------+
| 600K objects     | | 600K objects     | | 600K objects     | | 600K objects     | | 600K objects     |
|                  | |                  | |                  | |                  | |                  |
| Sample 300       | | Sample 300       | | Sample 300       | | Sample 300       | | Sample 300       |
+------------------+ +------------------+ +------------------+ +------------------+ +------------------+

Total objects evaluated: 300 × 5 = 1,500 (instead of 3,000,000!)
Coverage: Guaranteed samples from each size class
```

### Comparison with Container Stratification

| Aspect | SingleRandomStratified | SingleRandomObjectStratified |
|--------|----------------------|------------------------------|
| **What's stratified** | Destination containers | Source objects |
| **Sample from** | Container groups | Object groups |
| **Use case** | Many heterogeneous containers | Many heterogeneous objects |
| **Best for** | Destination diversity | Source diversity |

## Complexity

**Moves evaluated per iteration**: O(S × K × C)

Where:
- S = sample size per object group (`stratifiedSampleSize`)
- K = number of object groups
- C = number of destination containers (per sampled object)

**Effective complexity**: O(S × K) for object sampling

**Example - Large object problem**:
- Total objects: 3,000,000
- Object groups: 5
- Sample per group: 300
- **Objects sampled**: 300 × 5 = **1,500** (vs 3M for full Single)
- **Speedup**: 2000x

## Usage Patterns

### Size-Based Object Stratification

Sample across object sizes:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_random_object_stratified_examples.py start=size_stratified_start end=size_stratified_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_random_object_stratified_examples.cpp start=size_stratified_start end=size_stratified_end
```

</TabItem>
</Tabs>

### Type-Based Stratification

Ensure coverage across object types:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_random_object_stratified_examples.py start=type_stratified_start end=type_stratified_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_random_object_stratified_examples.cpp start=type_stratified_start end=type_stratified_end
```

</TabItem>
</Tabs>

### Very Large Object Sets

Millions of objects with stratification:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_random_object_stratified_examples.py start=large_scale_start end=large_scale_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_random_object_stratified_examples.cpp start=large_scale_start end=large_scale_end
```

</TabItem>
</Tabs>

### Priority-Based Stratification

Sample by object priority:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_random_object_stratified_examples.py start=priority_stratified_start end=priority_stratified_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_random_object_stratified_examples.cpp start=priority_stratified_start end=priority_stratified_end
```

</TabItem>
</Tabs>

## Performance Characteristics

### Object Stratification Benefit

| Object Groups | Sample/Group | Total Samples | Total Objects | Speedup | Coverage |
|---------------|--------------|---------------|---------------|---------|----------|
| 5 | 300 | 1.5K | 3M | 2000x | Excellent |
| 10 | 100 | 1K | 1M | 1000x | Excellent |
| 20 | 50 | 1K | 5M | 5000x | Good |
| 1 | 1K | 1K | 3M | 3000x | Poor (no stratification) |

**Key insight**: Object stratification is most valuable when you have **many heterogeneous objects**.

### When Does It Help?

SingleRandomObjectStratified helps when:
- **Heterogeneous objects**: Different sizes/types/priorities
- **Need coverage**: Important to try all object types
- **Very large object sets**: Millions of objects
- **Object partitions exist**: Natural object grouping
- **Biased objectives**: Some object types heavily preferred

SingleRandomObjectStratified does NOT help when:
- **Homogeneous objects**: All similar, no natural grouping
- **Small object sets**: Overhead not worth it
- **No partitions**: Can't define meaningful object groups
- **Container stratification more important**: Use [SingleRandomStratified](single-random-stratified)

## Comparison with Variants

| Move Type | What's Stratified | Sample From | Use Case |
|-----------|------------------|-------------|----------|
| [Single](single) | Nothing | All objects, all containers | Small problems |
| [SingleFast](single-fast) | Nothing (early exit) | Objects in order | Medium problems |
| [SingleRandomBatches](single-random-batches) | Nothing (batches) | Random containers | Large + multi-core |
| [SingleRandomStratified](single-random-stratified) | Containers | Containers per stratum | Large + container diversity |
| [SingleColdestStratified](single-coldest-stratified) | Containers (coldest) | Coldest containers per stratum | Capacity balancing |
| **SingleRandomObjectStratified** | Objects | Objects per group | Large + object diversity |

**Decision tree**:
1. **Many heterogeneous objects?** → **SingleRandomObjectStratified**
2. **Many heterogeneous containers?** → [SingleRandomStratified](single-random-stratified)
3. **Both?** → Combine both strategies
4. **Neither?** → [Single](single) or [SingleFast](single-fast)

## Troubleshooting

### Problem: Poor solution quality despite object stratification

**Diagnosis**: Sample size too small per group OR grouping not meaningful

**Solutions**:
- Increase `stratifiedSampleSize`
- Verify object groups are meaningful (similar objects in same group)
- Check if objects vary significantly within groups
- Try different partition/grouping
- Combine with [Single](single) for final refinement

### Problem: Not seeing speedup vs uniform sampling

**Diagnosis**: Objects are homogeneous OR too few groups

**Solutions**:
- Check if objects naturally group by partition
- May not need object stratification (use [Single](single))
- Verify number of groups is reasonable (5-20 ideal)
- Ensure groups have balanced sizes

### Problem: Missing good moves for rare object types

**Diagnosis**: Some groups have few objects but small sample size

**Solutions**:
- Increase sample size for small groups
- Use per-group sample size overrides in `SampleSize`
- Consider group size when setting samples
- May need different grouping approach

### Problem: Non-deterministic results

**Diagnosis**: Random sampling produces different results

**Solutions**:
- Set `randomSeed` in LocalSearchSolverSpec
- Increase sample size for stability
- Run multiple times and pick best
- Accept variance as trade-off for speed

## Choosing Object Stratification

**Good object stratification characteristics**:
1. **Meaningful groups**: Objects in same group are similar
2. **Balanced sizes**: Groups have roughly equal number of objects
3. **Diverse outcomes**: Moving different groups has different effects
4. **Reasonable count**: 5-20 groups ideal

**Common object stratification strategies**:
- **Size**: Small/medium/large objects
- **Type**: Object type, class, category
- **Priority**: High/medium/low priority
- **Resource usage**: CPU/memory/disk bins

**Example partition hierarchies**:
```
size_class > object_type > object
priority_level > team > object
resource_bin > workload_type > object
```

## Related Move Types

**Stratified variants**:
- [SingleRandomStratified](single-random-stratified) - Stratify containers
- [SingleColdestStratified](single-coldest-stratified) - Coldest containers per stratum
- **SingleRandomObjectStratified** - Stratify objects (this)

**Sampling alternatives**:
- [Single](single) - Full exploration (no sampling)
- [SingleRandomBatches](single-random-batches) - Uniform random container batches
- [SingleFast](single-fast) - Early exit

**Use together**:
1. SingleRandomObjectStratified for object diversity
2. Can combine with container stratification for double stratification
3. Follow with [Single](single) for final refinement

## Source Code

- Thrift definition: [`interface/thrift/SolverSpecs.thrift:656`](https://github.com/facebookincubator/rebalancer/blob/main/interface/thrift/SolverSpecs.thrift#L656)
- Implementation: [`solver/moves/SingleRandomObjectStratifiedMoveType.h`](https://github.com/facebookincubator/rebalancer/blob/main/solver/moves/SingleRandomObjectStratifiedMoveType.h)
- Tests: [`solver/moves/tests/`](https://github.com/facebookincubator/rebalancer/tree/main/solver/moves/tests/)

## Next Steps

- Learn about [SingleRandomStratified](single-random-stratified) for container stratification
- Try [SingleColdestStratified](single-coldest-stratified) for capacity-focused stratification
- Review [Groups and Partitions](../../core-concepts/overview#partitions) for partition design
- See [Move Types Overview](../) for choosing move types
