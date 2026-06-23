---
sidebar_position: 5
---

# Solver Strategies

Advanced techniques for combining and configuring solvers to get the best results.

## Multi-Solver Strategies

You can run multiple solvers sequentially, each building on the previous solution.

### Strategy 1: Local Search → Optimal

Get quick solution, then refine:

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/strategy_examples.py start=sequential_solvers_start end=sequential_solvers_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/strategy_examples.cpp start=sequential_solvers_start end=sequential_solvers_end
```

</TabItem>
</Tabs>

**When to use**:
- Medium problems (100-1K objects)
- Need good solution quickly, best solution eventually
- Can afford 1-2 minutes total

**Benefits**:
- Local Search finds feasible solution in seconds
- Optimal refines from good starting point (faster)
- Get intermediate results if Optimal times out

### Strategy 2: Multiple Local Search Runs

Try multiple random seeds, pick best:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
from rebalancer import ProblemSolver
from rebalancer.specs import LocalSearchSolverSpec, SolverSpec

best_solution = None
best_objective = float("inf")

for seed in range(10):
    solver = ProblemSolver(service_name="test", service_scope="demo")
    # ... set up problem ...

    solver.add_solver(
        SolverSpec(
            localSearchSolverSpec=LocalSearchSolverSpec(
                solveTime=30,  # seconds
                randomSeed=seed,
            )
        )
    )

    solution = solver.solve()
    objective = solution["finalObjective"]["value"]
    if objective < best_objective:
        best_objective = objective
        best_solution = solution

# best_solution is best of 10 runs
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
Solution* best_solution = nullptr;
double best_objective = std::numeric_limits<double>::infinity();

for (int seed = 0; seed < 10; ++seed) {
    ProblemSolver solver("test", "demo");
    // ... set up problem ...

    LocalSearchSolverSpec spec;
    spec.timeLimitMs = 30000;
    spec.seed = seed;
    solver.addSolver(spec);

    auto solution = solver.solve();
    if (solution.objectiveValue < best_objective) {
        best_objective = solution.objectiveValue;
        best_solution = new Solution(solution);
    }
}

// best_solution is best of 10 runs
```

</TabItem>
</Tabs>

**When to use**:
- Local Search getting stuck in poor local optima
- Have time for multiple runs
- Want to improve solution quality without Optimal

**Benefits**:
- Different seeds explore different search paths
- Better chance of finding good local optimum
- Still fast (parallelizable)

### Strategy 3: Incremental Solving

Solve progressively:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
from rebalancer import ProblemSolver
from rebalancer.specs import (
    GoalSpec,
    LocalSearchSolverSpec,
    MinimizeMovementSpec,
    SolverSpec,
)

# Phase 1: Satisfy constraints (ignore goals)
solver.add_goal(
    GoalSpec(minimizeMovementSpec=MinimizeMovementSpec(name="stabilize")),
    weight=1.0,
)  # Just stabilize
solver.add_solver(
    SolverSpec(localSearchSolverSpec=LocalSearchSolverSpec(solveTime=5))
)
phase1 = solver.solve()

# Phase 2: Optimize goals from feasible solution
solver2 = ProblemSolver(service_name="rebalancer", service_scope="phase2")
# Build a container -> [object, object, ...] map from the assignment dict.
container_to_objects: dict[str, list[str]] = {}
for obj, container in phase1["assignment"].items():
    container_to_objects.setdefault(container, []).append(obj)
solver2.set_assignment(container_to_objects)
# ... add real goals ...
solver2.add_solver(
    SolverSpec(localSearchSolverSpec=LocalSearchSolverSpec(solveTime=30))
)
phase2 = solver2.solve()
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
// Phase 1: Satisfy constraints (ignore goals)
MinimizeMovementSpec movement_spec;
solver.addGoal(movement_spec, 1.0);  // Just stabilize
LocalSearchSolverSpec spec1;
spec1.timeLimitMs = 5000;
solver.addSolver(spec1);
auto phase1 = solver.solve();

