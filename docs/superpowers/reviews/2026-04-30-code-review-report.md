# Code Review Report (2026-04-30)

## Scope

- This update is the Task 6 final quality-gate synthesis over Tasks 3-6 evidence, consolidating runtime boundary/failure probes, maintainability/test-gap closure, and release/packaging risk audit artifacts.
- Scope now includes Task 5 maintainability/redundancy and test-gap evidence closure updates on top of prior Task 4 runtime boundary transcripts, plus Task 6 release/packaging risk audit and report finalization.

## Severity Model

- Blocker: Release must stop until fixed.
- High: Serious regression or correctness risk; fix before merge.
- Medium: Important quality risk; schedule promptly.
- Low: Minor issue or improvement; track and resolve.

Checklist/report status rule: plan contract tracks `pass`/`fail`/`unknown`; this execution uses checklist `Pass`/`Partial`/`Fail` where `Partial` maps to `unknown` with anchored partial evidence. Severity indicates impact triage. A checklist `Fail` can map to `Medium` severity when impact is bounded but contract is still unmet.

## Findings Format Guidance

- Each finding records: impacted file/path, severity, failure/regression mode, user/release impact, minimum remediation, and a reproducible verification step.
- Findings in this report are evidence-backed only; unknown or unexecuted checks remain in checklist/report gap sections as pending items rather than speculative findings.

## Findings

- File: `src/store.cpp:36-38,56-58` / `src/main.cpp:2016-2020,2203-2204`
- Severity: Medium
- What fails or can regress: Duplicate tag input is persisted as duplicates (`"tags":["a","a"]`) instead of normalized set semantics; invalid header format is rejected correctly, and duplicate-tag uniqueness contract is currently failing.
- Why this matters: Duplicate tags can skew tag-filter-based update/export selection semantics and create user-visible confusion in list/json outputs; behavior drift risk rises if downstream logic assumes uniqueness.
- Minimal fix: Normalize tags on ingest (dedup while preserving first-seen order) for add/edit paths; add regression tests for duplicate `--tag` and `--tags` inputs.
- Verification: Re-run Task 4 duplicate-tag probe and assert resulting JSON contains unique tags only.

- File: `src/main.cpp`
- Severity: Medium
- What fails or can regress: CLI command orchestration is concentrated in very large handlers (`doSubCommand` 562 lines, `doExportCommand` 523 lines, `doConfigCommand` 431 lines) within a 4393-line file, with repeated branch and warning-emission structures.
- Why this matters: Large multi-responsibility handlers increase regression probability for option parsing, error contracts, and output formatting whenever one behavior changes.
- Minimal fix: Incrementally extract command-path helpers (selection/filtering, warning/report formatting, target-run pipelines) into focused units with explicit inputs/outputs; retain behavior parity tests while splitting.
- Verification: Keep existing test suite green and add focused unit coverage for extracted helpers; line-count hotspot evidence captured at `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-5-1-line-count-hotspots` and `#probe-5-1-main-function-lengths`.

- File: `.github/workflows` (missing `release*` automation) / `README.md` / `README.subcli.md`
- Severity: Medium
- What fails or can regress: Packaging path is documented and locally executable (`cmake --build build --target package`), but no release automation workflow is present to enforce repeatable packaging checks or artifact publication.
- Why this matters: Release readiness depends on manual steps, increasing risk of drift between documented release expectations and shipped artifacts, especially under time pressure.
- Minimal fix: Add a minimal CI release-validation workflow (build, test, package target, artifact upload/check) and wire it to release/tag events or a manual dispatch gate.
- Verification: Confirm `.github/workflows/release*.yml` exists and successfully runs `cmake -S . -B build`, `cmake --build build -j`, `ctest --test-dir build --output-on-failure`, and `cmake --build build --target package` with artifact output.

