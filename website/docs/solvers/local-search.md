---
sidebar_position: 2
---

# Local Search Solver

The Local Search solver uses iterative improvement to find good solutions quickly. It's the recommended solver for large problems (>10,000 objects).

## How It Works

Local Search starts with the current assignment and repeatedly makes "moves" that improve the objective:

1. **Start** with initial assignment
2. **Generate** candidate moves (e.g., move object X to container Y)
3. **Evaluate** each move's impact on objective
4. **Apply** the best improving move
5. **Repeat** until no improving moves found (or hit limits)

This finds a **local optimum** - a solution where no single move improves things further.

## Quick Example

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/local_search_examples.py start=quick_example_start end=quick_example_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/local_search_examples.cpp start=quick_example_start end=quick_example_end
```

</TabItem>
</Tabs>

## Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `solveTime` | int | No limit | Maximum solve time in milliseconds |
| `stopAfterMoves` | int | No limit | Maximum number of moves to apply |
| `moveTypeList` | list&lt;MoveTypeSpec&gt; | Auto | Which move types to use (see below) |
| `randomSeed` | int | Random | Random seed for reproducibility |
| `enableObjectPotentialSorting` | bool | false | Enable potential-based object sorting |
| `minHotObjects` | int | 1 | Minimum hot objects to consider |

## Move Types

Local Search explores different types of moves. Each move type searches a different "neighborhood" of solutions. See the **[Move Types Reference](../move-types/)** for complete documentation of all 27 move types.

### Common Move Types

**[Single](../move-types/basic/single)**: Move one object to a different container
- **When**: Always useful - most fundamental move type
- **Cost**: O(objects × containers) per iteration
- **Example**: Move task5 from host1 to host3

**[Swap](../move-types/swap/)**: Swap two objects between containers
- **When**: Capacity constrained (can't just add objects)
- **Cost**: O(objects²) per iteration
- **Example**: Swap task5 on host1 with task8 on host3

**[TripleLoop](../move-types/advanced/triple-loop)**: Try complex multi-object rearrangements
- **When**: Need to escape local optima
- **Cost**: Expensive, O(objects³)
- **Example**: Move 3+ objects in a cycle

### Specialized Move Types

**[KLSearch](../move-types/advanced/kl-search)**: Kernighan-Lin style search with sequences of moves
- **When**: Graph partitioning-style problems
- **Cost**: Expensive but powerful

**[Chain Moves](../move-types/chain/single-chain)**: Move sequences of objects in a chain
- **When**: Moving objects creates opportunities for other moves
- **Cost**: Medium
- **Best**: [SingleEndChain](../move-types/chain/single-end-chain) (recommended over SingleChain)

**[Group Moves](../move-types/group/colocate-groups)**: Move entire groups together
- **When**: Using MoveGroupSpec or colocation goals
- **Cost**: Depends on group sizes
- **Example**: [ColocateGroups](../move-types/group/colocate-groups)

**[Fixed Source/Dest](../move-types/fixed/fixed-dest)**: Only consider moves from/to specific containers
- **When**: Draining specific containers (`ToFree`) or filling specific containers
- **Cost**: Reduced search space, faster
- **Example**: [FixedSource](../move-types/fixed/fixed-source) for draining

### Configuring Move Types

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
from rebalancer.specs import (
    LocalSearchSolverSpec,
    MoveTypeSpec,
    SingleMoveTypeSpec,
    SolverSpec,
    SwapMoveTypeSpec,
)

# Default: auto-selected move types
solver.add_solver(SolverSpec(localSearchSolverSpec=LocalSearchSolverSpec()))

# Custom: specify exact move types to use
solver.add_solver(
    SolverSpec(
        localSearchSolverSpec=LocalSearchSolverSpec(
            moveTypeList=[
                MoveTypeSpec(singleMoveTypeSpec=SingleMoveTypeSpec()),
                MoveTypeSpec(swapMoveTypeSpec=SwapMoveTypeSpec()),
            ]
        )
    )
)
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
#include <rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h>

using namespace facebook::rebalancer::interface;

// Default: auto-selected move types
LocalSearchSolverSpec spec;
solver.addSolver(spec);

// Custom: specify exact move types
LocalSearchSolverSpec customSpec;
MoveTypeSpec singleSpec;
singleSpec.singleMoveTypeSpec_ref() = SingleMoveTypeSpec();
customSpec.moveTypeList_ref()->push_back(singleSpec);

MoveTypeSpec swapSpec;
swapSpec.swapMoveTypeSpec_ref() = SwapMoveTypeSpec();
customSpec.moveTypeList_ref()->push_back(swapSpec);

solver.addSolver(customSpec);
```

