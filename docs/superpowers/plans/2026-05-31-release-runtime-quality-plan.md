# Release Runtime Quality Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn `SOLUTION.md` into working project changes: reliable cross-platform CI/release automation, runtime command tests, small safety fixes, and updated documentation proving subcli is fit as a flexible subscription-to-core-config generator.

**Architecture:** Keep the product architecture unchanged: subscription parsing, profile/template-driven export, and optional runtime helpers remain separate concerns. Add focused shell scripts under `scripts/`, one GitHub Actions workflow for runtime command smoke tests, targeted C++ safety fixes in existing modules, and tests/docs that prove behavior. Avoid large refactors of `main.cpp`; record them as future work instead.

**Tech Stack:** C++17, CMake/CTest, GitHub Actions, GitHub CLI (`gh`), Bash, PowerShell-compatible GitHub runner shell behavior, yaml-cpp, nlohmann_json, libcurl.

---

## How to use this plan

1. Work from the repository root: `/home/lisztzy/prj/subcli-cpp`.
2. Before starting, make sure you are on a feature branch:

```bash
git status --short
git switch -c chore/release-runtime-quality
```

Expected:
- `git status --short` shows either nothing or only files you intentionally want to keep.
- `git switch -c ...` succeeds, or says the branch already exists.

3. Execute tasks in order. Do not skip the verification steps.
4. Commit after each task using the commit command shown in that task.
5. If a step fails, stop and fix that step before moving on.

---

## File Structure

### Files to create

- `scripts/verify-cross-platform.sh`
  - Uses `gh` to trigger `.github/workflows/release-validation.yml`, waits for completion, prints failed logs on failure.

- `scripts/local-package.sh`
  - Builds, tests, and runs CPack locally. Produces local release archives.

- `scripts/release.sh`
  - Creates/pushes a version tag and waits for `.github/workflows/release.yml` to publish GitHub Release assets.

- `.github/workflows/runtime-test.yml`
  - Runs runtime command smoke tests on Linux, macOS, and Windows.

- `tests/runtime_command_smoke.cmake`
  - CTest script that checks `run`, `stop`, `status`, and `daemon` commands fail gracefully or work where possible.

- `docs/release-and-runtime.md`
  - Human-facing guide for cross-platform validation, release, runtime command testing, and final product-position checks.

### Files to modify

- `CMakeLists.txt`
  - Add a `subcli_runtime_command_smoke` CTest entry using `tests/runtime_command_smoke.cmake`.

- `src/fetch.cpp`
  - Add `CURLOPT_MAXREDIRS` for HTTP redirects.

- `tests/subcli_tests.cpp`
  - Add a unit test proving redirected HTTP fetches are bounded.

- `tests/stability_http_server.hpp`
  - Add a redirect endpoint declaration.

- `tests/stability_http_server.cpp`
  - Implement redirect-loop test endpoint.

- `SOLUTION.md`
  - Update it after implementation so it links to the scripts/workflow/docs created by this plan.

### Files intentionally not refactored in this plan

- `src/main.cpp`
  - It is too large, but splitting it is a broad refactor. This plan focuses on high-value, low-risk release/runtime improvements first.

- `src/exporter_common.cpp` WireGuard path
  - Existing tests already cover sing-box WireGuard endpoint behavior (`testExportSingBoxWireGuardUsesEndpoint`). This plan verifies the concern and documents it, but does not change working exporter behavior unless tests fail.

---

## Task 1: Add cross-platform validation script using `gh`

**Files:**
- Create: `scripts/verify-cross-platform.sh`

- [ ] **Step 1: Create the scripts directory**

Run:

```bash
mkdir -p scripts
```

Expected: command exits with code `0`.

- [ ] **Step 2: Create `scripts/verify-cross-platform.sh`**

Write this exact file:

