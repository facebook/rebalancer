# CapacityWithGroupPresence

**Type**: [Goal or Constraint](#goal-vs-constraint)

Limit each scope item's utilization for a dimension, where every present group
(from a [partition](../../core-concepts/overview#partitions)) counts for at least a
minimum **presence weight**, regardless of its actual utilization. A group is
present when its utilization for the dimension is positive. For example, model a
fixed per-host cost that any job incurs just by placing a task on a host (loading
its binary into memory, say), even when those tasks use little of the dimension.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | No | `""` | Descriptive name for logging/debugging. |
| `scope` | string | Yes | - | Scope the bound applies to. Each scope item (e.g. a `"host"`, or a `"rack"` of hosts) gets a utilization bound. |
| `partition` | string | Yes | - | Partition defining the groups (e.g. `"job"`). |
| `dimension` | string | Yes | - | Dimension whose utilization is measured. Must be scalar and non-negative. |
| `groupToPresenceWeight` | [Limit](../common/limit) | No | ABSOLUTE, `globalLimit` 1 | Minimum a present group contributes to a scope item's utilization (see [Presence weight](#presence-weight)). Must be `ABSOLUTE` and non-negative. |
| `scopeItemToLimit` | [Limit](../common/limit) | No | RELATIVE, `globalLimit` 1 | The utilization bound (see [Bound](#bound) and [Intent](#intent)). A `RELATIVE` limit is a fraction of the scope item's same-named dimension, as in [CapacitySpec](capacity). |
| `bound` | enum | No | `MAX` | Whether `scopeItemToLimit` is an upper (`MAX`) or lower (`MIN`) bound (see [Bound](#bound)). |
| `scopeItemFilter` | [Filter](../common/filter) | No | all scope items | Which scope items the spec applies to (see [Filters](#filters)). |
| `roundUpGroupUtilOnScopeItem` | bool | No | true | Round each group's contribution up to the next integer (see [Rounding](#rounding)). |
| `aggregationScope` | string | No | = `scope` | Finer scope at which contributions are computed before summing up to `scope` (see [Aggregation scope](#aggregation-scope)). |
| `groupFilter` | [Filter](../common/filter) | No | all groups | Which groups contribute to utilization (see [Filters](#filters)). |
| `intent` | enum | No | `PER_SCOPE_ITEM` | Limit per scope item, or per (group, scope item) (see [Intent](#intent)). |
| `aggregationPartition` | string | No | = `partition` | Finer partition aggregated up to `partition`; used only with the per-(group, scope item) intent (see [Intent](#intent)). |
| `groupUtilMultipliers` | list | No | `[]` | Multipliers (`GroupUtilMultiplier`) applied to a group's contribution (see [Multipliers](#multipliers)). |
| `definition` | enum | No | `AFTER` | Which utilization to bound: the final assignment (`AFTER`) or the transient peak during moves (`DURING`) (see [Definition](#definition)). |

## Example

An example use: keep at most one job per host, even though each job's real task
load is tiny, because each job carries a fixed per-host overhead. `job0` has 3
tasks and `job1` has 1, all starting on `host0`; each task uses only `0.2` of
`load`. With a presence weight of `1.0` per job and a per-host limit of `1.0`,
`host0`'s utilization is `2`---one unit per present job, since each job's
contribution is `max(1.0, its real load) = 1.0`---so the limit is broken.
Rebalancer moves `job1` onto its own host:

```cpp
solver.setObjectName("task");
solver.setContainerName("host");

// job0 (3 tasks) and job1 (1 task) all start on host0.
solver.setAssignment(std::map<std::string, std::vector<std::string>>{
    {"host0", {"t0", "t1", "t2", "t3"}},
    {"host1", {}},
    {"host2", {}},
});

// Tiny real load: all four tasks together use only 0.8.
solver.addObjectDimension(
    "load",
    std::map<std::string, double>{
        {"t0", 0.2}, {"t1", 0.2}, {"t2", 0.2}, {"t3", 0.2}});

solver.addPartition(
    "job",
    std::map<std::string, std::vector<std::string>>{
        {"job0", {"t0", "t1", "t2"}}, {"job1", {"t3"}}});

CapacityWithGroupPresenceSpec spec;
spec.scope() = "host";
spec.partition() = "job";
spec.dimension() = "load";

// Every job present on a host counts for at least 1.0, whatever its load.
spec.groupToPresenceWeight()->type() = LimitType::ABSOLUTE;
spec.groupToPresenceWeight()->globalLimit() = 1.0;

// At most 1.0 of utilization per host, so at most one job per host.
spec.scopeItemToLimit()->type() = LimitType::ABSOLUTE;
spec.scopeItemToLimit()->globalLimit() = 1.0;

solver.addConstraint(spec);
```

The result places `job0` and `job1` on separate hosts. A plain
[CapacitySpec](capacity) would leave everything on `host0` (total real load `0.8`
is well under the limit); it is the presence weight that forces the split.

## How utilization is measured

Each group present in a scope item has a **contribution**. For a group `G` in
scope item `S`:

```
contribution(G, S) = max(
    presence weight of G in S   // only if G has non-zero utilization in S,
    actual utilization of G in S  // the sum of the dimension over G's objects in S
)
```

So a present group always contributes at least its presence weight, even if its
real utilization is lower; a group with no utilization in `S` (no objects there, or
all of them zero for the dimension) contributes nothing.

How `scopeItemToLimit` bounds these contributions depends on [`intent`](#intent):
under `PER_SCOPE_ITEM` (the default) it bounds their sum over all groups in a scope
item; under `PER_GROUP_AND_SCOPE_ITEM` it bounds each group's contribution
separately.

### Presence weight

`groupToPresenceWeight` is an `ABSOLUTE`, non-negative [Limit](../common/limit)
giving the minimum a group contributes when present. `globalLimit` sets the weight
for every group (default `1`); `groupLimits` and `scopeItemToGroupLimits` override
it for specific groups or (scope item, group) pairs, following the usual
[Limit resolution order](../common/limit#resolution-order).

### Rounding

With `roundUpGroupUtilOnScopeItem` (the default), each group's contribution is
rounded up with `ceil`---useful for modeling discrete resources where any
fractional use rounds up to a full integer. For example, a contribution of `2.3`
becomes `3`. Set it to `false` to use the raw contribution.

## Intent

`intent` (`CapacityWithGroupPresenceUsageIntent`) selects what `scopeItemToLimit`
bounds:

| Intent | The limit bounds... |
|--------|---------------------|
| `PER_SCOPE_ITEM` (default) | The total utilization of each scope item, summed over all groups. |
| `PER_GROUP_AND_SCOPE_ITEM` | Each individual group's utilization within each scope item. |

Under `PER_SCOPE_ITEM`, the limit applies to the whole scope item, so per-group
limit fields (`groupLimits` / `scopeItemToGroupLimits`) on `scopeItemToLimit` are
rejected with an error, and `aggregationPartition` must equal `partition`. Under
`PER_GROUP_AND_SCOPE_ITEM`, the limit is read per (group, scope item) and you may
set `aggregationPartition` to compute a group's utilization by summing over a finer
partition (the partition-level analogue of [aggregation scope](#aggregation-scope)).

## Bound

`bound` (`CapacityWithGroupPresenceBound`) sets an upper or lower bound on the
utilization that [`intent`](#intent) measures (a scope item's total, or a single
group's within a scope item):

| Bound | Meaning |
|-------|---------|
| `MAX` (default) | That utilization must not exceed `scopeItemToLimit`. |
| `MIN` | That utilization must be at least `scopeItemToLimit`. |

## Definition

`definition` (`CapacityWithGroupPresenceDefinition`) selects which utilization the
bound applies to, exactly as in [CapacitySpec](capacity):

| Definition | Meaning |
|------------|---------|
| `AFTER` (default) | Utilization of the final assignment. |
| `DURING` | Transient peak while moves are in flight (`during = after + initial - stayed`): an object counts toward a scope item if it is there either initially or finally. A scope item that is over its limit cannot be brought under it merely by moving objects out, since they still count during the transition. |

The `DURING` definition is intended for use as a **constraint**, not a goal: it
tracks a transient peak that the solver usually cannot reduce, so as a goal it
contributes a mostly-fixed offset rather than a useful gradient.

## Goal vs. constraint

**As a constraint**, the bound is enforced. If the initial assignment already
satisfies it, the final assignment is guaranteed to satisfy it too. If the initial
assignment breaks it, the general [constraint policy](../constraint-policy)
applies: under the default policy a broken case becomes a high-priority goal to
fix, while "do not make it worse" stays a hard constraint. The spec adds one
constraint per scope item (or per (group, scope item) under
`PER_GROUP_AND_SCOPE_ITEM`), so the default policy applies to each independently.

**As a goal**, the spec's value is proportional to the total amount by which the
bound is exceeded (for `MAX`) or undershot (for `MIN`), aggregated across all scope
items (or (group, scope item) pairs). See [goal priorities](../goal-priorities)
for how it trades off against other goals.

## Multipliers

`groupUtilMultipliers` scales a group's contribution, which is useful for modeling
overhead or scaling factors. Each `GroupUtilMultiplier` pairs a `value`
([Limit](../common/limit)) with a `target` controlling what it scales: `UTILIZATION`
(the actual utilization), `PRESENCE_WEIGHT` (the presence weight), or `COMMON`
(both). The presence-weight side and the utilization side are scaled separately and
then combined with `max`. Multipliers of the same target are applied in sequence;
with rounding enabled, each application is followed by a `ceil`, so the effect
compounds. For example, with rounding on, a value of `3` scaled by `1.2` then `4.0`
yields `ceil(ceil(3 * 1.2) * 4.0) = 16`. A multiplier of `0` zeroes that side.

## Aggregation scope

By default contributions are computed directly on `scope`. Setting
`aggregationScope` to a finer scope computes each group's contribution (with
rounding and multipliers) at that finer level first, then sums up to the main scope
item before the bound is applied:

```
contribution(G, S) = sum over S_i in aggregationScope, S_i a subset of S
                       of contribution(G, S_i)
```

Every aggregation scope item must be a subset of exactly one main scope item. For
example, with `scope = "region"` and `aggregationScope = "host"`, rounding and
presence weights apply per host, but the bound is enforced per region.

When aggregation is in use, the per-scope-item and per-group entries of
`groupToPresenceWeight` and the multipliers are keyed by the aggregation scope and
`aggregationPartition` (that is where rounding and multipliers run), not the main
scope. Only `scopeItemToLimit` is read on the main scope.

### Aggregation partition

`aggregationPartition` is the partition-level analogue of `aggregationScope`, and
applies only under `PER_GROUP_AND_SCOPE_ITEM`. When set, a group `G` of the main
`partition` has its utilization in a scope item computed by summing the
contributions of the finer groups in `aggregationPartition` whose objects make up
`G` (each object belongs to exactly one finer group). Rounding and multipliers run
at the `aggregationPartition` level; `scopeItemToLimit` is still read per group of
the main `partition`.

## Filters

Both filters take a [Filter](../common/filter) with `itemsWhitelist` (consider
only these) or `itemsBlacklist` (consider all but these):

- `scopeItemFilter` selects which scope items the spec applies to.
- `groupFilter` selects which groups contribute to utilization. Groups excluded
  here add nothing, even if present.

## Source

- Thrift definition: [`interface/thrift/ProblemSpecs.thrift`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/interface/thrift/ProblemSpecs.thrift) (`CapacityWithGroupPresenceSpec`, `CapacityWithGroupPresenceBound`, `CapacityWithGroupPresenceUsageIntent`, `CapacityWithGroupPresenceDefinition`)
- SpecBuilder: [`materializer/spec_builder/CapacityWithGroupPresenceSpecBuilder.cpp`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/materializer/spec_builder/CapacityWithGroupPresenceSpecBuilder.cpp)---the code that defines this spec's behavior
- Tests and runnable examples: [`interface/tests/CapacityWithGroupPresenceTest.cpp`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/interface/tests/CapacityWithGroupPresenceTest.cpp)---the unit tests the snippets on this page are drawn from
