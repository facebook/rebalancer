---
sidebar_position: 15
---

# Single Random Stratified

Evaluates moving an object to a **sample of destination containers** rather than to
every container. Containers are grouped into similarity classes (scope items), and
the sample is drawn evenly from those classes, drastically reducing the destination
search space when there are very many containers.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `stratifiedSampleSize` | SampleSize | Yes | - | Number of destination containers to sample, distributed evenly across the similarity classes |
| `destinationsToExplore` | DestinationsToExploreOptions | No | all scope items | How destination containers are grouped/sampled (e.g. per scope item) |
| `minObjectsToExplore` | int | No | 1 | Minimum number of objects to fully explore before returning the best improving move found so far |

## Behavior

Given an object to move, the move type samples destination containers from each
similarity class (scope item) and evaluates moving the object to the sampled
containers, keeping the best move. Sampling a fixed number per class keeps the number
of evaluated moves small even when there are tens of thousands of containers.

Once `minObjectsToExplore` objects have been explored and an improving move has been
found, that move is returned; otherwise more objects are explored until one improves
the objective or the source container is exhausted. Raising it improves move quality
at the cost of roughly `minObjectsToExplore * stratifiedSampleSize` evaluations per
move.

## Example

Configure local search to use only the single random stratified move type, sampling
100 destination containers:

```cpp
SingleRandomStratifiedMoveTypeSpec stratified;

SampleSize sampleSize;
sampleSize.defaultSampleSize() = 100;
stratified.stratifiedSampleSize() = sampleSize;

LocalSearchSolverSpec localSearch;
localSearch.moveTypeList()->push_back(
    ProblemSolver::makeMoveTypeSpec(stratified));

solver.addSolver(localSearch);
```

([source](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/solver/moves/tests/MoveTypeFactoryTest.cpp#L1004-L1031))
