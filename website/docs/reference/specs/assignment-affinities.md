# AssignmentAffinitiesSpec

**Type**: Goal

Make specific objects prefer specific scope items. For example, give each task a
preference score for each host and let Rebalancer place tasks on their most
preferred hosts, trading preferences off against the other goals and constraints.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | No | `""` | Descriptive name for logging/debugging. |
| `scope` | string | No | container scope | Scope whose items objects express affinities for (e.g. `"host"`, `"rack"`). Empty means the container scope. |
| `affinities` | list of [AssignmentAffinity](#affinity-entries) | Yes | empty | One entry per (object, scope item) preference. Unlisted pairs have affinity 0. |

### Affinity entries

Each `AssignmentAffinity` is a single object-to-scope-item preference:

| Field | Type | Description |
|-------|------|-------------|
| `objectName` | string | The object the preference is for |
| `scopeItemName` | string | The scope item (in `scope`) the object prefers |
| `affinity` | double | Preference strength; higher is more preferred. May be negative. |

Any (object, scope item) pair not listed is treated as affinity 0.

## Example

An example use: three hosts and four tasks, all initially on `host0`. Each task
has a preference for some hosts. With no competing goals or constraints, each
task ends up on its most preferred host.

```cpp
solver.setObjectName("task");
solver.setContainerName("host");

solver.setAssignment(std::map<std::string, std::vector<std::string>>{
    {"host0", {"task0", "task1", "task2", "task3"}},
    {"host1", {}},
    {"host2", {}},
});

// Preference of each task for each host; unlisted (task, host) pairs are 0.
//       | task0 | task1 | task2 |
// host0 |     1 |    -2 |       |
// host1 |     2 |       |   1.4 |
// host2 |     3 |    -3 |       |
auto pref = [](std::string task, std::string host, double affinity) {
  AssignmentAffinity entry;
  entry.objectName() = std::move(task);
  entry.scopeItemName() = std::move(host);
  entry.affinity() = affinity;
  return entry;
};

AssignmentAffinitiesSpec affinitiesSpec;
affinitiesSpec.scope() = "host";
affinitiesSpec.affinities() = {
    pref("task0", "host0", 1),
    pref("task0", "host1", 2),
    pref("task0", "host2", 3),
    pref("task1", "host0", -2),
    pref("task1", "host2", -3),
    pref("task2", "host1", 1.4),
};

solver.addGoal(affinitiesSpec);
```

The resulting assignment:

- `task0` goes to `host2` (its highest affinity, 3).
- `task1` goes to `host1`. It lists no affinity for `host1`, so that pair is 0,
  which is higher than its only listed values (both negative).
- `task2` goes to `host1` (its only positive affinity, 1.4).
- `task3` stays on `host0`. It lists no affinities, so every host is equally
  affinity 0 and there is no incentive to move it.

The sum of the realized affinities is `3 + 0 + 1.4 + 0 = 4.4`, the most
attainable here.

## How affinities are scored

Every object has a *best* affinity: the highest affinity specified, with 0 for
unspecified scope items. The solver penalizes each object by the difference
between that best and the affinity of the scope item it is actually placed on:

```
penalty(object) = best affinity - affinity of the scope item it is placed on
```

An object on its top choice pays nothing; any worse placement pays the
difference (an unlisted scope item, or a placement outside `scope`, counts as
affinity 0). The goal is the sum of these penalties over all objects, which the
solver minimizes --- equivalently, it maximizes the total affinity actually
realized.

### Missing and negative affinities

Negative affinities are allowed and express "would rather not": if all of an
object's listed affinities are negative, an unspecified scope item (affinity 0)
is its best, so the object prefers anywhere it does not list. An object with no
affinities at all is indifferent and has no reason to move.

### Competing objects

Affinities add up across objects, so the solver maximizes the total realized
affinity. When a constraint lets only one of two objects take the scope item
they both want, it goes to whichever choice gives the higher total. For example,
with a [CapacitySpec](capacity) of one task per host: both tasks rate `host1` at
10, but `task0` rates its fallback `host2` at 5 and `task1` rates it at 7.
Placing `task0` on `host1` and `task1` on `host2` totals `10 + 7 = 17`; the
reverse totals `10 + 5 = 15`. So `task0` gets `host1`.

## Scope

By default the affinities are over the container scope, so `scopeItemName` is a
container name. Set `scope` to another scope (e.g. `"rack"`) to express
preferences at that level instead; `scopeItemName` is then a scope item name of
that scope, and an object placed outside the scope has affinity 0 there. Leaving
`scope` empty is the same as naming the container scope.

## Goal only

AssignmentAffinitiesSpec is a goal and cannot be used as a constraint; passing it
to `addConstraint` throws. Like any goal, its `weight` and `tuplePos` (the second
and third arguments to `addGoal`) set how it trades off against other goals; see
[goal priorities](../goal-priorities).

## Source

- Thrift definition: [`interface/thrift/ProblemSpecs.thrift`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/thrift/ProblemSpecs.thrift) (`AssignmentAffinitiesSpec`, `AssignmentAffinity`)
- SpecBuilder: [`materializer/spec_builder/AssignmentAffinitiesSpecBuilder.cpp`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/materializer/spec_builder/AssignmentAffinitiesSpecBuilder.cpp)---the code that defines this spec's behavior
- Tests and runnable examples: [`interface/tests/AssignmentAffinitiesTest.cpp`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/AssignmentAffinitiesTest.cpp)---the unit tests the snippets on this page are drawn from
