# MovesInProgress

**Type**: Constraint

Force a set of objects to specific destination containers, modeling moves already
underway in the real system. Rebalancer honors these moves, and every other goal
and constraint accounts for them. For example, if the live system is already
migrating a task to another host, pin that destination so Rebalancer plans around
it.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | No | `""` | Descriptive name for logging/debugging. |
| `moves` | list of [MoveInProgress](#moveinprogress-entries) | Yes | empty | The forced (object, destination container) moves. |

### MoveInProgress entries

Each `MoveInProgress` forces one object to a destination container:

| Field | Type | Description |
|-------|------|-------------|
| `objName` | string | The object being moved |
| `toContainer` | string | The container it must end up in |

## Example

An example use: four tasks across two hosts, with `task2` already moving from
`host0` to `host1`. The constraint forces that move; nothing else changes.

```cpp
solver.setObjectName("task");
solver.setContainerName("host");

solver.setAssignment(std::map<std::string, std::vector<std::string>>{
    {"host0", {"task0", "task1", "task2"}},
    {"host1", {"task3"}},
});

// task2 is already moving from host0 to host1.
MoveInProgress move;
move.objName() = "task2";
move.toContainer() = "host1";

MovesInProgressSpec movesInProgressSpec;
movesInProgressSpec.moves() = {move};
solver.addConstraint(movesInProgressSpec);
```

`task2` ends on `host1` as required. The other tasks have no reason to move, so
they stay put.

## How in-flight moves are counted

Other goals and constraints see each moving object at its destination, so it
counts toward that container's utilization. A [CapacitySpec](capacity) using the
`AFTER` [definition](../common/utilization-definitions) counts the object only at
its destination; `DURING` counts it at both its source and destination while the
move is in flight, so a `DURING` limit must also hold for that transient overlap.

For example, with a limit of 3 tasks per host and `task2` moving off `host0`:
under `AFTER`, another task may move onto `host0` (after the move, `host0` holds 3);
under `DURING` it may not, because while `task2` is in flight it still counts on
`host0`, which already holds 3 (`task0`, `task1`, and the departing `task2`).

## Constraint only

MovesInProgressSpec cannot be used as a goal; passing it to `addGoal` throws.

The moves are treated as already committed, so the constraint holds from the start
and is never broken: the solution always places each listed object in its
destination container.

If a move's destination is the object's current container, the object is simply
held there, equivalent to an [AvoidMovingSpec](avoid-moving) for it. And if the
same object appears in both, MovesInProgressSpec wins and the object moves to the
forced destination.

## Source

- Thrift definition: [`interface/thrift/ProblemSpecs.thrift`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/interface/thrift/ProblemSpecs.thrift) (`MovesInProgressSpec`, `MoveInProgress`)
- SpecBuilder: [`materializer/spec_builder/MovesInProgressSpecBuilder.cpp`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/materializer/spec_builder/MovesInProgressSpecBuilder.cpp)---the code that defines this spec's behavior
- Tests and runnable examples: [`interface/tests/MovesInProgressTest.cpp`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/interface/tests/MovesInProgressTest.cpp)---the unit tests the snippets on this page are drawn from
