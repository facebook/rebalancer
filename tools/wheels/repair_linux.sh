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
pip install --quiet "lief>=0.14"
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

# Strip debug symbols from getdeps libs before auditwheel runs.
#
# patchelf corrupts .init_array (C++ static constructors crash at dlopen)
# when it must move the .dynstr section to make room for the extended SONAME
# strings auditwheel adds (e.g. "libfolly-92ded3c6.so.0.58.0-dev").  The
# move happens because the existing PT_LOAD segments leave no gap.
# Stripping debug sections (.debug_*, .gnu_debuglink, etc.) from the
# getdeps-installed shared libs before auditwheel runs shrinks each file,
# leaving enough free space in the last PT_LOAD segment that patchelf can
# extend .dynstr in-place — no section move, no .init_array corruption.
# --strip-debug removes only debug info; it keeps .dynsym, .dynstr, and all
# executable code, so symbol lookup and dynamic linking are unaffected.
echo "Pre-stripping debug symbols from getdeps libs..."
for lib_dir in $(ls -d /tmp/fbcode_builder_getdeps-*/installed/*/lib \
                          /tmp/fbcode_builder_getdeps-*/installed/*/lib64 \
                       2>/dev/null); do
    find "$lib_dir" -name '*.so' -o -name '*.so.*' 2>/dev/null | while read -r so; do
        strip --strip-debug "$so" 2>/dev/null || true
    done
done
echo "Done pre-stripping debug symbols."

# Record original NEEDED entries from every getdeps-installed shared lib.
# After auditwheel+patchelf runs, some NEEDED entries in the bundled libs
# may be corrupted to empty strings (patchelf --replace-needed writes offset 0
# instead of the correct offset for the renamed SONAME).  We use this snapshot
# to repair them with lief below.
echo "Snapshotting original NEEDED entries..."
python3 - <<'PYEOF' > /tmp/repair_linux_needed.json
import json, subprocess, glob, os, re, sys

snapshot = {}   # soname_of_lib -> [original_needed_sonames]
for lib_dir in glob.glob('/tmp/fbcode_builder_getdeps-*/installed/*/lib') + \
               glob.glob('/tmp/fbcode_builder_getdeps-*/installed/*/lib64'):
    for so in glob.glob(f'{lib_dir}/*.so') + glob.glob(f'{lib_dir}/*.so.*'):
        r = subprocess.run(['readelf', '--wide', '-d', so],
                           capture_output=True, text=True)
        soname = None
        needed = []
        for line in r.stdout.splitlines():
            m = re.search(r'\[([^\]]+)\]', line)
            if not m:
                continue
            val = m.group(1)
            if '(SONAME)' in line:
                soname = val
            elif '(NEEDED)' in line:
                needed.append(val)
        if soname:
            snapshot[soname] = needed
json.dump(snapshot, sys.stdout)
PYEOF
echo "Snapshot written."

LD_LIBRARY_PATH="$(ls -d /tmp/fbcode_builder_getdeps-*/installed/*/lib \
                          /tmp/fbcode_builder_getdeps-*/installed/*/lib64 \
                       2>/dev/null | tr '\n' ':'):${LD_LIBRARY_PATH:-}" \
    auditwheel repair -w "$dest_dir" "$wheel"

# Post-repair lief fix: patchelf 0.18.0 --replace-needed can write string-
# table offset 0 (empty string) instead of the correct offset for the new
# hash-suffixed SONAME.  Use the pre-repair snapshot and lief to restore any
# corrupted NEEDED entries.  lief rebuilds the ELF correctly, updating all
# relocations (including .init_array) so no constructors are misaddressed.
echo "Post-repair lief NEEDED fixup..."
python3 - "$dest_dir" /tmp/repair_linux_needed.json <<'PYEOF'
import sys, json, zipfile, tempfile, glob, os, re, shutil
import lief

dest = sys.argv[1]
snapshot = json.load(open(sys.argv[2]))   # original_soname -> [original_needed_sonames]

for whl in glob.glob(f'{dest}/*.whl'):
    with tempfile.TemporaryDirectory() as tmp:
        with zipfile.ZipFile(whl) as zf:
            zf.extractall(tmp)

        # ELF dynamic tag constants — stable across lief versions.
        DT_NEEDED = 1
        DT_SONAME = 14

        def tag_int(e):
            t = e.tag
            return t.value if hasattr(t, 'value') else int(t)

        # Collect bundled libs: new_soname -> file_path
        bundled = {}
        for so in glob.glob(f'{tmp}/**/rebalancer.libs/*.so*', recursive=True):
            elf = lief.parse(so)
            if elf is None:
                continue
            sn = next((e.name for e in elf.dynamic_entries if tag_int(e) == DT_SONAME), None)
            if sn:
                bundled[sn] = so

        # Build mapping: original_soname -> new_soname (by matching filenames)
        # auditwheel inserts a hash between lib name and version, e.g.
        # libfolly.so.0.58.0-dev -> libfolly-92ded3c6.so.0.58.0-dev
        orig_to_new = {}
        for new_sn, path in bundled.items():
            # Strip the hash suffix: libfoo-XXXXXXXX.so.X -> libfoo.so.X
            orig = re.sub(r'-[0-9a-f]{8}(?=\.)', '', new_sn)
            if orig != new_sn:
                orig_to_new[orig] = new_sn

        if not orig_to_new:
            print('No SONAME renames detected; skipping lief fixup.')
            continue

        print(f'SONAME renames: {orig_to_new}')

        # Fix each .so in the wheel
        fixed = 0
        for root, _, files in os.walk(tmp):
            for fname in files:
                if '.so' not in fname:
                    continue
                path = os.path.join(root, fname)
                elf = lief.parse(path)
                if elf is None:
                    continue
                changed = False
                my_soname = next(
                    (e.name for e in elf.dynamic_entries if tag_int(e) == DT_SONAME),
                    '')
                for entry in elf.dynamic_entries:
                    if tag_int(entry) != DT_NEEDED:
                        continue
                    if entry.name == '':
                        # Determine the correct name: look at original NEEDED
                        # entries for this lib (matched by SONAME) and find
                        # which one maps to a bundled SONAME.
                        orig_needed = snapshot.get(my_soname, [])
                        for on in orig_needed:
                            nn = orig_to_new.get(on)
                            if nn:
                                # Check not already referenced by a non-empty entry
                                already = any(
                                    tag_int(e) == DT_NEEDED and e.name == nn
                                    for e in elf.dynamic_entries)
                                if not already:
                                    print(f'  {fname}: fixing empty NEEDED -> {nn}')
                                    entry.name = nn
                                    changed = True
                                    fixed += 1
                                    break
                # Always rewrite through lief, even if no entries were
                # corrected. patchelf leaves DT_STRTAB pointing to a new
                # string table that differs from what readelf/.dynstr shows.
                # When lief rebuilds the ELF it sets DT_STRTAB = the rebuilt
                # .dynstr, resolving the inconsistency that causes the runtime
                # linker to find empty NEEDED strings.
                elf.write(path)

        print(f'lief fixup complete: {fixed} entries corrected (all bundled libs rewritten).')

        # Repack the wheel in-place
        os.remove(whl)
        with zipfile.ZipFile(whl, 'w', zipfile.ZIP_DEFLATED) as zf:
            for root, _, files in os.walk(tmp):
                for f in files:
                    fp = os.path.join(root, f)
                    zf.write(fp, os.path.relpath(fp, tmp))
PYEOF

# Post-repair sanity check: verify that all NEEDED entries in bundled libs
# look like real library names (start with "lib" or are well-known
# exceptions like "ld-linux*").  A patchelf string-table corruption produces
# fragments of C++ mangled names, version strings ("ev", "3.1.5", …), or
# empty strings.  Fail loudly here rather than publishing a broken wheel.
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
                    # Use * (not +) to also catch empty-string NEEDED entries.
                    m = re.search(r'\[([^\]]*)\]', line)
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