- File: `src/cli_completion.cpp:56`
- Severity: Low
- What fails or can regress: Export shell completion omits documented/implemented flags (`--strict-capabilities`, `--explain-policy`, `--json`) while `subcli export --help` and parser support them.
- Why this matters: CLI documentation and behavior advertise options that interactive completion does not suggest; discoverability regresses and can mislead users relying on completion as command reference.
- Minimal fix: Extend export `COMPREPLY` options to include all exported flags shown in help/usage.
- Verification: Compare `./build/subcli export --help` options with `src/cli_completion.cpp:56` completion word list after patch.

- File: `src/parser.cpp:23` / `src/main.cpp:1108,4389`
- Severity: Low
- What fails or can regress: Broad `catch (...)` handlers remain in parser and top-level CLI paths, potentially suppressing error-specific diagnostics.
- Why this matters: Catch-all handling can preserve UX continuity but weakens root-cause observability, slowing triage for parser/runtime regressions.
- Minimal fix: Narrow catches to typed exceptions where possible and attach structured error context before fallback catch-all.
- Verification: Re-run parse-failure matrix and check that user-facing contracts remain stable while preserving richer diagnostic context.

## Feature Completion Summary

- covered: CLI help surface audited for root + workspace/sub/export/profile/template/asset/config; workspace precedence is partially evidenced (runtime default + invalid env rejection probes, plus unit/smoke precedence assertions) but still lacks direct runtime success/conflict transcript in this pass; strict capability/doc-code anchors are evidence-backed; release/packaging metadata now has direct evidence for version/install/CPack declarations and package artifact generation.
- not covered: Direct runtime evidence for export empty-selection and zero-node-failure contracts remains missing in this review pass.
- risk: Medium - workspace lifecycle uncertainty reduced, but export failure-contract evidence gaps and maintainability hotspots keep residual risk above Low.
- owner/ETA: CLI maintainer; 1 day for completion-flag sync and targeted export failure-contract probes, 1-2 days for maintainability refactor slicing plan.

## Boundary Coverage Summary

- covered: Task 4 boundary matrix remains evidence-backed (invalid parse payloads, missing assets/templates/profiles, invalid workspace path/marker, permission denial, strict-network timeout, duplicate/invalid headers/tags, CLI usage errors).
- not covered: Exit-code assertions for all usage-error paths remain incomplete; non-strict network fallback behavior still not directly exercised in this review pass.
- risk: Medium - one known failing contract (duplicate tag persistence) plus incomplete exit-code/fallback evidence leaves moderate residual boundary risk.
- owner/ETA: CLI maintainer; duplicate-tag normalization + tests within 1 day, remaining exit-code/fallback evidence closure within 1-2 days.

## Test Gap Summary

- covered: Workspace lifecycle precedence and env-source behavior are directly covered by unit + smoke tests; strict-capabilities blocked contract is covered by smoke assertions; parser edge invalid-input matrix has runtime evidence.
- not covered: Direct tests/probes for export failure contracts (`no subscriptions selected for export`, `no nodes parsed from enabled subscriptions`) remain missing.
- risk: Medium - high-priority workspace and strict-capability gaps reduced, but missing export-failure assertions still impact release confidence.
- owner/ETA: CLI maintainer; add targeted export failure-contract tests/probes within 1 day.

## Release Risk Summary

- covered: Five concrete quality risks now triaged (completion flag drift, duplicate-tag persistence, command-handler maintainability hotspot, broad catch concentration, missing release automation workflow) with reproducible source/test anchors; release/packaging metadata inventory is now captured.
- not covered: Direct runtime/test closure for export failure contracts remains incomplete (`no subscriptions selected for export`, `no nodes parsed from enabled subscriptions`).
- risk: Medium - no blocker/high finding, but unresolved export-failure assertions and missing release automation still prevent high-confidence release readiness.
- owner/ETA: CLI maintainer + reviewer; close export-failure evidence within 1 day and land minimal release-validation workflow within 1-2 days before release decision.

## Release Claim to Evidence Map