```bash
cat > scripts/verify-cross-platform.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

WORKFLOW="release-validation.yml"
REF="${1:-$(git branch --show-current)}"

if ! command -v gh >/dev/null 2>&1; then
  echo "error: GitHub CLI 'gh' is not installed or not on PATH" >&2
  echo "install: https://cli.github.com/" >&2
  exit 127
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "error: gh is not authenticated" >&2
  echo "run: gh auth login" >&2
  exit 1
fi

if [[ -z "$REF" ]]; then
  echo "error: could not determine git ref; pass branch/tag as first argument" >&2
  echo "usage: scripts/verify-cross-platform.sh [ref]" >&2
  exit 2
fi

echo "=== Triggering $WORKFLOW on ref: $REF ==="
gh workflow run "$WORKFLOW" --ref "$REF"

echo "Waiting for GitHub to create a run..."
sleep 8

RUN_ID=""
for attempt in 1 2 3 4 5 6 7 8 9 10; do
  RUN_ID="$(gh run list --workflow="$WORKFLOW" --branch "$REF" --limit=1 --json databaseId -q '.[0].databaseId' 2>/dev/null || true)"
  if [[ -n "$RUN_ID" && "$RUN_ID" != "null" ]]; then
    break
  fi
  echo "Run not visible yet, retry $attempt/10..."
  sleep 5
done

if [[ -z "$RUN_ID" || "$RUN_ID" == "null" ]]; then
  echo "error: could not find a workflow run for $WORKFLOW on $REF" >&2
  gh run list --workflow="$WORKFLOW" --limit=10 || true
  exit 1
fi

echo "=== Watching run: $RUN_ID ==="
gh run watch "$RUN_ID"

echo "=== Run summary ==="
gh run view "$RUN_ID"

CONCLUSION="$(gh run view "$RUN_ID" --json conclusion -q '.conclusion')"
if [[ "$CONCLUSION" != "success" ]]; then
  echo "error: cross-platform validation failed with conclusion: $CONCLUSION" >&2
  echo "=== Failed logs ===" >&2
  gh run view "$RUN_ID" --log-failed >&2 || true
  exit 1
fi

echo "success: Linux, macOS, and Windows validation passed"
EOF
chmod +x scripts/verify-cross-platform.sh
```

Expected:
- File exists.
- File is executable.

- [ ] **Step 3: Verify shell syntax**

Run:

```bash
bash -n scripts/verify-cross-platform.sh
```

Expected: no output, exit code `0`.

- [ ] **Step 4: Verify help/error behavior without running CI**

Run:

```bash
scripts/verify-cross-platform.sh --definitely-not-a-real-ref 2>&1 | head -5 || true
```

Expected:
- If `gh` is installed and authenticated, the script may attempt to trigger a workflow and fail because the ref is invalid.
- If `gh` is missing or unauthenticated, it prints a clear error.
- It must not print a Bash syntax error.

- [ ] **Step 5: Commit**

Run:

```bash
git add scripts/verify-cross-platform.sh
git commit -m "chore: add gh cross-platform validation script"
```

Expected: commit succeeds.

---

## Task 2: Add local packaging script

**Files:**
- Create: `scripts/local-package.sh`

- [ ] **Step 1: Create `scripts/local-package.sh`**

Write this exact file:

```bash
cat > scripts/local-package.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD_DIR:-build}"
CONFIG="${CONFIG:-Release}"
JOBS="${JOBS:-}"

if [[ -z "$JOBS" ]]; then
  if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
  else
    JOBS="2"
  fi
fi

VERSION="$(python3 - <<'PY'
import re
from pathlib import Path
text = Path('CMakeLists.txt').read_text(encoding='utf-8')
match = re.search(r'project\s*\(\s*subcli\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)', text, re.I)
print(match.group(1) if match else '')
PY
)"

if [[ -z "$VERSION" ]]; then
  echo "error: could not read project version from CMakeLists.txt" >&2
  exit 1
fi

echo "=== Building subcli v$VERSION ==="
echo "Build dir: $BUILD_DIR"
echo "Config:    $CONFIG"
echo "Jobs:      $JOBS"

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$BUILD_DIR" --config "$CONFIG" -j "$JOBS"
ctest --test-dir "$BUILD_DIR" --build-config "$CONFIG" --output-on-failure
cmake --build "$BUILD_DIR" --config "$CONFIG" --target package

echo "=== Package artifacts ==="
find "$BUILD_DIR" -maxdepth 1 -type f \
  \( -name 'subcli-*.tar.gz' -o -name 'subcli-*.tgz' -o -name 'subcli-*.zip' -o -name 'subcli-*.deb' \) \
  -print | sort

echo "success: local package build completed"
EOF
chmod +x scripts/local-package.sh
```

Expected: file created and executable.

- [ ] **Step 2: Verify shell syntax**

Run:

```bash
bash -n scripts/local-package.sh
```

Expected: no output, exit code `0`.

- [ ] **Step 3: Run a real local package build**

Run:

```bash
BUILD_DIR=build CONFIG=Release JOBS=2 scripts/local-package.sh
```

Expected:
- CMake configure succeeds.
- Build succeeds.
- `ctest` reports all tests passed.
- Output lists package files such as `build/subcli-0.2.7-Linux-x86_64.zip` or similar.

- [ ] **Step 4: Commit**

Run:

```bash
git add scripts/local-package.sh
git commit -m "chore: add local package build script"
```

Expected: commit succeeds.

---

## Task 3: Add release helper script

**Files:**
- Create: `scripts/release.sh`

- [ ] **Step 1: Create `scripts/release.sh`**

Write this exact file:

