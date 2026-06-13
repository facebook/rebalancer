# Release Workflow Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `wait-for-builds` in `release.yml` with a `find-release-sha` job that identifies the oldest commit >= `base_sha` where all required workflows simultaneously completed successfully, then tags and publishes from that exact SHA.

**Architecture:** Three-job pipeline — `validate` (skip-check only), `find-release-sha` (git scan + GH API check, outputs `qualifying_sha`), `publish` (downloads artifacts from `qualifying_sha`, tags, releases). No polling loops; fail fast if no qualifying SHA exists.

**Tech Stack:** GitHub Actions, `gh` CLI, `jq`, `git`, `pypa/gh-action-pypi-publish`

---

## File Map

- Modify: `.github/workflows/release.yml` — single file, three targeted changes

---

### Task 1: Remove `sha` output from `validate` and strip `wait-for-builds`

**Files:**
- Modify: `.github/workflows/release.yml`

The `validate` job currently outputs `sha` (always `github.sha`). In the new design `find-release-sha` owns the SHA, so `validate` only needs `version` and `skip`. The entire `wait-for-builds` job is deleted.

- [ ] **Step 1: Remove `sha` from `validate` outputs block**

In `.github/workflows/release.yml`, change the `validate.outputs` block from:
```yaml
    outputs:
      version: ${{ steps.read.outputs.version }}
      sha: ${{ steps.read.outputs.sha }}
      skip: ${{ steps.read.outputs.skip }}
```
to:
```yaml
    outputs:
      version: ${{ steps.read.outputs.version }}
      skip: ${{ steps.read.outputs.skip }}
```

- [ ] **Step 2: Remove `sha` from the `read` step inside `validate`**

In the `Check version against latest release` step, remove these two lines:
```sh
        SHA=$(git rev-parse HEAD)
        echo "sha=$SHA"         >> "$GITHUB_OUTPUT"
```

- [ ] **Step 3: Delete the entire `wait-for-builds` job**

Remove the full `wait-for-builds:` block (lines 98–174 in the original file).

- [ ] **Step 4: Validate YAML syntax**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/release.yml')); print('OK')"
```
Expected output: `OK`

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci: remove wait-for-builds job and sha output from validate"
```

---

### Task 2: Add `find-release-sha` job

**Files:**
- Modify: `.github/workflows/release.yml`

Insert the new job between `validate` and `publish`. It checks out the repo (full history), finds `base_sha`, iterates commits oldest-first, and exits 1 if no qualifying SHA exists.

- [ ] **Step 1: Insert `find-release-sha` job after `validate`**

Add the following block immediately after the closing line of the `validate` job (before `publish:`):

```yaml
  find-release-sha:
    needs: validate
    if: needs.validate.outputs.skip == 'false'
    runs-on: ubuntu-latest
    timeout-minutes: 10
    outputs:
      qualifying_sha: ${{ steps.scan.outputs.qualifying_sha }}
    env:
      GH_TOKEN: ${{ github.token }}
    steps:
    - uses: actions/checkout@v6
      with:
        ref: ${{ github.event.inputs.ref || github.sha }}
        fetch-depth: 0
    - name: Find oldest qualifying SHA
      id: scan
      run: |
        set -euo pipefail

        # Most recent commit that touched version.txt — lower bound of the search.
        base_sha=$(git log --format=%H -1 -- version.txt)
        if [ -z "$base_sha" ]; then
          echo "::error::version.txt has no git history"
          exit 1
        fi
        echo "base_sha=$base_sha"

        # All commits from base_sha through HEAD, oldest first.
        mapfile -t commits < <(git log --reverse --format=%H "${base_sha}^..HEAD")
        echo "Scanning ${#commits[@]} commits (oldest first)"

        qualifying_sha=""
        for sha in "${commits[@]}"; do
          all_pass=true
          for wf in $REQUIRED_WORKFLOWS; do
            run_json=$(gh run list \
              --repo "${{ github.repository }}" \
              --workflow "$wf" \
              --commit "$sha" \
              --limit 1 \
              --json status,conclusion 2>/dev/null || echo '[]')
            status=$(echo "$run_json"     | jq -r '.[0].status     // "missing"')
            conclusion=$(echo "$run_json" | jq -r '.[0].conclusion // ""')
            if [ "$status" != "completed" ] || [ "$conclusion" != "success" ]; then
              echo "  $sha: $wf not qualifying (status=$status conclusion=$conclusion)"
              all_pass=false
              break
            fi
          done
          if [ "$all_pass" = "true" ]; then
            qualifying_sha="$sha"
            echo "✅ Qualifying SHA: $sha"
            break
          fi
        done

        if [ -z "$qualifying_sha" ]; then
          echo "::error::No qualifying SHA found in [base_sha..HEAD] — re-trigger after all required workflows pass at a single commit"
          exit 1
        fi
        echo "qualifying_sha=$qualifying_sha" >> "$GITHUB_OUTPUT"
```