// Phase 2: Optimize goals from feasible solution
ProblemSolver solver2(...);
solver2.setAssignment(phase1.assignment);  // Start from phase 1
// ... add real goals ...
LocalSearchSolverSpec spec2;
spec2.timeLimitMs = 30000;
solver2.addSolver(spec2);
auto phase2 = solver2.solve();
```

</TabItem>
</Tabs>

**When to use**:
- Initial assignment heavily violates constraints
- Want to ensure feasibility first
- Complex multi-objective problems

## Multi-Stage Local Search

Configure different move types for different stages using `LocalSearchStageSolverSpec`:

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
    SwapMoveTypeSpec,
)

# Stage 1: Fast coarse moves for quick improvements
stage1 = LocalSearchStageSpec(
    name="coarse",
    begin=0,
    end=1,  # Focus on first objective
    solverSpec=LocalSearchSolverSpec(
        moveTypeList=[
            MoveTypeSpec(singleFastMoveTypeSpec=SingleFastMoveTypeSpec()),
        ],
        stopAfterMoves=50000,
    ),
)

# Stage 2: More thorough moves for refinement
stage2 = LocalSearchStageSpec(
    name="fine-tuning",
    begin=0,
    end=2,  # Expand to more objectives
    solverSpec=LocalSearchSolverSpec(
        moveTypeList=[
            MoveTypeSpec(singleMoveTypeSpec=SingleMoveTypeSpec()),
            MoveTypeSpec(swapMoveTypeSpec=SwapMoveTypeSpec()),
        ],
        stopAfterMoves=20000,
    ),
)

solver.add_solver(
    SolverSpec(
        localSearchStageSolverSpec=LocalSearchStageSolverSpec(
            stageSpecs=[stage1, stage2],
            solveTime=120000,  # Overall 2 minute limit
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
stage1.end_ref() = 1;  // Focus on first objective

LocalSearchSolverSpec stage1Solver;
MoveTypeSpec fastMove;
fastMove.singleFastMoveTypeSpec_ref() = SingleFastMoveTypeSpec();
stage1Solver.moveTypeList_ref() = {fastMove};
stage1Solver.stopAfterMoves_ref() = 50000;
stage1.solverSpec_ref() = stage1Solver;

// Stage 2: Fine-tuning
LocalSearchStageSpec stage2;
stage2.name_ref() = "fine-tuning";
stage2.begin_ref() = 0;
stage2.end_ref() = 2;

LocalSearchSolverSpec stage2Solver;
MoveTypeSpec singleMove, swapMove;
singleMove.singleMoveTypeSpec_ref() = SingleMoveTypeSpec();
swapMove.swapMoveTypeSpec_ref() = SwapMoveTypeSpec();
stage2Solver.moveTypeList_ref() = {singleMove, swapMove};
stage2Solver.stopAfterMoves_ref() = 20000;
stage2.solverSpec_ref() = stage2Solver;

// Multi-stage solver
LocalSearchStageSolverSpec multiStage;
multiStage.stageSpecs_ref() = {stage1, stage2};
multiStage.solveTime_ref() = 120000;  // Overall 2 minute limit
solver.addSolver(multiStage);
```

</TabItem>
</Tabs>

**When to use**:
- Very large problems
- Want to balance speed and quality
- Different move types effective at different stages

## Problem Decomposition

For very large problems, solve in parts:

### Spatial Decomposition

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
# Split by datacenter
for dc in datacenters:
    dc_objects = objects_in_dc(dc)
    dc_containers = containers_in_dc(dc)

    solver = ProblemSolver(service_name="rebalancer", service_scope=dc)
    # Solve just this datacenter
    solver.set_assignment(dc_assignment)
    # ... goals/constraints for this DC ...
    dc_solution = solver.solve()

    # Merge into global solution
    global_solution.update(dc_solution["assignment"])
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
// Split by datacenter
for (const auto& dc : datacenters) {
    auto dc_objects = objectsInDc(dc);
    auto dc_containers = containersInDc(dc);

    ProblemSolver solver(...);
    // Solve just this datacenter
    solver.setAssignment(dc_assignment);
    // ... goals/constraints for this DC ...
    auto dc_solution = solver.solve();

    // Merge into global solution
    global_solution.update(dc_solution.assignment);
}
```

</TabItem>
</Tabs>

**When to use**:
- 100K+ objects
- Natural partitioning (datacenters, regions)
- Local constraints dominate global ones

### Hierarchical Decomposition

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
# Level 1: Assign objects to datacenters (coarse)
dc_solver = ProblemSolver(service_name="rebalancer", service_scope="dc-level")
# Treat each DC as one container
dc_solution = dc_solver.solve()

# Level 2: Within each DC, assign to racks (fine)
for dc in datacenters:
    rack_solver = ProblemSolver(
        service_name="rebalancer", service_scope=f"rack-level/{dc}"
    )
    # Solve rack-level placement
    rack_solution = rack_solver.solve()
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
// Level 1: Assign objects to datacenters (coarse)
ProblemSolver dc_solver(...);
// Treat each DC as one container
auto dc_solution = dc_solver.solve();

// Level 2: Within each DC, assign to racks (fine)
for (const auto& dc : datacenters) {
    ProblemSolver rack_solver(...);
    // Solve rack-level placement
    auto rack_solution = rack_solver.solve();
}
```

