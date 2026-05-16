# Stability Maintenance Design

**Date:** 2026-05-16

**Branch:** `feat/stability-maintenance`

**Version policy:** This work does not bump the project version. It improves stability, first-use behavior, tests, and documentation on top of the current `master` state.

## Purpose

This maintenance project makes `subcli` safer and easier to use across Linux, macOS, and Windows by validating real user workflows, testing boundary conditions, and improving first-use guidance. The work treats a normal downloaded package as the primary user experience: a user should be able to read help text and Chinese documentation, initialize storage, add a subscription URL, update it, and export a client config without knowing the internal layout.

## Confirmed Decisions

- Do not bump the project version.
- Implement the recommended black-box stability suite and help usability improvements.
- Test both the build-tree binary and the CPack package extracted into a temporary directory.
- Make the full stability suite locally runnable and run it by default in CI on Linux, macOS, and Windows.
- Improve existing English CLI help text.
- Add Chinese documentation and an English-command Chinese glossary, but do not add Chinese CLI output or a language switch.
- Add a workspace-first initialization model:
  - `subcli init [DIR]` initializes a workspace and remembers it as the default workspace.
  - `subcli workspace init [DIR]` initializes a workspace and remembers it as the default workspace.
  - Later commands use the remembered workspace automatically unless overridden.
  - `workspace use` remains available for switching the default workspace.
  - `workspace unset` remains available for clearing the remembered workspace.
  - Existing precedence remains: `--workspace` > `SUBCLI_WORKSPACE` > marker discovery > persisted default > platform default.

## Goals

1. Validate realistic first-use flows for users who only have the executable/package and help text.
2. Test subscription update/export using a deterministic local HTTP server instead of external network services.
3. Cover boundary conditions: invalid HTTP status, empty content, malformed content, large response, slow response, Unicode names, and paths containing spaces/Unicode.
4. Keep platform-specific API usage isolated behind platform or test-support boundaries.
5. Improve help text so users understand what `subcli` is, what it is not, and what commands to run first.
6. Add Chinese documentation that translates major commands and options.
7. Ensure Linux, macOS, and Windows all run the stability suite in CI.

## Non-Goals

- No version bump.
- No full localization of CLI output.
- No system proxy integration.
- No systemd, launchd, or Windows Service integration.
- No replacement of libcurl for production HTTP downloads.
- No broad performance rewrite without evidence from tests or profiling.

## Current Behavior to Change

Currently, `subcli workspace init ./ws` initializes a workspace, while `subcli workspace use ./ws` is required to remember it as the default. The top-level `subcli init` initializes default files under the currently resolved environment but is not clearly presented as a first-use workspace initializer.

This is confusing for new users. The new model makes initialization and remembering a workspace one step.

## Workspace-First Initialization Design

### `subcli init [DIR]`

`subcli init [DIR]` becomes the ordinary first-use command.

Behavior:

1. Resolve the target workspace directory:
   - If `DIR` is provided, use that directory.
   - If `DIR` is omitted, use the platform default application data root:
     - Linux: `~/.local/share/subcli`
     - macOS: `~/Library/Application Support/subcli`
     - Windows: `%APPDATA%\subcli` when available, otherwise the existing Windows fallback.
2. Create the workspace root and standard subdirectories:
   - `profiles/`
   - `templates/`
   - `assets/`
   - `cache/`
   - `outputs/`
   - `state/`
3. Write workspace markers and metadata:
   - `.subcli-workspace`
   - `subcli.env.yaml`
4. Ensure default files exist without overwriting user-owned content:
   - `config.yaml`
   - `sub.yaml`
5. Persist the target workspace as the default workspace.
6. Print a clear next-step message.

Example output shape:

```text
workspace initialized: /path/to/ws
default workspace set to: /path/to/ws
Next steps:
  subcli doctor
  subcli sub add --name my-sub --url <subscription-url>
  subcli sub update
  subcli export mihomo
```

### `subcli workspace init [DIR]`

`subcli workspace init [DIR]` has the same persistence behavior as `subcli init [DIR]`.

Behavior:

1. Initialize the specified or default workspace.
2. Persist it as the default workspace.
3. Print both the initialized path and the remembered default path.

### Existing Workspace Commands