</TabItem>
</Tabs>

## Termination Conditions

Local Search stops when:

1. **No improving move found** - Reached local optimum (`UNABLE_TO_FIND_MORE_MOVES`)
2. **Move limit reached** - Hit `stopAfterMoves` (`HIT_MOVE_LIMIT`)
3. **Time limit reached** - Hit `solveTime` (`HIT_TIME_LIMIT`)
4. **Plateau timeout** - Stuck in plateau too long (`HIT_PLATEAU_TIME`)

Check termination reason from the solver report:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
# Access the solver report (first solver if multiple)
solver_report = solution["solverSummaries"][0]

end_reason = solver_report["endReason"]
if end_reason == "UNABLE_TO_FIND_MORE_MOVES":
    print("Found local optimum")
elif end_reason == "HIT_MOVE_LIMIT":
    print("Hit iteration limit - may improve with more moves")
elif end_reason == "HIT_TIME_LIMIT":
    print("Hit time limit - may improve with more time")
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
// Access the solver report (first solver if multiple)
auto& solverReport = solution.solverSummaries[0];

if (solverReport.endReason == EndReason::UNABLE_TO_FIND_MORE_MOVES) {
    std::cout << "Found local optimum\n";
} else if (solverReport.endReason == EndReason::HIT_MOVE_LIMIT) {
    std::cout << "Hit iteration limit - may improve with more moves\n";
} else if (solverReport.endReason == EndReason::HIT_TIME_LIMIT) {
    std::cout << "Hit time limit - may improve with more time\n";
}
```

</TabItem>
</Tabs>

## Performance Characteristics

### Scalability

| Problem Size | Typical Time | Iterations |
|--------------|--------------|------------|
| 100 objects, 10 containers | &lt;1 second | 100-1,000 |
| 1,000 objects, 100 containers | 1-5 seconds | 1,000-10,000 |
| 10,000 objects, 1,000 containers | 10-60 seconds | 10,000-100,000 |
| 100,000 objects, 10,000 containers | 1-10 minutes | 100,000-1,000,000 |

### Memory Usage

- **Per object**: ~100-200 bytes
- **Per container**: ~50-100 bytes
- **Example**: 100K objects + 10K containers ≈ 20MB

### Parallelization

Local Search uses multi-threading for move evaluation:
- Move generation: Can be parallelized across move types
- Move evaluation: Parallelized across candidate moves
- Cores used: Effectively utilizes multiple cores (typically 2-8+)
- Note: Some move types like SingleGreedy are single-threaded

## Solution Quality

Local Search provides **no optimality guarantee**, but empirically:

- Typically **95-99% of optimal** for well-structured problems
- Quality improves with more time/iterations
- Quality depends on initial assignment

### Quality Factors

**Good quality when**:
- Good initial assignment
- Well-balanced problem
- Sufficient time/moves
- Appropriate move types

**Poor quality when**:
- Terrible initial assignment (many broken constraints)
- Highly constrained problem (few feasible solutions)
- Insufficient time/moves
- Wrong move types for problem structure

## Improving Solution Quality

### Increase Limits

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/local_search_examples.py start=increase_limits_start end=increase_limits_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/local_search_examples.cpp start=increase_limits_start end=increase_limits_end
```

</TabItem>
</Tabs>

### Better Initial Assignment

```python
# Start with a better assignment
# E.g., pre-balance object counts before running solver
initial_assignment = distribute_evenly(objects, containers)
solver.set_assignment(initial_assignment)
```

### Enable More Move Types

By default, Local Search auto-selects appropriate move types. To use all available move types or a custom set, specify them explicitly using `moveTypeList`:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
from rebalancer.specs import (
    LocalSearchSolverSpec,
    MoveTypeSpec,
    SingleChainMoveTypeSpec,
    SingleMoveTypeSpec,
    SolverSpec,
    SwapMoveTypeSpec,
    TripleLoopMoveTypeSpec,
)

# Use a comprehensive set of move types
solver.add_solver(
    SolverSpec(
        localSearchSolverSpec=LocalSearchSolverSpec(
            moveTypeList=[
                MoveTypeSpec(singleMoveTypeSpec=SingleMoveTypeSpec()),
                MoveTypeSpec(swapMoveTypeSpec=SwapMoveTypeSpec()),
                MoveTypeSpec(tripleLoopMoveTypeSpec=TripleLoopMoveTypeSpec()),
                MoveTypeSpec(singleChainMoveTypeSpec=SingleChainMoveTypeSpec()),
            ]
        )
    )
)
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
// Use a comprehensive set of move types
LocalSearchSolverSpec spec;
std::vector<MoveTypeSpec> moveTypes;