```bash
cat > scripts/release.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

WORKFLOW="release.yml"
REMOTE="${REMOTE:-origin}"
DRY_RUN="${DRY_RUN:-0}"

if ! command -v gh >/dev/null 2>&1; then
  echo "error: GitHub CLI 'gh' is not installed or not on PATH" >&2
  exit 127
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "error: gh is not authenticated" >&2
  echo "run: gh auth login" >&2
  exit 1
fi

VERSION="$(python3 - <<'PY'
import re
from pathlib import Path
text = Path('CMakeLists.txt').read_text(encoding='utf-8')
match = re.search(r'project\s*\(\s*subcli\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)', text, re.I)
print(match.group(1) if match else '')
PY
)"

if [[ -z "$VERSION" ]]; then
  echo "error: could not read project version from CMakeLists.txt" >&2
  exit 1
fi

TAG="v$VERSION"

echo "=== Release preparation ==="
echo "Version: $VERSION"
echo "Tag:     $TAG"
echo "Remote:  $REMOTE"

if ! git diff --quiet || ! git diff --cached --quiet; then
  echo "error: working tree has uncommitted changes" >&2
  git status --short >&2
  exit 1
fi

if git rev-parse "$TAG" >/dev/null 2>&1; then
  echo "error: tag already exists locally: $TAG" >&2
  exit 1
fi

if git ls-remote --tags "$REMOTE" "$TAG" | grep -q "$TAG"; then
  echo "error: tag already exists on remote $REMOTE: $TAG" >&2
  exit 1
fi

if [[ "$DRY_RUN" == "1" ]]; then
  echo "dry-run: would run: git tag -a $TAG -m 'Release $TAG'"
  echo "dry-run: would run: git push $REMOTE $TAG"
  echo "dry-run: would wait for workflow: $WORKFLOW"
  exit 0
fi

read -r -p "Create and push $TAG? Type 'yes' to continue: " CONFIRM
if [[ "$CONFIRM" != "yes" ]]; then
  echo "cancelled"
  exit 0
fi

git tag -a "$TAG" -m "Release $TAG"
git push "$REMOTE" "$TAG"

echo "Waiting for GitHub to create release workflow run..."
sleep 10

RUN_ID=""
for attempt in 1 2 3 4 5 6 7 8 9 10; do
  RUN_ID="$(gh run list --workflow="$WORKFLOW" --limit=1 --json databaseId -q '.[0].databaseId' 2>/dev/null || true)"
  if [[ -n "$RUN_ID" && "$RUN_ID" != "null" ]]; then
    break
  fi
  echo "Run not visible yet, retry $attempt/10..."
  sleep 5
done

if [[ -z "$RUN_ID" || "$RUN_ID" == "null" ]]; then
  echo "error: could not find release workflow run" >&2
  exit 1
fi

gh run watch "$RUN_ID"
CONCLUSION="$(gh run view "$RUN_ID" --json conclusion -q '.conclusion')"
if [[ "$CONCLUSION" != "success" ]]; then
  echo "error: release workflow failed: $CONCLUSION" >&2
  gh run view "$RUN_ID" --log-failed >&2 || true
  exit 1
fi

echo "=== GitHub Release ==="
gh release view "$TAG"

echo "success: release published for $TAG"
EOF
chmod +x scripts/release.sh
```

Expected: file created and executable.

- [ ] **Step 2: Verify shell syntax**

Run:

```bash
bash -n scripts/release.sh
```

Expected: no output, exit code `0`.

- [ ] **Step 3: Verify dry-run mode**

Run:

```bash
DRY_RUN=1 scripts/release.sh
```

Expected:
- If `gh` is installed/authenticated and the version tag does not already exist, it prints `dry-run: would run...` lines.
- If `gh` is unavailable, it prints a clear `gh` error.
- It must not create or push a tag.

- [ ] **Step 4: Commit**

Run:

```bash
git add scripts/release.sh
git commit -m "chore: add release helper script"
```

Expected: commit succeeds.

---

## Task 4: Add runtime command smoke CTest script

**Files:**
- Create: `tests/runtime_command_smoke.cmake`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create `tests/runtime_command_smoke.cmake`**

Write this exact file:

```bash
cat > tests/runtime_command_smoke.cmake <<'EOF'
cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SUBCLI_BIN)
  message(FATAL_ERROR "SUBCLI_BIN is required")
endif()
if(NOT DEFINED TEST_WORK_DIR)
  message(FATAL_ERROR "TEST_WORK_DIR is required")
endif()
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}")
file(COPY "${SOURCE_DIR}/templates" DESTINATION "${TEST_WORK_DIR}")
file(COPY "${SOURCE_DIR}/profiles" DESTINATION "${TEST_WORK_DIR}")

function(run_cmd NAME EXPECTED_RESULT)
  execute_process(
    COMMAND "${SUBCLI_BIN}" ${ARGN}
    WORKING_DIRECTORY "${TEST_WORK_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 20
  )
  string(CONCAT combined "${output}" "${error}")
  message(STATUS "[${NAME}] exit=${result}")
  message(STATUS "[${NAME}] output=${combined}")

  if(EXPECTED_RESULT STREQUAL "zero")
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "${NAME} expected exit 0 but got ${result}: ${combined}")
    endif()
  elseif(EXPECTED_RESULT STREQUAL "non_crash")
    if(result EQUAL 134 OR result EQUAL 139)
      message(FATAL_ERROR "${NAME} crashed with ${result}: ${combined}")
    endif()
  else()
    message(FATAL_ERROR "unknown EXPECTED_RESULT: ${EXPECTED_RESULT}")
  endif()

  set("${NAME}_OUTPUT" "${combined}" PARENT_SCOPE)
  set("${NAME}_RESULT" "${result}" PARENT_SCOPE)
endfunction()

run_cmd(config_init zero config init --portable)
run_cmd(doctor zero doctor --json)
run_cmd(status_without_runtime non_crash status)
run_cmd(stop_without_runtime non_crash stop)
run_cmd(daemon_status_without_runtime non_crash daemon status)
run_cmd(daemon_once_no_subscriptions non_crash daemon once --target mihomo --interval 1)
run_cmd(run_without_core non_crash run mihomo)

if(run_without_core_RESULT EQUAL 0)
  message(FATAL_ERROR "run mihomo without a configured core should not succeed")
endif()

string(FIND "${run_without_core_OUTPUT}" "not found" not_found_pos)
string(FIND "${run_without_core_OUTPUT}" "core_paths" core_paths_pos)
string(FIND "${run_without_core_OUTPUT}" "mihomo" mihomo_pos)
if(not_found_pos EQUAL -1 AND core_paths_pos EQUAL -1 AND mihomo_pos EQUAL -1)
  message(FATAL_ERROR "run mihomo error should mention missing core/mihomo/core_paths: ${run_without_core_OUTPUT}")
endif()
EOF
```

Expected: file exists.

- [ ] **Step 2: Add CTest entry to `CMakeLists.txt`**

Find this block near the existing tests:

```cmake
add_test(
    NAME subcli_platform_boundary_scan
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/tests/platform_boundary_scan.cmake"
)
```

Immediately after that block, insert:

```cmake
add_test(
    NAME subcli_runtime_command_smoke
    COMMAND ${CMAKE_COMMAND}
        "-DSUBCLI_BIN=$<TARGET_FILE:subcli>"
        "-DTEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/subcli-runtime-command-smoke-$<CONFIG>"
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/tests/runtime_command_smoke.cmake"
)
```

Use this Python command to do the insertion exactly:

```bash
python3 - <<'PY'
from pathlib import Path
path = Path('CMakeLists.txt')
text = path.read_text()
old = '''add_test(
    NAME subcli_platform_boundary_scan
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/tests/platform_boundary_scan.cmake"
)
'''
new = old + '''
add_test(
    NAME subcli_runtime_command_smoke
    COMMAND ${CMAKE_COMMAND}
        "-DSUBCLI_BIN=$<TARGET_FILE:subcli>"
        "-DTEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/subcli-runtime-command-smoke-$<CONFIG>"
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/tests/runtime_command_smoke.cmake"
)
'''
if old not in text:
    raise SystemExit('target block not found')
path.write_text(text.replace(old, new, 1))
PY
```

Expected: command exits with code `0`.

- [ ] **Step 3: Configure and run the new test**

Run:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build -R subcli_runtime_command_smoke --output-on-failure
```

Expected:
- Configure succeeds.
- Build succeeds.
- `subcli_runtime_command_smoke` passes.

- [ ] **Step 4: Run all tests**

Run:

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

Run:

```bash
git add CMakeLists.txt tests/runtime_command_smoke.cmake
git commit -m "test: add runtime command smoke coverage"
```

Expected: commit succeeds.

---

## Task 5: Add cross-platform runtime GitHub Actions workflow

**Files:**
- Create: `.github/workflows/runtime-test.yml`

- [ ] **Step 1: Create `.github/workflows/runtime-test.yml`**

Write this exact file:

```bash
cat > .github/workflows/runtime-test.yml <<'EOF'
name: Runtime Command Tests

on:
  workflow_dispatch:
  pull_request:
  push:
    branches:
      - "**"

permissions:
  contents: read

jobs:
  runtime-test:
    name: Runtime ${{ matrix.name }}
    runs-on: ${{ matrix.os }}
    strategy:
      fail-fast: false
      matrix:
        include:
          - name: linux-x86_64
            os: ubuntu-latest
          - name: macos-arm64
            os: macos-latest
          - name: windows-x86_64
            os: windows-latest

    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Install Linux dependencies
        if: runner.os == 'Linux'
        run: sudo apt-get update && sudo apt-get install -y cmake g++ make

      - name: Configure
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

      - name: Build
        run: cmake --build build --config Release -j

      - name: Runtime smoke test
        run: ctest --test-dir build --build-config Release -R subcli_runtime_command_smoke --output-on-failure

      - name: Full test suite
        run: ctest --test-dir build --build-config Release --output-on-failure
