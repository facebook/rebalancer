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

from rebalancer.specs import (
    AssignmentSolution,
    ConstraintParams,
    ConstraintPolicy,
    ConstraintSpec,
    GoalSpec,
    GroupRoutingRings,
    ManifoldBackupParams,
    MoveStatsSpec,
    PrecisionTolerances,
    SolverSpec,
    TupperwareMoveValidatorSpec,
)

class ProblemSolver:
    def __init__(
        self,
        service_name: str,
        service_scope: str,
        can_execute_async: bool = ...,
    ) -> None: ...
    @staticmethod
    def ping() -> int: ...
    def add_constraint(
        self,
        spec: ConstraintSpec,
        policy: ConstraintPolicy | None = ...,
        invalid_cost: float | None = ...,
        invalid_state: float | None = ...,
        tuple_pos_if_broken: int | None = ...,
    ) -> ProblemSolver: ...
    def add_goal(
        self,
        spec: GoalSpec,
        weight: float = ...,
        tuple_pos: int | None = ...,
    ) -> ProblemSolver: ...
    def add_goal_boundary(self) -> ProblemSolver: ...
    def add_solver(self, spec: SolverSpec) -> ProblemSolver: ...
    def add_container_dimension(
        self,
        dimension_name: str,
        container_to_value: dict[str, float],
        default_value: float = ...,
    ) -> ProblemSolver: ...
    def add_object_dimension(
        self,
        dimension_name: str,
        object_to_value: dict[str, float],
        default_value: float = ...,
    ) -> ProblemSolver: ...
    def add_object_dimension_vector(
        self,
        dimension_name: str,
        object_to_values: dict[str, list[float]],
        default_value: float = ...,
    ) -> ProblemSolver: ...
    def add_dynamic_object_dimension(
        self,
        dimension_name: str,
        scope: str,
        scope_item_to_object_to_value: dict[str, dict[str, float]],
        default_value: float = ...,
    ) -> ProblemSolver: ...
    def add_dynamic_object_dimension_by_partition(
        self,
        dimension_name: str,
        scope: str,
        partition_name: str,
        scope_item_to_group_to_value: dict[str, dict[str, float]],
        default_value: float = ...,
    ) -> ProblemSolver: ...
    def add_object_partition_routing_dimension(
        self,
        dimension_name: str,
        partition_name: str,
        routing_config_name: str,
        group_to_value: dict[str, float],
        default_value: float = ...,
    ) -> ProblemSolver: ...
    def add_object_partition_routing_dimension_with_static_values(
        self,
        dimension_name: str,
        partition_name: str,
        routing_config_name: str,
        group_to_value: dict[str, float],
        group_to_static_value: dict[str, float],
        default_value: float = ...,
        default_static_value: float = ...,
    ) -> ProblemSolver: ...
    def add_partition(
        self,
        partition_name: str,
        group_to_objects: dict[str, list[str]],
    ) -> ProblemSolver: ...
    def add_scope(
        self,
        scope_name: str,
        container_to_scope_item: dict[str, str],
    ) -> ProblemSolver: ...
    def add_scope_dimension(
        self,
        dimension_name: str,
        scope_name: str,
        scope_item_to_value: dict[str, float],
    ) -> ProblemSolver: ...
    def add_similar_containers(
        self, similar_container_classes: list[list[str]]
    ) -> ProblemSolver: ...
    def add_routing_config(
        self,
        config_name: str,
        scope_name: str,
        partition_name: str,
        group_to_routing_rings: dict[str, GroupRoutingRings],
        origin_to_destination_latency: dict[str, dict[str, float]],
        default_origin_to_destination_scope_item_sets: (
            dict[str, list[list[str]]] | None
        ) = ...,
    ) -> ProblemSolver: ...
    def disable_solution_summary(self) -> ProblemSolver: ...
    def enable_move_stats(self, spec: MoveStatsSpec) -> ProblemSolver: ...
    def enable_move_validator(
        self, spec: TupperwareMoveValidatorSpec
    ) -> ProblemSolver: ...
    def enable_profiler(self) -> ProblemSolver: ...
    def enable_restrict_moving_object_only_once(self) -> ProblemSolver: ...
    def disable_scuba_logging(self) -> ProblemSolver: ...
    def enable_stable_as_much_as_possible(self) -> ProblemSolver: ...
    def get_current_goal_index(self) -> int: ...
    def get_run_id(self) -> str: ...
    def print_problem_setup(self) -> None: ...
    def persist_to_manifold(self) -> ProblemSolver: ...
    def save_bundle(self, path: str) -> ProblemSolver: ...
    def publish_metrics(self) -> ProblemSolver: ...
    def set_assignment(
        self, container_to_objects: dict[str, list[str]]
    ) -> ProblemSolver: ...
    def set_compact_assignment(
        self, compact_assignment_input: dict[str, dict[str, int]]
    ) -> ProblemSolver: ...
    def set_constraint_policy(self, policy: ConstraintPolicy) -> ProblemSolver: ...
    def set_default_constraint_params(
        self, params: ConstraintParams
    ) -> ProblemSolver: ...
    def set_container_name(self, container_name: str) -> ProblemSolver: ...
    def set_feasibility_tolerance(
        self, feasibility_tolerance: float
    ) -> ProblemSolver: ...
    def set_log_level(self, level: str) -> ProblemSolver: ...
    def set_manifold_backup_params(
        self, params: ManifoldBackupParams
    ) -> ProblemSolver: ...
    def set_logging_label(self, logging_label: str) -> ProblemSolver: ...
    def set_object_name(self, object_name: str) -> ProblemSolver: ...
    def set_run_id(self, run_id: str) -> ProblemSolver: ...
    def use_parallelized_new_materializer(self) -> ProblemSolver: ...
    def should_use_dynamic_object_ordering(
        self, use_dynamic_object_ordering: bool
    ) -> ProblemSolver: ...
    def set_precision(self, tolerances: PrecisionTolerances) -> ProblemSolver: ...
    def solve(self) -> AssignmentSolution: ...
