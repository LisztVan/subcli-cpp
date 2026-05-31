# Release and Runtime Verification Guide

This guide explains how to validate, package, release, and smoke-test `subcli` across Linux, macOS, and Windows.

## 1. Cross-platform validation

Use GitHub CLI to trigger the validation workflow:

```bash
scripts/verify-cross-platform.sh
```

What it does:

1. Triggers `.github/workflows/release-validation.yml` on the current branch.
2. Waits for the GitHub Actions run to finish.
3. Fails if any of Linux, macOS, or Windows fails.
4. Prints failed logs when validation fails.

Manual equivalent:

```bash
gh workflow run release-validation.yml
gh run watch
gh run view --log-failed
```

## 2. Local package build

Build, test, and package locally:

```bash
scripts/local-package.sh
```

Useful environment variables:

```bash
BUILD_DIR=build CONFIG=Release JOBS=2 scripts/local-package.sh
```

Expected result: archives are created under `build/`, such as `.tar.gz`, `.zip`, and platform-specific packages supported by CPack.

## 3. GitHub Release

Release is tag-driven. The version tag must match the CMake project version.

```bash
scripts/release.sh
```

Dry run:

```bash
DRY_RUN=1 scripts/release.sh
```

What it does:

1. Reads version from `CMakeLists.txt`.
2. Creates an annotated tag such as `v0.2.7`.
3. Pushes the tag.
4. Waits for `.github/workflows/release.yml`.
5. Shows the created GitHub Release.

## 4. Runtime command tests

Runtime commands are optional helpers. The primary product guarantee is still subscription management and config generation. Runtime smoke tests check that helper commands work or fail gracefully.

Local test:

```bash
ctest --test-dir build -R subcli_runtime_command_smoke --output-on-failure
```

Cross-platform workflow:

```bash
gh workflow run runtime-test.yml
gh run watch
```

Covered commands:

- `subcli status`
- `subcli stop`
- `subcli run mihomo` without a configured core path
- `subcli daemon status`
- `subcli daemon once --target mihomo`

Expected behavior:

- Commands must not crash.
- Missing core binaries must produce clear errors.
- Daemon/status commands must handle empty state gracefully.

## 5. Product fitness checklist

`subcli` is considered fit for its main goal when all checks below pass:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
scripts/local-package.sh
```

And the following design points remain true:

1. Built-in templates are minimal skeletons, not hard-coded policy.
2. Profile JSON controls DNS, strategy groups, routing, default outbound, and template merge policy.
3. Users can select custom templates and profiles without recompiling.
4. Unsupported or degraded features are reported through capability findings.
5. Runtime helpers (`run`, `daemon`) are optional and do not block config generation.
