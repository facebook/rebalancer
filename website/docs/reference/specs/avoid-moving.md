# AvoidMoving

**Type**: Constraint

Pin a set of objects to the containers they start in, so Rebalancer never moves
them. For example, hold a few tasks in place while it rebalances everything else.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | No | `""` | Descriptive name for logging/debugging. |
| `objects` | list of string | Yes | empty | The objects pinned to their initial container. |

## Example

An example use: three tasks start on `host0`, and an
[AssignmentAffinitiesSpec](assignment-affinities) goal makes `task0` and `task1`
prefer `host1`. Pinning `task0` keeps it on `host0`, so only `task1` moves.

```cpp
solver.setObjectName("task");
solver.setContainerName("host");

solver.setAssignment(std::map<std::string, std::vector<std::string>>{
    {"host0", {"task0", "task1", "task2"}},
    {"host1", {}},
});

// task0 and task1 prefer host1 (an AssignmentAffinitiesSpec goal).
auto pref = [](std::string task, std::string host, double affinity) {
  AssignmentAffinity entry;
  entry.objectName() = std::move(task);
  entry.scopeItemName() = std::move(host);
  entry.affinity() = affinity;
  return entry;
};
AssignmentAffinitiesSpec prefersHost1;
prefersHost1.scope() = "host";
prefersHost1.affinities() = {pref("task0", "host1", 1), pref("task1", "host1", 1)};
solver.addGoal(prefersHost1);

// Pin task0 to its initial container.
AvoidMovingSpec avoidMovingSpec;
avoidMovingSpec.objects() = {"task0"};
solver.addConstraint(avoidMovingSpec);
```

`task1` moves to its preferred `host1`. `task0` would prefer `host1` too but is
pinned, so it stays on `host0`. `task2` has no reason to move.

## Constraint only

AvoidMovingSpec cannot be used as a goal; passing it to `addGoal` throws.

It keeps each listed object in the container it starts in. The initial assignment
already satisfies this (nothing has moved yet), so the constraint is never broken
and these objects never move.

If an object is also named in a `MovesInProgressSpec` (a forced move), the forced
move takes precedence and the object may move.

## Source

- Thrift definition: [`interface/thrift/ProblemSpecs.thrift`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/interface/thrift/ProblemSpecs.thrift) (`AvoidMovingSpec`)
- SpecBuilder: [`materializer/spec_builder/AvoidMovingSpecBuilder.cpp`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/materializer/spec_builder/AvoidMovingSpecBuilder.cpp)---the code that defines this spec's behavior
- Tests and runnable examples: [`interface/tests/AvoidMovingTest.cpp`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/interface/tests/AvoidMovingTest.cpp)---the unit tests the snippets on this page are drawn from
