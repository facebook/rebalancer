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

from collections import Counter

from algopt.rebalancer.interface.polyglot.py_bindings.manifold import Manifold
from algopt.rebalancer.interface.py_client.ProblemSolver import (
    AsyncManifoldUploadHandle,
    ProblemSolver,
)
from libfb.py.testutil import BaseFacebookTestCase
from rebalancer.interface.thrift.Types.thrift_types import AssignmentSolution
from rebalancer.interface.thrift.v2.ProblemSolver.thrift_types import (
    ManifoldBackupParams,
    ManifoldUploadPolicy,
)
from rebalancer.interface.thrift.v2.ProblemSpecs.thrift_types import CapacitySpec
from rebalancer.interface.thrift.v2.SolverSpecs.thrift_types import (
    LocalSearchSolverSpec,
    MoveTypeSpec,
    SingleMoveTypeSpec,
)


class TestAsyncManifoldUploadHandle(BaseFacebookTestCase):
    """Tests for AsyncManifoldUploadHandle Python bindings."""

    @staticmethod
    def _setup_problem(solver: ProblemSolver) -> None:
        """Set up a simple capacity problem: 2 hosts, 4 tasks, capacity 2 each."""
        solver.setObjectName("task")
        solver.setContainerName("host")
        solver.setAssignment(
            {
                "host0": ["task0", "task1", "task2"],
                "host1": ["task3"],
            }
        )
        solver.addObjectDimension(
            "memory",
            {"task0": 10.0, "task1": 10.0, "task2": 10.0, "task3": 10.0},
        )
        solver.addContainerDimension(
            dimensionName="memory",
            containerToValue={},
            defaultValue=20.0,
        )
        solver.addConstraint(
            CapacitySpec(name="memory_capacity", scope="host", dimension="memory")
        )
        solver.addSolver(
            LocalSearchSolverSpec(
                moveTypeList=[MoveTypeSpec(singleMoveTypeSpec=SingleMoveTypeSpec())],
            )
        )

    def _verify_solution(self, solution: AssignmentSolution) -> None:
        """Verify the solution has 2 tasks per host."""
        tasks_per_host: Counter[str] = Counter()
        for host in solution.assignment.values():
            tasks_per_host[host] += 1
        self.assertEqual(2, tasks_per_host["host0"])
        self.assertEqual(2, tasks_per_host["host1"])

    def _create_new_solver_solve_and_verify(
        self,
        params: ManifoldBackupParams,
        handle: AsyncManifoldUploadHandle | None = None,
    ) -> ProblemSolver:
        """Create a solver, set up a problem, configure manifold backup, solve, verify, and return (solver, run_id)."""
        solver = ProblemSolver(service_name="rebalancer", service_scope="tests")
        self._setup_problem(solver)
        solver.setManifoldBackupParams(params, handle)
        solution = solver.solve()
        self._verify_solution(solution)
        return solver

    def _verify_and_cleanup_uploads(self, run_ids: list[str]) -> None:
        """Download bundles from manifold, verify runIds, then delete."""
        for run_id in run_ids:
            bundle = Manifold.download(run_id)
            self.assertEqual(run_id, bundle.problem.runId)
            self.assertIsNotNone(bundle.solution)
            self.assertEqual(run_id, bundle.solution.runId)

        for run_id in run_ids:
            Manifold.deleteObj(run_id)

    def test_persist_with_none_handle(self) -> None:
        """Test that persistToManifold and setManifoldBackupParams work with None handle."""
        params = ManifoldBackupParams(uploadPolicy=ManifoldUploadPolicy.ALWAYS)

        # setManifoldBackupParams with None handle (default)
        solver1 = self._create_new_solver_solve_and_verify(params)
        run_id1 = solver1.getRunId()

        # persistToManifold with None handle (default)
        solver2 = self._create_new_solver_solve_and_verify(params)
        run_id2 = solver2.getRunId()
        solver2.persistToManifold()

        self._verify_and_cleanup_uploads([run_id1, run_id2])

    def test_handle_can_be_shared_across_solvers(self) -> None:
        """Test that the same handle can be shared across multiple solvers."""
        handle = AsyncManifoldUploadHandle()
        params = ManifoldBackupParams(uploadPolicy=ManifoldUploadPolicy.ALWAYS)

        # solver1: configure handle via setManifoldBackupParams before solve
        solver1 = self._create_new_solver_solve_and_verify(params, handle)
        run_id1 = solver1.getRunId()

        # solver2: call persistToManifold with handle after solve
        solver2 = self._create_new_solver_solve_and_verify(params)
        solver2.persistToManifold(handle)
        run_id2 = solver2.getRunId()

        handle.wait()

        self._verify_and_cleanup_uploads([run_id1, run_id2])
