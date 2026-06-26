---
sidebar_position: 6
---

# SingleColdestStratified

**Move Type**: Basic
**Complexity**: O(strata × sample_size) with coldest-first ordering

Move objects to the coldest (least loaded) containers within each stratum. Combines stratified sampling with capacity-aware greedy selection.

## Overview

`SingleColdestStratified` evaluates single object moves to the **coldest containers** (containers with lowest load/potential) within each stratum. Unlike random stratified sampling, this move type preferentially targets underutilized containers, making it excellent for **capacity balancing** and **bin-packing**.

**Use when**:
- Capacity balancing is critical
- Want to fill underutilized containers
- Containers group into natural strata
- Bin-packing or consolidation scenarios

**Avoid when**:
- Load balancing across all containers equally important
- Containers are homogeneous (no strata)
- "Coldest" doesn't align with objective
- Need deterministic results

## Quick Example

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_coldest_stratified_examples.py start=quick_example_start end=quick_example_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_coldest_stratified_examples.cpp start=quick_example_start end=quick_example_end
```

</TabItem>
</Tabs>

## Parameters

**Note**: SingleColdestStratified uses the legacy string-based move type specification. Parameters are set on `LocalSearchSolverSpec`, not a dedicated move type spec.

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `moveTypeName` | string | **Yes** | - | Set to `"SINGLE_COLDEST_STRATIFIED"` |
| `stratifiedSampleSize` | int32 | No | 10 | Sample size of coldest containers per stratum |
| `includeEqualSizeRandomSampleForSingleColdestMoveType` | bool | No | false | Double sample size by adding random containers |

### Parameter Details

**stratifiedSampleSize**:
- Number of **coldest** containers to sample per stratum
- Containers are ranked by "coldness" (low load/potential)
- Default: 10 containers per stratum

**includeEqualSizeRandomSampleForSingleColdestMoveType**:
- When `true`: Sample `k` coldest + `k` random containers per stratum (total 2k)
- When `false`: Sample only `k` coldest containers per stratum
- Helps avoid getting stuck in local optima by adding randomness

## How It Works

Given a **hot container** and **object to move**:

1. **Define strata**: Group containers by scope or similarity
2. **Find coldest per stratum**: Rank containers by coldness (low load)
3. **Sample coldest**: Take `stratifiedSampleSize` coldest from each stratum
4. **Optional random**: If enabled, add equal number of random containers
5. **Evaluate samples**: Test moving object to each sampled container
6. **Apply best**: Apply the move that improves objective most

### What is "Coldest"?

A container is **cold** if it has:
- **Low container potential**: Low contribution to objective
- **Few objects**: Underutilized capacity
- **Spare capacity**: Room to accept more objects

**Coldness ranking**: Lower potential = colder = more preferred

### Visual Example

```
System with 5 regions (strata), sample size = 2:

Region 1           Region 2           Region 3           Region 4           Region 5
+-----------+      +-----------+      +-----------+      +-----------+      +-----------+
| Coldest 1 |  ←   | Coldest 1 |  ←   | Coldest 1 |  ←   | Coldest 1 |  ←   | Coldest 1 |
| Coldest 2 |  ←   | Coldest 2 |  ←   | Coldest 2 |  ←   | Coldest 2 |  ←   | Coldest 2 |
|  (rest)   |      |  (rest)   |      |  (rest)   |      |  (rest)   |      |  (rest)   |
+-----------+      +-----------+      +-----------+      +-----------+      +-----------+

Total evaluations: 2 × 5 = 10 coldest containers

With includeEqualSizeRandomSampleForSingleColdestMoveType=true:
  2 coldest + 2 random per region = 4 per region
  Total evaluations: 4 × 5 = 20 containers
```

### Comparison with Variants

| Move Type | Selection Strategy | Coverage | Use Case |
|-----------|-------------------|----------|----------|
| [SingleRandomStratified](single-random-stratified) | Random per stratum | Balanced | General stratified sampling |
| **SingleColdestStratified** | Coldest per stratum | Capacity-aware | Bin-packing, consolidation |
| [SingleRandomObjectStratified](single-random-object-stratified) | Random objects | Object diversity | Object-level stratification |

## Complexity

**Moves evaluated per iteration**: O(S × K)

Where:
- S = `stratifiedSampleSize` (or 2×S if random sampling enabled)
- K = number of strata

**Example - Capacity balancing**:
- Regions (strata): 5
- Sample per region: 10 coldest
- **Moves evaluated**: 10 × 5 = **50**
- With random sampling: 20 × 5 = **100**

## Usage Patterns

### Bin-Packing/Consolidation

Fill underutilized containers:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_coldest_stratified_examples.py start=bin_packing_start end=bin_packing_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_coldest_stratified_examples.cpp start=bin_packing_start end=bin_packing_end
```

</TabItem>
</Tabs>

### Capacity Balancing Across Regions

Balance load while respecting regions:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_coldest_stratified_examples.py start=capacity_balancing_start end=capacity_balancing_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_coldest_stratified_examples.cpp start=capacity_balancing_start end=capacity_balancing_end
```

</TabItem>
</Tabs>

### With Random Sampling for Diversity

Add randomness to avoid local optima:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_coldest_stratified_examples.py start=random_sampling_start end=random_sampling_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_coldest_stratified_examples.cpp start=random_sampling_start end=random_sampling_end
```

