---
oncalls: ['algopt']
apply_to_regex: 'algopt/rebalancer/.*'
---

# Rebalancer

> Inherits team-wide rules from `algopt/.llms/rules/coding-standards.md`.

Rebalancer is a C++ assignment solver library that optimizes the placement of objects into containers subject to constraints and objectives. It handles problems at scale (millions of objects/containers) and supports both fast heuristic (local search) and exact (mixed-integer programming) solving strategies. Developed by the Algorithmic Optimization (algopt) team. BUCK files use `oncall("algopt")`.

**DON'T** reference the old path `fbcode/rebalancer/` — most code was migrated to `fbcode/algopt/rebalancer/`, but some components (e.g. `rebalancer/explorer`) remain at the old path.

## Quick Reference

- Wiki: https://www.internalfb.com/wiki/ReBalancer/
- Tutorial: https://www.internalfb.com/wiki/ReBalancer/Tutorial%3A_Intro_to_Rebalancer/
- Architecture: https://www.internalfb.com/wiki/ReBalancer/Rebalancer_Internals/Architecture/
- Workplace groups: `rebalancer.users`, `algo.opt`

## Critical Safety Rules

Rules in this section prevent data corruption, CI failures, and production hangs. Violating them will break builds, block diffs, or corrupt persisted data.

### Thrift Backward Compatibility

Rebalancer persists solver instances to Manifold as Thrift binary (zstd-compressed). These are deserialized by the standalone solver for replay. Field ID changes in Thrift structs break deserialization of all saved instances. Retention is limited; use `--update_manifold_expiration_days` to extend.

- **DO** use `@thrift.ReserveIds` when deprecating fields — add the removed field ID to the annotation
- **DO** only append new fields with the next available ID
- **DON'T** reuse or reassign a Thrift field ID that was previously used, even if the field was removed
- **DON'T** rebind a retired field's *name* to a new field ID — `@thrift.ReserveIds` reserves the ID only,
  so pick a new name too (`minHotObjects` → `minObjectsToExplore` in `SingleRandomStratifiedMoveTypeSpec`)
- **DON'T** change the type of an existing Thrift field — create a new field instead
- **DON'T** remove a field from a Thrift struct without adding its ID to `@thrift.ReserveIds`

These rules apply to all `.thrift` files under `interface/thrift/` and `entities/thrift/`.

### ServiceLab Benchmarks Can Block Your Diff

ServiceLab benchmarks auto-trigger when diffs touch production code directories (test directories are excluded). Trigger directories and thresholds are defined in `benchmarks/utils/benchmarkDefs.bzl`.

- Exceeding the variation threshold **blocks landing**
- **DON'T** assume benchmark failures are flakes — investigate first
- **DO** check ServiceLab benchmark results on your diff before requesting review
- **DO** set TTL to `-1` (permanent) for benchmark replay instances on Manifold via `manifold updateExpiration rebalancer/flat/solver_run_{runID} -1`

### OSS Export Boundary

The `REBALANCER_OSS_BUILD` preprocessor define separates internal and OSS code paths. `fb/` directories are excluded from OSS export.

- **DO** add Apache 2.0 license headers to all new files intended for OSS
- **DO** guard internal-only includes with `#ifndef REBALANCER_OSS_BUILD`
- **DON'T** add internal dependencies (Manifold, ScubaLog, etc.) to files outside `fb/` directories without an `#ifndef REBALANCER_OSS_BUILD` guard
- **DON'T** modify `oss_root/CMakeLists.txt` without verifying the OSS build still compiles

### Polyglot (Ligen) Bindings

Python client bindings are auto-generated from `.ligen` files in `interface/polyglot/`. When adding or changing external C++ API in `interface/`:

- **DO** update the corresponding `.ligen` file in `interface/polyglot/` to expose new API to Python clients
- **DON'T** add new external API without ensuring the polyglot Ligen bindings are also added

### LP Solver License Requirements

Tests using `OptimalSolver` require MIP solver licenses. Availability is compile-time via `#ifdef` defines in `algopt/lp/environment/fb/Constants.h` and `constexpr` functions in `algopt/lp/environment/Environment.h`:
- **x86**: Both Gurobi and Xpress available
- **aarch64**: Only Gurobi available
- **OSS**: HiGHS only (no commercial solvers)

Use `tests/SolverTestUtils.h` for portable test setup:
- `getAvailableMIPSolver()` — returns best available solver (Gurobi > XPRESS > HiGHS) as `std::optional<OptimalSolverPackage>`
- `REBALANCER_SKIP_IF_NO_MIP_SOLVER()` — skips test if no solver is available
- `REBALANCER_SKIP_IF_NO_GUROBI()` / `REBALANCER_SKIP_IF_NO_XPRESS()` / `REBALANCER_SKIP_IF_NO_HIGHS()` — skips if a specific solver is unavailable

## Build & Test

Use `@fbcode//mode/opt` for performance-sensitive builds and benchmarking — debug builds are significantly slower for solver runs.

## Validating Solver Changes with `rebalancer_perf`

`rebalancer_perf` is the primary tool for comparing solver performance across commits. Full documentation lives in the `rebalancer-perf` skill at `fbcode/algopt/rebalancer/.llms/skills/rebalancer-perf/SKILL.md`.

**When to validate:**
- **DO** include perf validation in your plan when changing: move evaluation logic, expression tree behavior, solver parameters, or materializer output
- **DO** ask the user which run IDs to test against — suggest filtering by service for targeted changes or using the `pareto` collection for broad solver changes, but let the user decide
- **DON'T** run perf validation for small features, refactors, or changes that don't affect solver behavior
- **DON'T** run it without being asked to or unless the change strictly requires it

**Note:** `rebalancer_perf` is not wired into CI. When planning diffs that touch solver behavior, call out the perf validation step in the plan and wait for approval before running it.

## Debugging

- **Standalone Solver** — Replay any saved Manifold instance locally: `buck2 run @fbcode//mode/opt fbcode//algopt/rebalancer/interface/standalone:standalone_solver -- --run_id <RUN_ID>`
- **Rebalancer Explorer** — Web UI for visualizing solutions (`bunnylol rebalancer <runId>`)
- **Scuba** — All runs (including local) logged to `rebalancer_runs` table
- **treeprof** — Hierarchical profiler; automatically instruments solver runs. Output goes to `XLOG(INFO)` with a 5%-of-total filter; use `XLOG(DBG2)` for all nodes. Memory tracking requires `@fbcode//mode/opt` (jemalloc). Do not try to toggle it manually.

### Solver Strategies

- **LocalSearch**: Heuristic — use for large production problems
- **OptimalSolver** (MIP): Guaranteed optimal, practical for ~10K objects — use for small problems requiring exact solutions
- **RasHybridSolver** (`solver/solvers/fb/`): Meta-internal hybrid solver used specifically by RAS
- **ChainSolver**: Legacy — do not use for new work