EOF
```

Expected: workflow file exists.

- [ ] **Step 2: Validate YAML shape with Python**

Run:

```bash
python3 - <<'PY'
from pathlib import Path
text = Path('.github/workflows/runtime-test.yml').read_text()
required = [
    'name: Runtime Command Tests',
    'ubuntu-latest',
    'macos-latest',
    'windows-latest',
    'subcli_runtime_command_smoke',
]
for item in required:
    if item not in text:
        raise SystemExit(f'missing {item}')
print('runtime-test.yml contains required entries')
PY
```

Expected: prints `runtime-test.yml contains required entries`.

- [ ] **Step 3: Commit**

Run:

```bash
git add .github/workflows/runtime-test.yml
git commit -m "ci: add cross-platform runtime command tests"
```

Expected: commit succeeds.

---

## Task 6: Bound HTTP redirects in fetcher

**Files:**
- Modify: `src/fetch.cpp`
- Modify: `tests/stability_http_server.hpp`
- Modify: `tests/stability_http_server.cpp`
- Modify: `tests/subcli_tests.cpp`

- [ ] **Step 1: Inspect current fetch redirect settings**

Run:

```bash
grep -n "CURLOPT_FOLLOWLOCATION" src/fetch.cpp
```

Expected: one line similar to:

```text
149:    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
```

- [ ] **Step 2: Add failing unit test for redirect loop**

Append this function to `tests/subcli_tests.cpp` immediately after `testFetchFileUrlDecodesPercentEscapes()`:

```cpp
void testFetchHttpRedirectLoopIsBounded() {
    const fs::path fixtureDir = fs::temp_directory_path() / "subcli-fetch-redirect-fixtures";
    fs::remove_all(fixtureDir);
    fs::create_directories(fixtureDir);

    subcli::StabilityHttpServer server(fixtureDir);
    server.start();

    subcli::Subscription sub;
    sub.id = "redirect-loop";
    sub.name = "redirect-loop";
    sub.url = server.url("/redirect-loop");
    sub.timeout = 5;
    sub.retry = 0;
    sub.fetchMaxBytes = 1024 * 1024;

    auto result = subcli::fetchSubscription(sub, false);
    server.stop();
    fs::remove_all(fixtureDir);

    require(!result.ok, "redirect loop fetch should fail");
    require(result.error.find("redirect") != std::string::npos || result.error.find("Redirect") != std::string::npos,
            "redirect loop error should mention redirect");
}
```

Then register it near the bottom of `tests/subcli_tests.cpp` with the other `runTest(...)` calls, immediately after `runTest("testFetchFileUrlDecodesPercentEscapes", testFetchFileUrlDecodesPercentEscapes);`:

```cpp
    runTest("testFetchHttpRedirectLoopIsBounded", testFetchHttpRedirectLoopIsBounded);
```

Use this Python command to apply both edits:

```bash
python3 - <<'PY'
from pathlib import Path
path = Path('tests/subcli_tests.cpp')
text = path.read_text()
anchor = '''void testDecodeFileUrlPathPreservesWindowsStyleDrivePath() {
'''
insert = '''void testFetchHttpRedirectLoopIsBounded() {
    const fs::path fixtureDir = fs::temp_directory_path() / "subcli-fetch-redirect-fixtures";
    fs::remove_all(fixtureDir);
    fs::create_directories(fixtureDir);

    subcli::StabilityHttpServer server(fixtureDir);
    server.start();

    subcli::Subscription sub;
    sub.id = "redirect-loop";
    sub.name = "redirect-loop";
    sub.url = server.url("/redirect-loop");
    sub.timeout = 5;
    sub.retry = 0;
    sub.fetchMaxBytes = 1024 * 1024;

    auto result = subcli::fetchSubscription(sub, false);
    server.stop();
    fs::remove_all(fixtureDir);

    require(!result.ok, "redirect loop fetch should fail");
    require(result.error.find("redirect") != std::string::npos || result.error.find("Redirect") != std::string::npos,
            "redirect loop error should mention redirect");
}

'''
if anchor not in text:
    raise SystemExit('function insertion anchor not found')
text = text.replace(anchor, insert + anchor, 1)
run_anchor = '    runTest("testFetchFileUrlDecodesPercentEscapes", testFetchFileUrlDecodesPercentEscapes);\n'
run_insert = run_anchor + '    runTest("testFetchHttpRedirectLoopIsBounded", testFetchHttpRedirectLoopIsBounded);\n'
if run_anchor not in text:
    raise SystemExit('runTest insertion anchor not found')
