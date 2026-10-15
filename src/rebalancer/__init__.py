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
"""Rebalancer: a library for balanced assignment optimization.

The wheel ships:
  * the bundled librebalancer shared library (see :func:`get_library_path`);
  * the nanobind ``ProblemSolver`` extension class, re-exported here so
    ``from rebalancer import ProblemSolver`` works.
"""

from __future__ import annotations

import importlib.metadata
import importlib.resources
import sys

from . import specs
from ._rebalancer import ProblemSolver
from .specs import (
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
)

__all__ = [
    "AssignmentSolution",
    "ConstraintParams",
    "ConstraintPolicy",
    "ConstraintSpec",
    "GoalSpec",
    "GroupRoutingRings",
    "ManifoldBackupParams",
    "MoveStatsSpec",
    "PrecisionTolerances",
    "ProblemSolver",
    "SolverSpec",
    "__version__",
    "get_library_path",
    "specs",
]


try:
    __version__ = importlib.metadata.version(__package__ or __name__)
except importlib.metadata.PackageNotFoundError:  # pragma: no cover
    __version__ = "0+unknown"


def _library_filename() -> str:
    if sys.platform == "darwin":
        return "librebalancer.dylib"
    if sys.platform.startswith("linux"):
        return "librebalancer.so"
    raise RuntimeError(f"unsupported platform: {sys.platform!r}")


def get_library_path() -> str:
    """Return the absolute path to the bundled librebalancer shared library."""
    files = importlib.resources.files(__package__) / "_lib" / _library_filename()
    return str(files)
