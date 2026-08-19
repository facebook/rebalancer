#!/usr/bin/env fbpython
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
import unittest

from algopt.rebalancer.interface.py_client.ProblemSolver import ProblemSolver
from rebalancer.interface.thrift.v2.ProblemSolver.thrift_types import (
    ConstraintSpecs,
    GoalSpecs,
    SolverSpecs,
)
from rebalancer.interface.thrift.v2.ProblemSpecs.thrift_types import (
    AvoidMovingSpec,
    CapacitySpec,
    CapacitySpecBound,
    CapacitySpecDefinition,
    Filter,
    Limit,
    NonAcceptingSpec,
)
from rebalancer.interface.thrift.v2.SolverSpecs.thrift_types import (
    LocalSearchSolverSpec,
    LocalSearchStageSolverSpec,
    LocalSearchStageSpec,
    MoveTypeSpec,
    SingleMoveTypeSpec,
)


class StageMovesTest(unittest.TestCase):
    def setup_problem(self, solver: ProblemSolver) -> None:
        solver.setObjectName("task")
        solver.setContainerName("host")

        initial_assignment = {
            "host0": ["task1"],
            "host1": ["task2"],
            "host2": ["task3"],
            "host3": [],
            "host4": ["task10", "task11", "task12"],
            "host5": ["task13", "task14"],
            "host6": ["task15"],
            "host7": [],
        }
        solver.setAssignment(initial_assignment)

        solver.addScope(
            scopeName="important_containers",
            containerToScopeItem={
                "host3": "scopeItem1",
                "host4": "scopeItem1",
                "host5": "scopeItem1",
                "host6": "scopeItem1",
                "host7": "scopeItem1",
            },
        )

        solver.addGoal(
            GoalSpecs(
                capacitySpec=CapacitySpec(
                    name="maximize new capacity of host3...host7",
                    scope="important_containers",
                    dimension="task_count",
                    limit=Limit(globalLimit=1),
                    bound=CapacitySpecBound.MIN,
                    definition=CapacitySpecDefinition.NEW,
                )
            )
        )

        solver.addConstraint(
            ConstraintSpecs(
                capacitySpec=CapacitySpec(
                    name="new capacity all hosts <= 1",
                    scope="host",
                    dimension="task_count",
                    limit=Limit(globalLimit=1),
                    bound=CapacitySpecBound.MAX,
                    definition=CapacitySpecDefinition.NEW,
                    filter=Filter(
                        itemsWhitelist=["host3", "host4", "host5", "host6", "host7"]
                    ),
                )
            )
        )

        # the following constraints together make "host4", "host5", "host6", "host7"
        # fixed (i.e., those hosts can neither take new objects nor give out any)
        solver.addConstraint(
            ConstraintSpecs(
                nonAcceptingSpec=NonAcceptingSpec(
                    name="all hosts expect cannot accept tasks",
                    scope="host",
                    items=[
                        "host0",
                        "host1",
                        "host2",
                        "host4",
                        "host5",
                        "host6",
                        "host7",
                    ],
                )
            )
        )

        solver.addConstraint(
            ConstraintSpecs(
                avoidMovingSpec=AvoidMovingSpec(
                    name="do not move objects",
                    objects=[
                        "task10",
                        "task11",
                        "task12",
                        "task13",
                        "task14",
                        "task15",
                    ],
                )
            )
        )

        # pyre-fixme[16]: `StageMovesTest` has no attribute `stageSpecs`.
        self.stageSpecs = [
            LocalSearchStageSpec(
                begin=0,
                end=1,
                solverSpec=LocalSearchSolverSpec(
                    moveTypeList=[
                        MoveTypeSpec(singleMoveTypeSpec=SingleMoveTypeSpec()),
                    ],
                    exploreMovesFromContainersNotInObjective=False,
                ),
            ),
        ]

    # pyre-fixme[2]: Parameter must be annotated.
    def getTaskCountInEachHost(self, solution) -> dict[str, int]:
        taskCount: dict[str, int] = {}
        for _, host in solution.assignment.items():
            if host not in taskCount:
                taskCount[host] = 1
            else:
                taskCount[host] += 1

        return taskCount

    def test_explore_with_containers_not_in_objective(self) -> None:
        # This test verifies that when using LocalSearchStageSolver, once exploration with all containers relevant to the objective is exhausted,
        # there is exploration with containers that are not part of the objective (in the example below, that is all hosts except host0)
        # when exploreMovesFromContainersNotInObjective is set to 'true'
        solver = ProblemSolver(service_name="rebalancer", service_scope="tests")
        solver.setLogLevel("dbg2")
        self.setup_problem(solver)
        solver.shouldUseDynamicObjectOrdering(True)

        solver.addSolver(
            SolverSpecs(
                localSearchStageSolverSpec=LocalSearchStageSolverSpec(
                    stageSpecs=self.stageSpecs,
                    exploreMovesFromContainersNotInObjective=True,
                )
            )
        )

        solution = solver.solve()

        # we expect host3 to have 1 task
        taskCount: dict[str, int] = self.getTaskCountInEachHost(solution)
        self.assertEqual(taskCount.get("host3", 0), 1)

        self.assertAlmostEqual(solution.initialObjective.value, 1.0)
        self.assertAlmostEqual(solution.finalObjective.value, 0.0)

    def test_default_moves_exploration(self) -> None:
        # This test verifies that when using LocalSearchStageSolver, once exploration with all containers relevant to the objective is exhausted,
        # there is exploration with containers that are not part of the objective only when it is explicitly
        # opted into using exploreMovesFromContainersNotInObjective (like in the test case above)
        solver = ProblemSolver(service_name="rebalancer", service_scope="tests")
        solver.setLogLevel("dbg2")
        self.setup_problem(solver)
        solver.shouldUseDynamicObjectOrdering(True)

        solver.addSolver(
            SolverSpecs(
                localSearchStageSolverSpec=LocalSearchStageSolverSpec(
                    stageSpecs=self.stageSpecs,
                    # exploreMovesFromContainersNotInObjective is unset by default, which means that in each stage
                    # the moves from containers not part of objective are not explored
                )
            )
        )

        solution = solver.solve()

        # assert that there was no move
        taskCount: dict[str, int] = self.getTaskCountInEachHost(solution)
        self.assertEqual(taskCount["host0"], 1)
        self.assertEqual(taskCount["host1"], 1)
        self.assertEqual(taskCount["host2"], 1)
        self.assertEqual(taskCount.get("host3", 0), 0)

        self.assertAlmostEqual(solution.initialObjective.value, 1.0)
        self.assertAlmostEqual(solution.finalObjective.value, 1.0)
