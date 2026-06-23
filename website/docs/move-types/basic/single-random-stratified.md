---
sidebar_position: 5
---

# SingleRandomStratified

**Move Type**: Basic
**Complexity**: O(strata × sample_size) instead of O(containers)

Sample containers from stratified groups (scope items) for more intelligent exploration. Reduces search space while maintaining coverage across different container types.

## Overview

`SingleRandomStratified` evaluates single object moves but samples destination containers in a **stratified** manner - taking samples from different groups (scope items) rather than uniformly at random. This ensures coverage across different container types (e.g., small, medium, large) without exhaustive search.

**Use when**:
- Containers naturally group into strata (e.g., by size, region, type)
- Want balanced exploration across container types
- Problem is large (&gt;10K containers)
- Random sampling alone gives poor coverage

**Avoid when**:
- Containers are homogeneous (no natural strata)
- Problem is small (&lt;1K containers, use Single)
- Don't have meaningful scope hierarchy
- Need exhaustive search

## Quick Example

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_random_stratified_examples.py start=quick_example_start end=quick_example_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_random_stratified_examples.cpp start=quick_example_start end=quick_example_end
```

</TabItem>
</Tabs>

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `destinationsToExplore` | DestinationsToExploreOptions | **Yes** | - | Defines strata (scope items) for sampling |
| `stratifiedSampleSize` | SampleSize | **Yes** | - | Sample size per stratum |

### Parameter Details

**destinationsToExplore**:
- Defines the **stratification** - how to group containers
- Typically uses `moveToCurrentScopeItem` to stratify by scope
- Example: `region` scope creates strata by region

**stratifiedSampleSize**:
- Number of containers to sample **per stratum**
- Uses `SampleSize` struct with `defaultSampleSize`
- Total samples = `sample_size × number_of_strata`

## How It Works

Given a **hot container** and **object to move**:

1. **Define strata**: Group containers by scope (e.g., region, size class)
2. **Sample per stratum**: Take `sample_size` random containers from each stratum
3. **Evaluate samples**: Test moving object to each sampled container
4. **Apply best**: Apply the move that improves objective most

### Visual Example

```
System with 30,000 containers stratified by size into 5 groups:

Stratum 1         Stratum 2         Stratum 3         Stratum 4         Stratum 5
+-------------+   +-------------+   +-------------+   +-------------+   +-------------+
| Very Small  |   | Small       |   | Medium      |   | Large       |   | Very Large  |
| 6000 cont.  |   | 6000 cont.  |   | 6000 cont.  |   | 6000 cont.  |   | 6000 cont.  |
|             |   |             |   |             |   |             |   |             |
| Sample 100  |   | Sample 100  |   | Sample 100  |   | Sample 100  |   | Sample 100  |
+-------------+   +-------------+   +-------------+   +-------------+   +-------------+

Total evaluations: 100 × 5 = 500 (instead of 30,000!)
Coverage: Guaranteed samples from each size class
```

### Comparison with Random Sampling

| Aspect | Uniform Random | Stratified Random |
|--------|---------------|-------------------|
| **Sampling** | Random from all containers | Random per stratum |
| **Coverage** | May miss rare types | Guarantees coverage |
| **Bias** | Toward common types | Balanced across types |
| **Use case** | Homogeneous containers | Heterogeneous containers |

## Complexity

**Moves evaluated per iteration**: O(S × K)

Where:
- S = sample size per stratum (`stratifiedSampleSize`)
- K = number of strata (scope items)

**Example - Large stratified problem**:
- Total containers: 30,000
- Strata (regions): 5
- Sample per stratum: 100
- **Moves evaluated**: 100 × 5 = **500** (vs 30,000 for full Single)
- **Speedup**: 60x

**Example - Very large problem**:
- Total containers: 100,000
- Strata (regions): 10
- Sample per stratum: 50
- **Moves evaluated**: 50 × 10 = **500** (vs 100,000 for full Single)
- **Speedup**: 200x

## Usage Patterns

### Region-Based Stratification

Sample across regions for coverage:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_random_stratified_examples.py start=region_stratified_start end=region_stratified_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_random_stratified_examples.cpp start=region_stratified_start end=region_stratified_end
```

</TabItem>
</Tabs>

### Size-Based Stratification

Ensure coverage across container sizes:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_random_stratified_examples.py start=size_stratified_start end=size_stratified_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_random_stratified_examples.cpp start=size_stratified_start end=size_stratified_end
```

</TabItem>
</Tabs>

### Large-Scale with Stratification

Very large problems with stratification:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_random_stratified_examples.py start=large_scale_start end=large_scale_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_random_stratified_examples.cpp start=large_scale_start end=large_scale_end
```

</TabItem>
</Tabs>

### Adaptive Sample Size

