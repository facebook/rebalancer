# Exclusive Scope Items

**Type**: [Goal or Constraint](#goal-vs-constraint)

Declare scope items that may not be **in use at the same time**. For example, mark
two hosts as mutually exclusive so tasks are never placed on both at once, or keep a
single group's objects off conflicting scope items. A scope item counts as "in use"
when its utilization for `dimension` is greater than zero.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | Yes | - | Descriptive name for logging/debugging |
| `scope` | string | Yes | - | Scope whose scope items are made mutually exclusive (e.g. `"host"`) |
| `dimension` | string | Yes | - | Dimension whose utilization marks a scope item as in use (commonly the object-count dimension); a scope item is "in use" when its sum is `> 0` |
| `conflictInfoList` | list&lt;[ScopeItemConflictInfo](#conflicts)&gt; | Yes | - | The conflicting scope items (see [Conflicts](#conflicts)). Conflicts are symmetric |
| `partitionName` | string | No | (none) | If set, exclusivity is enforced **per group**: two conflicting scope items may not both hold objects of the *same* group (see [Per-group conflicts](#per-group-conflicts)) |
| `formula` | ExclusiveScopeItemsFormula | No | `MINIMIZE_INVALIDATED_SCOPE_ITEMS_COUNT` | Goal-only; how packing is scored (see [Goal vs. constraint](#goal-vs-constraint)) |
| `scopeItemWeights` | map&lt;string, double&gt; | No | 1 each | Per-scope-item weight, used only by the `AGGRESSIVE_PACKING` goal formula |

## Example

Three hosts each start with one task. We declare all three mutually exclusive
(`host1` conflicts with `host2` and `host3`; `host2` conflicts with `host3`). As a
constraint, no two conflicting hosts may be in use at once, so Rebalancer must
consolidate all tasks onto a single host, leaving the other two empty.

```cpp
solver.setObjectName("task");
solver.setContainerName("host");

solver.setAssignment(std::map<std::string, std::vector<std::string>>{
    {"host1", {"task1"}},
    {"host2", {"task2"}},
    {"host3", {"task3"}},
});

// Helper to build one scope item's conflicts.
auto conflict = [](std::string scopeItem, std::vector<std::string> conflictsWith) {
  ScopeItemConflictInfo info;
  info.scopeItem() = std::move(scopeItem);
  std::vector<ConflictingScopeItemInfo> conflicts;
  for (auto& other : conflictsWith) {
    ConflictingScopeItemInfo c;
    c.conflictingScopeItem() = other;
    conflicts.push_back(std::move(c));
  }
  info.conflictingScopeItemsWithOverlap() = std::move(conflicts);
  return info;
};

// host1, host2, host3 are pairwise mutually exclusive.
ExclusiveScopeItemsSpec spec;
spec.scope() = "host";
spec.dimension() = "task_count";
spec.conflictInfoList() = {
    conflict("host1", {"host2", "host3"}),
    conflict("host2", {"host3"}),
};

solver.addConstraint(spec);
```

Since the conflicts make all three hosts mutually exclusive, the only valid
solutions place all three tasks on one host (the other two end up empty). This spec
is usually solved with the [optimal (MIP) solver](../../solvers/overview).
([source](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/interface/tests/ExclusiveScopeItemsTest.cpp#L87-L132))

## Conflicts

Conflicts are described by `conflictInfoList`, a list of `ScopeItemConflictInfo`.
Each entry names a `scopeItem` and the scope items it conflicts with, given as
`conflictingScopeItemsWithOverlap` --- a list of `ConflictingScopeItemInfo`
(`conflictingScopeItem`, plus an `overlap` that defaults to 1 and is only used by
the `AGGRESSIVE_PACKING` goal). Each declared conflict is symmetric, so it is enough
to list a pair once.

## Goal vs. constraint

**As a constraint**, the exclusivity is enforced: if a scope item is in use, none of
its conflicting scope items may be in use. If the initial assignment already
satisfies this, so will the final one; otherwise the general
[constraint policy](../constraint-policy) applies.

**As a goal**, the spec instead *encourages* packing that avoids conflicts, scored
by `formula`:

- `MINIMIZE_INVALIDATED_SCOPE_ITEMS_COUNT` (default) minimizes the number of scope
  items that are "invalidated"---i.e. that have at least one in-use conflicting
  scope item. Simple and effective for most cases.
- `AGGRESSIVE_PACKING` is a weighted formula for harder packing problems: it uses
  `scopeItemWeights` and per-conflict `overlap` values to more aggressively pack
  conflicting scope items together.

## Per-group conflicts

When `partitionName` is set, the exclusivity is enforced **per group** rather than
globally: two conflicting scope items may not both hold objects of the *same* group,
but objects from different groups are independent. For example, with tasks
partitioned into jobs, this keeps any one job's tasks off two conflicting hosts at
once, while different jobs are unaffected.
([source](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/interface/tests/ExclusiveScopeItemsTest.cpp#L134-L203))

## Source

- Thrift definition: [`interface/thrift/ProblemSpecs.thrift`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/interface/thrift/ProblemSpecs.thrift) (`ExclusiveScopeItemsSpec`)
- SpecBuilder: [`materializer/spec_builder/ExclusiveScopeItemsSpecBuilder.cpp`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/materializer/spec_builder/ExclusiveScopeItemsSpecBuilder.cpp)---the code that defines this spec's behavior
- Tests and runnable examples: [`interface/tests/ExclusiveScopeItemsTest.cpp`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/interface/tests/ExclusiveScopeItemsTest.cpp)---the unit tests the snippet on this page is drawn from
