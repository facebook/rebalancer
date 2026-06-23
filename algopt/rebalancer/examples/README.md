<!--
Copyright (c) Meta Platforms, Inc. and affiliates.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0
-->

# Rebalancer examples

Small, self-contained assignment problems built with the Rebalancer client API.
Each example constructs an `AssignmentProblem`, solves it, and prints the
solution. The "real" problem examples can also emit a **problem bundle** that the
[Rebalancer Explorer](../../../rebalancer/explorer/) can load and visualize.

> This is the fbcode (Buck) build of the examples. The open-source build uses
> CMake; that variant of this doc lives in the OSS export.

| Example | Language | Target |
| --- | --- | --- |
| Multiple Knapsack | Python | `//algopt/rebalancer/examples/knapsack:knapsack` |
| Vertex Cover | C++ | `//algopt/rebalancer/examples/vertex_cover:vertex_cover` |
| Sudoku | C++ / Python | `//algopt/rebalancer/examples/sudoku:sudoku_cpp` (and `:sudoku_py`) |
| Eight Queens | C++ / Python | `//algopt/rebalancer/examples/eightqueens:eightqueens_cpp` (and `:eightqueens_py`) |
| Web Balancing | C++ / Python | `//algopt/rebalancer/examples/web_balancing:web_balancing_cpp` (and `:web_balancing_py`) |
| Shard Allocation | C++ | `//algopt/rebalancer/examples/shard_allocation:shard_allocation_cpp` |
| Tasks on Hosts | C++ | `//algopt/rebalancer/examples/tasks_on_hosts:tasksonhosts_cpp` |

## What a "problem bundle" is

A bundle is the `Bundle` Thrift struct (the `AssignmentProblem` plus the optional
`AssignmentSolution`). It is serialized with the **same format Rebalancer
persists to Manifold** — Thrift Binary protocol, zstd-compressed — so bundle
files are interoperable with Manifold blobs. The standalone Explorer reads a
bundle straight from a local file path, so no Manifold is required.

## Producing a bundle

### C++ examples

Every C++ example accepts a shared `--bundle_out` flag (defined in
[`common/BundleOutput.h`](common/BundleOutput.h)). When set, the solved problem
is written to that path after `solve()`:

```bash
buck2 run @fbcode//mode/opt //algopt/rebalancer/examples/vertex_cover:vertex_cover -- \
  --input_file $(buck2 root)/fbcode/algopt/rebalancer/examples/vertex_cover/star_and_bridge.txt \
  --bundle_out /tmp/vertex_cover.bundle
```

Without the flag the examples behave exactly as before (no file is written).

### Python examples

The Python client exposes `saveBundle(path)`. The Multiple Knapsack example takes
the same opt-in `--bundle_out` flag; without it, no file is written:

```bash
buck2 run @fbcode//mode/opt //algopt/rebalancer/examples/knapsack:knapsack -- \
  --bundle_out /tmp/knapsack.bundle
```

## Loading a bundle in the Explorer

The standalone Explorer server's file sandbox loads a bundle directly from a
local file path (the path is passed as the `manifoldId`). See
[`rebalancer/explorer`](../../../rebalancer/explorer/) for how to run the server
and drive it. The bundle file must be readable by the server process.