Tune sample size by stratum:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_random_stratified_examples.py start=adaptive_sample_start end=adaptive_sample_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_random_stratified_examples.cpp start=adaptive_sample_start end=adaptive_sample_end
```

</TabItem>
</Tabs>

## Performance Characteristics

### Stratification Benefit

| Strata | Sample/Stratum | Total Samples | Total Containers | Speedup | Coverage |
|--------|----------------|---------------|------------------|---------|----------|
| 5 | 100 | 500 | 30K | 60x | Excellent |
| 10 | 50 | 500 | 100K | 200x | Excellent |
| 20 | 25 | 500 | 100K | 200x | Good |
| 1 | 500 | 500 | 30K | 60x | Poor (no stratification) |

**Key insight**: More strata with smaller samples often better than fewer strata with large samples.

### When Does It Help?

SingleRandomStratified helps when:
- **Heterogeneous containers**: Different types/sizes/regions
- **Need coverage**: Important to explore all container types
- **Large scale**: Too many containers for exhaustive search
- **Scope hierarchy exists**: Natural stratification available
- **Biased objectives**: Some container types heavily preferred

SingleRandomStratified does NOT help when:
- **Homogeneous containers**: All similar, no natural grouping
- **Small problems**: Overhead not worth it
- **No scope hierarchy**: Can't define meaningful strata
- **Equal sampling sufficient**: Uniform random works fine

## Comparison with Variants

| Move Type | Sampling Strategy | Coverage | Use Case |
|-----------|------------------|----------|----------|
| [Single](single) | Exhaustive | Complete | Small problems |
| [SingleFast](single-fast) | Early exit | Variable | Medium problems |
| [SingleRandomBatches](single-random-batches) | Random batches | Uniform random | Large + multi-core |
| **SingleRandomStratified** | Stratified random | Balanced across types | Large + heterogeneous |
| [SingleColdestStratified](single-coldest-stratified) | Coldest-first per stratum | Balanced, greedy | Capacity-focused |
| [SingleRandomObjectStratified](single-random-object-stratified) | Stratified objects | Object diversity | Object stratification |

**Decision tree**:
1. **Small problem** (&lt;1K containers) → [Single](single)
2. **Large + homogeneous** → [SingleRandomBatches](single-random-batches)
3. **Large + heterogeneous** → **SingleRandomStratified**
4. **Large + capacity focus** → [SingleColdestStratified](single-coldest-stratified)

## Troubleshooting

### Problem: Poor solution quality despite stratification

**Diagnosis**: Sample size too small per stratum OR stratification not meaningful

**Solutions**:
- Increase `stratifiedSampleSize`
- Verify scope hierarchy creates meaningful groups
- Check if containers vary significantly within strata
- Try different stratification (different scope)
- Combine with [Single](single) for final refinement

### Problem: Not seeing speedup vs uniform sampling

**Diagnosis**: Containers are homogeneous OR too few strata

**Solutions**:
- Check if containers naturally group by scope
- May not need stratification (use [SingleRandomBatches](single-random-batches))
- Verify number of strata is reasonable (5-20 ideal)
- Ensure strata have balanced sizes

### Problem: Missing good moves in small strata

**Diagnosis**: Some strata have few containers but small sample size

**Solutions**:
- Increase sample size for small strata
- Consider stratum size when setting samples
- Use per-object sample size overrides
- May need different stratification approach

### Problem: Non-deterministic results

**Diagnosis**: Random sampling produces different results

**Solutions**:
- Set `randomSeed` in LocalSearchSolverSpec
- Increase sample size for stability
- Run multiple times and pick best
- Accept variance as trade-off for speed

## Choosing Stratification

**Good stratification characteristics**:
1. **Meaningful groups**: Containers in same stratum are similar
2. **Balanced sizes**: Strata have roughly equal number of containers
3. **Diverse outcomes**: Moving to different strata has different effects
4. **Reasonable count**: 5-20 strata ideal

**Common stratification strategies**:
- **Geographic**: Region, datacenter, rack
- **Capacity**: Size classes (small/medium/large)
- **Type**: Container type, tier, priority
- **Load**: Current utilization bins

**Example scope hierarchies**:
```
region > datacenter > rack > container
tier > container_type > container
size_class > region > container
```

## Related Move Types

**Stratified variants**:
- **SingleRandomStratified** - Random sampling per stratum (this)
- [SingleColdestStratified](single-coldest-stratified) - Coldest-first per stratum
- [SingleRandomObjectStratified](single-random-object-stratified) - Stratify objects instead

**Sampling alternatives**:
- [Single](single) - Full exploration (no sampling)
- [SingleRandomBatches](single-random-batches) - Uniform random sampling
- [SingleFast](single-fast) - Early exit

**Use together**:
1. SingleRandomStratified for quick improvement with coverage
2. [Single](single) for final refinement
3. Combine with [Swap](../swap/) for capacity constraints

## Source Code

- Thrift definition: [`interface/thrift/SolverSpecs.thrift:616`](https://github.com/facebookincubator/rebalancer/blob/main/interface/thrift/SolverSpecs.thrift#L616)
- Implementation: [`solver/moves/SingleRandomStratifiedMoveType.h`](https://github.com/facebookincubator/rebalancer/blob/main/solver/moves/SingleRandomStratifiedMoveType.h)
- Tests: [`solver/moves/tests/`](https://github.com/facebookincubator/rebalancer/tree/main/solver/moves/tests/)

## Next Steps

- Try [SingleColdestStratified](single-coldest-stratified) for capacity-focused stratification
- Learn about [SingleRandomObjectStratified](single-random-object-stratified) for object stratification
- Review [Scopes and Partitions](../../core-concepts/overview#scopes) for hierarchy design
- See [Move Types Overview](../) for choosing move types
