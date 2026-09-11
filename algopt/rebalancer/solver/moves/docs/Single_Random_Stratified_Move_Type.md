**Name:** SINGLE_RANDOM_STRATIFIED

Evaluates moving an object to a set of sampled containers.

## **Parameters**
* **DestinationsToExploreOptions:** Sampling will happen on a per scope item basis. This will help reduce checking invalid containers.
* **SampleSize:** Sets the sample size of the sampling algorithm. This will be evenly distributed by scope items.
* **minObjectsToExplore:** the minimum number of objects to fully explore before returning the best move found so far. More objects will be evaluated if no move has yet been found that improves the objective. (Default: 1)

## **Behavior**
Given an object and an objective, rebalancer will select a random sample of containers and try to place that object to those containers to see if the objective is satisfied. By sampling containers with similar properties, we can reduce the number of containers.

### Example:
Suppose we have n = 30000 containers, all with varied container dimensions, we can sort containers by dimension size and then divide them into Y = 5 scope items (1/5 of n)

```
+---------------+  +---------------+  +---------------+  +---------------+  +---------------+
|               |  |               |  |               |  |               |  |               |
| Very small    |  | Small         |  | Medium sized  |  | Large         |  | Very large    |
| containers    |  | containers    |  | container     |  | containers    |  | containers    |
|               |  |               |  |               |  |               |  |               |
+---------------+  +---------------+  +---------------+  +---------------+  +---------------+
  Scope Item 1       Scope Item 2       Scope Item 3       Scope Item 4      Scope Item 5
```
Then we can take X = 100 sample size for each scope item and try all possible moves to find the "best container" within the sample space.

## **Complexity**
By doing this, we drastically reduce search space from n = 30000 to X*Y = 100 * 5 = 500

## **Example Usage**
```cpp
interface::MoveToCurrentScopeItemSpec moveToCurrentScopeItemSpec;
  moveToCurrentScopeItemSpec.scopeNameForExploringMovesToCurrentScopeItem() =
      "region";
  interface::DestinationsToExploreOptions destinationsToExplore;
  destinationsToExplore.set_moveToCurrentScopeItem() =
      moveToCurrentScopeItemSpec;
  interface::SingleRandomStratifiedMoveTypeSpec singleRandomSpec;
  singleRandomSpec.destinationsToExplore() = destinationsToExplore;

  interface::SampleSize sampleSize;
  sampleSize.defaultSampleSize() = 3;
  singleRandomSpec.stratifiedSampleSize() = std::move(sampleSize);

  interface::LocalSearchSolverSpec solverSpec;
  solverSpec.singleRandomStratifiedMoveTypeSpec() = singleRandomSpec;
```

## **References**
* https://www.internalfb.com/code/fbsource/fbcode/rebalancer/packer/solver/moves/SingleRandomStratifiedMoveType.h