- `subcli workspace use DIR` remains the explicit command for switching the remembered default workspace later.
- `subcli workspace unset` remains the explicit command for clearing the remembered default workspace.
- `subcli workspace status [--json]` must report the active workspace and persisted default state clearly.
- `subcli workspace migrate` continues to operate on explicit source/target workspace paths.

### Workspace Selection Precedence

The existing precedence remains unchanged:

```text
--workspace > SUBCLI_WORKSPACE > marker discovery > persisted default > platform default
```

This preserves advanced and CI usage while improving the default user path.

### Boundary Cases

The implementation must cover:

- Repeated `subcli init ./ws` does not destroy existing config/subscription data.
- Initializing into an existing directory creates missing files but preserves unrelated files.
- Initializing into a path containing spaces and Unicode works on all three platforms.
- If initialization fails, persisted default workspace is not updated to a broken path.
- `--workspace` still overrides the remembered default for a single command.
- `SUBCLI_WORKSPACE` still overrides the remembered default when `--workspace` is absent.

## Help and Documentation Design

### Root Help

`subcli --help` must start with a concise explanation:

- `subcli` generates Mihomo, sing-box, and Xray configuration files from subscriptions, profiles, templates, and workspace settings.
- `subcli` does not replace proxy clients and does not enable the system proxy by itself.
- New users should start with `subcli init`.

The root help must include a first-use flow:

```text
First use:
  subcli init
  subcli doctor
  subcli sub add --name my-sub --url <subscription-url>
  subcli sub update
  subcli export mihomo
```

### Init Help

`subcli init --help` must explain:

- It initializes and remembers a default workspace.
- The default platform paths when `DIR` is omitted.
- Later commands use this workspace automatically.
- The recommended next commands.

### Workspace Help

`subcli workspace --help` must explain:

- A workspace stores config, subscriptions, templates, assets, cache, outputs, runtime state, and logs.
- `workspace init` initializes and remembers the default workspace.
- `workspace use` switches the remembered workspace later.
- `workspace unset` clears the remembered workspace.

### Subscription Help

`subcli sub --help` must explain:

- Subscriptions are URLs or files containing proxy nodes.
- The typical flow is add, update, list, export.
- `file:///path/to/sub.txt` can be used for local subscription files.
- `--strict-network` makes network failures fail the command.

### Export Help

`subcli export --help` must explain:

- Export generates native client config files from current workspace subscriptions, profiles, templates, and assets.
- Examples for `mihomo`, `sing-box`, and `xray` are shown.
- `--output PATH` writes to an explicit output file.

### Chinese Documentation

Add `docs/cli-glossary.zh-CN.md`.

Required content:

- A plain-language description of `subcli` in Chinese.
- A statement that it does not directly turn on the system proxy and does not replace Mihomo, sing-box, or Xray.
- A first-use section:

```bash
subcli init
subcli doctor
subcli sub add --name my-sub --url <你的订阅链接>
subcli sub update
subcli export mihomo
```

- A command and option glossary covering at least:
  - `init`
  - `workspace init`
  - `workspace status`
  - `workspace use`
  - `workspace unset`
  - `doctor`
  - `sub add`
  - `sub update`
  - `sub list`
  - `export mihomo`
  - `export sing-box`
  - `export xray`
  - `--workspace DIR`
  - `--output PATH`
  - `--json`
  - `--strict-network`
  - `--help` / `-h`

README files must link to the Chinese glossary.

## Local HTTP Subscription Server Design

### Purpose

Tests must not depend on real subscription providers or external network availability. A local deterministic HTTP server will simulate subscription provider responses.

### Invocation

Add a test-helper mode to the unit-test binary:

```bash
subcli_tests --subcli-test-http-server <port-file> <fixture-dir>
```

Behavior:

1. Listen on `127.0.0.1` with port `0` so the OS assigns a free port.
2. Write the selected port to `<port-file>`.
3. Serve deterministic responses from fixtures or generated bodies.
4. Stop when `/shutdown` is requested or when the test process is terminated.

### Endpoints

