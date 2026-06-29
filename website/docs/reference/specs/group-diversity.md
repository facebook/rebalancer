# GroupDiversity

**Type**: [Goal or Constraint](#goal-vs-constraint)

Require each scope item to hold objects from at least, or at most, N distinct
groups of a [partition](../../core-concepts/overview#partitions). For example,
make each host run tasks from at least 2 different jobs, or from at most 2 jobs.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | No | `""` | Descriptive name for logging/debugging. |
| `scope` | string | Yes | - | The scope the bound applies to (e.g. `"host"`). |
| `partition` | string | Yes | - | Partition whose groups are counted (e.g. `"job"`). |
| `dimension` | string | Yes | - | A group counts as present in a scope item when its utilization for this dimension is positive. Use the object-count dimension (`<object>_count`) to count any group with at least one object there. |
| `limit` | [Limit](../common/limit) | No | `globalLimit` 1 | The threshold N. Set `globalLimit` to apply one N to every scope item, or `scopeItemLimits` to set a different N per scope item. |
| `bound` | GroupDiversityBound | No | `MIN` | Whether N is a lower (`MIN`) or upper (`MAX`) bound on distinct groups (see [Bound](#bound)). |
| `filter` | [Filter](../common/filter) | No | all scope items | Which scope items the spec applies to. |

## Example

An example use: place tasks on hosts so each host runs a mix of jobs. There are 8
tasks across 4 jobs (2 each), all initially unassigned. A `job` partition groups
the tasks, and a GroupDiversity goal asks `host0` to run at least 2 jobs and
`host1` at least 3.

```cpp
solver.setObjectName("task");
solver.setContainerName("host");

solver.setAssignment(std::map<std::string, std::vector<std::string>>{
    {"unassigned",
     {"task0", "task1", "task2", "task3", "task4", "task5", "task6", "task7"}},
    {"host0", {}},
    {"host1", {}},
});

// Group the tasks by job (2 tasks per job).
solver.addPartition(
    "job",
    std::map<std::string, std::string>{
        {"task0", "job0"}, {"task1", "job0"},
        {"task2", "job1"}, {"task3", "job1"},
        {"task4", "job2"}, {"task5", "job2"},
        {"task6", "job3"}, {"task7", "job3"}});

// Each host must run at least 2 jobs, except host1 (>= 3).
GroupDiversitySpec spec;
spec.scope() = "host";
spec.partition() = "job";
spec.dimension() = "task_count";
spec.limit()->globalLimit() = 2;
spec.limit()->scopeItemLimits() = {{"host1", 3}};
solver.addGoal(spec);
```

Rebalancer pulls in tasks from distinct jobs until each host meets its threshold:
`host0` ends with tasks from 2 jobs and `host1` from 3. Tasks it does not need
stay unassigned.

## Bound

`bound` selects whether N is a lower or upper bound on the number of distinct groups
in each scope item:

| Bound | Meaning |
|-------|---------|
| `MIN` (default) | Each scope item must hold objects from **at least** N distinct groups (spread for diversity). |
| `MAX` | Each scope item may hold objects from **at most** N distinct groups (limit how many groups share a scope item). |

`MIN` is the diversity use case above. `MAX` is the reverse: for example, with
`scope = "host"`, `partition = "job"`, and a `MAX` limit of 2, Rebalancer keeps
each host to tasks from at most 2 jobs.

## Goal vs. constraint

**As a constraint**, the bound must hold for every scope item. If the initial
assignment already satisfies it, so does the final one. If a scope item starts out
violating it, the general [constraint policy](../constraint-policy) takes over:
under the default policy Rebalancer fixes the violation as best it can.

**As a goal**, the bound is not required to hold; instead the solver minimizes how
far the scope items are from it---the number of groups each scope item is short of
(`MIN`) or over (`MAX`), summed across all scope items.

## Source

- Thrift definition: [`interface/thrift/ProblemSpecs.thrift`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/thrift/ProblemSpecs.thrift) (`GroupDiversitySpec`, `GroupDiversityBound`)
- SpecBuilder: [`materializer/spec_builder/GroupDiversitySpecBuilder.cpp`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/materializer/spec_builder/GroupDiversitySpecBuilder.cpp)---the code that defines this spec's behavior
- Tests and runnable examples: [`interface/tests/GroupDiversityTest.cpp`](https://github.com/facebookincubator/rebalancer/blob/main/algopt/rebalancer/interface/tests/GroupDiversityTest.cpp)---the unit tests the snippets on this page are drawn from