text = text.replace(run_anchor, run_insert, 1)
path.write_text(text)
PY
```

Expected: command exits with code `0`.

- [ ] **Step 3: Add redirect endpoint declaration**

Modify `tests/stability_http_server.hpp`. Find:

```cpp
    std::string responseForPath(const std::string& path);
```

Add this line immediately after it:

```cpp
    std::string redirectLoopResponse() const;
```

Use:

```bash
python3 - <<'PY'
from pathlib import Path
path = Path('tests/stability_http_server.hpp')
text = path.read_text()
old = '    std::string responseForPath(const std::string& path);\n'
new = old + '    std::string redirectLoopResponse() const;\n'
if old not in text:
    raise SystemExit('header anchor not found')
path.write_text(text.replace(old, new, 1))
PY
```

Expected: command exits with code `0`.

- [ ] **Step 4: Implement redirect-loop endpoint**

Inspect the server implementation:

```bash
grep -n "responseForPath\|HTTP/1.1" tests/stability_http_server.cpp | head -40
```

Then apply this Python patch:

```bash
python3 - <<'PY'
from pathlib import Path
path = Path('tests/stability_http_server.cpp')
text = path.read_text()
anchor = '''std::string StabilityHttpServer::responseForPath(const std::string& path) {
'''
if anchor not in text:
    raise SystemExit('responseForPath anchor not found')
insert = '''std::string StabilityHttpServer::redirectLoopResponse() const {
    return "HTTP/1.1 302 Found\\r\\n"
           "Location: /redirect-loop\\r\\n"
           "Content-Length: 0\\r\\n"
           "Connection: close\\r\\n"
           "\\r\\n";
}

'''
text = text.replace(anchor, insert + anchor, 1)
old = '''std::string StabilityHttpServer::responseForPath(const std::string& path) {
'''
new = '''std::string StabilityHttpServer::responseForPath(const std::string& path) {
    if (path == "/redirect-loop") {
        return redirectLoopResponse();
    }
'''
if old not in text:
    raise SystemExit('responseForPath body anchor not found')
text = text.replace(old, new, 1)
path.write_text(text)
PY
```

Expected: command exits with code `0`.

- [ ] **Step 5: Run the new test and confirm it fails before the fix**

Run:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build -R subcli_tests --output-on-failure
```

Expected before implementation:
- The test may fail because curl follows too many redirects without producing the expected bounded error quickly, or it may pass if libcurl has a built-in redirect cap.
- If it already passes, still continue to Step 6 because the explicit cap is still required.

- [ ] **Step 6: Add explicit redirect cap in `src/fetch.cpp`**

Find:

```cpp
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
```

Replace it with:

```cpp
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
```

Use:

```bash
python3 - <<'PY'
from pathlib import Path
path = Path('src/fetch.cpp')
text = path.read_text()
old = '    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);\n'
new = old + '    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);\n'
if old not in text:
    raise SystemExit('CURLOPT_FOLLOWLOCATION line not found')
if 'CURLOPT_MAXREDIRS' in text:
    raise SystemExit('CURLOPT_MAXREDIRS already present')
path.write_text(text.replace(old, new, 1))
PY
```

Expected: command exits with code `0`.

- [ ] **Step 7: Build and run tests**

Run:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build -R subcli_tests --output-on-failure
ctest --test-dir build --output-on-failure
```

Expected:
- `subcli_tests` passes.
- All tests pass.

- [ ] **Step 8: Commit**

Run:

```bash
git add src/fetch.cpp tests/stability_http_server.hpp tests/stability_http_server.cpp tests/subcli_tests.cpp
git commit -m "fix: bound subscription HTTP redirects"
```

Expected: commit succeeds.

---

## Task 7: Verify and document WireGuard exporter concern

**Files:**
- Modify: `SOLUTION.md`

- [ ] **Step 1: Run existing WireGuard tests**

Run:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build -R subcli_tests --output-on-failure
```

Expected: `subcli_tests` passes, including:
- `testWireGuardWritersUseTargetSchemas`
- `testExportSingBoxWireGuardUsesEndpoint`
- `testParseWireGuardNativeShapes`

- [ ] **Step 2: Confirm code path uses endpoint writer**

Run:

```bash
grep -n "makeSingBoxWireGuardEndpoint\|testExportSingBoxWireGuardUsesEndpoint" src/exporter_common.cpp tests/subcli_tests.cpp
```

Expected: output includes:
- `makeSingBoxWireGuardEndpoint` in `src/exporter_common.cpp`
- `testExportSingBoxWireGuardUsesEndpoint` in `tests/subcli_tests.cpp`