- [ ] **Step 2: Validate YAML syntax**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/release.yml')); print('OK')"
```
Expected output: `OK`

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci: add find-release-sha job (oldest green SHA since version bump)"
```

---

### Task 3: Update `publish` to consume `qualifying_sha`

**Files:**
- Modify: `.github/workflows/release.yml`

Three changes: `needs`, `env.SHA`, and the checkout `ref`.

- [ ] **Step 1: Update `needs` in `publish`**

Change:
```yaml
  publish:
    needs: [validate, wait-for-builds]
```
to:
```yaml
  publish:
    needs: [validate, find-release-sha]
```

- [ ] **Step 2: Update `env.SHA` in `publish`**

Change:
```yaml
      SHA: ${{ needs.validate.outputs.sha }}
```
to:
```yaml
      SHA: ${{ needs.find-release-sha.outputs.qualifying_sha }}
```

- [ ] **Step 3: Update checkout `ref` in `publish`**

Change:
```yaml
    - uses: actions/checkout@v6
      with:
        ref: ${{ needs.validate.outputs.sha }}
        fetch-depth: 0
```
to:
```yaml
    - uses: actions/checkout@v6
      with:
        ref: ${{ needs.find-release-sha.outputs.qualifying_sha }}
        fetch-depth: 0
```

- [ ] **Step 4: Validate YAML syntax**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/release.yml')); print('OK')"
```
Expected output: `OK`

- [ ] **Step 5: Verify no remaining references to old job or old output**

```bash
grep -n "wait-for-builds\|validate\.outputs\.sha\b" .github/workflows/release.yml
```
Expected output: nothing (empty)

- [ ] **Step 6: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci: publish from find-release-sha's qualifying_sha"
```

---

### Task 4: Final review

**Files:**
- Read: `.github/workflows/release.yml`

- [ ] **Step 1: Confirm job graph**

```bash
grep -E "^  [a-z]|needs:" .github/workflows/release.yml
```
Expected output shows three jobs (`validate`, `find-release-sha`, `publish`) with `find-release-sha` needing `validate` and `publish` needing both.

- [ ] **Step 2: Confirm `REQUIRED_WORKFLOWS` env is still at top level**

```bash
grep "REQUIRED_WORKFLOWS" .github/workflows/release.yml
```
Expected: one definition at the top-level `env:` block and two references — one in `find-release-sha` and one in the existing `publish` download step (which still iterates a subset of workflows for artifact download).

- [ ] **Step 3: Confirm no references to ancestor-fallback logic**

```bash
grep -n "fallback\|compare.*ahead\|fb_sha\|fb_id" .github/workflows/release.yml
```
Expected output: nothing (empty)

- [ ] **Step 4: Read the final file to confirm overall structure**

Read `.github/workflows/release.yml` end-to-end and verify:
- `validate` outputs only `version` and `skip`
- `find-release-sha` outputs `qualifying_sha`, has `timeout-minutes: 10`
- `publish` env has `SHA: ${{ needs.find-release-sha.outputs.qualifying_sha }}`
- Tag is pushed at `qualifying_sha` (via `git tag -a` after checkout at `qualifying_sha`)