MoveTypeSpec single;
single.singleMoveTypeSpec_ref() = SingleMoveTypeSpec();
moveTypes.push_back(single);

MoveTypeSpec swap;
swap.swapMoveTypeSpec_ref() = SwapMoveTypeSpec();
moveTypes.push_back(swap);

spec.moveTypeList_ref() = moveTypes;
solver.addSolver(spec);
```

</TabItem>
</Tabs>

### Run Multiple Times

```python
from rebalancer import ProblemSolver
from rebalancer.specs import LocalSearchSolverSpec, SolverSpec

# Try multiple random seeds, pick best
best_solution = None
best_objective = float("inf")

for seed in range(10):
    solver = ProblemSolver(service_name="rebalancer", service_scope="seed-search")
    # ... set up problem ...
    solver.add_solver(
        SolverSpec(
            localSearchSolverSpec=LocalSearchSolverSpec(randomSeed=seed)
        )
    )
    solution = solver.solve()

    objective = solution["finalObjective"]["value"]
    if objective < best_objective:
        best_objective = objective
        best_solution = solution

# best_solution is the best of 10 runs
```

## Common Patterns

### Fast Interactive Rebalancing

Quick responses for interactive tools:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/local_search_examples.py start=fast_interactive_start end=fast_interactive_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/local_search_examples.cpp start=fast_interactive_start end=fast_interactive_end
```

</TabItem>
</Tabs>

### Production Rebalancing

Balance speed and quality:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/local_search_examples.py start=production_rebalancing_start end=production_rebalancing_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/local_search_examples.cpp start=production_rebalancing_start end=production_rebalancing_end
```

</TabItem>
</Tabs>

### Offline Optimization

Take time to find best solution:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/local_search_examples.py start=offline_optimization_start end=offline_optimization_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/local_search_examples.cpp start=offline_optimization_start end=offline_optimization_end
```

</TabItem>
</Tabs>

### Draining Containers

Use [FixedSource](../move-types/fixed/fixed-source) move type for draining specific containers (e.g., with `ToFreeSpec`):

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
from rebalancer.specs import (
    LocalSearchSolverSpec,
    MoveTypeSpec,
    SingleFixedSourceMoveTypeSpec,
    SolverSpec,
)