</TabItem>
</Tabs>

**When to use**:
- Hierarchical infrastructure
- Different objectives at different levels
- Very large problems

## Time-Based Strategies

### Anytime Algorithm

Return best solution found so far:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
import threading
import time

from rebalancer.specs import LocalSearchSolverSpec, SolverSpec

best_solution = None

def solve_background():
    global best_solution
    # Run with long time limit
    solver.add_solver(
        SolverSpec(
            localSearchSolverSpec=LocalSearchSolverSpec(solveTime=3600)  # 1 hour
        )
    )
    best_solution = solver.solve()

# Start solving in background
solver_thread = threading.Thread(target=solve_background)
solver_thread.start()

# Check progress periodically
for i in range(60):  # Check for 1 minute
    time.sleep(1)
    if best_solution is not None:
        print(f"Current best: {best_solution['finalObjective']['value']}")

# Use best solution so far (even if not finished)
current_best = best_solution
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
#include <thread>
#include <chrono>
#include <atomic>
#include <iostream>

std::atomic<Solution*> best_solution{nullptr};

void solveBackground(ProblemSolver& solver) {
    // Run with long time limit
    LocalSearchSolverSpec spec;
    spec.timeLimitMs = 3600000;  // 1 hour
    solver.addSolver(spec);
    auto solution = solver.solve();
    best_solution.store(new Solution(solution));
}

// Start solving in background
std::thread solver_thread(solveBackground, std::ref(solver));

// Check progress periodically
for (int i = 0; i < 60; ++i) {  // Check for 1 minute
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (best_solution.load() != nullptr) {
        std::cout << "Current best: " << best_solution.load()->objectiveValue << "\n";
    }
}

// Use best solution so far (even if not finished)
auto* current_best = best_solution.load();
solver_thread.join();
```

</TabItem>
</Tabs>

**When to use**:
- Interactive systems
- Want intermediate results
- Uncertain how long to wait

### Progressive Refinement

Start with relaxed problem, progressively tighten:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
from rebalancer import ProblemSolver
from rebalancer.specs import CapacitySpec, ConstraintSpec, GoalSpec

# Round 1: Relax constraints, find any solution
solver1 = ProblemSolver(service_name="rebalancer", service_scope="round1")
# Use constraints as goals (soft)
solver1.add_goal(
    GoalSpec(
        capacitySpec=CapacitySpec(name="cap", scope="host", dimension="cpu")
    ),
    weight=100.0,
)
solution1 = solver1.solve()

# Round 2: Tighten from round 1 solution
solver2 = ProblemSolver(service_name="rebalancer", service_scope="round2")
container_to_objects: dict[str, list[str]] = {}
for obj, container in solution1["assignment"].items():
    container_to_objects.setdefault(container, []).append(obj)
solver2.set_assignment(container_to_objects)
# Now use as hard constraint
solver2.add_constraint(
    ConstraintSpec(
        capacitySpec=CapacitySpec(name="cap", scope="host", dimension="cpu")
    )
)
solution2 = solver2.solve()
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
// Round 1: Relax constraints, find any solution
ProblemSolver solver1(...);
// Use constraints as goals (soft)
CapacitySpec cap_spec(...);
solver1.addGoal(cap_spec, 100.0);
auto solution1 = solver1.solve();

// Round 2: Tighten from round 1 solution
ProblemSolver solver2(...);
solver2.setAssignment(solution1.assignment);
// Now use as hard constraint
CapacitySpec cap_constraint(...);
solver2.addConstraint(cap_constraint);
auto solution2 = solver2.solve();
```

