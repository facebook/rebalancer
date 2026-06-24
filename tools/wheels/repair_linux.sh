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
# Use --strip-unneeded (not --strip-debug) so the post-build GHA strip step
# is a no-op on these libs.  The GHA step runs strip --strip-unneeded on
# every .so in the repaired wheel AFTER the manylinux smoke test passes.
# If auditwheel's patchelf adds a new PT_LOAD segment for the extended
# .dynstr, a second strip --strip-unneeded can drop that segment, making
# DT_STRTAB point to an unmapped address on glibc 2.39 (which is strict
# about PT_LOAD coverage) while glibc 2.28 is lenient.  Pre-stripping fully
# ensures patchelf only has to cope with debug-symbol-free libs and the
# post-build strip has nothing left to do.
echo "Pre-stripping getdeps libs (--strip-unneeded)..."
for lib_dir in $(ls -d /tmp/fbcode_builder_getdeps-*/installed/*/lib \
                          /tmp/fbcode_builder_getdeps-*/installed/*/lib64 \
                       2>/dev/null); do
    find "$lib_dir" -name '*.so' -o -name '*.so.*' 2>/dev/null | while read -r so; do
        strip --strip-unneeded "$so" 2>/dev/null || true
    done
done
echo "Done pre-stripping."

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

# Post-repair DT_STRTAB fixup: patchelf leaves DT_STRTAB in the dynamic
# section pointing to its NEW extended string table (at a different offset)
# while .dynstr section header still points to the OLD location.  readelf and
# static tools use section headers → see correct strings; the runtime linker
# uses DT_STRTAB → finds corrupted content (empty NEEDED names, etc.).
#
# Fix: surgically update DT_STRTAB in-place to match .dynstr's sh_addr.
# This is an 8-byte binary patch — no sections are moved, no ELF rebuild.
echo "Post-repair DT_STRTAB fixup..."
python3 - "$dest_dir" <<'PYEOF'
import sys, zipfile, tempfile, glob, os, struct

dest = sys.argv[1]
DT_NULL=0; DT_STRTAB=5; PT_DYNAMIC=2

def fix_dt_strtab(path):
    with open(path, 'r+b') as f:
        data = bytearray(f.read())
    if data[:4] != b'\x7fELF':
        return False
    ei_class, ei_data = data[4], data[5]
    if ei_class not in (1, 2) or ei_data not in (1, 2):
        return False
    is64 = (ei_class == 2)
    bo = '<' if ei_data == 1 else '>'

    if is64:
        e_phoff = struct.unpack_from(bo+'Q', data, 0x20)[0]
        e_phentsize, e_phnum = struct.unpack_from(bo+'HH', data, 0x36)
        e_shoff = struct.unpack_from(bo+'Q', data, 0x28)[0]
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(bo+'HHH', data, 0x3a)
    else:
        e_phoff = struct.unpack_from(bo+'I', data, 0x1c)[0]
        e_phentsize, e_phnum = struct.unpack_from(bo+'HH', data, 0x2a)
        e_shoff = struct.unpack_from(bo+'I', data, 0x20)[0]
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(bo+'HHH', data, 0x2e)

    # Find .dynstr section address via section-header string table
    shstr_off = e_shoff + e_shstrndx * e_shentsize
    if is64:
        shstr_file_off = struct.unpack_from(bo+'Q', data, shstr_off + 24)[0]
        shstr_size     = struct.unpack_from(bo+'Q', data, shstr_off + 32)[0]
    else:
        shstr_file_off = struct.unpack_from(bo+'I', data, shstr_off + 16)[0]
        shstr_size     = struct.unpack_from(bo+'I', data, shstr_off + 20)[0]
    shstrtab = data[shstr_file_off:shstr_file_off + shstr_size]

    dynstr_addr = None
    for i in range(e_shnum):
        sh = e_shoff + i * e_shentsize
        sh_name = struct.unpack_from(bo+'I', data, sh)[0]
        name_end = shstrtab.find(b'\x00', sh_name)
        name = shstrtab[sh_name:name_end].decode('utf-8', errors='replace')
        if name == '.dynstr':
            dynstr_addr = struct.unpack_from(bo+('Q' if is64 else 'I'), data, sh + (16 if is64 else 12))[0]
            break

    if dynstr_addr is None:
        return False

    # Find PT_DYNAMIC segment
    dyn_off = dyn_sz = None
    for i in range(e_phnum):
        ph = e_phoff + i * e_phentsize
        p_type = struct.unpack_from(bo+'I', data, ph)[0]
        if p_type == PT_DYNAMIC:
            if is64:
                dyn_off = struct.unpack_from(bo+'Q', data, ph + 8)[0]
                dyn_sz  = struct.unpack_from(bo+'Q', data, ph + 32)[0]
            else:
                dyn_off = struct.unpack_from(bo+'I', data, ph + 4)[0]
                dyn_sz  = struct.unpack_from(bo+'I', data, ph + 16)[0]
            break

    if dyn_off is None:
        return False

    esz = 16 if is64 else 8
    patched = False
    pos = dyn_off
    while pos + esz <= dyn_off + dyn_sz:
        d_tag = struct.unpack_from(bo+('q' if is64 else 'i'), data, pos)[0]
        if d_tag == DT_NULL:
            break
        if d_tag == DT_STRTAB:
            cur = struct.unpack_from(bo+('Q' if is64 else 'I'), data, pos + (8 if is64 else 4))[0]
            if cur != dynstr_addr:
                struct.pack_into(bo+('Q' if is64 else 'I'), data, pos + (8 if is64 else 4), dynstr_addr)
                patched = True
        pos += esz

    if patched:
        with open(path, 'wb') as f:
            f.write(data)
    return patched

for whl in glob.glob(f'{dest}/*.whl'):
    with tempfile.TemporaryDirectory() as tmp:
        with zipfile.ZipFile(whl) as zf:
            zf.extractall(tmp)

        # Scan ALL .so files in the wheel — auditwheel's patchelf touches both
        # the bundled deps (rebalancer.libs/) and librebalancer.so (_lib/).
        fixed = 0
        for root, _, files in os.walk(tmp):
            for fname in files:
                if '.so' not in fname:
                    continue
                path = os.path.join(root, fname)
                if fix_dt_strtab(path):
                    print(f'  {os.path.relpath(path, tmp)}: DT_STRTAB patched')
                    fixed += 1

        print(f'DT_STRTAB fixup: {fixed} libs patched.')

        os.remove(whl)
        with zipfile.ZipFile(whl, 'w', zipfile.ZIP_DEFLATED) as zf:
            for r, _, files in os.walk(tmp):
                for f in files:
                    fp = os.path.join(r, f)
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