| Endpoint | Expected response |
| --- | --- |
| `/sub/plain` | HTTP 200 plain URI list |
| `/sub/base64` | HTTP 200 base64 subscription |
| `/sub/malformed` | HTTP 200 content with malformed lines and at least one valid line |
| `/sub/empty` | HTTP 200 empty body |
| `/sub/unicode` | HTTP 200 content with Unicode node names |
| `/sub/large` | HTTP 200 generated body exceeding configured fetch size limits |
| `/sub/slow` | HTTP 200 delayed response to exercise timeout behavior |
| `/sub/404` | HTTP 404 |
| `/sub/500` | HTTP 500 |
| `/shutdown` | HTTP 200 and server exits |

### Fixtures

Add stable fixture files under:

```text
tests/stability_fixtures/subscriptions/
```

Required fixtures:

- `plain.txt`
- `base64.txt`
- `malformed.txt`
- `unicode.txt`
- `empty.txt`

The large response may be generated at runtime to avoid committing a large file.

### Platform Boundary

Socket API usage is allowed only in test-support files, for example:

- `tests/stability_http_server.cpp`
- `tests/platform_test_support.cpp`

Production code must continue using libcurl for HTTP downloads.

## Stability Test Design

### Build Binary User Journey

Add `tests/stability_user_journey.cmake` and register it as a CTest.

Run against `$<TARGET_FILE:subcli>`.

Flow:

1. Run `subcli --help` and verify first-use guidance exists.
2. Start the local HTTP subscription server.
3. Run `subcli init "<tmp>/subcli 稳定性 workspace with space"`.
4. Run `subcli workspace status --json` and verify the active workspace is the initialized path.
5. Run `subcli doctor --json`.
6. Run `subcli config list`.
7. Run `subcli template list`.
8. Run `subcli profile list`.
9. Run `subcli sub add --name local-http --url http://127.0.0.1:<port>/sub/plain`.
10. Run `subcli sub update`.
11. Run `subcli sub list`.
12. Run `subcli export mihomo --output "<tmp>/mihomo.yaml"`.
13. Verify the output file exists and is non-empty.
14. Shut down the local HTTP server.

Important: after `subcli init`, these commands intentionally do not pass `--workspace`. This proves the remembered workspace works.

### Package User Journey

Add `tests/stability_package_journey.cmake` and run it after CPack package generation.

Flow:

1. Find the package created by CPack:
   - Linux/macOS: `.tar.gz`
   - Windows: `.zip`
2. Extract it into a temporary directory.
3. Locate `subcli` or `subcli.exe` inside the extracted package.
4. Run `subcli --help` from the extracted package.
5. Run `subcli init "<tmp>/download-user-workspace"`.
6. Run `subcli doctor --json`.
7. Run `subcli template list`.
8. Run `subcli profile list`.
9. Run `subcli profile validate` against a packaged or source profile path.
10. Start the local HTTP subscription server.
11. Add/update/export a subscription using `/sub/plain`.
12. Verify the exported config file exists and is non-empty.
13. Shut down the local HTTP server.

This simulates a normal user downloading and unpacking the release artifact.

### HTTP Boundary Cases

Add black-box coverage for:

- HTTP 404
- HTTP 500
- Empty subscription body
- Malformed subscription body
- Unicode node names
- Large response enforcing `fetch_max_bytes`
- Slow response enforcing timeout behavior

Expected behavior:

- The program must not crash.
- Failures must use non-zero exit codes where strict mode requires failure.
- Error output must include a user-readable reason.
- Workspace files must remain readable after failed updates.
- Valid content inside partially malformed input should be preserved if the existing parser semantics allow partial success.

### Workspace Behavior Cases

Add tests for:

- `subcli init <ws>` persists `<ws>` as the default.
- `subcli workspace init <ws>` persists `<ws>` as the default.
- A second init switches the remembered workspace to the new path.
- `workspace use` switches after init.
- `workspace unset` clears the remembered workspace.
- `--workspace` overrides the remembered default for one command.
- `SUBCLI_WORKSPACE` overrides the remembered default when no `--workspace` is present.
- Paths with spaces and Unicode work across Linux, macOS, and Windows.

## Platform API and Performance Design

The existing v0.2.7 platform process boundary remains the model for production code.

Allowed production platform boundary files:

- `include/subcli/platform.hpp`
- `src/platform_common.cpp`
- `src/platform_posix.cpp`
- `src/platform_windows.cpp`