</TabItem>
</Tabs>

**When to use**:
- Hard to find initial feasible solution
- Complex constraints
- Incremental improvement acceptable

## Goal Priority Strategies

### Lexicographic (Tuple) Approach

Strict priority ordering:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/strategy_examples.py start=goal_prioritization_start end=goal_prioritization_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/strategy_examples.cpp start=goal_prioritization_start end=goal_prioritization_end
```

</TabItem>
</Tabs>

**When to use**:
- Clear priority ordering
- Need to balance multiple objectives
- Both Local Search and Optimal

### Constraint vs Goal

Use constraints for hard requirements, goals for preferences:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python file=algopt/rebalancer/examples/website/solvers/strategy_examples.py start=constraint_vs_goal_start end=constraint_vs_goal_end
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp file=algopt/rebalancer/examples/website/solvers/strategy_examples.cpp start=constraint_vs_goal_start end=constraint_vs_goal_end
```

</TabItem>
</Tabs>

**When to use**:
- Goals can be traded off
- Quantifiable relative importance
- Both Local Search and Optimal

### Iterative Weight Tuning

Find good weights empirically:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
weights = [
    (10.0, 1.0),  # Balance:Movement ratio
    (5.0, 1.0),
    (20.0, 1.0),
]

for balance_weight, movement_weight in weights:
    solver = ProblemSolver(...)
    solver.add_goal(BalanceSpec(...), weight=balance_weight)
    solver.add_goal(MinimizeMovementSpec(), weight=movement_weight)
    solver.add_solver(LocalSearchSolverSpec(timeLimitMs=10000))

    solution = solver.solve()
    print(f"Weights ({balance_weight}, {movement_weight}): {solution["objectiveValue"]}")

# Pick best weights from results
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
std::vector<std::pair<double, double>> weights = {
    {10.0, 1.0},  // Balance:Movement ratio
    {5.0, 1.0},
    {20.0, 1.0},
};

for (const auto& [balance_weight, movement_weight] : weights) {
    ProblemSolver solver(...);
    BalanceSpec balance_spec(...);
    solver.addGoal(balance_spec, balance_weight);
    MinimizeMovementSpec movement_spec;
    solver.addGoal(movement_spec, movement_weight);

    LocalSearchSolverSpec spec;
    spec.timeLimitMs = 10000;
    solver.addSolver(spec);

    auto solution = solver.solve();
    std::cout << "Weights (" << balance_weight << ", " << movement_weight
              << "): " << solution.objectiveValue << "\n";
}

// Pick best weights from results
```

</TabItem>
</Tabs>

## Warmstart Strategies

### From Current Assignment

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
# Use current production assignment as starting point
solver.set_assignment(current_production_assignment)
solver.add_solver(LocalSearchSolverSpec())
# Starts from current state, makes minimal changes
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
// Use current production assignment as starting point
solver.setAssignment(current_production_assignment);
LocalSearchSolverSpec spec;
solver.addSolver(spec);
// Starts from current state, makes minimal changes
```

</TabItem>
</Tabs>

**When to use**:
- Incremental rebalancing
- Want minimal disruption
- Current assignment mostly good

### From Simple Heuristic

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
# Generate reasonable initial assignment
initial = distribute_evenly(objects, containers)
solver.set_assignment(initial)
solver.add_solver(LocalSearchSolverSpec())
# Better starting point than random
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
// Generate reasonable initial assignment
auto initial = distributeEvenly(objects, containers);
solver.setAssignment(initial);
LocalSearchSolverSpec spec;
solver.addSolver(spec);
// Better starting point than random
```

</TabItem>
</Tabs>

**When to use**:
- No current assignment
- Want better-than-random start
- Simple heuristic available

### From Previous Solution

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
# Cache previous solution
previous_solution = cache.get("last_solution")

if previous_solution:
    solver.set_assignment(previous_solution.assignment)

solver.add_solver(LocalSearchSolverSpec())
solution = solver.solve()

cache.set("last_solution", solution)
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
// Cache previous solution
auto previous_solution = cache.get("last_solution");

if (previous_solution) {
    solver.setAssignment(previous_solution->assignment);
}

LocalSearchSolverSpec spec;
solver.addSolver(spec);
auto solution = solver.solve();

cache.set("last_solution", solution);
```

