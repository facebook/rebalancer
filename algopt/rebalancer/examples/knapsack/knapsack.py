# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# pyre-strict

from __future__ import annotations

"""
Multiple Knapsack Problem

This example demonstrates how to solve the Multiple Knapsack Problem using
Rebalancer.

Problem:
- 15 items, each with a value and weight
- 5 bins, each with weight capacity 100
- Goal: Maximize total value of packed items without exceeding bin capacity

"""

import argparse

from algopt.rebalancer.interface.py_client.ProblemSolver import ProblemSolver
from rebalancer.interface.thrift.Types.thrift_types import AssignmentSolution
from rebalancer.interface.thrift.v2.ProblemSolver.thrift_types import (
    ConstraintSpecs,
    GoalSpecs,
    SolverSpecs,
)
from rebalancer.interface.thrift.v2.ProblemSpecs.thrift_types import (
    CapacitySpec,
    Filter,
    MaximizeAllocationSpec,
)
from rebalancer.interface.thrift.v2.SolverSpecs.thrift_types import (
    LocalSearchSolverSpec,
    MoveTypeSpec,
    SingleMoveTypeSpec,
    SwapMoveTypeSpec,
)


# Data initialization

WEIGHTS: list[int] = [48, 30, 42, 36, 36, 48, 42, 42, 36, 24, 30, 30, 42, 36, 36]
VALUES: list[int] = [10, 30, 25, 50, 35, 30, 15, 40, 30, 35, 45, 10, 20, 30, 25]
BINS: list[int] = [100, 100, 100, 100, 100]


def main(bundle_out: str | None = None) -> None:
    solver: ProblemSolver = ProblemSolver(
        service_name="knapsack", service_scope="examples"
    )

    solver.setObjectName("item")
    solver.setContainerName("bin")

    # Create bin names
    bins: list[str] = [f"bin{i}" for i in range(len(BINS))]
    items: list[str] = [f"item{i}" for i in range(len(WEIGHTS))]

    # Initial assignment: all items start unassigned
    # The solver will move items to bins to maximize value
    assignment: dict[str, list[str]] = {bin_name: [] for bin_name in bins}
    assignment["unassigned"] = items.copy()
    solver.setAssignment(assignment)

    # Add weight dimension for objects (used for capacity constraint)
    item_weights: dict[str, float] = {
        f"item{i}": float(WEIGHTS[i]) for i in range(len(WEIGHTS))
    }
    solver.addObjectDimension("weight", item_weights)

    # Add value dimension for objects (used for maximization goal)
    item_values: dict[str, float] = {
        f"item{i}": float(VALUES[i]) for i in range(len(VALUES))
    }
    solver.addObjectDimension("value", item_values)

    # Set bins capacity 100, unassigned has an infinite capacity
    bin_capacities: dict[str, float] = {
        f"bin{i}": float(BINS[i]) for i in range(len(BINS))
    }
    bin_capacities["unassigned"] = 1e9
    solver.addContainerDimension("weight", bin_capacities)

    # CONSTRAINT: Items in each bin cannot exceed bin's weight capacity
    solver.addConstraint(
        ConstraintSpecs(
            capacitySpec=CapacitySpec(
                name="bin-weight-capacity",
                scope="bin",
                dimension="weight",
            )
        )
    )

    # GOAL: Maximize total value of items assigned to bins (exclude unassigned bin)
    solver.addGoal(
        GoalSpecs(
            maximizeAllocationSpec=MaximizeAllocationSpec(
                name="maximize-value",
                scope="bin",
                dimension="value",
                filter=Filter(itemsWhitelist=bins),
            )
        ),
        weight=1.0,
    )

    # Use Local Search solver. Alternatively, use OptimalSolver for such small scale problem
    solver.addSolver(
        SolverSpecs(
            localSearchSolverSpec=LocalSearchSolverSpec(
                moveTypeList=[
                    MoveTypeSpec(singleMoveTypeSpec=SingleMoveTypeSpec()),
                    MoveTypeSpec(swapMoveTypeSpec=SwapMoveTypeSpec()),
                ]
            )
        )
    )

    # Solve the problem
    solution: AssignmentSolution = solver.solve()
    solver.persistToManifold()

    # If --bundle_out is given, write a Bundle the Explorer can load (same
    # format as Manifold: zstd-compressed Thrift Binary).
    if bundle_out:
        solver.saveBundle(bundle_out)
        print(f"Wrote problem bundle to {bundle_out}")

    # Print results
    print("=" * 60)
    print("MULTIPLE KNAPSACK PROBLEM - SOLUTION")
    print("=" * 60)
    print(f"Materialization time: {solution.problemProfile.materializationSec}s")
    print(f"Solve time: {solution.problemProfile.solveSec}s")

    # Calculate and display solution details
    total_value: float = 0.0
    total_weight: float = 0.0
    items_packed: int = 0

    # get bin -> items for easy printing
    bin_to_items: dict[str, list[str]] = {bin_name: [] for bin_name in bins}
    bin_to_items["unassigned"] = []
    for item, bin_name in solution.assignment.items():
        if bin_name in bin_to_items:
            bin_to_items[bin_name].append(item)

    print("\n" + "-" * 60)
    print(f"{'Bin':<12} {'Items':<30} {'Weight':<10} {'Value':<10}")
    print("-" * 60)

    for bin_name in bins:
        bin_items: list[str] = bin_to_items.get(bin_name, [])
        bin_weight: float = sum(item_weights.get(item, 0.0) for item in bin_items)
        bin_value: float = sum(item_values.get(item, 0.0) for item in bin_items)

        # Format items list
        items_str: str = ", ".join(bin_items) if bin_items else "(empty)"
        if len(items_str) > 28:
            items_str = items_str[:25] + "..."

        print(f"{bin_name:<12} {items_str:<30} {bin_weight:<10.0f} {bin_value:<10.0f}")

        total_value += bin_value
        total_weight += bin_weight
        items_packed += len(bin_items)

    print("-" * 60)
    print(f"{'TOTAL':<12} {'':<30} {total_weight:<10.0f} {total_value:<10.0f}")

    # Show unassigned items
    unassigned_items: list[str] = bin_to_items.get("unassigned", [])
    if unassigned_items:
        unassigned_value: float = sum(
            item_values.get(item, 0.0) for item in unassigned_items
        )
        unassigned_weight: float = sum(
            item_weights.get(item, 0.0) for item in unassigned_items
        )
        print(f"\nUnassigned items ({len(unassigned_items)}):")
        for item in unassigned_items:
            print(
                f"  {item}: weight={item_weights[item]:.0f}, "
                f"value={item_values[item]:.0f}"
            )
        print(
            f"  Total unassigned: weight={unassigned_weight:.0f}, "
            f"value={unassigned_value:.0f}"
        )


def cli() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--bundle_out",
        default=None,
        help=(
            "If set, write the solved problem+solution as a Bundle to this path "
            "(same format as Manifold: zstd-compressed Thrift Binary), loadable "
            "by the Rebalancer Explorer."
        ),
    )
    args = parser.parse_args()
    main(bundle_out=args.bundle_out)


if __name__ == "__main__":
    cli()
