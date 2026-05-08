# Subcli v2 Ecosystem Workspace Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement v2 phase-1 environment/workspace architecture with deterministic path resolution, migration from v1 data layout, and full CLI integration without breaking current default workflows.

**Architecture:** Introduce a dedicated environment resolver that selects one active environment per invocation (`--workspace` > env var > marker discovery > persisted default > platform default). Add a workspace command group for lifecycle operations, plus a migration module that copies durable data into the new layout safely. Integrate resolved paths into existing command flows by replacing direct runtime path assumptions.

**Tech Stack:** C++17, std::filesystem, yaml-cpp, nlohmann::json, CMake/CTest

---

## File Responsibility Map

- `include/subcli/environment.hpp` (create): environment data structures, resolver API, workspace metadata API.
- `src/environment.cpp` (create): path resolution priority logic, platform defaults, marker discovery, normalization.
- `include/subcli/workspace.hpp` (create): workspace command helpers and migration/report models.
- `src/workspace.cpp` (create): workspace init/status/use/unset/migrate/doctor helpers.
- `src/main.cpp` (modify): wire new `workspace` command group, add `--workspace` support for all major commands, replace runtime path bootstrap with environment resolver.
- `src/store.cpp` (modify): support safe default config generation for v2 environment bootstrapping.
- `include/subcli/diagnostic.hpp` and/or `src/cli_output.cpp` (modify): JSON/text diagnostics payload for environment resolution trace.
- `tests/subcli_tests.cpp` (modify): unit/integration coverage for resolver and migration behavior.
- `tests/cli_smoke.sh` (modify): smoke flows for workspace commands and export under explicit workspace.
- `README.md` and `README.subcli.md` (modify): document workspace model and migration workflow.

### Task 1: Add environment resolver API and model

**Files:**
- Create: `include/subcli/environment.hpp`
- Test: `tests/subcli_tests.cpp`

- [ ] **Step 1: Write the failing test for resolver priority contract**

```cpp
void testEnvironmentResolutionPrefersCliWorkspaceOverEnvAndDefault() {
    using namespace subcli;
    EnvironmentResolveInput input;
    input.cliWorkspace = "/tmp/ws-cli";
    input.envWorkspace = "/tmp/ws-env";
    input.cwd = "/tmp/project";
    input.persistedWorkspace = "/tmp/ws-persisted";
    input.platform = PlatformKind::Linux;

    EnvironmentResolveResult out = resolveEnvironment(input);
    require(out.source == EnvironmentSource::CliOption, "cli workspace should win");
    require(out.root == "/tmp/ws-cli", "resolved root should use cli workspace");
}
```

- [ ] **Step 2: Run targeted test and verify failure**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R subcli_tests`
Expected: FAIL with missing `EnvironmentResolveInput`/`resolveEnvironment` symbols.

- [ ] **Step 3: Add environment API header**

```cpp
// include/subcli/environment.hpp
#pragma once

#include <map>
#include <string>
#include <vector>

namespace subcli {

enum class PlatformKind { Linux, MacOS, Windows };
enum class EnvironmentSource { CliOption, EnvVar, MarkerDiscovery, PersistedDefault, PlatformDefault };

struct EnvironmentPaths {
    std::string root;
    std::string configDir;
    std::string dataDir;
    std::string cacheDir;
    std::string stateDir;
    std::string outputDir;
    std::string templateDir;
    std::string profileDir;
    std::string subPath;
    std::string configPath;
};

struct EnvironmentResolveInput {
    std::string cliWorkspace;
    std::string envWorkspace;
    std::string cwd;
    std::string persistedWorkspace;
    PlatformKind platform = PlatformKind::Linux;
};

struct EnvironmentResolveResult {
    bool ok = false;
    EnvironmentSource source = EnvironmentSource::PlatformDefault;
    std::string root;
    EnvironmentPaths paths;
    std::vector<std::string> trace;
    std::string error;
};

EnvironmentResolveResult resolveEnvironment(const EnvironmentResolveInput& input);

} // namespace subcli
```

- [ ] **Step 4: Re-run tests and verify compile proceeds to unresolved implementation failure**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R subcli_tests`
Expected: still FAIL, now at link stage or runtime because implementation is not present.

- [ ] **Step 5: Commit**

```bash
git add include/subcli/environment.hpp tests/subcli_tests.cpp
git commit -m "feat: add workspace environment resolver API contract"
```

### Task 2: Implement environment resolution and platform defaults

**Files:**
- Create: `src/environment.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/subcli_tests.cpp`

- [ ] **Step 1: Write failing tests for priority chain and invalid explicit workspace behavior**

```cpp
void testEnvironmentResolutionFailsForInvalidExplicitWorkspace() {
    using namespace subcli;
    EnvironmentResolveInput input;
    input.cliWorkspace = "/path/does/not/exist";
    input.cwd = "/tmp";
    EnvironmentResolveResult out = resolveEnvironment(input);
    require(!out.ok, "invalid explicit workspace must fail");
    require(!out.error.empty(), "error message expected");
}
```

