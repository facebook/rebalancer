---
sidebar_position: 2
---

# Your First Problem

In this tutorial, you'll learn how to solve a simple load balancing problem using Rebalancer. We'll distribute 12 tasks across 4 hosts to balance the load.

## The Problem

You have:
- 12 tasks that need to run
- 4 hosts to run them on
- Currently, all 12 tasks are on `host0`
- **Goal**: Distribute tasks evenly across all hosts

This is a classic load balancing problem.

## Solution

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

<Tabs groupId="programming-language">
<TabItem value="python" label="Python" default>

```python
from rebalancer import ProblemSolver
from rebalancer.specs import (
    BalanceSpec,
    CapacitySpec,
    ConstraintSpec,
    GoalSpec,
    Limit,
    OptimalSolverSpec,
    SolverSpec,
)


def main():
    # Create a ProblemSolver instance
    # service_name and service_scope are required parameters stored in the
    # Thrift problem definition. They're used internally for logging and
    # monitoring. Both must be provided; the values can be any descriptive
    # strings.
    solver = ProblemSolver(service_name="rebalancer", service_scope="examples")

    # The OSS binding takes Thrift specs as plain dicts shaped like the Thrift
    # JSON wire format (one key per active union arm). ``rebalancer.specs``
    # provides a typed view over those dict shapes via TypedDicts. At runtime
    # the constructors just produce the same dicts, but in an editor you get
    # autocomplete and warnings on typos.
    (
        solver.set_object_name("task")
        .set_container_name("host")
        # Set the initial assignment - currently all 12 tasks are on host0
        .set_assignment({
            "host0": [f"task{i}" for i in range(12)],  # All tasks on host0
            "host1": [],  # Empty
            "host2": [],  # Empty
            "host3": [],  # Empty
        })
        # Balance goal — distribute tasks evenly across hosts.
        #   scope="host"          balance across all hosts
        #   dimension="task_count"  automatic ``{objectName}_count`` dimension
        #                           created from set_object_name("task"); each
        #                           object contributes 1.0
        # The solver minimizes the imbalance in task counts across all hosts.
        .add_goal(
            GoalSpec(
                balanceSpec=BalanceSpec(
                    name="balance-hosts",
                    scope="host",
                    dimension="task_count",
                    formula="LEGACY",
                    fixAverageToInitial=True,
                )
            ),
            weight=1.0,
        )
        # Optional: capacity constraint — each host caps at 4 tasks.
        .add_constraint(
            ConstraintSpec(
                capacitySpec=CapacitySpec(
                    name="host-capacity",
                    scope="host",
                    dimension="task_count",
                    limit=Limit(type="ABSOLUTE", globalLimit=4.0),
                )
            )
        )
        # Use the optimal solver to find the best solution.
        .add_solver(SolverSpec(optimalSolverSpec=OptimalSolverSpec()))
    )

    solution = solver.solve()

    # The solution dict matches ``rebalancer.specs.AssignmentSolution``:
    #   solution["assignment"]   {object: container}
    #   solution["finalObjective"]["value"]   how good the solution is
    #                                         (lower is better; 0 is perfect)
    #   solution["solverSummaries"]   per-solver report (end reason, etc.)
    # See AssignmentSolution in interface/thrift/Types.thrift for all fields.
    print(f"Objective value: {solution['finalObjective']['value']}")

    # Print the resulting assignment, grouped by host
    by_host: dict[str, list[str]] = {}
    for task, host in solution["assignment"].items():
        by_host.setdefault(host, []).append(task)
    for host, tasks in sorted(by_host.items()):
        print(f"{host}: {' '.join(sorted(tasks))}")


if __name__ == "__main__":
    main()
```

</TabItem>
<TabItem value="cpp" label="C++">

