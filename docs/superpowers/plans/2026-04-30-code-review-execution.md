# Project Code Review Execution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute a full-repository code review that produces a prioritized, evidence-backed findings report for feature completion, boundary behavior, redundancy, test gaps, documentation drift, and release risk.

**Architecture:** Use an evidence-first review pipeline with three artifacts: (1) a checklist file for pass/fail/unknown tracking, (2) an evidence log with exact commands and outputs, and (3) a final findings report with severity-ranked issues and concrete fixes. Run baseline build/tests first, then perform six scoped review passes that map directly to the approved spec dimensions. Keep changes documentation-only unless a separate implementation task is explicitly requested.

**Tech Stack:** C++17 codebase, CMake/CTest, Bash, Markdown, Git

---

## File Responsibility Map

- `docs/superpowers/reviews/2026-04-30-code-review-checklists.md` (create): tracking tables for feature completion, boundary condition coverage, and test-gap mapping.
- `docs/superpowers/reviews/2026-04-30-code-review-evidence.md` (create): immutable evidence log of executed commands, key outputs, and reviewed file references.
- `docs/superpowers/reviews/2026-04-30-code-review-report.md` (create): final review output with severity-ranked findings and recommended next actions.
- `docs/superpowers/specs/2026-04-30-code-review-plan-design.md` (reference only): approved design contract for scope and acceptance criteria.

### Task 1: Initialize review artifacts

**Files:**
- Create: `docs/superpowers/reviews/2026-04-30-code-review-checklists.md`
- Create: `docs/superpowers/reviews/2026-04-30-code-review-evidence.md`
- Create: `docs/superpowers/reviews/2026-04-30-code-review-report.md`

- [ ] **Step 1: Create review directory**

Run: `mkdir -p docs/superpowers/reviews`
Expected: directory exists with no error.

- [ ] **Step 2: Create checklist artifact with explicit matrices**

```markdown
# 2026-04-30 Code Review Checklists

## Feature Completion Checklist

| Area | Requirement | Evidence File/Line | Status (pass/fail/unknown) | Notes |
| --- | --- | --- | --- | --- |
| CLI command surface | `README.subcli.md` commands exist in live help | | unknown | |
| Workspace precedence | `--workspace` > `SUBCLI_WORKSPACE` > defaults | | unknown | |
| Export guarantees | Mihomo/sing-box/Xray claims match behavior | | unknown | |
| Profile schema behavior | loader/validator behavior matches schema doc | | unknown | |
| Capability warnings | matrix claims match warning/strict behavior | | unknown | |
| Release claims | release plan status matches repository state | | unknown | |

## Boundary Condition Checklist

| Scenario | Expected Safe Behavior | Evidence File/Line | Status (pass/fail/unknown) | Notes |
| --- | --- | --- | --- | --- |
| Invalid YAML/JSON/base64/URI | clear error, no corrupted writes | | unknown | |
| Missing assets/templates/profiles | actionable diagnostic | | unknown | |
| Invalid workspace path/marker | deterministic failure mode | | unknown | |
| Permission denied writes | fail with clear path-level error | | unknown | |
| Network failure/timeout | bounded retries/cache fallback policy | | unknown | |
| Duplicate/invalid sub headers/tags | validation rejects bad input | | unknown | |
| CLI parse/usage errors | consistent non-zero exit codes | | unknown | |

## Test Gap Checklist

| Risk Area | Existing Coverage Evidence | Gap Description | Proposed Test | Priority |
| --- | --- | --- | --- | --- |
| Parser edge inputs | | | | high |
| Workspace lifecycle | | | | high |
| Export strict capability | | | | high |
| CLI usage error contracts | | | | medium |
| Release packaging metadata | | | | medium |
```

- [ ] **Step 3: Create evidence log template**

```markdown
# 2026-04-30 Code Review Evidence Log

## Environment

- Review date: 2026-04-30
- Repository: `/home/lisztzy/prj/subcli-cpp`
- Baseline branch/commit: record from `git log -1 --oneline`

## Command Evidence

### 1) Working tree snapshot

Command:
```bash
git status --short
```

Key output:
- Paste exact output lines.

### 2) Build and test baseline

Commands:
```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Key output:
- Configure result summary.
- Build success/failure summary.
- Test pass/fail counts.

## Reviewed Source References

- Record each reviewed file with line references and one-line finding context.
```

- [ ] **Step 4: Create final report template with severity contract**

```markdown
# 2026-04-30 Code Review Report

## Scope

Reviewed areas: feature completion, boundary conditions, redundancy/maintainability, test coverage, documentation consistency, release/packaging risk.

## Severity Model

- Blocker: release blocker, data loss risk, private leak risk, or broken primary workflow.
- High: major feature mismatch, unsafe edge case, invalid config generation, or missing critical test.
- Medium: maintainability risk, confusing diagnostics, docs drift, or secondary test gap.
- Low: polish-only issues.

## Findings

### Finding Title
- Severity: Blocker | High | Medium | Low
- File: `path:line`
- What fails or can regress:
- Why this matters:
- Minimal fix:
- Verification:

## Feature Completion Summary

- Pass:
- Fail:
- Unknown:

## Boundary Coverage Summary

- Covered:
- Gaps:

## Test Gap Summary

- Critical gaps:
- Recommended new tests:

## Release Risk Summary

- Blockers:
- Watch items:

## Recommended Next Actions

1. Immediate fixes before release.
2. Follow-up refactors with low risk.
3. Test additions to close critical gaps.
```

- [ ] **Step 5: Commit artifact scaffolding**

```bash
git add docs/superpowers/reviews/2026-04-30-code-review-checklists.md docs/superpowers/reviews/2026-04-30-code-review-evidence.md docs/superpowers/reviews/2026-04-30-code-review-report.md
git commit -m "docs: scaffold code review execution artifacts"
```

### Task 2: Capture baseline repository and verification evidence

**Files:**
- Modify: `docs/superpowers/reviews/2026-04-30-code-review-evidence.md`

- [ ] **Step 1: Record repository snapshot**

Run:
```bash
git status --short
git log --oneline -8
```

Expected: command output captured and copied into evidence log exactly.

- [ ] **Step 2: Run baseline configure/build/test**

Run:
```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: capture final status (pass or fail) with counts and first failing test if any.

- [ ] **Step 3: Append exact verification summary to evidence log**

```markdown
### Baseline verification result

- Configure/build result: pass or fail, include last success/failure line.
- Test suite result: pass or fail, include totals like `X% tests passed, Y tests failed out of Z`.
- Immediate blocker detected: yes or no, include reason.
```

- [ ] **Step 4: Commit baseline evidence**

```bash
git add docs/superpowers/reviews/2026-04-30-code-review-evidence.md
git commit -m "docs: capture baseline build and test evidence for review"
```

### Task 3: Execute feature-completion and documentation-consistency audit

**Files:**
- Modify: `docs/superpowers/reviews/2026-04-30-code-review-checklists.md`
- Modify: `docs/superpowers/reviews/2026-04-30-code-review-evidence.md`
- Modify: `docs/superpowers/reviews/2026-04-30-code-review-report.md`

- [ ] **Step 1: Validate CLI command surface against docs**

Run:
```bash
./build/subcli --help
./build/subcli workspace --help
./build/subcli sub --help
./build/subcli export --help
./build/subcli profile --help
./build/subcli template --help
./build/subcli asset --help
./build/subcli config --help
```

Expected: evidence shows whether documented command groups and key subcommands are present.

- [ ] **Step 2: Verify documented examples and path precedence claims**

Run:
```bash
./build/subcli doctor --json
SUBCLI_WORKSPACE="/tmp/subcli-review-ws" ./build/subcli doctor --json
./build/subcli workspace status --json
```

Expected: outputs reveal active environment source and workspace path behavior.

- [ ] **Step 3: Compare schema and capability docs to code paths**

Run:
```bash
rg "template_policy|default_outbound|capability_unsupported|capability_degraded|strict" src include tests
```

Expected: each major documentation claim has at least one implementation/test anchor in evidence log.

- [ ] **Step 4: Update checklists and append mismatches as findings**

```markdown
### Feature/doc mismatch finding format

- Severity: High
- File: `README.subcli.md:line` and `src/main.cpp:line`
- Mismatch: documented behavior differs from observed CLI/code behavior
- Impact: user workflow breakage or false expectation
- Minimal fix: update code or docs (choose one explicit path)
- Verification: rerun exact command proving alignment
```

- [ ] **Step 5: Commit feature/doc audit updates**

```bash
git add docs/superpowers/reviews/2026-04-30-code-review-checklists.md docs/superpowers/reviews/2026-04-30-code-review-evidence.md docs/superpowers/reviews/2026-04-30-code-review-report.md
git commit -m "docs: complete feature and documentation consistency audit"
```

### Task 4: Execute boundary-condition and failure-mode audit

**Files:**
- Modify: `docs/superpowers/reviews/2026-04-30-code-review-checklists.md`
- Modify: `docs/superpowers/reviews/2026-04-30-code-review-evidence.md`
- Modify: `docs/superpowers/reviews/2026-04-30-code-review-report.md`

- [ ] **Step 1: Run invalid input and usage-error probes**

Run:
```bash
./build/subcli sub add
./build/subcli sub validate "non-exist-id"
./build/subcli export all --profile "non-exist-profile"
./build/subcli config set fetch.timeout_sec 0
```

Expected: deterministic non-zero exits with actionable error messages.

- [ ] **Step 2: Run workspace and path failure probes**

Run:
```bash
./build/subcli doctor --workspace "/path/does/not/exist" --json
SUBCLI_WORKSPACE="/path/does/not/exist" ./build/subcli doctor --json
./build/subcli workspace status --workspace "/path/does/not/exist" --json
```

Expected: explicit failure diagnostics for invalid explicit workspace sources.

- [ ] **Step 3: Inspect exception and error-handling hotspots in code**

Run:
```bash
rg "catch \(|throw std::runtime_error|parse_error" src
```

