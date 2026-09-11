---
sidebar_position: 2
---

# Local Search Solver

## Description

This is the default solver used when unspecified. It's a greedy solver which optimizes the problem by making incremental local improvements to the initial solution.

At a high level it can be described as an iterative algorithm where each iteration does the following:

1. Evaluate several small changes to the current solution. A typical small change is moving a single object to a different container, and the specific behavior is governed by the [move types](move-types/move-types.md).
2. Pick the best evaluated change and, if it improves the objective, transform the current solution by applying the change.
3. Rinse and repeat a new iteration from step 1 until no further progress is possible (a local optimum has been found) or the time limit is hit.

A more detailed description of the algorithm and pseudo-code can be found in the [common logic of local search](move-types/move-types.md#common-logic).

For finer-grained control of the heuristic, see [Local Search With Stages](local-search-with-stages.md), a variant that splits the optimization into separate stages, each focused on different goals of a [goal tuple](../../reference/goal-priorities.md#tuples-strict-priority).

## Pros

- One of the most scalable solvers available, as it can deal with fairly large problems (100,000s of objects).
- Fully implemented in-house by the Rebalancer team, giving engineers full freedom to evolve it as needed.
- Highly customizable with a variety of [parameters](#parameters) and [move types](move-types/move-types.md). Adjustable tradeoff between speed and quality.

## Cons

- The local optimum problem. Although in most practical cases this algorithm finds reasonably good solutions, it doesn't guarantee an optimal solution. The algorithm will get stuck at clearly sub-optimal or bad solutions when the objective is not smooth (in other words, when it requires complex moves in order to make progress, as opposed to making progress with small incremental changes). This bad behavior is prevalent, for example, when a problem has non-continuous constraints such as "container A must contain either 5 or 8 objects exactly". Oftentimes, new [move types](move-types/move-types.md) can be implemented to address the local optimum issue in a particular use case.

## Code examples

### C++

```cpp
auto solver = ProblemSolverFactory::makeProblemSolver();

// Use the single move type and limit run time to 5 minutes.
LocalSearchSolverSpec localSearch;
localSearch.moveTypeList()->push_back(
    ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
localSearch.solveTime() = 300;

solver->addSolver(localSearch);
```

### Python

```python
from rebalancer import ProblemSolver
from rebalancer.specs import (
    LocalSearchSolverSpec,
    MoveTypeSpec,
    SingleMoveTypeSpec,
    SolverSpec,
)

solver = ProblemSolver()

# Use the single move type and limit run time to 5 minutes.
solver.add_solver(
    SolverSpec(
        localSearchSolverSpec=LocalSearchSolverSpec(
            moveTypeList=[MoveTypeSpec(singleMoveTypeSpec=SingleMoveTypeSpec())],
            solveTime=300,
        )
    )
)
```

## Parameters

Local search is highly configurable by setting different parameters of [`LocalSearchSolverSpec`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/interface/thrift/SolverSpecs.thrift):

- **`solveTime` (optional):** amount of time in seconds the solver may take before giving a solution. The solver may find a locally optimal solution before `solveTime` is reached, and return early. Otherwise, the solver will stop searching as soon as `solveTime` is reached and return the best solution found so far. If left unset, the run time is not bounded and the solver will stop only when a local optimum is found.
- **`stopAfterMoves` (optional):** the solver will stop and return a solution as soon as it has incrementally applied this number of changes on top of the initial solution. If unset, then the number of changes local search may perform is not bounded.
- **`timePerMove` (optional):** amount of time the algorithm may spend evaluating changes for the same combination of `{source container, move type}` within a single iteration of local search. Learn more about source container selection and move type invocation in the [common logic of local search](move-types/move-types.md#common-logic).
- **`moveTypeList` (required):** the [move types](move-types/move-types.md) local search may use, given as a list of move type specs (a union). This controls which neighboring solutions will be evaluated within a single iteration of local search before picking the best neighbor and continuing the search from there in the next iteration. The list must contain at least one move type---an empty list is rejected. A good starting set is [single](move-types/single.md), [swap](move-types/swap.md), and [triple loop](move-types/triple-loop.md). If the move types chosen are too little exhaustive, local search may make little progress per iteration or get easily stuck at a local optimum. On the other hand, if the move types are too exhaustive, it will be computationally expensive to compute a single iteration and take a long time to reach a good solution.
- **`objectOrderingDimension` (optional):** by default, once local search selects a source container to move objects from, the objects within that container are not prioritized in any particular way, and their moves out of the container get evaluated in an arbitrary order. This option lets you override that behavior by providing an explicit evaluation priority for objects within the source container. You must define a dimension on the objects where the value determines the priority (objects with a higher value will be evaluated before others), and objects whose value is not explicitly given will get the lowest priority possible.
- **`enableConstrainedBoundsOptimization` (default: false):** when set to true, it enables an experimental feature that in some cases can detect when a local optimum has been reached by comparing the numeric bounds against current values of nodes in the objective formula, returning early and saving unnecessary evaluation work when a local optimum is detected. Enabling this feature does not reduce the quality of the solution found given enough time, and it typically makes local search faster in cases where optimality can easily be detected. In other cases, local search may become a bit slower due to the extra optimality checks.
- **`constrainedBoundsCheckMs` (default: 0):** the number of constrained bounds optimization checks will be limited to one within the same window of this amount of milliseconds.
- **`objectivesForHottestContainers` (optional):** when using goal tuples, setting this parameter will limit the objectives which the [source container sorting logic](move-types/move-types.md#common-logic) looks at to only the first `objectivesForHottestContainers` items in the tuple.
- **`allowedPlateauTime` (optional):** the algorithm will stop after spending this amount of time in seconds without finding any improvements to the current solution.

[Move type](move-types/move-types.md)-specific parameters:

- **`stratifiedSampleSize` (default: 10):** total number of containers in a stratified sample. It applies to the [single random stratified](move-types/single-random-stratified.md) move type.
- **`minHotObjects` (default: 1):** minimum number of objects in the source container moves have to be evaluated for before settling on a move which improves the objective. If no improvements are found after evaluating `minHotObjects` objects in the source container, more objects will be evaluated until an improvement is found or until there are no more objects left. It applies to the [single coldest stratified](move-types/single-coldest-stratified.md) move type. [Single fast](move-types/single-fast.md) and [single random stratified](move-types/single-random-stratified.md) have their own equivalent on their move type spec.
- **`randomSeed` (default: 0):** generic seed to randomize arbitrary decisions within local search. Currently it only serves the purpose of determining the relative order of source containers that are equally promising.
