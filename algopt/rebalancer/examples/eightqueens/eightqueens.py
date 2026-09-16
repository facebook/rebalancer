#!/usr/bin/env python3
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

import sys
from collections.abc import Callable, Collection

from algopt.rebalancer.interface.py_client.ProblemSolver import ProblemSolver
from rebalancer.interface.thrift.v2.ProblemSolver.thrift_types import (
    ConstraintSpecs,
    SolverSpecs,
)
from rebalancer.interface.thrift.v2.ProblemSpecs.thrift_types import (
    GroupCountSpec,
    GroupCountSpecDefinition,
    Limit,
    LimitType,
)
from rebalancer.interface.thrift.v2.SolverSpecs.thrift_types import (
    LocalSearchSolverSpec,
    MoveTypeSpec,
    OptimalSolverPackage,
    OptimalSolverSpec,
    SingleFastMoveTypeSpec,
)


# See tutorial at:
# https://www.internalfb.com/intern/wiki/ReBalancer/API/Tutorial/


def print_board(
    squares: Collection[tuple[int, int] | str],
    pos_func: Callable[[int, int], tuple[int, int] | str],
) -> None:
    board_size = len(squares)
    for i in range(board_size):
        print(
            "".join(
                " Q |" if pos_func(i, j) in squares else "   |"
                for j in range(board_size)
            )
        )
        print("---+---+---+---+---+---+---+---+")


def main() -> None:
    chess_size = 8
    # Chess board has 8x8 squares, indexed from 0 to 7
    # We specify that 8 queens will be placed on these positions.
    initial_assigned = {
        (0, 0): 0,
        (1, 4): 1,
        (2, 1): 2,
        (3, 2): 3,
        (4, 6): 4,
        (5, 0): 5,
        (6, 5): 6,
        (7, 3): 7,
    }

    print("Initial assignment")
    print_board(initial_assigned.keys(), lambda i, j: (i, j))

    solver = ProblemSolver(service_name="rebalancer", service_scope="examples")
    solver.setObjectName("queen")
    solver.setContainerName("square")

    # We're optimizing how the queens are assigned to different squares on a
    # chess board. In ReBalancer terminology, we have queens as objects and
    # squares as containers. Objects can move, containers are immutable.
    # Objects can be assigned to one container at a time during the object's
    # lifecycle.
    initial_board = {}
    for i in range(chess_size):
        for j in range(chess_size):
            initial_board["s_{}_{}".format(i, j)] = (
                ["q_{}".format(initial_assigned[(i, j)])]
                if (i, j) in initial_assigned
                else []
            )
    # Call to setAssignment also tells the solver which objects and which
    # containers we are dealing with. In our case containers are "s_{i}_{j}"
    # and objects are "q_{n}"
    solver.setAssignment(initial_board)

    # Scopes are a way to put containers (squares) into groups that can
    # potentially overlap. These will allow us specify constraints later on.
    chess_rows = {}
    chess_columns = {}
    chess_diag1 = {}
    chess_diag2 = {}
    for i in range(chess_size):
        for j in range(chess_size):
            chess_rows["s_{}_{}".format(i, j)] = "row_{}".format(i)
            chess_columns["s_{}_{}".format(i, j)] = "column_{}".format(j)
            # Right-left diagonals can be uniquely identified with: i + j
            chess_diag1["s_{}_{}".format(i, j)] = "diag1_{}".format(i + j)
            # Left-right diagonals can be uniquely identified with: i - j
            chess_diag2["s_{}_{}".format(i, j)] = "diag2_{}".format(i - j + chess_size)

    solver.addScope("chess_rows", chess_rows)
    solver.addScope("chess_columns", chess_columns)
    solver.addScope("chess_diag1", chess_diag1)
    solver.addScope("chess_diag2", chess_diag2)

    # All queens belong to a single partition
    queens_group = {"queens": ["q_{}".format(i) for i in range(chess_size)]}
    solver.addPartition("queens_group", queens_group)

    limit = Limit(globalLimit=1, type=LimitType.ABSOLUTE)

    # Limit each scope to have only queen (from the queens group)
    for scope in ["chess_rows", "chess_columns", "chess_diag1", "chess_diag2"]:
        constraint = GroupCountSpec(
            name=f"group_count_{scope}",
            scope=scope,
            limit=limit,
            partitionName="queens_group",
            definition=GroupCountSpecDefinition.AFTER,
        )
        solver.addConstraint(ConstraintSpecs(groupCountSpec=constraint))

    # The greedy solver tries one move at a time and is meant to be highly
    # scalable for simple types of moves. However this leaner solver can't find
    # an optimal solution, as we can see in this example. The reason being none
    # of the simple individual moves can take this problem to a global optimum.
    # In other words, greedy solvers usually get stuck in local optimum
    # solutions. This flag executes a MIP (Mixed Integer Programming) solver
    # which looks for a globally optimal solution.
    if "--optimal" in sys.argv:
        solver_spec = OptimalSolverSpec(solverPackage=OptimalSolverPackage.HIGHS)
        solver.addSolver(SolverSpecs(optimalSolverSpec=solver_spec))
    else:
        solver.addSolver(
            SolverSpecs(
                localSearchSolverSpec=LocalSearchSolverSpec(
                    moveTypeList=[
                        MoveTypeSpec(singleFastMoveTypeSpec=SingleFastMoveTypeSpec())
                    ],
                )
            )
        )

    solution = solver.solve()
    squares = set(solution.assignment.values())
    print("Final assignment")
    print_board(squares, lambda i, j: "s_{}_{}".format(i, j))
    if solution.finalObjective.value > 0.01:
        raise Exception("failed to solve completely")


if __name__ == "__main__":
    main()
