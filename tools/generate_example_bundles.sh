#!/usr/bin/env bash
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0

# Runs the built example binaries with --bundle_out to produce problem bundles
# the Rebalancer Explorer can load. Each example is deterministic, so this is
# meant to run once (at image build time) rather than on every container start.
#
# Usage: generate_example_bundles.sh <bin_dir> <out_dir> <src_dir>
#   bin_dir  directory containing the installed example *.exe binaries
#   out_dir  directory to write the *.bundle files into
#   src_dir  repo root (for example input data files)

set -euo pipefail

BIN_DIR="${1:?usage: generate_example_bundles.sh <bin_dir> <out_dir> <src_dir>}"
OUT_DIR="${2:?usage: generate_example_bundles.sh <bin_dir> <out_dir> <src_dir>}"
SRC_DIR="${3:?usage: generate_example_bundles.sh <bin_dir> <out_dir> <src_dir>}"

mkdir -p "$OUT_DIR"

# gen <exe-name> <bundle-name> [extra example args...]
gen() {
  local exe="$BIN_DIR/$1"
  local name="$2"
  shift 2
  if [[ ! -x "$exe" ]]; then
    echo "ERROR: missing example binary: $exe" >&2
    exit 1
  fi
  echo ">>> generating ${name}.bundle"
  "$exe" --bundle_out "$OUT_DIR/${name}.bundle" "$@"
}

VC_INPUT="$SRC_DIR/algopt/rebalancer/examples/vertex_cover/star_and_bridge.txt"

gen VertexCover.exe     vertex_cover     --input_file "$VC_INPUT"
gen Sudoku.exe          sudoku
gen EightQueens.exe     eightqueens
gen WebBalancing.exe    web_balancing
gen ShardAllocation.exe shard_allocation
gen TasksOnHosts.exe    tasks_on_hosts

echo
echo "Wrote bundles to ${OUT_DIR}:"
ls -la "$OUT_DIR"
