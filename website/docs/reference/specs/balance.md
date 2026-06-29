# Balance

**Type**: [Goal](#goal-only)

Spread a dimension's utilization evenly across the scope items of a scope. For example,
even out the memory used across all hosts so that no host runs much hotter than
the others.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | Yes | - | Descriptive name for logging/debugging |
| `scope` | string | Yes | - | Scope whose scope items are being balanced (e.g. `"host"`, `"rack"`) |
| `dimension` | string | Yes | - | Dimension whose utilization is balanced |
| `formula` | BalanceSpecFormula | No | `LINEAR` | How imbalance is penalized (see [Formula](#formula)) |
| `upperBound` | double | No | 1 | Threshold above which utilization is penalized (see [Upper bound](#upper-bound-and-bound-type)) |
| `boundType` | BalanceSpecBoundType | No | `RELATIVE` | How `upperBound` is interpreted (see [Upper bound](#upper-bound-and-bound-type)) |
| `definition` | BalanceSpecDefinition | No | `AFTER` | Which objects count toward a scope item's utilization (see [Definition](#definition)) |
| `filter` | [Filter](../common/filter) | No | all scope items | Which scope items the spec applies to (see [Filter](#filter)) |

Unlike [CapacitySpec](capacity), BalanceSpec does **not** take a
[`Limit`](../common/limit): the threshold is expressed with the scalar
`upperBound` plus a `boundType`, since balance is about evening out utilization
rather than capping it.

## Example

An example use: spread memory use evenly across hosts. Each task uses 10 GB and
each host holds 20 GB. Both are modeled with a `memory` dimension: on the tasks
it is their usage, on the hosts it is their capacity (see
[how utilization is measured](capacity#how-utilization-is-measured)). `host0` starts with
3 tasks (30 GB, relative utilization 1.5) while `host1` holds 1 task (10 GB,
relative utilization 0.5), so the goal makes Rebalancer move one task from `host0`
to `host1` until both sit at 20 GB (relative utilization 1.0).

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

// Spread memory utilization evenly across hosts.
BalanceSpec memoryBalance;
memoryBalance.scope() = "host";
memoryBalance.dimension() = "memory";
solver.addGoal(memoryBalance);
```

Note that balance is added with `addGoal`, not `addConstraint`: BalanceSpec is
[goal only](#goal-only).

## Goal only

BalanceSpec can only be used as a **goal**---there is no constraint form, because
"perfectly balanced" is rarely a hard requirement. Add it with `solver.addGoal`,
and use `weight` / `tuplePos` to control how it trades off against other goals
(see [goal priorities](../goal-priorities)). The goal's value is the total penalty
across scope items as defined by the chosen [formula](#formula).

## Upper bound and bound type

Balance only penalizes utilization **above a threshold**; utilization at or below
the threshold is considered balanced enough and incurs no penalty. The threshold
is set by `upperBound`, and `boundType` controls how that number is interpreted:

| Bound type | Meaning | Example (`upperBound = 1.2`) |
|------------|---------|------------------------------|
| `RELATIVE` (default) | A multiplier of the average utilization | threshold = `1.2 * average` |
| `ABSOLUTE` | An offset added on top of the average relative utilization | threshold = `average + 0.2` |
| `RELATIVE_UTIL` | A fixed relative-utilization threshold, independent of the average | threshold = `1.2` |

With the default `upperBound` of `1` and `RELATIVE` bound type, the threshold is
exactly the average, so any utilization above the average is penalized---driving
all scope items toward the mean.

:::note Balance everything equally
To balance purely on relative utilization without regard for differing
capacities, use the `RELATIVE_UTIL_VARIANCE` [formula](#formula) with an
`upperBound` of `0` and a `RELATIVE` or `RELATIVE_UTIL` bound type.
:::

## Formula

The `formula` controls **how** imbalance above the threshold is penalized. The
examples below assume the default upper bound, an average relative utilization of
`0.5`, and three scope items with relative utilizations `{0.1, 0.6, 0.8}`.

| Formula | Behavior |
|---------|----------|
| `LINEAR` (default) | Penalty is the total excess above the threshold, summed across scope items: `{0, 0.1, 0.3}` → `0.4`. Cheap, but cannot distinguish `{0.1, 0.6, 0.8}` from the more-balanced `{0.1, 0.7, 0.7}` (both total `0.4`). |
| `SQUARES` | Each scope item's excess is raised to a power greater than 1 before summing, so hotter scope items pay disproportionately more. This favors `{0.1, 0.7, 0.7}` over `{0.1, 0.6, 0.8}` and gives the most natural balancing behavior. (The implementation uses an exponent a little above 1 rather than a literal square, to keep the output range easy to weigh against other goals.) |
| `MAX` | Minimizes only the single largest excess above the threshold (`0.3` here). The hottest scope item dominates; the rest of the distribution has no effect. Local search struggles to optimize this alone---**combine it with a low-weight `SQUARES` goal** to guide the search. |
| `IDEAL` | Lexicographically minimizes the largest relative utilization, then the second largest, and so on. The most intuitive notion of balance, and the one formula that does not over-load bigger scope items (see below). |
| `RELATIVE_UTIL_VARIANCE` | Minimizes the variance of relative utilization across scope items (penalty = `n * Var(relUtil)`), ignoring capacity and efficiency differences. Use when you want fairness in percentage utilization rather than equalizing absolute load. |

:::caution Heterogeneous capacities: prefer `IDEAL`
`LINEAR`, `MAX`, and `SQUARES` all penalize a scope item's **relative-utilization
excess** above the threshold. They share a blind spot: a bigger scope item absorbs
many more objects for the same relative-utilization delta, so under any of them
Rebalancer can pile much more load onto the large scope items than the small ones
and still consider the distribution balanced.

`IDEAL` solves this. Because it lexicographically minimizes the actual largest
relative utilization (then the next, and so on), it directly targets the hottest
scope items regardless of their size, so it does not over-load the big ones.

The one caveat with `IDEAL` is that its penalty scales with
`relativeUtil * absoluteUtil`, so it depends not just on how hot a scope item is
but on how much **absolute** resource the object consumes there. When the
dimension is *not* capacity-consistent across scope items---the same unit meaning
different real capacity depending on the scope item, such as replica or host
counts---Rebalancer can prefer a *hotter* scope item simply because the object
appears to consume fewer units there, counter to the intuitive "pick the coldest
scope item" expectation.
:::

## Definition

The `definition` controls *which* objects are counted toward a scope item's
utilization. The default, `AFTER`, counts the objects in the scope item in the
final assignment---the natural choice for standard balancing. The others count
objects entering, leaving, or in flight, which helps when moves are not
instantaneous in the system being modeled.

| Definition | Counts |
|------------|--------|
| `AFTER` (default) | Objects in the scope item in the final assignment |
| `DURING` | Objects that stayed, plus those moving in and those moving out |
| `NEW` | Only objects moving into the scope item |
| `OLD` | Only objects moving out of the scope item |

See [Utilization definitions](../common/utilization-definitions) for a detailed
explanation of these choices, with diagrams and guidance on when to use them
(BalanceSpec supports the subset above).

## Filter

The `filter` ([`Filter`](../common/filter)) selects which scope items the spec
applies to: set `itemsWhitelist` (consider only these) or `itemsBlacklist`
(consider all but these). For example, to balance across every host except
`host0`:

```cpp
BalanceSpec memoryBalance;
memoryBalance.scope() = "host";
memoryBalance.dimension() = "memory";

// Exclude host0 from the balance computation (use itemsWhitelist to restrict instead).
memoryBalance.filter()->itemsBlacklist() = {"host0"};

solver.addGoal(memoryBalance);
```

## Notes

**Zero-capacity scope items.** Relative utilization is the sum of object sizes
divided by the scope item's capacity, which is undefined when that capacity is
zero. Rebalancer automatically excludes zero-capacity scope items from a balance
goal, so they neither contribute to the average nor incur a penalty. If every
scope item has zero capacity the goal simply evaluates to zero.

## Source

- Thrift definition: [`interface/thrift/ProblemSpecs.thrift`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/thrift/ProblemSpecs.thrift) (`BalanceSpec`)
- SpecBuilder: [`materializer/spec_builder/BalanceSpecBuilder.cpp`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/materializer/spec_builder/BalanceSpecBuilder.cpp)---the code that defines this spec's behavior
- Tests and runnable examples: [`interface/tests/BalanceTest.cpp`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/BalanceTest.cpp)---the unit tests the snippets on this page are drawn from
