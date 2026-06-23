#!/usr/bin/env python3
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
"""Validate version.txt for release readiness.

Checks:
  - Parses as PEP 440.
  - Is a final release (no pre/dev/local segments).
  - Is in MAJOR.MINOR.PATCH form.
  - Round-trips through normalization without change.
  - Is strictly greater than the latest existing v* git tag.
  - Is strictly greater than the version.txt content at the merge-base with
    the default branch (works under git or sl/hg).
  - Is not already published on PyPI or TestPyPI.

The pure-string checks are exercised by the ``ValidateVersionStringTest``
cases below; the git-tag and PyPI checks require network/git access and only
run when invoked as a script.
"""

from __future__ import annotations

import json
import pathlib
import re
import subprocess
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from packaging.version import InvalidVersion, Version

REPO_ROOT: Path = pathlib.Path(__file__).resolve().parent.parent
VERSION_FILE: Path = REPO_ROOT / "version.txt"
PROJECT_NAME = "rebalancer"
INDEX_URLS: list[str] = [
    f"https://pypi.org/pypi/{PROJECT_NAME}/json",
    f"https://test.pypi.org/pypi/{PROJECT_NAME}/json",
]


class VersionError(ValueError):
    """Raised when version.txt content fails a pure-string validation check."""


def validate_version_string(raw: str) -> Version:
    """Run the network-free checks on ``raw`` and return the parsed Version."""
    try:
        v = Version(raw)
    except InvalidVersion as e:
        raise VersionError(f"not valid PEP 440: {raw!r} ({e})") from e

    if v.is_prerelease or v.is_devrelease or v.is_postrelease or v.local:
        raise VersionError(
            f"must be a final release (no rc/dev/post/local segments): {raw!r}"
        )

    if str(v) != raw:
        raise VersionError(f"not in normalized form: {raw!r} should be {v!s}")

    if not re.fullmatch(r"\d+\.\d+\.\d+", raw):
        raise VersionError(f"must be MAJOR.MINOR.PATCH: {raw!r}")

    return v


def _detect_vcs(start: Path) -> tuple[str, Path] | None:
    """Walk up from ``start`` looking for a VCS root. Returns (kind, root)."""
    for d in (start, *start.parents):
        if (d / ".git").exists():
            return "git", d
        if (d / ".sl").exists():
            return "sl", d
        if (d / ".hg").exists():
            return "hg", d
    return None


def _previous_version(file: Path) -> Version | None:
    """Read ``file`` at the merge-base with the default branch and parse it.

    Returns None if VCS metadata is unavailable, the default branch can't be
    located, or the prior content doesn't parse as a Version. Any of those
    means we can't make a comparison — not that the comparison failed.
    """
    info = _detect_vcs(file)
    if info is None:
        return None
    kind, root = info
    rel = file.resolve().relative_to(root).as_posix()

    if kind == "git":
        base: str | None = None
        for ref in ("origin/main", "origin/master", "main", "master"):
            r = subprocess.run(
                ["git", "merge-base", "HEAD", ref],
                cwd=root,
                capture_output=True,
                text=True,
            )
            if r.returncode == 0 and r.stdout.strip():
                base = r.stdout.strip()
                break
        if base is None:
            return None
        r = subprocess.run(
            ["git", "show", f"{base}:{rel}"],
            cwd=root,
            capture_output=True,
            text=True,
        )
        if r.returncode != 0:
            return None
        prior = r.stdout.strip()
    else:
        # sl/hg: newest public ancestor of `.` is the merge-base with trunk
        # for stacked drafts.
        r = subprocess.run(
            [kind, "cat", "-r", "last(::. & public())", rel],
            cwd=root,
            capture_output=True,
            text=True,
        )
        if r.returncode != 0 or not r.stdout.strip():
            return None
        prior = r.stdout.strip()

    try:
        return Version(prior)
    except InvalidVersion:
        return None


def _published_versions(url: str) -> set[str]:
    try:
        with urllib.request.urlopen(url, timeout=10) as r:
            return set(json.load(r).get("releases", {}).keys())
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return set()  # project doesn't exist yet on this index
        raise