- [ ] **Step 2: Run test to verify failure**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R subcli_tests`
Expected: FAIL due to unresolved `resolveEnvironment` behavior.

- [ ] **Step 3: Implement resolver with deterministic priority and path construction**

```cpp
// src/environment.cpp (core skeleton)
#include "subcli/environment.hpp"

#include <filesystem>

namespace subcli {

namespace fs = std::filesystem;

static bool isValidWorkspaceRoot(const std::string& root) {
    std::error_code ec;
    return !root.empty() && fs::exists(root, ec) && fs::is_directory(root, ec);
}

EnvironmentResolveResult resolveEnvironment(const EnvironmentResolveInput& input) {
    EnvironmentResolveResult out;
    // apply priority: cli > env > marker > persisted > platform default
    // if explicit (cli/env) invalid: fail
    // if selected root missing required subdirs: still ok, created by init/ensure layer
    out.ok = true;
    return out;
}

} // namespace subcli
```

- [ ] **Step 4: Register implementation file in build**

```cmake
# CMakeLists.txt subcli target sources
src/environment.cpp
```

- [ ] **Step 5: Run tests to verify resolver tests pass**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure -R subcli_tests`
Expected: resolver tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/environment.cpp CMakeLists.txt tests/subcli_tests.cpp
git commit -m "feat: implement environment resolution priority and defaults"
```

### Task 3: Add workspace service layer (init/status/use/unset)

**Files:**
- Create: `include/subcli/workspace.hpp`
- Create: `src/workspace.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/subcli_tests.cpp`

- [ ] **Step 1: Write failing tests for workspace init and persisted default selection**

```cpp
void testWorkspaceInitCreatesExpectedTree() {
    using namespace subcli;
    const std::string root = makeTempDir("subcli-ws-init");
    WorkspaceInitResult r = workspaceInit(root);
    require(r.ok, "workspace init should succeed");
    require(pathExists(root + "/profiles"), "profiles dir should exist");
    require(pathExists(root + "/templates"), "templates dir should exist");
}
```

- [ ] **Step 2: Run tests to verify failure**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R subcli_tests`
Expected: FAIL with missing workspace APIs.

- [ ] **Step 3: Implement workspace API and minimal behavior**

```cpp
// include/subcli/workspace.hpp
struct WorkspaceInitResult { bool ok = false; std::string error; };
WorkspaceInitResult workspaceInit(const std::string& root);
bool workspaceUse(const std::string& root, std::string& error);
bool workspaceUnset(std::string& error);
```

```cpp
// src/workspace.cpp
WorkspaceInitResult workspaceInit(const std::string& root) {
    // create: profiles templates assets cache outputs state
    // create marker file .subcli-workspace
    // create config.yaml/sub.yaml defaults when absent
}
```

- [ ] **Step 4: Add new source file to build target**

Run: `cmake -S . -B build`
Expected: configure succeeds with workspace source included.

- [ ] **Step 5: Run tests to verify pass**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R subcli_tests`
Expected: workspace init/use/unset tests PASS.

- [ ] **Step 6: Commit**

```bash
git add include/subcli/workspace.hpp src/workspace.cpp CMakeLists.txt tests/subcli_tests.cpp
git commit -m "feat: add workspace lifecycle service layer"
```

### Task 4: Integrate resolver into main command bootstrap

**Files:**
- Modify: `src/main.cpp`
- Test: `tests/cli_smoke.sh`

- [ ] **Step 1: Write failing smoke scenario for explicit workspace routing**

```bash
# tests/cli_smoke.sh snippet
./build/subcli --workspace "$TMPDIR/ws1" init
./build/subcli --workspace "$TMPDIR/ws1" config list | grep "output_dir"
```

- [ ] **Step 2: Run smoke test and confirm failure**

Run: `bash tests/cli_smoke.sh`
Expected: FAIL because root command does not accept `--workspace` yet.

- [ ] **Step 3: Add global option parsing and resolver bootstrap in main**

```cpp
// src/main.cpp (flow)
// 1) parse root-level --workspace
// 2) build EnvironmentResolveInput
// 3) resolveEnvironment(...)
// 4) populate gPaths from resolved environment paths
```

- [ ] **Step 4: Ensure backward compatibility for no-workspace invocation**

Run: `./build/subcli doctor --json`
Expected: succeeds with default environment behavior unchanged.

- [ ] **Step 5: Re-run smoke tests to verify pass**

Run: `bash tests/cli_smoke.sh`
Expected: explicit workspace scenario PASS.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp tests/cli_smoke.sh
git commit -m "feat: resolve active environment before command dispatch"
```

### Task 5: Add `workspace` CLI command group

**Files:**
- Modify: `src/main.cpp`
- Modify: `README.subcli.md`
- Test: `tests/cli_smoke.sh`

- [ ] **Step 1: Write failing smoke tests for new command surface**

```bash
./build/subcli workspace init "$TMPDIR/ws2"
./build/subcli workspace status --json
./build/subcli workspace use "$TMPDIR/ws2"
./build/subcli workspace unset
```