| Claim | Current state | Evidence anchor |
| --- | --- | --- |
| CLI command/help surface matches implemented command set | Verified | `./build/subcli --help` -> `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-3-1-root-help`; `./build/subcli workspace --help` -> `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-3-1-workspace-help`; `./build/subcli export --help` -> `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-3-1-export-help` |
| Workspace precedence behavior is partially verified for default + env override/conflict paths | Partial | `./build/subcli doctor --json` -> `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-3-2-doctor-default`; `SUBCLI_WORKSPACE="/tmp/subcli-review-ws" ./build/subcli doctor --json` -> `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-3-2-doctor-env-invalid`; `./build/subcli workspace status --json` -> `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-3-2-workspace-status`; precedence assertions indexed by `rg -n "resolveEnvironment|EnvironmentResolveInput|SUBCLI_WORKSPACE|resolution_source" tests/subcli_tests.cpp tests/cli_smoke.sh src/main.cpp` -> `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-5-4-test-gap-anchor-map` (`tests/subcli_tests.cpp:5671-5762`, `tests/cli_smoke.sh:173,187`). |
| Export runtime guarantees matrix (selected target success, empty selection failure, zero-node parse failure) is partially verified | Partial | `./build/subcli export mihomo --profile bypass-cn --sub explain --json` and `./build/subcli export all --profile bypass-cn --sub explain --strict-capabilities --json` are evidenced by `rg -n "strict-capabilities|strict_capabilities_blocked|export mihomo|no subscriptions selected|no nodes parsed" tests/subcli_tests.cpp tests/cli_smoke.sh src/main.cpp` -> `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-5-4-test-gap-anchor-map` (`tests/cli_smoke.sh:244,250-251`, `src/main.cpp:3687`); direct command-output probes still missing for failure contracts at `src/main.cpp:3482` and `src/main.cpp:3529`. |
| Profile schema and strict-capability warning contracts are anchored in source/tests | Verified (anchor-level) | Probe `probe-3-3-anchor-rg` command `rg "template_policy|default_outbound|capability_unsupported|capability_degraded|strict" src include tests` -> `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-3-3-anchor-rg`; line-level confirmations in `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-3-3-source-refs` (e.g., `src/profile.cpp:260-315`, `src/main.cpp:3687`, `tests/cli_smoke.sh:250-251`). |
| Shell completion options stay aligned with export help/options | Failing (low severity drift) | Help command `./build/subcli export --help` -> `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-3-1-export-help`; completion option source anchor `src/cli_completion.cpp:56`. |
| Packaging metadata and artifact naming are defined and reproducible | Verified | `rg -n "project\(|install\(|CPACK_|include\(CPack\)" CMakeLists.txt` -> `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-6-1-cmake-release-packaging`; `cmake --build build --target package && ls build` -> `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-6-4-package-target-run` (artifact `subcli-0.1.0-Linux-x86_64.tar.gz`). |
| Release automation workflow exists for packaging validation/publication | Failing (medium severity gap) | `ls .github/workflows/release*` -> `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-6-2-release-workflow-presence` (no file found). |

## Recommended Next Actions

1. [Medium][P1] Normalize duplicate tags on `sub add|edit` ingest and add regression tests for duplicate `--tag`/`--tags` paths.
2. [Medium][P1] Add direct runtime probes/tests for export empty-selection and zero-node failure contracts (anchors: `src/main.cpp:3482`, `src/main.cpp:3529`) so export guarantees move from Partial to Verified.
3. [Medium][P1] Add a minimal release-validation workflow under `.github/workflows/release*.yml` that runs build/test/package and captures package artifacts.
4. [Medium][P2] Build a small refactor plan to split `doSubCommand`/`doExportCommand`/`doConfigCommand` and centralize warning formatting, then execute incrementally with parity tests.
5. [Low][P2] Sync export completion flags in `src/cli_completion.cpp` with the currently supported export options (`--strict-capabilities`, `--explain-policy`, `--json`), then verify parity against `./build/subcli export --help`.