```cpp
#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include <iostream>

using namespace facebook::rebalancer::interface;

int main() {
    // Create a ProblemSolver instance
    // service_name and service_scope are required parameters stored in the Thrift
    // problem definition. They're used internally for logging and monitoring.
    ProblemSolver solver(
        std::make_shared<folly::ThreadPoolExecutor>(4),
        "rebalancer",
        "examples"
    );

    // Define what we're assigning: tasks (objects) to hosts (containers)
    solver.setObjectName("task");
    solver.setContainerName("host");

    // Set the initial assignment - currently all 12 tasks are on host0
    std::map<std::string, std::vector<std::string>> assignment;
    for (int i = 0; i < 12; i++) {
        assignment["host0"].push_back("task" + std::to_string(i));
    }
    assignment["host1"] = {};
    assignment["host2"] = {};
    assignment["host3"] = {};
    solver.setAssignment(assignment);

    // Add a balance goal to distribute tasks evenly across hosts
    // - scope="host" means we're balancing across all hosts
    // - dimension="task_count" is an automatically created dimension
    //   The dimension name comes from setObjectName("task") above:
    //   Rebalancer creates a "{objectName}_count" dimension (here: "task_count")
    //   with a value of 1.0 for each object
    // - The solver will minimize the imbalance in task counts across all hosts
    BalanceSpec balanceSpec;
    balanceSpec.name() = "balance-hosts";
    balanceSpec.scope() = "host";
    balanceSpec.dimension() = "task_count";
    balanceSpec.formula() = BalanceSpecFormula::LEGACY;
    balanceSpec.fixAverageToInitial() = true;

    solver.addGoal(balanceSpec, 1.0);

    // Optional: Add a capacity constraint if hosts have limits
    // This example limits each host to a maximum of 4 tasks
    CapacitySpec capacitySpec;
    capacitySpec.name() = "host-capacity";
    capacitySpec.scope() = "host";
    capacitySpec.dimension() = "task_count";
    capacitySpec.limitType() = LimitType::ABSOLUTE;
    Limit limit;
    limit.globalLimit() = 4.0;
    capacitySpec.limit() = limit;

    solver.addConstraint(capacitySpec);

    // Use the optimal solver to find the best solution
    OptimalSolverSpec optimalSpec;
    solver.addSolver(optimalSpec);

    auto solution = solver.solve();

    // Understanding the solution object:
    // - solution.assignment(): Map of container -> list of objects
    // - solution.profile(): Solver statistics and timing info
    // - solution.objectiveValue(): How good the solution is (lower is better)
    // See the AssignmentSolution Thrift struct for all fields:
    // https://github.com/facebookincubator/rebalancer/blob/main/interface/thrift/Types.thrift#L342

    // Print the objective value (lower is better, 0 is perfect)
    std::cout << "Objective value: " << solution.objectiveValue() << "\n";

    // Print the resulting assignment
    for (const auto& [container, objects] : solution.assignment()) {
        std::cout << container << ": ";
        for (const auto& obj : objects) {
            std::cout << obj << " ";
        }
        std::cout << "\n";
    }

    return 0;
}
```

</TabItem>
</Tabs>

### Expected Output

```
Objective value: 0.0
host0: task0 task1 task2
host1: task3 task4 task5
host2: task6 task7 task8
host3: task9 task10 task11
```

The objective value of 0.0 indicates a perfect solution - all hosts are perfectly balanced with exactly 3 tasks each!

## Understanding the Solution

### What Happened?

1. **ProblemSolver** analyzed the current assignment (all on host0)
2. **BalanceSpec** calculated the imbalance in task counts
3. **OptimalSolver** found the optimal way to redistribute tasks
4. Tasks were moved to achieve perfect balance

### The Solution Object

The `solve()` method returns an `AssignmentSolution` object with these key fields:

- **`assignment`**: A map of containers to their assigned objects. Each object appears in exactly one container.
  ```python
  {'host0': ['task0', 'task1', 'task2'], 'host1': ['task3', ...], ...}
  ```

- **`profile`**: Solver statistics and timing information (e.g., solve time, iterations)

- **`objectiveValue`**: The quality of the solution (lower is better - 0 means perfect)

For the complete structure, see the [`AssignmentSolution` Thrift definition](https://github.com/facebookincubator/rebalancer/blob/main/interface/thrift/Types.thrift#L342).

### Key Concepts

- **Objects** (`task0`...`task11`): Things to be assigned
- **Containers** (`host0`...`host3`): Where objects go
- **Dimension** (`task_count`): What we're balancing
  - Automatically created from `setObjectName("task")`
  - Pattern: `{objectName}_count` (e.g., `task_count`, `shard_count`)
  - Each object has a value of 1.0 for this dimension
- **Scope** (`host`): Balance across all hosts
- **Goal**: What we want to optimize (balance)
- **Solver**: The algorithm that finds the solution
- **Constraint** (optional): Limits like capacity (e.g., max 4 tasks per host)

## Next Steps

Congratulations! You've solved your first assignment problem with Rebalancer.

To learn more:

- **Add resource dimensions**: [Working with Dimensions](../core-concepts/overview#dimensions)
- **Use more goals**: [Goals & Constraints Reference](../reference/)
- **Solve real problems**: [Cookbook Examples](../cookbook/)
- **Choose the right solver**: [Solver Guide](../solvers/overview)

## Full Example Code

The code from this tutorial is available in the Rebalancer repository:
- Python: [`examples/tasks_on_hosts/version1.py`](https://github.com/facebookincubator/rebalancer/blob/main/examples/tasks_on_hosts/version1.py)
- Python with goals: [`examples/tasks_on_hosts/version2.py`](https://github.com/facebookincubator/rebalancer/blob/main/examples/tasks_on_hosts/version2.py)