</TabItem>
</Tabs>

### Server Decommissioning

Empty servers by moving to coldest alternatives:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/move_types/single_coldest_stratified_examples.py start=decommission_start end=decommission_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/move_types/single_coldest_stratified_examples.cpp start=decommission_start end=decommission_end
```

</TabItem>
</Tabs>

## Performance Characteristics

### Sample Size Tuning

| Sample Size | Random Enabled | Total Samples (5 strata) | Quality | Use Case |
|-------------|----------------|-------------------------|---------|----------|
| 5 | No | 25 | Low | Very fast, rough balancing |
| 10 (default) | No | 50 | Medium | Balanced speed/quality |
| 10 | Yes | 100 | Higher | Add diversity |
| 20 | No | 100 | High | Thorough coldest search |
| 20 | Yes | 200 | Highest | Maximum quality |

### When Does It Help?

SingleColdestStratified helps when:
- **Capacity imbalance**: Some containers nearly full, others nearly empty
- **Bin-packing**: Want to minimize active containers
- **Consolidation**: Fill existing containers before using new ones
- **Stratified structure**: Containers naturally group (regions, sizes)
- **Greedy works**: Moving to coldest aligns with objective

SingleColdestStratified does NOT help when:
- **All containers similar load**: No "cold" containers to target
- **Coldest ≠ best**: Objective doesn't favor underutilized containers
- **Need randomness**: Deterministic coldest selection gets stuck
- **Homogeneous containers**: No meaningful stratification

## Comparison with Variants

| Move Type | Selection | Randomness | Best For |
|-----------|-----------|------------|----------|
| [Single](single) | All containers | None | Small, complete search |
| [SingleFast](single-fast) | Early exit | None | Medium, fast |
| [SingleRandomBatches](single-random-batches) | Batches | Uniform random | Large, parallel |
| [SingleRandomStratified](single-random-stratified) | Per stratum | Stratified random | Large, balanced |
| **SingleColdestStratified** | Coldest per stratum | Optional random | Capacity balancing |

**Decision tree**:
1. **Capacity balancing needed?** → **SingleColdestStratified**
2. **General stratified?** → [SingleRandomStratified](single-random-stratified)
3. **No stratification?** → [Single](single) or [SingleFast](single-fast)

## Troubleshooting

### Problem: Always moving to same cold containers

**Diagnosis**: Sample size too small OR not using random sampling

**Solutions**:
- Increase `stratifiedSampleSize`
- Enable `includeEqualSizeRandomSampleForSingleColdestMoveType=true`
- Check if "coldest" definition aligns with objective
- May need different move type if cold containers filling up

### Problem: Not balancing capacity well

**Diagnosis**: Coldness metric doesn't capture capacity

**Solutions**:
- Verify objective function includes capacity terms
- Check container potential calculation
- May need explicit capacity constraints/goals
- Consider `MinimizeContainers` goal

### Problem: Getting stuck in local optimum

**Diagnosis**: Deterministic coldest selection

**Solutions**:
- Enable `includeEqualSizeRandomSampleForSingleColdestMoveType=true`
- Increase sample size for more options
- Combine with [SingleRandomStratified](single-random-stratified)
- Use multi-stage with different move types

### Problem: Stratification not helping

**Diagnosis**: Strata not meaningful OR coldest similar across strata

**Solutions**:
- Verify containers actually group into meaningful strata
- Check coldness varies across strata
- May need different stratification (different scope)
- Consider [SingleRandomBatches](single-random-batches) instead

## When to Use SingleColdestStratified

**DO use when**:
- Bin-packing or container consolidation
- Capacity balancing across regions/groups
- Filling underutilized containers
- Decommissioning servers (move to cold alternatives)
- Objective favors moving to less-loaded containers

**DO NOT use when**:
- All containers equally loaded
- Coldest selection doesn't align with objective
- Need random exploration
- Containers are homogeneous

## Related Move Types

**Stratified variants**:
- [SingleRandomStratified](single-random-stratified) - Random per stratum
- **SingleColdestStratified** - Coldest per stratum (this)
- [SingleRandomObjectStratified](single-random-object-stratified) - Object stratification

**Capacity-focused**:
- [SwapFullWithEmpty](../swap/swap-full-with-empty) - Move all to empty
- `MinimizeContainers` - Goal for consolidation

**Use together**:
1. SingleColdestStratified for capacity balancing
2. [Swap](../swap/) for fine-tuning
3. `MinimizeContainers` goal

## Source Code

- Move type name: `"SINGLE_COLDEST_STRATIFIED"` (legacy string-based)
- Parameters: [`interface/thrift/SolverSpecs.thrift:168,174`](https://github.com/facebookincubator/rebalancer/blob/main/interface/thrift/SolverSpecs.thrift#L168)
- Implementation: [`solver/moves/SingleColdestStratifiedMoveType.h`](https://github.com/facebookincubator/rebalancer/blob/main/solver/moves/SingleColdestStratifiedMoveType.h)
- Tests: [`solver/moves/tests/`](https://github.com/facebookincubator/rebalancer/tree/main/solver/moves/tests/)

## Next Steps

- Learn about [SingleRandomStratified](single-random-stratified) for random stratified sampling
- Try [SingleRandomObjectStratified](single-random-object-stratified) for object stratification
- Review `MinimizeContainers` for consolidation goals
- See [Move Types Overview](../) for choosing move types