- [ ] **Step 2: Run smoke tests and verify failure**

Run: `bash tests/cli_smoke.sh`
Expected: FAIL with unknown `workspace` command.

- [ ] **Step 3: Implement `workspace` command handlers and usage text**

```cpp
// src/main.cpp
// add printWorkspaceUsage()
// add doWorkspaceCommand(args)
// wire into root dispatch table
```

- [ ] **Step 4: Update README command docs**

```markdown
subcli workspace init [DIR]
subcli workspace status [--json]
subcli workspace use DIR
subcli workspace unset
subcli workspace migrate [--to DIR]
```

- [ ] **Step 5: Run smoke tests to verify pass**

Run: `bash tests/cli_smoke.sh`
Expected: workspace commands PASS.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp README.subcli.md tests/cli_smoke.sh
git commit -m "feat: add workspace CLI commands"
```

### Task 6: Implement v1->v2 migration command

**Files:**
- Modify: `src/workspace.cpp`
- Modify: `include/subcli/workspace.hpp`
- Modify: `src/main.cpp`
- Test: `tests/subcli_tests.cpp`

- [ ] **Step 1: Write failing migration test with fixture source tree**

```cpp
void testWorkspaceMigrateCopiesDurableDataOnly() {
    using namespace subcli;
    // arrange v1-like source with config.yaml sub.yaml profiles templates assets cache state
    // run migrate
    // assert durable files copied, cache/state skipped by default
}
```

- [ ] **Step 2: Run test to verify failure**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R subcli_tests`
Expected: FAIL (migrate API missing).

- [ ] **Step 3: Implement migrator with dry-run and overwrite policy hooks**

```cpp
struct WorkspaceMigrateOptions {
    std::string fromRoot;
    std::string toRoot;
    bool dryRun = false;
    bool overwrite = false;
};

struct WorkspaceMigrateResult {
    bool ok = false;
    std::vector<std::string> copied;
    std::vector<std::string> skipped;
    std::string error;
};
```

- [ ] **Step 4: Wire CLI `workspace migrate [--to DIR] [--dry-run] [--overwrite]`**

Run: `./build/subcli workspace migrate --dry-run`
Expected: prints migration summary without filesystem writes.

- [ ] **Step 5: Run tests to verify pass**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R subcli_tests`
Expected: migration tests PASS.

- [ ] **Step 6: Commit**

```bash
git add include/subcli/workspace.hpp src/workspace.cpp src/main.cpp tests/subcli_tests.cpp
git commit -m "feat: add workspace migration from v1 layout"
```

### Task 7: Add environment diagnostics to JSON/text outputs

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/cli_output.cpp`
- Test: `tests/subcli_tests.cpp`

- [ ] **Step 1: Write failing test for `doctor --json` environment section**

```cpp
void testDoctorJsonContainsEnvironmentResolutionFields() {
    // invoke doctor --json in controlled environment
    // assert keys: resolution_source, active_workspace_root, resolved_path_map
}
```

- [ ] **Step 2: Run test to verify failure**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R subcli_tests`
Expected: FAIL due to missing keys.

- [ ] **Step 3: Implement diagnostics payload extension**

```cpp
// doctor --json additions
{"environment": {
  "resolution_source": "cli|env|marker|persisted|default",
  "active_workspace_root": "...",
  "resolved_path_map": {"config": "...", "data": "..."}
}}
```

- [ ] **Step 4: Re-run tests to verify pass**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R subcli_tests`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp src/cli_output.cpp tests/subcli_tests.cpp
git commit -m "feat: expose environment resolution diagnostics"
```

### Task 8: End-to-end verification and documentation sync

**Files:**
- Modify: `README.md`
- Modify: `README.subcli.md`

- [ ] **Step 1: Add final user docs for workspace model and migration workflow**

```markdown
## Workspace Mode (v2)
- `subcli workspace init ./my-subcli`
- `subcli workspace use ./my-subcli`
- `subcli workspace migrate --dry-run`
```

- [ ] **Step 2: Run full build and full tests**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: all tests PASS.

- [ ] **Step 3: Run target command-level verification**

Run:
`./build/subcli workspace status --json`
`./build/subcli export mihomo --output-dir ./outputs`

Expected: workspace status reports active environment; export still succeeds.

- [ ] **Step 4: Commit**

```bash
git add README.md README.subcli.md
git commit -m "docs: document v2 workspace environment and migration"
```

## Final Verification Checklist

- [ ] `cmake -S . -B build`
- [ ] `cmake --build build -j`
- [ ] `ctest --test-dir build --output-on-failure`
- [ ] `./build/subcli doctor --json`
- [ ] `./build/subcli workspace status --json`
- [ ] `./build/subcli export all --output-dir ./outputs`

## Spec Coverage Check

- Path priority model: covered by Tasks 1, 2, 4, 7.
- Workspace command lifecycle: covered by Tasks 3, 5.
- Migration and compatibility: covered by Task 6.
- Cross-platform/default safety and diagnostics: covered by Tasks 2, 7.
- Docs and verification: covered by Task 8.