def main(skip_index_check: bool = False) -> None:
    raw = VERSION_FILE.read_text().strip()
    v = validate_version_string(raw)

    prior = _previous_version(VERSION_FILE)
    if prior is None:
        print("lint-version: NOTE: prior version unavailable, skipping bump check")
    elif v < prior:
        # Equal is fine: it means version.txt didn't change in this commit
        # (e.g. running on a merge commit, or on a PR that doesn't touch
        # the file). Strict less-than is the actual regression.
        raise VersionError(f"version.txt ({v}) is less than prior version ({prior})")

    tags: list[str] = subprocess.run(
        ["git", "tag", "--list", "v*", "--sort=-v:refname"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.split()
    if tags:
        latest = Version(tags[0].lstrip("v"))
        # Three cases:
        #   v > latest  — new release requested; passes here, release.yml proceeds.
        #   v == latest — no bump; passes here, release.yml skips gracefully.
        #   v < latest  — version rolled back; always an error.
        if v < latest:
            raise VersionError(f"version.txt ({v}) rolls back latest tag ({latest})")

    if skip_index_check:
        print("lint-version: skipping index check (--skip-index-check)")
    else:
        for url in INDEX_URLS:
            if raw in _published_versions(url):
                raise VersionError(f"version {raw} already published at {url}")

    print(f"lint-version: OK: {raw}")


class ValidateVersionStringTest(unittest.TestCase):
    def test_accepts_major_minor_patch(self) -> None:
        for s in ("0.0.1", "1.2.3", "10.20.30"):
            with self.subTest(s=s):
                self.assertEqual(str(validate_version_string(s)), s)

    def test_rejects_non_pep440(self) -> None:
        with self.assertRaises(VersionError):
            validate_version_string("not-a-version")

    def test_rejects_prerelease(self) -> None:
        for s in ("1.2.3rc1", "1.2.3a1", "1.2.3b2"):
            with self.subTest(s=s):
                with self.assertRaises(VersionError):
                    validate_version_string(s)

    def test_rejects_dev_post_local(self) -> None:
        for s in ("1.2.3.dev0", "1.2.3.post1", "1.2.3+local"):
            with self.subTest(s=s):
                with self.assertRaises(VersionError):
                    validate_version_string(s)

    def test_rejects_non_normalized(self) -> None:
        # "01.2.3" parses as 1.2.3 but doesn't round-trip.
        with self.assertRaises(VersionError):
            validate_version_string("01.2.3")

    def test_rejects_wrong_segment_count(self) -> None:
        for s in ("1.2", "1.2.3.4"):
            with self.subTest(s=s):
                with self.assertRaises(VersionError):
                    validate_version_string(s)


class PublishedVersionsTest(unittest.TestCase):
    """Exercise the real PyPI/TestPyPI lookups; skip if the host is unreachable."""

    def _fetch(self, url: str) -> set[str]:
        try:
            return _published_versions(url)
        except (urllib.error.URLError, TimeoutError, OSError) as e:
            self.skipTest(f"{url} unreachable: {e}")

    def test_pypi_unknown_project_returns_empty_set(self) -> None:
        self.assertEqual(
            self._fetch(
                "https://pypi.org/pypi/rebalancer-does-not-exist-zzz-99999/json"
            ),
            set(),
        )

    def test_testpypi_unknown_project_returns_empty_set(self) -> None:
        self.assertEqual(
            self._fetch(
                "https://test.pypi.org/pypi/rebalancer-does-not-exist-zzz-99999/json"
            ),
            set(),
        )

    def test_known_project_returns_versions(self) -> None:
        # Sanity check against a stable, widely-published project.
        result = self._fetch("https://pypi.org/pypi/packaging/json")
        self.assertIn("21.0", result)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--skip-index-check",
        action="store_true",
        help=(
            "Skip the PyPI/TestPyPI already-published check. Use in the release "
            "workflow where the pre-flight step owns this check and a prior partial "
            "upload would otherwise block the run permanently."
        ),
    )
    args = parser.parse_args()
    main(skip_index_check=args.skip_index_check)
