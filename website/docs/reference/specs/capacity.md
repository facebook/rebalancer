# CapacitySpec

**Type**: [Goal or Constraint](#goal-vs-constraint)

Limit each scope item's utilization for a given dimension. For example,
ensure the memory used by the tasks on a host does not exceed the host's memory
capacity.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | Yes | - | Descriptive name for logging/debugging |
| `scope` | string | Yes | - | Scope whose items are limited (e.g. `"host"`, `"rack"`) |
| `dimension` | string | Yes | - | Dimension whose utilization is limited |
| `limit` | [Limit](../common/limit) | No | RELATIVE, `globalLimit` 1 | The bound on each scope item's utilization |
| `definition` | CapacitySpecDefinition | No | `AFTER` | Which objects count toward a scope item's utilization (see [Definition](#definition)) |
| `bound` | CapacitySpecBound | No | `MAX` | Whether `limit` is an upper (`MAX`) or lower (`MIN`) bound |
| `filter` | [Filter](../common/filter) | No | all scope items | Which scope items the spec applies to (see [Filter](#filter)) |
| `zeroAllowed` | bool | No | false | Used with a `MIN` bound: lets a scope item be empty as an alternative to meeting the minimum (each scope item is then empty or at/above the limit) |

## Example

An example use: keep each host's memory use within its capacity. Each task uses
10 GB and each host holds 20 GB. Both are modeled with a `memory` dimension: on
the tasks it is their usage, on the hosts it is their capacity (see
[How utilization is measured](#how-utilization-is-measured)). `host0` starts with
3 tasks (30 GB) on a 20 GB host, so the constraint makes Rebalancer move one task
off it.

```cpp
solver.setObjectName("task");
solver.setContainerName("host");

solver.setAssignment(std::map<std::string, std::vector<std::string>>{
    {"host0", {"task0", "task1", "task2"}},
    {"host1", {"task3"}},
});

// Add "memory" dimension w.r.t. both objects and containers.
solver.addObjectDimension(
    "memory",
    std::map<std::string, double>{
        {"task0", 10}, {"task1", 10}, {"task2", 10}, {"task3", 10}});

solver.addContainerDimension(
    "memory", std::map<std::string, double>{{"host0", 20}, {"host1", 20}});

// Default limit (RELATIVE, globalLimit 1): utilization <= capacity.
CapacitySpec memoryCapacity;
memoryCapacity.scope() = "host";
memoryCapacity.dimension() = "memory";
solver.addConstraint(memoryCapacity);
```

This uses the **default limit**: `RELATIVE` with `globalLimit` 1, i.e.
utilization may not exceed capacity, so no limit needs to be set explicitly.

## How utilization is measured

The **utilization** of a scope item for a dimension is the sum of that
dimension's values over the objects counted in the scope item. **Which objects
are counted is set by the [`definition`](#definition)**---by default `AFTER`,
which counts the objects in the scope item in the final assignment. A capacity
limit bounds this utilization, either in absolute terms or relative to the scope
item's own capacity.

A scope item's **capacity** is its value for the *same dimension*---set with a
container/scope dimension that has the same name as the spec's `dimension`. So to
limit `memory`, give the objects a `memory` dimension (their usage) and give the
containers a `memory` dimension (their capacity). The **relative utilization** is
then:

```
                                   sum of the dimension over the counted objects
relative utilization(scope item) = ---------------------------------------------
                                        scope item's capacity for that dimension
```

A `MAX` bound with the default limit of `1` enforces `utilization <= capacity`
for every scope item. See [Limit](#limit) for relative vs. absolute limits.

### Zero capacity

Relative utilization is not well defined when a scope item's capacity (the
denominator) is zero. Rebalancer handles this gracefully rather than dividing by
zero: for a `MAX` bound it enforces that the numerator (the utilization) is also
zero, i.e. no objects may be placed in a zero-capacity scope item.

## Definition

The `definition` controls *which* objects are counted toward a scope item's
utilization (the numerator above). The default, `AFTER`, counts the objects in
the scope item in the final assignment---the natural choice for a standard
capacity limit. Others count only objects entering, leaving, or in flight, which
helps when moves are not instantaneous in the system being modeled.

| Definition | Counts |
|------------|--------|
| `AFTER` (default) | Objects in the scope item in the final assignment |
| `DURING` | Objects that stayed, plus those moving in and those moving out |
| `NEW` | Only objects moving into the scope item |
| `OLD` | Only objects moving out of the scope item |
| `MOVED_DATA` | Objects moving in or out (equivalent to `NEW` + `OLD`) |
| `DURING_AND_AFTER` | Shorthand for an `AFTER` plus a `DURING` spec |
| `DOUBLE_DURING` | `DURING` plus `OLD` |
| `DOUBLE_DURING_AND_AFTER` | `DOUBLE_DURING` plus `AFTER` |

See [Utilization definitions](../common/utilization-definitions) for a detailed explanation of
each, with diagrams and guidance on when to use them (and why `DURING` should not
be used on its own).

## Goal vs. constraint

**As a constraint**, the limit is enforced. If the initial assignment already
satisfies the limit for a scope item, the final assignment is guaranteed to
satisfy it too.

If the initial assignment *breaks* the limit, what happens is governed by the
general [constraint policy](../constraint-policy). Under the
default policy, an initially-broken constraint becomes a high-priority goal to fix
it, while "do not make it worse" stays a hard constraint.

In the case of CapacitySpec, since it adds one constraint *per scope item*, the
default policy applies to each scope item independently:

- Scope items that satisfy the limit initially stay hard constraints; only the
  scope items that break it (e.g. `host0` in the [example](#example)) become
  goals.
- Each broken scope item also gets a hard constraint that its excess does not
  increase, while the goal tries to reduce it.

**As a goal**, the spec's value is proportional to the total amount of
utilization above the limit (or below it, for a `MIN` bound), summed across scope
items.

## Limit

The `limit` parameter is a [`Limit`](../common/limit). CapacitySpec honors three
of its fields: `type` (`RELATIVE` by default, or `ABSOLUTE`), `globalLimit`
(default 1), and `scopeItemLimits` (per-scope-item overrides).

:::note Relative vs. absolute
With the default `RELATIVE` type, the limit is a fraction of each scope item's
same-named capacity dimension (e.g. `0.8` = 80% of capacity). Set that
dimension on each scope item to its real capacity; if it is not defined it
defaults to `1` per scope item, so the relative limit then behaves like an
absolute one. Use `ABSOLUTE` for a raw cap such as `64.0` GB or "1 task per host".
See [Limit](../common/limit)
for the full field reference.
:::

### Per-scope-item limits

To give different scope items different limits, set `scopeItemLimits` (a map from
scope item to limit); scope items not listed fall back to `globalLimit`. For
example, `scopeItemLimits = {{"host0", 32.0}, {"host1", 64.0}}` with
`type = ABSOLUTE` caps `host0` at 32 and `host1` at 64.

## Bound

The `bound` parameter selects an upper or lower bound:

| Bound | Meaning |
|-------|---------|
| `MAX` (default) | Utilization must not exceed the limit |
| `MIN` | Utilization must be at least the limit |

A `MIN` bound forbids a scope item from being empty (its utilization must reach
the limit). Often the desired behavior is "either empty, or above the limit":
set `zeroAllowed` to `true` for that.

## Filter

The `filter` ([`Filter`](../common/filter)) selects which scope items the spec
applies to: set `itemsWhitelist` (consider only these) or `itemsBlacklist`
(consider all but these). For example, to apply a limit to `host0` only:

```cpp
CapacitySpec capacitySpec;
capacitySpec.scope() = "host";
capacitySpec.dimension() = "memory";

Limit limit;
limit.type() = LimitType::ABSOLUTE;
limit.globalLimit() = 64.0;
capacitySpec.limit() = limit;

// Apply the limit to host0 only (use itemsBlacklist to exclude instead).
capacitySpec.filter()->itemsWhitelist() = {"host0"};

solver.addConstraint(capacitySpec);
```

## Source

- Thrift definition: [`interface/thrift/ProblemSpecs.thrift`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/thrift/ProblemSpecs.thrift) (`CapacitySpec`)
- SpecBuilder: [`materializer/spec_builder/CapacitySpecBuilder.cpp`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/materializer/spec_builder/CapacitySpecBuilder.cpp)---the code that defines this spec's behavior
- Tests and runnable examples: [`interface/tests/CapacityTest.cpp`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/CapacityTest.cpp)---the unit tests the snippets on this page are drawn from
