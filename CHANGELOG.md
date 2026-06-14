# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.2] - 2026-06-14

### Fixed

- Release pipeline now publishes to PyPI after a successful TestPyPI upload,
  then creates the signed tag and GitHub Release. Previously only TestPyPI
  was targeted.
- `find-release-sha` now retries when sibling CI workflows are still running
  on HEAD, rather than failing immediately. It waits up to 80 minutes before
  giving up.

## [1.0.1] - 2026-06-13

### Fixed

- Linux wheels are now stripped of debug symbols before upload, reducing
  per-wheel size from ~133 MB to ~13 MB and bringing them under PyPI's
  100 MB per-file limit. Debug symbols are retained during CI smoke tests
  so crash diagnostics in CI are unaffected.
- Homebrew bottle files (`.bottle.tar.gz`) are no longer incorrectly
  included in the PyPI distribution set; they are still attached to the
  GitHub Release.
- Release workflow now finds the oldest commit since the version bump where
  all required CI workflows simultaneously passed, rather than waiting on
  the triggering commit. This makes releases more robust to transient CI
  failures on later commits.
- Added pre-flight validation before PyPI upload: checks file sizes against
  the 100 MB per-file limit and runs `twine check --strict` on all
  distributions. A failed pre-flight aborts before touching the index,
  preventing partial uploads.
- Release workflow no longer retries over a prior partial upload; the
  pre-flight step detects existing files on TestPyPI and instructs the
  operator to bump the version or yank the partial release.

## [1.0.0] - 2026-06-12

### Added

- Initial release of the `rebalancer` Python package and C++ SDK.
- Python wheels for Linux (`manylinux_2_28`, x86\_64) and macOS (arm64,
  14.0 Sonoma and later) for CPython 3.12, 3.13, and 3.14.
- Source distribution (sdist) for building from source on other platforms.
- C++ SDK packages: `.deb` (Debian/Ubuntu, x86\_64 and arm64) and `.rpm`
  (Fedora/RHEL, x86\_64), attached to the GitHub Release.
- `ProblemSolver`: core solver for balanced assignment optimization problems.
- Expression tree API: `Rectangle`, `Piecewise`, `Product`, `Quotient`,
  `SumOverThreshold`, `LinearSum`, `NthLargest`, `GroupRoutingLatencyLookup`,
  `GroupRoutingTrafficLookup`, and leaf transforms (`Ceil`, `Log`, `Step`,
  `Square`, `Power`).
- `ConstraintSpec`, `GoalSpec`, `SolverSpec`: declarative problem definition.
- `AssignmentSolution`: structured solver output.

[Unreleased]: https://github.com/facebookincubator/rebalancer/compare/v1.0.2...HEAD
[1.0.2]: https://github.com/facebookincubator/rebalancer/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/facebookincubator/rebalancer/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/facebookincubator/rebalancer/releases/tag/v1.0.0
