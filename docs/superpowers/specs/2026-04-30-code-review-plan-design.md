# Code Review Plan Design

## Summary

This plan defines the next project-wide code review for `subcli-cpp`. The review focuses on product readiness rather than isolated style cleanup: feature completion, boundary behavior, redundancy, test adequacy, documentation consistency, and release risk.

## Review Goal

Produce a prioritized review report that answers three questions:

- Are documented features implemented and usable through the CLI?
- Do important edge cases fail safely with clear diagnostics?
- Which redundant or oversized areas create near-term maintenance risk?

## Scope

The review covers the whole repository, with deeper attention on high-risk paths:

- CLI command wiring and exit behavior in `src/main.cpp`.
- Environment and workspace behavior in `src/environment.cpp`, `src/workspace.cpp`, and related headers.
- Subscription parsing in `src/parser*.cpp`.
- Profile loading, validation, explanation, and template policy handling in `src/profile*.cpp`.
- Native exporters and capability findings in `src/exporter*.cpp`, `src/capabilities.cpp`, and `src/capability_matrix.cpp`.
- Persistence, runtime path handling, assets, daemon/runtime helpers, and core checks.
- Test coverage in `tests/subcli_tests.cpp` and `tests/cli_smoke.sh`.
- Release/package metadata in `CMakeLists.txt`, packaging scripts, docs, and release plans.

## Non-Goals

- Do not rewrite modules during review.
- Do not perform broad formatting-only changes.
- Do not add new feature scope.
- Do not commit private subscription URLs, local workspace state, generated outputs, cache files, or tool archives.

## Review Dimensions

### 1. Feature Completion

Compare implementation against:

- `README.md`
- `README.subcli.md`
- `docs/profile-schema.md`
- `docs/capability-matrix.md`
- `docs/superpowers/specs/*`
- `docs/superpowers/plans/*`
- Recent commit intent from `git log`

Checks:

- Every documented command exists and has consistent help text.
- Command behavior matches documented examples.
- Workspace precedence matches documented priority.
- Profile schema, built-in profiles, template policy, and capability matrix match runtime behavior.
- Export target support matches documented Mihomo, sing-box, and Xray guarantees.
- Release plan status matches current files, especially versioning, CPack setup, CI workflow, and package contents.

### 2. Boundary Conditions

Review behavior for:

- Empty subscriptions and empty node sets.
- Invalid YAML, JSON, base64, and URI inputs.
- Unsupported protocols and target capability mismatches.
- Missing assets, missing templates, missing profiles, and missing core binaries.
- Invalid `--workspace`, `SUBCLI_WORKSPACE`, marker discovery, and persisted workspace paths.
- Relative vs absolute paths for config, profile, template, output, and cache values.
- Permission-denied paths and failed writes.
- Network failures, strict network mode, fetch limits, timeouts, and cache fallback behavior.
- Duplicate subscription ids/names, repeated headers, invalid header syntax, tag filters, and disabled subscriptions.
- CLI parse errors and exit-code consistency.

Expected review output for each issue:

- Failure mode.
- User-visible diagnostic quality.
- Whether data is preserved or partially written.
- Suggested minimal fix or test.

### 3. Redundancy And Maintainability

Inspect for near-term maintenance risk rather than cosmetic duplication.

Primary targets:

- Oversized command orchestration in `src/main.cpp`.
- Repeated path resolution and environment assumptions.
- Similar parser normalization logic across parser modules.
- Similar exporter node/group/rule generation logic across target exporters.
- Repeated JSON/YAML validation and warning formatting.
- Test helper duplication or brittle shell smoke assertions.

Classify redundancy:

- `High`: likely to cause inconsistent behavior or missed fixes.
- `Medium`: slows changes but current behavior is safe.
- `Low`: cosmetic duplication; no immediate action.

### 4. Test Coverage

Check existing tests against feature and boundary risks.

Required coverage map:

- Unit-like coverage for profile parsing and validation.
- Parser fixtures for URI lists, base64 lists, Mihomo provider formats, sing-box JSON, and Xray JSON.
- Export coverage for all targets and profile modes.
- Capability warning and strict-capability behavior.
- Workspace init/status/use/unset/migrate/doctor behavior.
- CLI smoke coverage for command presence, help output, usage errors, JSON output validity, and config mutation.
- Failure tests for malformed input and invalid paths.

Testing gaps should be reported with concrete proposed tests, not generic suggestions.

### 5. Documentation Consistency

Check that docs are neither stale nor overpromising.

Focus areas:

- Command examples are accepted by the current CLI.
- Documented default paths match implemented path resolution.
- Profile schema fields match loader validation.
- Capability matrix matches actual exporter warnings and strict-mode blocking.
- Release docs do not claim package/CI changes absent from tracked files.
- Private validation instructions avoid leaking private URLs.

### 6. Release And Packaging Risk

Review:

- `project()` version and CPack version/file naming.
- Platform generator selection.
- Installed files under `bin`, `share/subcli/templates`, `share/subcli/profiles`, systemd, and docs.
- Exclusion of local runtime files such as `config.yaml`, `sub.yaml`, `cache`, `outputs`, private workspaces, and private URLs.
- GitHub Actions release workflow existence and matrix behavior if present.
- First configure network dependency from `FetchContent`.

## Severity Model

Use this severity scale in the final review report:

- `Blocker`: prevents release, risks data loss, leaks private data, or documented primary workflow is broken.
- `High`: important feature mismatch, unsafe edge case, invalid config generation, or missing critical test.
- `Medium`: maintainability risk, confusing diagnostic, partial docs drift, or missing secondary coverage.
- `Low`: polish, minor duplication, or non-blocking docs/test improvement.

Each finding must include:

- Severity.
- File and line reference when available.
- What fails or can regress.
- Why it matters.
- Minimal recommended action.
- Verification command or test idea.

## Review Workflow

1. Snapshot repository state with `git status --short` and recent commits.
2. Build and run baseline tests using `cmake -S . -B build && cmake --build build -j` and `ctest --test-dir build --output-on-failure`.
3. Map documented features to source files and tests.
4. Review high-risk code paths manually.
5. Run targeted CLI probes only when they clarify behavior and do not require private data.
6. Produce prioritized findings.
7. Summarize test gaps and recommended next review/implementation steps.

## Acceptance Criteria

The review is complete when it produces:

- A prioritized findings list using the severity model.
- A feature-completion checklist with pass/fail/unknown status.
- A boundary-condition checklist with covered/gap status.
- A redundancy/maintainability risk list.
- A test-gap list with concrete proposed tests.
- A release-risk summary.
- Clear verification commands and results.

## Constraints

- Preserve existing user or agent worktree changes.
- Do not revert unrelated files.
- Do not commit unless explicitly requested.
- Do not store private URLs or local real-subscription data in repository files.
- Prefer minimal recommendations over broad rewrites.