Expected: hotspot list linked to boundary checklist items and report findings.

- [ ] **Step 4: Record data-safety outcome per scenario**

```markdown
### Boundary finding format

- Severity:
- Scenario:
- Expected safe behavior:
- Observed behavior:
- Data safety result: preserved | partial write risk
- Minimal fix:
- Verification:
```

- [ ] **Step 5: Commit boundary audit updates**

```bash
git add docs/superpowers/reviews/2026-04-30-code-review-checklists.md docs/superpowers/reviews/2026-04-30-code-review-evidence.md docs/superpowers/reviews/2026-04-30-code-review-report.md
git commit -m "docs: complete boundary condition and failure-mode audit"
```

### Task 5: Execute redundancy/maintainability and test-gap audit

**Files:**
- Modify: `docs/superpowers/reviews/2026-04-30-code-review-checklists.md`
- Modify: `docs/superpowers/reviews/2026-04-30-code-review-evidence.md`
- Modify: `docs/superpowers/reviews/2026-04-30-code-review-report.md`

- [ ] **Step 1: Quantify oversized and duplicated code risk**

Run:
```bash
wc -l src/main.cpp tests/subcli_tests.cpp
rg "resolve|workspace|template_policy|capability" src/main.cpp src/environment.cpp src/workspace.cpp src/profile.cpp src/exporter*.cpp
```

Expected: evidence includes size and repeated-logic candidates mapped to High/Medium/Low redundancy levels.

- [ ] **Step 2: Map current tests to required risk areas**

Run:
```bash
rg "workspace|template|profile|export|capability|strict|doctor|sub add|sub validate" tests/subcli_tests.cpp tests/cli_smoke.sh
```

Expected: each risk area in checklist has explicit coverage evidence or a declared gap.

- [ ] **Step 3: Add concrete proposed tests for each uncovered high-risk gap**

```markdown
### Proposed test entry format

- Risk area:
- Missing behavior assertion:
- Test file to update:
- Minimal test case:
  - Input:
  - Expected output/exit code:
```

- [ ] **Step 4: Update report with maintainability and test-gap findings**

```markdown
### Maintainability finding format

- Severity: Medium
- File: `src/main.cpp:line`
- Redundancy pattern:
- Regression risk:
- Minimal refactor seam:
- Verification strategy:
```

- [ ] **Step 5: Commit redundancy and coverage audit updates**

```bash
git add docs/superpowers/reviews/2026-04-30-code-review-checklists.md docs/superpowers/reviews/2026-04-30-code-review-evidence.md docs/superpowers/reviews/2026-04-30-code-review-report.md
git commit -m "docs: complete maintainability and test-gap audit"
```

### Task 6: Execute release/packaging risk audit and finalize report

**Files:**
- Modify: `docs/superpowers/reviews/2026-04-30-code-review-evidence.md`
- Modify: `docs/superpowers/reviews/2026-04-30-code-review-report.md`

- [ ] **Step 1: Verify release metadata and packaging assumptions**

Run:
```bash
rg "project\(|CPACK_GENERATOR|CPACK_PACKAGE_VERSION" CMakeLists.txt
ls .github/workflows
```

Expected: evidence shows actual versioning/generator/workflow presence state.

- [ ] **Step 2: Validate install surface and private-data exclusion risks**

Run:
```bash
rg "install\(" CMakeLists.txt
rg "config.yaml|sub.yaml|cache|outputs|workspace" README.md README.subcli.md docs/superpowers/plans/2026-04-29-v0.2.2-release.md
```

Expected: report identifies mismatches between release claims and tracked install/package behavior.

- [ ] **Step 3: Normalize report to severity-first ordering**

```markdown
## Final ordering rule

1. Blocker findings first.
2. High findings second.
3. Medium findings third.
4. Low findings last.

Within each severity, sort by release impact: config correctness > data safety > user-facing CLI behavior > maintainability.
```

- [ ] **Step 4: Run final verification pass for report completeness**

Run:
```bash
rg "Blocker|High|Medium|Low" docs/superpowers/reviews/2026-04-30-code-review-report.md
rg "Status \(pass/fail/unknown\)" docs/superpowers/reviews/2026-04-30-code-review-checklists.md
```

Expected: report includes all severity levels used as needed, and checklist statuses are explicitly filled.

- [ ] **Step 5: Commit final review package**

```bash
git add docs/superpowers/reviews/2026-04-30-code-review-evidence.md docs/superpowers/reviews/2026-04-30-code-review-report.md docs/superpowers/reviews/2026-04-30-code-review-checklists.md
git commit -m "docs: finalize full-project code review findings and evidence"
```

## Plan Self-Review

- Spec coverage check: tasks map directly to all six design dimensions plus severity model and acceptance criteria from `docs/superpowers/specs/2026-04-30-code-review-plan-design.md`.
- Placeholder scan: no `TBD`/`TODO` placeholders in executable steps; all steps include exact commands or concrete markdown structures.
- Consistency check: all artifacts use one date (`2026-04-30`) and one severity taxonomy (`Blocker/High/Medium/Low`).
