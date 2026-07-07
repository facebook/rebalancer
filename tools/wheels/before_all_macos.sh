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
# CIBW_BEFORE_ALL_MACOS hook for the wheels workflow.
#
# Installs Homebrew LLVM (for the C++23 features AppleClang lacks),
# runs getdeps to produce librebalancer's transitive deps, and writes
# .cmake_prefix_path for the wheel-build cmake invocation to read via
# CMakeLists.txt.
set -euo pipefail

brew install llvm
export CC=/opt/homebrew/opt/llvm/bin/clang
export CXX=/opt/homebrew/opt/llvm/bin/clang++
$CXX --version

python3 build/fbcode_builder/getdeps.py --allow-system-packages \
    install-system-deps --recursive rebalancer
python3 build/fbcode_builder/getdeps.py --allow-system-packages \
    build --build-type RelWithDebInfo --src-dir=. --no-tests rebalancer \
    --extra-cmake-defines \
    '{"CMAKE_POSITION_INDEPENDENT_CODE":"ON"}'

# Use actual install root contents, not query-paths (see Linux note).
# tr (not paste) so this works on BSD userland -- BSD paste requires
# '-' to read stdin and was silently emitting an empty
# .cmake_prefix_path here. Append Homebrew's keg-only icu4c@78 prefix
# (Boost transitively links -licudata/-licuuc/-licui18n; the icu4c@78
# cellar is not in the default linker search path).
ls -d "$TMPDIR"fbcode_builder_getdeps-*/installed/* 2>/dev/null \
    | tr '\n' ':' > .cmake_prefix_path
printf '%s:' "$(brew --prefix icu4c@78)" >> .cmake_prefix_path
echo "wrote CMAKE_PREFIX_PATH:"
tr ':' '\n' < .cmake_prefix_path
