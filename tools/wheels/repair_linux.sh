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
# CIBW_REPAIR_WHEEL_COMMAND_LINUX wrapper. auditwheel needs
# LD_LIBRARY_PATH to point at getdeps-installed shared libs so it can
# bundle them into the wheel.
#
# cibuildwheel passes {dest_dir} and {wheel} as $1 and $2.
set -euo pipefail
dest_dir="$1"
wheel="$2"

# patchelf 0.17.2 (bundled with older auditwheel/cibuildwheel) corrupts
# .init_array entries in shared libs when expanding .dynstr for SONAME
# renaming. Specifically: gflags/glog static initializers baked into
# libfolly.so crash with SIGSEGV after patchelf moves sections.
# patchelf 0.18.0+ fixes this. Install the PyPI patchelf package (which
# ships the binary) to ensure a fixed version is in PATH before auditwheel
# runs. auditwheel finds patchelf via shutil.which, so this overrides
# whatever the container has. The >=0.18 release is pre-release on PyPI
# so --pre is required to find it.
pip install --quiet --pre "patchelf>=0.18"
echo "patchelf version: $(patchelf --version)"

# Pre-clear long getdeps RPATHs with chrpath before auditwheel runs.
#
# auditwheel uses patchelf to rename bundled-lib SONAMEs (appending a hash)
# and set RPATH=$ORIGIN.  Both operations extend .dynstr; when the existing
# RPATH is very long (getdeps embed absolute /tmp/... paths) patchelf must
# create a new, larger string table.  In patchelf <=0.18 the new table can
# be placed at a file offset that overlaps with the existing .strtab
# (C++ symbol names), causing the dynamic linker to read C++ mangled
# fragments as library names ("ev", "3.1.5", …) on glibc 2.29+.
#
# chrpath edits the RPATH field in-place (overwrites existing bytes, no
# reallocation) so it cannot trigger the overlap.  After this step every
# getdeps lib has RPATH="" (eight null bytes in the dynstr slot that held
# the long path); patchelf then only needs to write "$ORIGIN\0" (8 bytes)
# into that same slot — a one-for-one swap that never grows the string
# table.
if command -v chrpath >/dev/null 2>&1; then
    echo "Pre-clearing getdeps RPATHs with chrpath..."
    for lib_dir in $(ls -d /tmp/fbcode_builder_getdeps-*/installed/*/lib \
                              /tmp/fbcode_builder_getdeps-*/installed/*/lib64 \
                           2>/dev/null); do
        find "$lib_dir" -name '*.so' -o -name '*.so.*' 2>/dev/null | while read -r so; do
            # chrpath -d removes the RPATH entirely; chrpath -r '' sets it to
            # empty.  Use -d so patchelf starts from a clean slate.
            chrpath -d "$so" 2>/dev/null || true
        done
    done
    echo "Done pre-clearing RPATHs."
else
    echo "WARNING: chrpath not found; skipping RPATH pre-clear. Install chrpath to avoid patchelf string-table corruption."
fi

LD_LIBRARY_PATH="$(ls -d /tmp/fbcode_builder_getdeps-*/installed/*/lib \
                          /tmp/fbcode_builder_getdeps-*/installed/*/lib64 \
                       2>/dev/null | tr '\n' ':'):${LD_LIBRARY_PATH:-}" \
    auditwheel repair -w "$dest_dir" "$wheel"

# Post-repair sanity check: verify that all NEEDED entries in bundled libs
# look like real library names (start with "lib" or are well-known
# exceptions like "ld-linux*").  A patchelf string-table corruption produces
# fragments of C++ mangled names or version strings ("ev", "3.1.5", …).
# Fail loudly here rather than publishing a broken wheel.
echo "Post-repair NEEDED sanity check..."
python3 - "$dest_dir" <<'PYEOF'
import sys, zipfile, tempfile, glob, subprocess, os, re

dest = sys.argv[1]
bad = []
for whl in glob.glob(f'{dest}/*.whl'):
    with tempfile.TemporaryDirectory() as tmp:
        with zipfile.ZipFile(whl) as zf:
            zf.extractall(tmp)
        for root, _, files in os.walk(tmp):
            for fname in files:
                if '.so' not in fname:
                    continue
                path = os.path.join(root, fname)
                r = subprocess.run(
                    ['readelf', '--wide', '-d', path],
                    capture_output=True, text=True)
                for line in r.stdout.splitlines():
                    if '(NEEDED)' not in line:
                        continue
                    m = re.search(r'\[([^\]]+)\]', line)
                    if not m:
                        continue
                    needed = m.group(1)
                    # Accept: starts with "lib", or is a known loader name.
                    if not (needed.startswith('lib') or
                            needed.startswith('ld-') or
                            needed.startswith('ld.')):
                        bad.append((os.path.relpath(path, tmp), needed))

if bad:
    print('ERROR: suspicious NEEDED entries found (patchelf string-table corruption):', file=sys.stderr)
    for so, needed in bad:
        print(f'  {so}: NEEDED={needed!r}', file=sys.stderr)
    sys.exit(1)
else:
    print('All NEEDED entries look valid.')
PYEOF