- [ ] **Step 3: Update `SOLUTION.md` P8 note**

Replace this text:

```markdown
#### P8: Xray 导出中 WireGuard 节点处理不完整
```

with:

```markdown
#### P8: WireGuard 导出路径已由现有测试覆盖，但建议保持回归测试
```

Also replace the paragraph under it with:

```markdown
代码中 `makeSingBoxOutbound` 对 WireGuard 的普通 outbound 路径直接返回，但 sing-box 的实际 WireGuard 输出使用 endpoint 路径 `makeSingBoxWireGuardEndpoint`。现有测试 `testExportSingBoxWireGuardUsesEndpoint` 已覆盖该行为。因此它不是当前 bug，但应在后续重构 exporter 时保留这组回归测试，避免 WireGuard 字段丢失。
```

Use:

```bash
python3 - <<'PY'
from pathlib import Path
path = Path('SOLUTION.md')
text = path.read_text()
old = '''#### P8: Xray 导出中 WireGuard 节点处理不完整

```cpp
// exporter_common.cpp:1260 (makeSingBoxOutbound)
if (type == "wireguard") {
    return o;  // 直接返回，没有填充 peers/private_key 等字段
}
```

需要确认 `applySingBoxStructuredProtocolFields` 是否在之前已经处理了 WireGuard 的字段。如果没有，sing-box 导出的 WireGuard 节点将缺少关键配置。
'''
new = '''#### P8: WireGuard 导出路径已由现有测试覆盖，但建议保持回归测试

代码中 `makeSingBoxOutbound` 对 WireGuard 的普通 outbound 路径直接返回，但 sing-box 的实际 WireGuard 输出使用 endpoint 路径 `makeSingBoxWireGuardEndpoint`。现有测试 `testExportSingBoxWireGuardUsesEndpoint` 已覆盖该行为。因此它不是当前 bug，但应在后续重构 exporter 时保留这组回归测试，避免 WireGuard 字段丢失。
'''
if old not in text:
    raise SystemExit('P8 block not found')
path.write_text(text.replace(old, new, 1))
PY
```

Expected: command exits with code `0`.

- [ ] **Step 4: Commit**

Run:

```bash
git add SOLUTION.md
git commit -m "docs: clarify wireguard exporter review finding"
```

Expected: commit succeeds.

---

## Task 8: Add release/runtime documentation

**Files:**
- Create: `docs/release-and-runtime.md`
- Modify: `README.md`

- [ ] **Step 1: Create `docs/release-and-runtime.md`**

Write this exact file:

```bash
cat > docs/release-and-runtime.md <<'EOF'
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

EOF
```

Expected: file exists.

- [ ] **Step 2: Link the guide from `README.md`**

Find this section in `README.md`:

```markdown
## Build
```

Insert this paragraph immediately before it:

```markdown
## Release and Runtime Verification

For cross-platform validation with `gh`, local packaging, GitHub Release publishing, and `run`/`daemon` smoke tests, see [`docs/release-and-runtime.md`](docs/release-and-runtime.md).

```

Use:

```bash
python3 - <<'PY'
from pathlib import Path
path = Path('README.md')
text = path.read_text()
anchor = '## Build\n'
insert = '## Release and Runtime Verification\n\nFor cross-platform validation with `gh`, local packaging, GitHub Release publishing, and `run`/`daemon` smoke tests, see [`docs/release-and-runtime.md`](docs/release-and-runtime.md).\n\n'
if anchor not in text:
    raise SystemExit('README Build anchor not found')
if 'docs/release-and-runtime.md' not in text:
    text = text.replace(anchor, insert + anchor, 1)
path.write_text(text)
PY
```

Expected: command exits with code `0`.

- [ ] **Step 3: Commit**

Run:

```bash
git add docs/release-and-runtime.md README.md
git commit -m "docs: add release and runtime verification guide"
```

Expected: commit succeeds.

---

## Task 9: Update SOLUTION.md with implementation references

**Files:**
- Modify: `SOLUTION.md`

- [ ] **Step 1: Add implementation status section**

Insert the following section after the initial numbered list in `SOLUTION.md` and before the first `---` line:

```markdown
## 实施状态

本方案已落地为以下可执行文件和验证入口：

- `scripts/verify-cross-platform.sh`：使用 `gh` 触发并等待三平台编译验证。
- `scripts/local-package.sh`：本地 Release 构建、测试、打包。
- `scripts/release.sh`：tag 驱动的 GitHub Release 发布助手。
- `.github/workflows/runtime-test.yml`：Linux/macOS/Windows 上的 `run`/`daemon` 命令烟雾测试。
- `tests/runtime_command_smoke.cmake`：本地和 CI 复用的 runtime 命令测试。
- `docs/release-and-runtime.md`：面向维护者的发布与 runtime 验证说明。

```

