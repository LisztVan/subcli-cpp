# v0.2.6 Release Readiness Follow-up

## Scope

This follow-up records the current state after the v0.2.6 release publication and closes the stale high-priority gaps from the 2026-04-30 review report.

## Current Release State

- Release tag: `v0.2.6`
- GitHub Release: https://github.com/LisztVan/subcli-cpp/releases/tag/v0.2.6
- Published artifacts:
  - `subcli-0.2.6-Darwin-arm64.tar.gz`
  - `subcli-0.2.6-Linux-x86_64.tar.gz`
  - `subcli-0.2.6-Windows-AMD64.tar.gz`
- Release workflow: passed on tag `v0.2.6`.
- Release validation workflow: passed on tag `v0.2.6`.

## Closed Review Gaps

| Previous gap | Current status | Evidence |
| --- | --- | --- |
| Duplicate tag persistence | Closed | `normalizeTags` coverage exists in `tests/subcli_tests.cpp`; CLI duplicate `--tag`/`--tags` regression is covered in `tests/cli_smoke.sh`. |
| Export empty-selection and zero-node failure probes | Closed | `tests/cli_smoke.sh` asserts `no subscriptions selected for export (check --sub/--tag filters)` and `no nodes parsed from enabled subscriptions`. |
| Release automation missing | Closed | `.github/workflows/release.yml` builds Linux/macOS/Windows artifacts and publishes GitHub Release assets; `.github/workflows/release-validation.yml` validates tag/version/build/test/package. |
| Export completion drift | Closed | `src/cli_completion.cpp` includes `--strict-capabilities`, `--explain-policy`, and `--json`; tests assert these options. |

## Remaining Non-blocking Work

- `src/main.cpp` remains a maintainability hotspot. Future work should split command handlers incrementally with parity tests.
- Windows release packaging is supported for config generation. POSIX-style managed runtime/daemon process hosting remains intentionally unsupported on Windows and should stay documented as optional helper behavior.
- GitHub Actions currently emits Node.js 20 deprecation warnings from upstream actions. This is not release-blocking, but action versions should be revisited before the deprecation deadline.

## Verification Snapshot

Fresh local verification on 2026-05-11:

```text
ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 2
```

GitHub verification on 2026-05-11:

```text
Release: success
Release Validation: success
Release assets: Darwin arm64, Linux x86_64, Windows AMD64
```
