#!/usr/bin/env bash
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
#
# CIBW_REPAIR_WHEEL_COMMAND_MACOS wrapper.
#
# cmake (3.31.x / scikit-build-core 0.12.x) fails to embed LC_RPATH entries
# in MODULE and SHARED targets on macOS arm64 for this project's build
# configuration: INSTALL_RPATH / INSTALL_RPATH_USE_LINK_PATH are set in
# CMakeLists.txt but the installed binaries come out with empty LC_RPATH.
# Without an LC_RPATH, delocate-wheel's @rpath resolver has an empty search
# path and raises DelocationError even though the libraries exist.
#
# This script patches the missing rpaths into the built wheel before handing
# off to the standard delocate-wheel repair. Two patches are needed:
#
#   _rebalancer.cpython-*.so   <- add @loader_path/_lib so delocate can walk
#                                  the dep chain to librebalancer.dylib.
#   _lib/librebalancer.dylib   <- add each getdeps/brew prefix lib/ dir so
#                                  delocate can locate and bundle transitive
#                                  deps (libfolly, libglog, libfmt, …).
#
# After delocate runs those transitive deps land in rebalancer/.dylibs/, their
# install names are rewritten to @loader_path/../.dylibs/<lib>, and all
# LC_LOAD_DYLIB entries in librebalancer.dylib are updated accordingly.
#
# cibuildwheel calls: bash tools/wheels/repair_macos.sh {dest_dir} {wheel} {delocate_archs}
set -euo pipefail

dest_dir="$1"
wheel="$2"
delocate_archs="${3:-arm64}"

# Project root (script lives at tools/wheels/repair_macos.sh).
project_dir="$(cd "$(dirname "$0")/../.." && pwd)"

tmpdir=$(mktemp -d)
trap "rm -rf $tmpdir" EXIT

# Extract the wheel (it's a zip file).
python3 -c "
import zipfile, sys
zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])
" "$wheel" "$tmpdir/wheel"

# Collect getdeps/brew lib dirs from .cmake_prefix_path.
# These are needed as rpaths on BOTH binaries:
#   _rebalancer.so: the macOS two-level-namespace linker records libfolly as a
#     direct LC_LOAD_DYLIB even with -undefined dynamic_lookup, because
#     Bindings.cpp uses folly symbols. Without a rpath pointing at the real
#     libfolly, delocate can't find it to bundle.
#   librebalancer.dylib: needs its own transitive deps (libfolly, libglog, …).
prefix_file="$project_dir/.cmake_prefix_path"
lib_dirs=()
delocate_L_args=()
if [[ -f "$prefix_file" ]]; then
    while IFS= read -r prefix; do
        [[ -z "$prefix" ]] && continue
        libdir="${prefix}/lib"
        [[ -d "$libdir" ]] || continue
        lib_dirs+=("$libdir")
        delocate_L_args+=("-L" "$libdir")
    done < <(tr ':' '\n' < "$prefix_file")
else
    echo "repair_macos: WARNING: $prefix_file not found; delocate may fail"
fi

# --- Patch 1: _rebalancer extension module ---
# Add @loader_path/_lib (→ librebalancer.dylib) plus all getdeps lib dirs
# (→ libfolly, libfmt, etc. that the macOS linker recorded as direct deps).
for ext in "$tmpdir/wheel/rebalancer/_rebalancer"*.so; do
    [[ -f "$ext" ]] || continue
    install_name_tool -add_rpath @loader_path/_lib "$ext" 2>/dev/null || true
    for libdir in "${lib_dirs[@]}"; do
        install_name_tool -add_rpath "$libdir" "$ext" 2>/dev/null || true
    done
    echo "repair_macos: patched rpaths in $(basename "$ext")"
done

# --- Patch 2: librebalancer.dylib ---
librebalancer="$tmpdir/wheel/rebalancer/_lib/librebalancer.dylib"
if [[ -f "$librebalancer" ]]; then
    for libdir in "${lib_dirs[@]}"; do
        install_name_tool -add_rpath "$libdir" "$librebalancer" 2>/dev/null || true
    done
    echo "repair_macos: patched rpaths in librebalancer.dylib"
fi

# Repack the patched wheel.
repacked="$tmpdir/repacked"
mkdir -p "$repacked"
wheel_name=$(basename "$wheel")
python3 -c "
import zipfile, os, sys
src, out_dir, name = sys.argv[1], sys.argv[2], sys.argv[3]
out = os.path.join(out_dir, name)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as zf:
    for root, dirs, files in os.walk(src):
        for f in files:
            fp = os.path.join(root, f)
            zf.write(fp, os.path.relpath(fp, src))
print('repair_macos: repacked to', out)
" "$tmpdir/wheel" "$repacked" "$wheel_name"

# Standard delocate repair: bundles transitive deps and rewrites LC_LOAD_DYLIB.
# -L <dir> provides extra fallback search paths so delocate can locate any
# @rpath dep that couldn't be found through LC_RPATH alone.
delocate-wheel --require-archs "$delocate_archs" \
    "${delocate_L_args[@]}" \
    -w "$dest_dir" -v "$repacked/$wheel_name"