Business logic should not directly use platform primitives such as:

- `fork`, `exec`, `waitpid`, `kill`
- POSIX socket APIs
- `CreateProcessW`, `TerminateProcess`, `WaitForSingleObject`
- WinSock APIs

Test-only platform APIs are allowed in test-support files.

### Immediate Optimization Policy

This maintenance round should not rewrite broad subsystems for performance without evidence. It should strengthen the existing platform API strategy:

- Keep process launch and executable discovery platform-native.
- Keep production HTTP downloads on libcurl.
- Add tests around timeout, max response size, HTTP status, and user-readable network errors.
- Add a platform-boundary scan so future changes do not leak OS primitives into business logic.

## Platform Boundary Scan

Add a CTest or CMake script that scans for platform primitive tokens.

Example tokens:

```text
#include <windows.h>
#include <winsock2.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
CreateProcess
TerminateProcess
WaitForSingleObject
fork(
exec
waitpid
kill(
socket(
bind(
listen(
accept(
```

Allowed files:

- `src/platform_posix.cpp`
- `src/platform_windows.cpp`
- `tests/platform_test_support.cpp`
- `tests/stability_http_server.cpp`

The scan is intended to prevent accidental platform-specific code leakage.

## CI Design

All three release platforms must run the full suite:

- Linux x86_64
- macOS arm64
- Windows x86_64

Recommended CI order:

1. Configure.
2. Build.
3. Run ordinary CTest, including unit tests, CLI smoke tests, help checks, HTTP integration, and build-binary stability journey.
4. Build CPack package.
5. Run package user journey CTest after package generation.
6. Upload package artifacts where the workflow already does so.

Local full verification command:

```bash
cmake -S . -B build &&
cmake --build build -j &&
ctest --test-dir build --output-on-failure &&
cmake --build build --target package &&
ctest --test-dir build -R subcli_stability_package_journey --output-on-failure
```

For multi-config generators:

```bash
cmake -S . -B build &&
cmake --build build --config Release -j &&
ctest --test-dir build --build-config Release --output-on-failure &&
cmake --build build --config Release --target package &&
ctest --test-dir build --build-config Release -R subcli_stability_package_journey --output-on-failure
```

## Documentation Updates

Update:

- `README.md`
- `README.subcli.md`
- `docs/config-file.md`
- Add `docs/cli-glossary.zh-CN.md`

Required documentation changes:

- Present `subcli init` as the first-use command.
- Explain that `workspace init` now remembers the workspace.
- Remove old first-use instructions that require `workspace use` immediately after `workspace init`.
- Link to the Chinese glossary.
- Explain that `--workspace` is mainly a per-command override.
- Preserve advanced workspace migration documentation.

## Acceptance Criteria

1. The project version is unchanged.
2. `subcli init [DIR]` initializes and remembers a default workspace.
3. `subcli workspace init [DIR]` initializes and remembers a default workspace.
4. Later commands default to the remembered workspace without `--workspace`.
5. `workspace use` still switches the remembered workspace.
6. `workspace unset` still clears the remembered workspace.
7. `--workspace` and `SUBCLI_WORKSPACE` still take precedence over the remembered workspace.
8. Build-binary first-use journey passes on Linux, macOS, and Windows.
9. Package-extraction first-use journey passes on Linux, macOS, and Windows.
10. Local HTTP subscription server supports success, failure, empty, malformed, large, slow, and Unicode responses.
11. HTTP boundary tests do not crash and report readable errors.
12. Help text tells new users what `subcli` is, what it is not, and what to run first.
13. `docs/cli-glossary.zh-CN.md` exists and covers common commands/options.
14. Platform-specific production APIs remain isolated in platform boundary files.
15. CI validates the full suite on Linux, macOS, and Windows.
16. A release-readiness or maintenance-readiness document records final local and CI verification evidence.

## Implementation Task Outline

Implementation should be split into small commits:

1. Workspace-first init behavior and tests.
2. Help usability improvements and Chinese glossary.
3. Local HTTP subscription test server.
4. Build-binary stability user journey.
5. Package-extraction stability user journey.
6. Boundary-condition tests and platform boundary scan.
7. Final three-platform verification and readiness evidence.