# When draining containers, use FixedSource to only consider
# moves from the containers being drained.
solver.add_solver(
    SolverSpec(
        localSearchSolverSpec=LocalSearchSolverSpec(
            moveTypeList=[
                MoveTypeSpec(
                    singleFixedSourceMoveTypeSpec=SingleFixedSourceMoveTypeSpec()
                )
            ]
        )
    )
)
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
// When draining containers, use FixedSource
LocalSearchSolverSpec spec;
MoveTypeSpec fixedSourceSpec;
fixedSourceSpec.singleFixedSourceMoveTypeSpec_ref() = SingleFixedSourceMoveTypeSpec();
spec.moveTypeList_ref() = {fixedSourceSpec};
solver.addSolver(spec);
```

</TabItem>
</Tabs>

## Debugging Poor Results

### Problem: Solution not improving

**Diagnosis**:
```python
print(f"Iterations: {solution["profile"].iterations}")
print(f"Time: {solution["profile"].solveTime}ms")
print(f"Reason: {solution["profile"].terminationReason}")
```

**Solutions**:
- Increase time/move limits
- Check if initial assignment is extremely poor
- Verify goals/constraints are achievable

### Problem: Slow convergence

**Diagnosis**:
- Many iterations but small improvements
- Terminating due to limits, not local optimum

**Solutions**:
- Try different move types
- Improve initial assignment
- Simplify problem (fewer goals/constraints)

### Problem: Stuck in local optimum

**Diagnosis**:
- Terminates quickly with NO_IMPROVING_MOVE
- Objective value seems poor compared to expectations

**Solutions**:
- Use more powerful move types (TripleLoop, KLSearch)
- Try multiple runs with different seeds
- Use better initial assignment
- Consider using Optimal solver for comparison

### Problem: Too slow

**Diagnosis**:
- Taking too long for problem size
- Not enough iterations in time limit

**Solutions**:
- Use simpler move types (SingleMove only)
- Reduce time limit (accept worse solution)
- Simplify problem
- Check for performance bottlenecks in goals/constraints

## Comparison with Optimal Solver

| Aspect | Local Search | Optimal |
|--------|--------------|---------|
| **Solution quality** | 95-99% of optimal | 100% optimal* |
| **Speed** | Fast (seconds) | Slow (minutes to hours) |
| **Scalability** | 1M+ objects | &lt;10K objects |
| **Guarantees** | None | Optimality gap |
| **Determinism** | Random (without seed) | Deterministic |
| **Memory** | Low | High (can OOM) |

\* *Given enough time*

**Recommendation**: Use Local Search for production, Optimal for validation/small problems.

## Advanced: Multi-Stage Solving

Local Search supports multi-stage solving using `LocalSearchStageSolverSpec`, where each stage can use different move types and focus on different objectives:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
from rebalancer.specs import (
    LocalSearchSolverSpec,
    LocalSearchStageSolverSpec,
    LocalSearchStageSpec,
    MoveTypeSpec,
    SingleFastMoveTypeSpec,
    SingleMoveTypeSpec,
    SolverSpec,
)

# Stage 1: Fast coarse moves
stage1 = LocalSearchStageSpec(
    name="coarse",
    begin=0,
    end=2,  # Focus on objectives 0-1
    solverSpec=LocalSearchSolverSpec(
        moveTypeList=[
            MoveTypeSpec(singleFastMoveTypeSpec=SingleFastMoveTypeSpec())
        ],
        stopAfterMoves=10000,
    ),
)

# Stage 2: Fine-tuning with more expensive moves
stage2 = LocalSearchStageSpec(
    name="fine-tuning",
    begin=0,
    end=3,  # Focus on all objectives 0-2
    solverSpec=LocalSearchSolverSpec(
        moveTypeList=[MoveTypeSpec(singleMoveTypeSpec=SingleMoveTypeSpec())],
        stopAfterMoves=5000,
    ),
)

solver.add_solver(
    SolverSpec(
        localSearchStageSolverSpec=LocalSearchStageSolverSpec(
            stageSpecs=[stage1, stage2],
            solveTime=60000,  # Overall time limit
        )
    )
)
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
#include <rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h>

using namespace facebook::rebalancer::interface;

// Stage 1: Fast coarse moves
LocalSearchStageSpec stage1;
stage1.name_ref() = "coarse";
stage1.begin_ref() = 0;
stage1.end_ref() = 2;  // Focus on objectives 0-1

LocalSearchSolverSpec stage1Solver;
MoveTypeSpec fastMove;
fastMove.singleFastMoveTypeSpec_ref() = SingleFastMoveTypeSpec();
stage1Solver.moveTypeList_ref() = {fastMove};
stage1Solver.stopAfterMoves_ref() = 10000;
stage1.solverSpec_ref() = stage1Solver;

// Stage 2: Fine-tuning
LocalSearchStageSpec stage2;
stage2.name_ref() = "fine-tuning";
stage2.begin_ref() = 0;
stage2.end_ref() = 3;  // Focus on all objectives

LocalSearchSolverSpec stage2Solver;
MoveTypeSpec singleMove;
singleMove.singleMoveTypeSpec_ref() = SingleMoveTypeSpec();
stage2Solver.moveTypeList_ref() = {singleMove};
stage2Solver.stopAfterMoves_ref() = 5000;
stage2.solverSpec_ref() = stage2Solver;

// Multi-stage solver
LocalSearchStageSolverSpec multiStage;
multiStage.stageSpecs_ref() = {stage1, stage2};
multiStage.solveTime_ref() = 60000;  // Overall time limit
solver.addSolver(multiStage);
```

</TabItem>
</Tabs>

See [Solver Strategies](strategies) for multi-stage solving.

## Troubleshooting

### Problem: Non-deterministic results

**Cause**: Random seed not set

**Solution**:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/local_search_examples.py start=reproducible_results_start end=reproducible_results_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/local_search_examples.cpp start=reproducible_results_start end=reproducible_results_end
```

</TabItem>
</Tabs>

### Problem: Violating constraints

**Cause**: Constraints initially broken, being treated as goals

**Solution**: Check initial assignment, or increase constraint penalty:
```python
solver.add_constraint(spec, invalid_cost=1000.0)
```

### Problem: Different results each run

**Cause**: Default random seed varies

**Solution**: Set explicit seed for reproducibility

## Next Steps

- **Learn Optimal Solver**: [Optimal Solver Guide](optimal)
- **Performance Tuning**: [Performance Guide](performance)
- **Multi-Stage Solving**: [Solver Strategies](strategies)

## Related Documentation

- [Solver Overview](overview) - Choosing between solvers
- [Performance Guide](performance) - Tuning for speed