Use:

```bash
python3 - <<'PY'
from pathlib import Path
path = Path('SOLUTION.md')
text = path.read_text()
anchor = '---\n\n## 1. 跨平台编译验证\n'
section = '''## 实施状态

本方案已落地为以下可执行文件和验证入口：

- `scripts/verify-cross-platform.sh`：使用 `gh` 触发并等待三平台编译验证。
- `scripts/local-package.sh`：本地 Release 构建、测试、打包。
- `scripts/release.sh`：tag 驱动的 GitHub Release 发布助手。
- `.github/workflows/runtime-test.yml`：Linux/macOS/Windows 上的 `run`/`daemon` 命令烟雾测试。
- `tests/runtime_command_smoke.cmake`：本地和 CI 复用的 runtime 命令测试。
- `docs/release-and-runtime.md`：面向维护者的发布与 runtime 验证说明。

'''
if section in text:
    raise SystemExit('implementation status already inserted')
if anchor not in text:
    raise SystemExit('SOLUTION insertion anchor not found')
path.write_text(text.replace(anchor, section + anchor, 1))
PY
```

Expected: command exits with code `0`.

- [ ] **Step 2: Verify links and file references exist**

Run:

```bash
for f in \
  scripts/verify-cross-platform.sh \
  scripts/local-package.sh \
  scripts/release.sh \
  .github/workflows/runtime-test.yml \
  tests/runtime_command_smoke.cmake \
  docs/release-and-runtime.md; do
  test -f "$f" || { echo "missing $f"; exit 1; }
done
echo "all referenced files exist"
```

Expected: `all referenced files exist`.

- [ ] **Step 3: Commit**

Run:

```bash
git add SOLUTION.md
git commit -m "docs: record implemented release quality plan"
```

Expected: commit succeeds.

---

## Task 10: Final verification

**Files:**
- No new files. This task verifies the whole branch.

- [ ] **Step 1: Check shell script syntax**

Run:

```bash
bash -n scripts/verify-cross-platform.sh
bash -n scripts/local-package.sh
bash -n scripts/release.sh
```

Expected: no output, exit code `0`.

- [ ] **Step 2: Configure and build**

Run:

```bash
cmake -S . -B build
cmake --build build -j
```

Expected: build succeeds.

- [ ] **Step 3: Run runtime smoke test**

Run:

```bash
ctest --test-dir build -R subcli_runtime_command_smoke --output-on-failure
```

Expected: test passes.

- [ ] **Step 4: Run all tests**

Run:

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 5: Run local packaging**

Run:

```bash
BUILD_DIR=build CONFIG=Release JOBS=2 scripts/local-package.sh
```

Expected:
- Build succeeds.
- Tests pass.
- Package artifacts are listed.

- [ ] **Step 6: Run gh validation dry checks**

Do not trigger release. Only run dry/safe checks:

```bash
DRY_RUN=1 scripts/release.sh || true
```

Expected:
- If `gh` is installed and authenticated and tag does not exist, it prints dry-run commands.
- If `gh` is not available or not authenticated, it prints a clear error.
- It must not create a tag.

- [ ] **Step 7: Inspect git status**

Run:

```bash
git status --short
```

Expected: no uncommitted files. If files remain, either commit them or intentionally discard them.

- [ ] **Step 8: Optional remote CI verification**

Only run this if you have network access and `gh` authentication:

```bash
scripts/verify-cross-platform.sh
```

Expected: GitHub Actions passes Linux, macOS, and Windows validation.

- [ ] **Step 9: Optional runtime workflow verification**

Only run this if you have network access and `gh` authentication:

```bash
gh workflow run runtime-test.yml --ref "$(git branch --show-current)"
gh run watch
```

Expected: GitHub Actions passes Linux, macOS, and Windows runtime tests.

---

## Self-review checklist

- [ ] Requirement 1 covered: cross-platform compile validation with `gh` is implemented by `scripts/verify-cross-platform.sh` and existing `release-validation.yml`.
- [ ] Requirement 2 covered: build/package/release is implemented by `scripts/local-package.sh`, `scripts/release.sh`, and existing `release.yml`.
- [ ] Requirement 3 covered: code review findings remain in `SOLUTION.md`, with WireGuard finding clarified and HTTP redirects fixed.
- [ ] Requirement 4 covered: `run` and `daemon` runtime commands are tested by `tests/runtime_command_smoke.cmake` and `.github/workflows/runtime-test.yml`.
- [ ] Requirement 5 covered: final product fitness criteria are documented in `docs/release-and-runtime.md` and `SOLUTION.md`.
- [ ] No large `main.cpp` refactor was attempted in this release-quality plan.
- [ ] All tests pass locally.
- [ ] Shell scripts pass `bash -n`.
- [ ] Documentation links point to existing files.