</TabItem>
</Tabs>

**When to use**:
- Repeated solving (e.g., continuous rebalancing)
- Similar problems over time
- Want consistency between runs

## Parallel Solving Strategies

### Solve Multiple Variants

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
from concurrent.futures import ThreadPoolExecutor

def solve_variant(variant_id):
    solver = ProblemSolver(...)
    # ... set up problem with variant ...
    solver.add_solver(LocalSearchSolverSpec(seed=variant_id))
    return solver.solve()

# Solve 10 variants in parallel
with ThreadPoolExecutor(max_workers=10) as executor:
    solutions = list(executor.map(solve_variant, range(10)))

# Pick best
best = min(solutions, key=lambda s: s.objectiveValue)
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
#include <future>
#include <vector>
#include <algorithm>

auto solve_variant(int variant_id) {
    ProblemSolver solver(...);
    // ... set up problem with variant ...
    LocalSearchSolverSpec spec;
    spec.seed = variant_id;
    solver.addSolver(spec);
    return solver.solve();
}

// Solve 10 variants in parallel
std::vector<std::future<Solution>> futures;
for (int i = 0; i < 10; ++i) {
    futures.push_back(std::async(std::launch::async, solve_variant, i));
}

std::vector<Solution> solutions;
for (auto& future : futures) {
    solutions.push_back(future.get());
}

// Pick best
auto best = std::min_element(solutions.begin(), solutions.end(),
    [](const Solution& a, const Solution& b) {
        return a.objectiveValue < b.objectiveValue;
    });
```

</TabItem>
</Tabs>

**When to use**:
- Multiple cores available
- Want best of multiple runs
- Variants are independent

### Portfolio Approach

Different solvers/configurations in parallel:

<Tabs groupId="programming-language">
<TabItem value="python" label="Python">

```python
def solve_local_search():
    solver = ProblemSolver(...)
    solver.add_solver(LocalSearchSolverSpec(timeLimitMs=60000))
    return solver.solve()

def solve_optimal():
    solver = ProblemSolver(...)
    solver.add_solver(OptimalSolverSpec(timeLimitMs=60000))
    return solver.solve()

# Run both, use whichever finishes first with best solution
with ThreadPoolExecutor(max_workers=2) as executor:
    futures = [
        executor.submit(solve_local_search),
        executor.submit(solve_optimal)
    ]
    # Get first to complete
    done, pending = wait(futures, return_when=FIRST_COMPLETED)
    # Or wait for all and pick best
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
#include <future>
#include <vector>

auto solve_local_search() {
    ProblemSolver solver(...);
    LocalSearchSolverSpec spec;
    spec.timeLimitMs = 60000;
    solver.addSolver(spec);
    return solver.solve();
}

auto solve_optimal() {
    ProblemSolver solver(...);
    OptimalSolverSpec spec;
    spec.timeLimitMs = 60000;
    solver.addSolver(spec);
    return solver.solve();
}

// Run both, use whichever finishes first with best solution
auto future1 = std::async(std::launch::async, solve_local_search);
auto future2 = std::async(std::launch::async, solve_optimal);

// Wait for both and pick best
auto solution1 = future1.get();
auto solution2 = future2.get();

auto best = (solution1.objectiveValue < solution2.objectiveValue)
    ? solution1 : solution2;
```

</TabItem>
</Tabs>

## Next Steps

- **Learn Local Search**: [Local Search Guide](local-search)
- **Learn Optimal**: [Optimal Solver Guide](optimal)
- **Performance Tuning**: [Performance Guide](performance)
- **See Examples**: [Cookbook](../cookbook/)

## Related Documentation

- [Solver Overview](overview) - Choosing solvers
- [Goals vs Constraints](../core-concepts/overview#goals-and-constraints) - Priority strategies
