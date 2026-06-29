---
sidebar_position: 1
---

# Goals and Constraints

Rebalancer lets you express what you want from an assignment as **goals** and
**constraints**, built from a library of 25+ reusable specs. Some specs work only as
a goal, some only as a constraint, and many as either.

- A **constraint** defines what makes an assignment valid; it must hold.
- A **goal** is a quantity to minimize; with several goals, their relative
  priority decides how they trade off.

To add them to the solver, use `addConstraint` and `addGoal`. Only the spec is
required; the remaining parameters are optional:

```cpp
solver.addConstraint(spec, policy, invalidCost, invalidState, tuplePosIfBroken);
solver.addGoal(spec, weight, tuplePos);
```

- For a constraint, `policy` and the `invalidCost` / `invalidState` /
  `tuplePosIfBroken` tuning control its [constraint policy](constraint-policy):
  how a constraint that is already broken in the initial assignment is treated
  (fixed best-effort by default, or made strictly hard or soft).
- For a goal, `weight` and `tuplePos` control its [goal priorities](goal-priorities):
  how it trades off against other goals.

## Available specs

Where a spec has its own page, its name links to it. The **Type** column shows
whether a spec can be used as a goal, a constraint, or either. **Examples** links
to the spec's unit tests.

### Capacity and limits

| Spec | Type | Description | Examples |
|------|------|-------------|----------|
| [CapacitySpec](specs/capacity) | Both | Limit each scope item's utilization for a dimension to a max (or min) | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/CapacityTest.cpp) |
| ToFreeSpec | Both | Drain the listed containers, driving their utilization for a dimension to zero | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/ToFreeTest.cpp) |
| GroupCountSpec | Both | Limit how many objects of a group a scope item holds, or their total for a dimension | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/GroupCountTest.cpp) |
| GroupDiversitySpec | Both | Require each scope item to hold at least, or at most, N distinct groups | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/GroupDiversityTest.cpp) |
| CapacityWithGroupPresenceSpec | Both | Limit each scope item's utilization, with every present group counting for at least a minimum weight | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/CapacityWithGroupPresenceTest.cpp) |
| GroupCapacitySpec | Both | Limit each group's total utilization across all scope items in a scope | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/GroupCapacitySpecTest.cpp) |
| GroupIsolationLimitSpec | Both | Limit how many groups may exceed their utilization limit in the same scope item (default 1) | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/GroupIsolationLimitSpecTest.cpp) |
| DisasterRecoveryCapacitySpec | Both | Reserve enough spare capacity so scope items can survive correlated failure scenarios | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/DisasterRecoveryCapacitySpecTest.cpp) |

### Balancing and packing

| Spec | Type | Description | Examples |
|------|------|-------------|----------|
| BalanceSpec | Goal | Balance a dimension's utilization evenly across scope items | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/BalanceTest.cpp) |
| UtilIncreaseCostSpec | Goal | Penalize raising a scope item's utilization above a lower bound | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/UtilIncreaseCostTest.cpp) |
| MinimizeContainersSpec | Goal | Minimize the number of scope items used | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/MinimizeContainersTest.cpp) |
| MaximizeAllocationSpec | Goal | Maximize utilization on a set of scope items | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/MaximizeAllocationTest.cpp) |
| MinimizeNthLargestUtilizationSpec | Goal | Minimize the n-th largest scope-item utilization (n is 0-based) | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/MinimizeNthLargestUtilizationTest.cpp) |
| MinimizeSquaresSpec | Goal | Minimize the sum of squared scope-item utilizations | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/MinimizeSquaresTest.cpp) |
| WorkingSetSpec | Goal | Minimize the average or maximum working-set size across scope items | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/WorkingSetTest.cpp) |

### Placement

| Spec | Type | Description | Examples |
|------|------|-------------|----------|
| NonAcceptingSpec | Constraint | Prevent objects from moving into the listed scope items | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/NonAcceptingTest.cpp) |
| AvoidAssignmentsSpec | Constraint | Forbid specific object-to-scope-item assignments | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/AvoidAssignmentsTest.cpp) |
| ColocateGroupsSpec | Both | Limit how many scope items each group spreads across (default: one) | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/ColocateGroupsTest.cpp) |
| ExclusiveScopeItemsSpec | Both | Forbid pairs of scope items from being used at the same time | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/ExclusiveScopeItemsTest.cpp) |

### Movement

| Spec | Type | Description | Examples |
|------|------|-------------|----------|
| AvoidMovingSpec | Constraint | Prevent the listed objects from moving | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/AvoidMovingTest.cpp) |
| GroupMoveLimitSpec | Constraint | Limit how many objects of a group may move | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/GroupMoveLimitSpecTest.cpp) |
| MovesInProgressSpec | Constraint | Account for objects already moving between containers | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/MovesInProgressTest.cpp) |
| MinimizeMovementSpec | Both | Minimize the number (or dimension-weighted amount) of objects moved | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/MinimizeMovementTest.cpp) |

### Affinities

| Spec | Type | Description | Examples |
|------|------|-------------|----------|
| AssignmentAffinitiesSpec | Goal | Make specific objects prefer specific scope items | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/AssignmentAffinitiesTest.cpp) |
| PairAffinitiesSpec | Both | Make pairs of objects prefer the same scope item | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/PairAffinitiesTest.cpp) |
| GroupAssignmentAffinitiesSpec | Goal | Make specific groups prefer specific scope items | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/GroupAssignmentAffinitiesTest.cpp) |
| ScopeAffinitiesSpec | Goal | Reward or penalize each scope item's utilization of a dimension via a per-scope-item weight | [unit tests](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/ScopeAffinitiesTest.cpp) |
