import useBaseUrl from '@docusaurus/useBaseUrl';

# Colocate Groups

**Type**: [Goal or Constraint](#goal-vs-constraint)

Keep the objects of the same group together in the same scope item. For example,
place all tasks of a job on the same host, or spread a group across no more than a
handful of racks.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | Yes | - | Descriptive name for logging/debugging |
| `scope` | string | Yes | - | Scope whose scope items a group is colocated within (e.g. `"host"`, `"rack"`) |
| `partitionName` | string | Yes | - | Partition whose groups are colocated (e.g. `"job"`) |
| `limits` | [Limit](../common/limit.md) | No | ABSOLUTE, `globalLimit` 1 | Max number of scope items a group may spread across (see [Limit](#limit)) |
| `bound` | ColocateGroupsSpecBound | No | `MAX` | Whether `limits` is an upper (`MAX`) or lower (`MIN`) bound on the spread |
| `scopeItemWeights` | map&lt;string, double&gt; | No | 1 per scope item | Per-scope-item weight toward the limit (see [Scope item weights](#scope-item-weights)) |
| `dimension` | string | No | object count | What a group's presence in a scope item is measured by; defaults to object count |
| `squares` | bool | No | false | As a goal, square each group's excess before summing |
| `filter` | [Filter](../common/filter.md) | No | all scope items | Which scope items count toward the limit (see [Filter](#filter)) |

## Example

An example use: force every job's tasks onto a single host. There are 9 tasks and
3 hosts, with tasks split evenly into 3 jobs (`job0` = `task0`-`task2`, `job1` =
`task3`-`task5`, `job2` = `task6`-`task8`). The initial placement scatters each job
across all three hosts.

**Initial assignment:**

<img src={useBaseUrl('/img/reference/colocate-groups/example-1-initial.png')} alt="Initial assignment: each job's tasks scattered across all three hosts" />

A ColocateGroups constraint with the default limit of 1 forces each job onto one
host. The final placement is not unique, but every job is fully contained in a
single host (here `host2` ends up empty).

```cpp
solver.setObjectName("task");
solver.setContainerName("host");

solver.setAssignment(std::map<std::string, std::vector<std::string>>{
    {"host0", {"task0", "task3", "task6"}},
    {"host1", {"task4", "task5", "task7"}},
    {"host2", {"task1", "task2", "task8"}},
});

// Tasks are evenly split into 3 jobs.
solver.addPartition(
    "job",
    std::map<std::string, std::string>{
        {"task0", "job0"}, {"task1", "job0"}, {"task2", "job0"},
        {"task3", "job1"}, {"task4", "job1"}, {"task5", "job1"},
        {"task6", "job2"}, {"task7", "job2"}, {"task8", "job2"}});

// Each job's tasks must land on a single host (default limit of 1).
ColocateGroupsSpec colocateGroups;
colocateGroups.scope() = "host";
colocateGroups.partitionName() = "job";
solver.addConstraint(colocateGroups);
```

**Final assignment:**

<img src={useBaseUrl('/img/reference/colocate-groups/example-1-final.png')} alt="Solution: each job fully contained within a single host" />

ColocateGroups is most often a hard constraint, which usually needs the
[optimal (MIP) solver](../../solvers/overview). See [more examples](#examples)
below for non-default limits, scope-item weights, and combining it with other
specs.

## Goal vs. constraint

**As a constraint**, the limit is enforced: each group spreads across at most
`limits` scope items (or at least, with a `MIN` bound). If the initial assignment
already satisfies it, the final one will too; if it is initially broken, the
general [constraint policy](../constraint-policy.md) applies.

**As a goal**, the spec's value is the aggregated violation amount---the sum, over
all groups, of how much each group exceeds its colocation limit.

## Limit

The `limits` ([`Limit`](../common/limit.md)) sets the maximum number of scope
items a group may spread across; the default is `1` (every group confined to a
single scope item). Raising it lets a group occupy more scope items---for example,
a limit of `2` permits a group to span two hosts even when another goal would
otherwise pull it wider apart ([Example 2](#example-2)). As with other specs, the
`Limit` fields let you vary the bound per group or per scope item.

## Scope item weights

By default, each scope item a group occupies contributes `1` toward the limit.
`scopeItemWeights` overrides that per scope item: a weight of `0` makes occupying
that scope item free (it never counts toward the limit), `2` makes it count
double, and so on. This expresses that spreading into some scope items is "more
expensive" than others ([Example 3](#example-3)).

## Filter

The `filter` ([`Filter`](../common/filter.md)) restricts which scope items the
colocation limit applies to: set `itemsWhitelist` (only these count) or
`itemsBlacklist` (all but these). Scope items excluded by the filter do not count
toward a group's limit.

## More Examples {#examples}

All examples below build on the same kind of setup as the [example
above](#example)---tasks partitioned into jobs across a few hosts. Each links to
its runnable unit test.

### Example 2: non-default limit {#example-2}

A ColocateGroups constraint with a limit of `2`, alongside a goal to balance tasks
across hosts. The limit caps each job at two hosts even though the balance goal
would otherwise spread it wider.
([source](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/ColocateGroupsTest.cpp#L128-L191))

**Initial assignment:**

<img src={useBaseUrl('/img/reference/colocate-groups/example-2-initial.png')} alt="Non-default limit: initial assignment" />

**Final assignment:**

<img src={useBaseUrl('/img/reference/colocate-groups/example-2-final.png')} alt="Non-default limit: each job spread across at most two hosts" />

### Example 3: scope item weights {#example-3}

A ColocateGroups constraint with a limit of `3` and weight overrides of `2` for
`host0` and `host2`, plus a balance goal and an AssignmentAffinities goal making
`task0` prefer `host0`. Each group ends up exactly at its colocation cost of 3
(e.g. `job0` pays 2 for `host0` plus 1 for `host1`).
([source](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/ColocateGroupsTest.cpp#L192-L283))

**Initial assignment:**

<img src={useBaseUrl('/img/reference/colocate-groups/example-3-initial.png')} alt="Scope item weights: initial assignment" />

**Final assignment:**

<img src={useBaseUrl('/img/reference/colocate-groups/example-3-final.png')} alt="Scope item weights: groups at their weighted colocation limit" />

### Example 4: primary, secondary, and dependent triplets {#example-4}

Triplets `(primary{i}, secondary{i}, dependent{i})` where the primary and
secondary must be on different hosts (a [GroupCount](group-count) constraint of 1
per ID group), while the dependent must share a host with one of them (a
ColocateGroups constraint limiting each triplet to at most 2 hosts).
([source](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/ColocateGroupsTest.cpp#L284-L382))

**Initial assignment:**

<img src={useBaseUrl('/img/reference/colocate-groups/example-4-initial.png')} alt="Triplets: initial assignment" />

**Final assignment:**

<img src={useBaseUrl('/img/reference/colocate-groups/example-4-final.png')} alt="Triplets: dependent colocated with primary or secondary" />

## Source

- Thrift definition: [`interface/thrift/ProblemSpecs.thrift`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/thrift/ProblemSpecs.thrift) (`ColocateGroupsSpec`)
- SpecBuilder: [`materializer/spec_builder/ColocateGroupsSpecBuilder.cpp`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/materializer/spec_builder/ColocateGroupsSpecBuilder.cpp)---the code that defines this spec's behavior
- Tests and runnable examples: [`interface/tests/ColocateGroupsTest.cpp`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/ColocateGroupsTest.cpp)---the unit tests the snippets on this page are drawn from
