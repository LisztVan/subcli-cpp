# Stability Maintenance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve cross-platform stability and first-use usability without bumping the project version.

**Architecture:** Keep production platform primitives behind the existing platform boundary, add a workspace-first initialization flow, and validate real user journeys through a dedicated C++ stability runner plus CTest/CI integration. Use a local in-process HTTP subscription server for deterministic subscription-provider behavior on Linux, macOS, and Windows.

**Tech Stack:** C++17, CMake/CTest, CLI11, yaml-cpp, nlohmann_json, libcurl, existing platform process APIs, WinSock/POSIX sockets only in test support.

---

## File Structure

### Production behavior

- Modify `include/subcli/environment.hpp`
  - Export a helper for the platform default workspace root.
- Modify `src/environment.cpp`
  - Reuse the existing private platform default root logic through the new public helper.
- Modify `include/subcli/workspace.hpp`
  - Add a small resource-seeding API for templates/profiles copied into a workspace.
- Modify `src/workspace.cpp`
  - Seed templates/profiles into initialized workspaces without overwriting existing files.
- Modify `src/main.cpp`
  - Change `subcli init [DIR]` and `subcli workspace init [DIR]` to initialize, seed resources, and remember the workspace.
  - Improve root, init, workspace, subscription, and export help text.

### Tests and support

- Modify `tests/subcli_tests.cpp`
  - Add unit tests for default root helper, workspace resource seeding, and help/documentation coverage.
- Modify `tests/cli_basic_smoke.cmake`
  - Verify `workspace init` remembers the workspace and later commands work without `--workspace`.
- Create `tests/stability_http_server.hpp`
  - Declare an in-process local HTTP subscription server used by stability tests.
- Create `tests/stability_http_server.cpp`
  - Implement cross-platform HTTP server behavior using existing socket test support.
- Create `tests/stability_runner.cpp`
  - Run black-box user journeys against a provided `subcli` binary while owning the in-process HTTP server.
- Create `tests/stability_package_journey.cmake`
  - Extract a CPack archive, locate `subcli`, and call the C++ stability runner.
- Create `tests/platform_boundary_scan.cmake`
  - Fail if platform primitives leak outside approved boundary files.
- Create `tests/stability_fixtures/subscriptions/plain.txt`
- Create `tests/stability_fixtures/subscriptions/base64.txt`
- Create `tests/stability_fixtures/subscriptions/malformed.txt`
- Create `tests/stability_fixtures/subscriptions/unicode.txt`
- Create `tests/stability_fixtures/subscriptions/empty.txt`
- Modify `CMakeLists.txt`
  - Build/register the stability runner, package journey, and boundary scan.

### Documentation and CI

- Modify `README.md`
  - Present `subcli init` as the first-use path and link Chinese glossary.
- Modify `README.subcli.md`
  - Update workspace usage and first-use examples.
- Modify `docs/config-file.md`
  - Update workspace precedence and init behavior notes.
- Create `docs/cli-glossary.zh-CN.md`
  - Chinese command/option glossary.
- Modify `.github/workflows/release.yml`
  - Run package generation before full tests or run package journey after package generation.
- Modify `.github/workflows/release-validation.yml`
  - Same full three-platform validation flow.
- Create `docs/superpowers/reviews/2026-05-16-stability-maintenance-readiness.md`
  - Record local and CI evidence after implementation.

---

## Task 1: Workspace-First Init Behavior

**Files:**
- Modify: `include/subcli/environment.hpp`
- Modify: `src/environment.cpp`
- Modify: `include/subcli/workspace.hpp`
- Modify: `src/workspace.cpp`
- Modify: `src/main.cpp`
- Modify: `tests/subcli_tests.cpp`
- Modify: `tests/cli_basic_smoke.cmake`

- [ ] **Step 1: Write failing unit tests for platform default root and resource seeding**

Add these declarations near the existing workspace tests in `tests/subcli_tests.cpp`:

```cpp
void testPlatformDefaultWorkspaceRootIsNonEmpty() {
    const std::string root = subcli::platformDefaultWorkspaceRoot(subcli::PlatformKind::Linux);
    require(!root.empty(), "platform default workspace root should be non-empty");
    require(root.find("subcli") != std::string::npos, "platform default workspace root should contain subcli");
}

void testWorkspaceSeedBuiltInsCopiesMissingFilesOnly() {
    const fs::path root = makeUniqueTestDir("subcli-workspace-seed-builtins");
    const fs::path source = makeUniqueTestDir("subcli-workspace-seed-source");
    fs::create_directories(source / "templates");
    fs::create_directories(source / "profiles");
    fs::create_directories(root / "templates");
    fs::create_directories(root / "profiles");

    {
        std::ofstream(source / "templates" / "mihomo_base.yaml") << "mixed-port: 7890\n";
        std::ofstream(source / "profiles" / "bypass-cn.json") << "{\"groups\":[],\"rules\":[]}\n";
        std::ofstream(root / "templates" / "mihomo_base.yaml") << "user-owned\n";
    }

    std::string error;
    const auto result = subcli::workspaceSeedBuiltIns(
        root.string(),
        (source / "templates").string(),
        (source / "profiles").string(),
        error
    );
    require(result.ok, "workspace seed built-ins should succeed: " + error);
    require(fs::exists(root / "profiles" / "bypass-cn.json"), "profile should be copied");
    require(subcli::readFile((root / "templates" / "mihomo_base.yaml").string()) == "user-owned\n", "existing template should not be overwritten");
}
```

Register them in `main()` after the existing environment/workspace tests:

```cpp
runTest("testPlatformDefaultWorkspaceRootIsNonEmpty", testPlatformDefaultWorkspaceRootIsNonEmpty);
runTest("testWorkspaceSeedBuiltInsCopiesMissingFilesOnly", testWorkspaceSeedBuiltInsCopiesMissingFilesOnly);
```

- [ ] **Step 2: Run tests and verify they fail to compile**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/stability-maintenance
cmake --build build -j
```

Expected: build fails because `platformDefaultWorkspaceRoot` and `workspaceSeedBuiltIns` do not exist.

- [ ] **Step 3: Expose the platform default workspace helper**

Add to `include/subcli/environment.hpp` after `resolveEnvironment`:

```cpp
std::string platformDefaultWorkspaceRoot(PlatformKind platform);
```

Add to `src/environment.cpp` after `resolveEnvironment`:

```cpp
std::string platformDefaultWorkspaceRoot(PlatformKind platform) {
    return platformDefaultRoot(platform).string();
}
```

- [ ] **Step 4: Add workspace resource seeding API**

Add to `include/subcli/workspace.hpp` after `WorkspaceInitResult`:

```cpp
struct WorkspaceSeedBuiltInsResult {
    bool ok = false;
    std::vector<std::string> copied;
    std::vector<std::string> skipped;
    std::string error;
};
```

Add this declaration near `workspaceInit`:

```cpp
WorkspaceSeedBuiltInsResult workspaceSeedBuiltIns(
    const std::string& root,
    const std::string& templateSourceDir,
    const std::string& profileSourceDir,
    std::string& error
);
```

Implement in `src/workspace.cpp` after `workspaceInit`:

```cpp
WorkspaceSeedBuiltInsResult workspaceSeedBuiltIns(
    const std::string& root,
    const std::string& templateSourceDir,
    const std::string& profileSourceDir,
    std::string& error
) {
    WorkspaceSeedBuiltInsResult out;
    error.clear();
    const fs::path workspaceRoot = normalizeRoot(root);

    auto copyDirectoryMissingOnly = [&](const fs::path& source, const fs::path& destination, const std::string& label) -> bool {
        std::error_code ec;
        if (source.empty() || !fs::exists(source, ec) || !fs::is_directory(source, ec)) {
            out.skipped.push_back(label + ": source missing: " + source.string());
            return true;
        }
        fs::create_directories(destination, ec);
        if (ec) {
            error = "failed to create workspace resource directory: " + destination.string();
            return false;
        }
        for (const auto& entry : fs::recursive_directory_iterator(source, ec)) {
            if (ec) {
                error = "failed to scan resource directory: " + source.string();
                return false;
            }
            const auto relative = fs::relative(entry.path(), source, ec);
            if (ec) {
                error = "failed to compute resource relative path: " + entry.path().string();
                return false;
            }
            const fs::path target = destination / relative;
            if (entry.is_directory(ec)) {
                fs::create_directories(target, ec);
                if (ec) {
                    error = "failed to create resource subdirectory: " + target.string();
                    return false;
                }
                continue;
            }
            if (!entry.is_regular_file(ec)) {
                out.skipped.push_back(label + ": non-regular: " + relative.generic_string());
                continue;
            }
            if (fs::exists(target, ec)) {
                out.skipped.push_back(label + ": existing: " + relative.generic_string());
                continue;
            }
            fs::create_directories(target.parent_path(), ec);
            if (ec) {
                error = "failed to create resource parent directory: " + target.parent_path().string();
                return false;
            }
            fs::copy_file(entry.path(), target, fs::copy_options::none, ec);
            if (ec) {
                error = "failed to copy resource file: " + entry.path().string();
                return false;
            }
            out.copied.push_back(label + ": " + relative.generic_string());
        }
        return true;
    };

    if (!copyDirectoryMissingOnly(fs::path(templateSourceDir), workspaceRoot / "templates", "templates")) {
        out.error = error;
        return out;
    }
    if (!copyDirectoryMissingOnly(fs::path(profileSourceDir), workspaceRoot / "profiles", "profiles")) {
        out.error = error;
        return out;
    }

    out.ok = true;
    return out;
}
```

- [ ] **Step 5: Use native config location for the remembered workspace file**

Replace the private `persistedDefaultFilePath()` in `src/workspace.cpp` with this platform-aware implementation so Windows users do not fall back to a temp directory when `HOME` is absent:

```cpp
fs::path persistedDefaultFilePath() {
    fs::path base;
#ifdef _WIN32
    const char* appData = std::getenv("APPDATA");
    if (appData != nullptr && *appData != '\0') {
        base = fs::path(appData);
    } else {
        const char* userProfile = std::getenv("USERPROFILE");
        if (userProfile != nullptr && *userProfile != '\0') {
            base = fs::path(userProfile) / "AppData" / "Roaming";
        } else {
            base = fs::temp_directory_path();
        }
    }
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        base = fs::path(xdg);
    } else {
        const char* home = std::getenv("HOME");
        if (home != nullptr && *home != '\0') {
            base = fs::path(home) / ".config";
        } else {
            base = fs::temp_directory_path();
        }
    }
#endif
    return base / "subcli" / "workspace-default";
}
```

- [ ] **Step 6: Change top-level `subcli init [DIR]` to initialize, seed, and remember**

In `src/main.cpp`, replace the body of `doInitCommand` with this structure:

```cpp
int doInitCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        printInitUsage();
        return ExitOk;
    }

    CLI::App parser("init");
    parser.set_help_flag("");
    parser.allow_extras(false);
    std::string dirArg;
    parser.add_option("dir", dirArg)->required(false);
    if (!parseCliArgs(parser, args)) {
        printInitUsage();
        return ExitUsage;
    }

    const std::string initRoot = dirArg.empty() ? platformDefaultWorkspaceRoot(detectPlatformKind()) : dirArg;
    const auto r = workspaceInit(initRoot);
    if (!r.ok) {
        std::cerr << "init failed: " << r.error << "\n";
        return ExitError;
    }

    const RuntimePaths installed = buildRuntimePaths(gExecutablePath);
    std::string seedError;
    const auto seed = workspaceSeedBuiltIns(r.root, installed.templateDir.string(), installed.profileDir.string(), seedError);
    if (!seed.ok) {
        std::cerr << "init failed while seeding built-ins: " << seedError << "\n";
        return ExitError;
    }

    std::string useError;
    if (!workspaceUse(r.root, useError)) {
        std::cerr << "init failed while remembering workspace: " << useError << "\n";
        return ExitError;
    }

    std::cout << "workspace initialized: " << r.root << "\n";
    std::cout << "default workspace set to: " << r.root << "\n";
    std::cout << "Next steps:\n";
    std::cout << "  subcli doctor\n";
    std::cout << "  subcli sub add --name my-sub --url <subscription-url>\n";
    std::cout << "  subcli sub update\n";
    std::cout << "  subcli export mihomo\n";
    return ExitOk;
}
```

- [ ] **Step 7: Change `workspace init [DIR]` to remember and seed**

In `src/main.cpp`, inside `doWorkspaceCommand`, replace the `mode == "init"` block with:

```cpp
if (mode == "init") {
    const std::string initRoot = dirArg.empty() ? platformDefaultWorkspaceRoot(detectPlatformKind()) : dirArg;
    const auto r = workspaceInit(initRoot);
    if (!r.ok) {
        std::cerr << "workspace init failed: " << r.error << "\n";
        return ExitError;
    }

    const RuntimePaths installed = buildRuntimePaths(gExecutablePath);
    std::string seedError;
    const auto seed = workspaceSeedBuiltIns(r.root, installed.templateDir.string(), installed.profileDir.string(), seedError);
    if (!seed.ok) {
        std::cerr << "workspace init failed while seeding built-ins: " << seedError << "\n";
        return ExitError;
    }

    std::string useError;
    if (!workspaceUse(r.root, useError)) {
        std::cerr << "workspace init failed while remembering workspace: " << useError << "\n";
        return ExitError;
    }

    std::cout << "workspace initialized: " << r.root << "\n";
    std::cout << "default workspace set to: " << r.root << "\n";
    return ExitOk;
}
```

- [ ] **Step 8: Update CLI basic smoke to prove remembered workspace behavior**

In `tests/cli_basic_smoke.cmake`, add a capture helper after `run_subcli`:

```cmake
function(run_subcli_capture _label _stdout_var)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env ${_subcli_env} "${SUBCLI_BIN}" ${ARGN}
        WORKING_DIRECTORY "${_smoke_root}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
    )
    if(NOT _result STREQUAL "0")
        string(JOIN " " _args ${ARGN})
        message(FATAL_ERROR
            "subcli CLI smoke failed: ${_label}\n"
            "Command: ${SUBCLI_BIN} ${_args}\n"
            "Exit code: ${_result}\n"
            "stdout:\n${_stdout}\n"
            "stderr:\n${_stderr}"
        )
    endif()
    set(${_stdout_var} "${_stdout}" PARENT_SCOPE)
endfunction()
```

Then replace the old post-init commands that pass `--workspace` with:

```cmake
run_subcli("workspace init" workspace init "${TEST_WORK_DIR}")
run_subcli_capture("workspace status --json" _workspace_status workspace status --json)
if(NOT _workspace_status MATCHES "${TEST_WORK_DIR}")
    message(FATAL_ERROR "workspace status did not report initialized workspace: ${_workspace_status}")
endif()

run_subcli("doctor --json" doctor --json)
run_subcli("profile list" profile list)
run_subcli("template list" template list)
run_subcli("config list" config list)
run_subcli("completion bash" completion bash)
run_subcli("profile validate" profile validate "${TEST_WORK_DIR}/profiles/bypass-cn.json")
```

Remove the manual `file(COPY "${SOURCE_DIR}/templates/" ...)` and manual `config.yaml` write if the new seeding makes them redundant. Keep the manual config block only if the first run shows an exporter-specific need; if kept, write it after `workspace init` and before command checks.

- [ ] **Step 9: Run targeted tests and verify pass**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/stability-maintenance
cmake -S . -B build
cmake --build build -j
ctest --test-dir build -R "subcli_tests|subcli_cli_basic" --output-on-failure
```

Expected: `subcli_tests` and `subcli_cli_basic` pass.

- [ ] **Step 10: Commit Task 1**

Run:

```bash
git add include/subcli/environment.hpp src/environment.cpp include/subcli/workspace.hpp src/workspace.cpp src/main.cpp tests/subcli_tests.cpp tests/cli_basic_smoke.cmake
git commit -m "feat: make init remember default workspace"
```

---

## Task 2: Help Usability and Chinese Glossary

**Files:**
- Modify: `src/main.cpp`
- Modify: `README.md`
- Modify: `README.subcli.md`
- Modify: `docs/config-file.md`
- Create: `docs/cli-glossary.zh-CN.md`
- Modify: `tests/subcli_tests.cpp`

- [ ] **Step 1: Add failing help and docs tests**

Add these tests to `tests/subcli_tests.cpp` near existing documentation tests:

```cpp
void testReadmeLinksChineseGlossary() {
    const auto readme = subcli::readFile((fs::path(SUBCLI_SOURCE_DIR) / "README.md").string());
    require(readme.find("docs/cli-glossary.zh-CN.md") != std::string::npos, "README should link Chinese CLI glossary");
}

void testChineseGlossaryExistsAndMentionsFirstUse() {
    const fs::path glossaryPath = fs::path(SUBCLI_SOURCE_DIR) / "docs/cli-glossary.zh-CN.md";
    require(fs::exists(glossaryPath), "Chinese CLI glossary should exist");
    const auto glossary = subcli::readFile(glossaryPath.string());
    require(glossary.find("第一次使用") != std::string::npos, "Chinese glossary should include first-use section");
    require(glossary.find("subcli init") != std::string::npos, "Chinese glossary should mention subcli init");
    require(glossary.find("subcli sub add") != std::string::npos, "Chinese glossary should mention subscription add flow");
    require(glossary.find("--workspace") != std::string::npos, "Chinese glossary should translate --workspace");
    require(glossary.find("--output-dir") != std::string::npos, "Chinese glossary should translate --output-dir");
}
```

Register:

```cpp
runTest("testReadmeLinksChineseGlossary", testReadmeLinksChineseGlossary);
runTest("testChineseGlossaryExistsAndMentionsFirstUse", testChineseGlossaryExistsAndMentionsFirstUse);
```

- [ ] **Step 2: Run tests and verify they fail**

Run:

```bash
cmake --build build -j
ctest --test-dir build -R subcli_tests --output-on-failure
```

Expected: `testReadmeLinksChineseGlossary` or `testChineseGlossaryExistsAndMentionsFirstUse` fails.

- [ ] **Step 3: Replace root help with first-use guidance**

In `src/main.cpp`, replace `printRootUsage()` output with text that begins:

```cpp
std::cout << "subcli - subscription to proxy client config tool\n"
          << "\n"
          << "Generate Mihomo, sing-box, and Xray configuration files from subscription URLs,\n"
          << "profiles, templates, and workspace settings. subcli does not replace proxy\n"
          << "clients and does not enable the system proxy by itself.\n"
          << "\n"
          << "First use:\n"
          << "  subcli init\n"
          << "  subcli doctor\n"
          << "  subcli sub add --name my-sub --url <subscription-url>\n"
          << "  subcli sub update\n"
          << "  subcli export mihomo\n"
          << "\n"
          << "Usage:\n"
          << "  subcli [--workspace DIR] <command> [args...]\n";
```

Keep the command list, but change these lines:

```cpp
<< "  init      Initialize and remember a default workspace.\n"
<< "  export    Generate Mihomo, sing-box, or Xray config files.\n"
<< "  workspace Manage workspace roots and migration.\n"
```

- [ ] **Step 4: Replace init/workspace/sub/export help descriptions**

In `printInitUsage()`, include:

```cpp
std::cout << "Usage:\n"
          << "  subcli init [DIR]\n"
          << "\n"
          << "Initialize a workspace and remember it as the default workspace.\n"
          << "Without DIR, subcli uses the platform default application data directory.\n"
          << "After init, later commands use this workspace automatically.\n"
          << "\n"
          << "Examples:\n"
          << "  subcli init\n"
          << "  subcli init ./my-subcli\n"
          << "\n"
          << "Next steps:\n"
          << "  subcli doctor\n"
          << "  subcli sub add --name my-sub --url <subscription-url>\n"
          << "  subcli sub update\n"
          << "  subcli export mihomo\n";
```

In `printWorkspaceUsage()`, include the sentence:

```cpp
<< "Workspace stores config, subscriptions, templates, assets, cache, outputs, runtime state, and logs.\n"
<< "workspace init initializes a workspace and remembers it as the default.\n"
<< "workspace use switches the default workspace later; workspace unset clears it.\n"
```

In `printSubUsage()`, include:

```cpp
<< "Subscriptions are URLs or files that contain proxy nodes.\n"
<< "Typical flow:\n"
<< "  subcli sub add --name my-sub --url <subscription-url>\n"
<< "  subcli sub update\n"
<< "  subcli sub list\n"
<< "  subcli export mihomo\n"
<< "Use file:///path/to/sub.txt for local subscription files.\n"
```

In `printExportUsage()`, include:

```cpp
<< "Generate native client config files from current workspace subscriptions, profiles, templates, and assets.\n"
<< "Use --output-dir DIR to choose the output directory.\n"
```

- [ ] **Step 5: Create Chinese glossary**

Create `docs/cli-glossary.zh-CN.md` with this content:

```markdown
# subcli 命令和选项中文对照表

subcli 是一个订阅管理和代理客户端配置生成工具。

它不会直接开启系统代理，也不会替代 Mihomo、sing-box 或 Xray。它的主要作用是初始化 workspace、保存订阅 URL 和配置、更新订阅节点，并根据 profile/template 导出目标客户端配置文件。

## 第一次使用

```bash
subcli init
subcli doctor
subcli sub add --name my-sub --url <你的订阅链接>
subcli sub update
subcli export mihomo
```

`subcli init` 会创建并记住默认 workspace。之后的命令会默认使用这个 workspace，不需要每次都写 `--workspace`。

## 常用命令

| 英文命令/选项 | 中文含义 | 示例 |
| --- | --- | --- |
| `init` | 初始化并记住默认 workspace | `subcli init` |
| `workspace init` | 初始化 workspace 并设为默认 | `subcli workspace init ./ws` |
| `workspace status` | 查看当前 workspace | `subcli workspace status` |
| `workspace use` | 切换默认 workspace | `subcli workspace use ./ws2` |
| `workspace unset` | 清除默认 workspace | `subcli workspace unset` |
| `doctor` | 检查环境和配置是否正常 | `subcli doctor` |
| `sub add` | 添加订阅 | `subcli sub add --name my-sub --url https://example/sub` |
| `sub update` | 更新订阅 | `subcli sub update` |
| `sub list` | 查看订阅列表 | `subcli sub list` |
| `export mihomo` | 导出 Mihomo 配置 | `subcli export mihomo` |
| `export sing-box` | 导出 sing-box 配置 | `subcli export sing-box` |
| `export xray` | 导出 Xray 配置 | `subcli export xray` |
| `--workspace DIR` | 本次命令临时使用某个 workspace | `subcli --workspace ./ws doctor` |
| `--output-dir DIR` | 指定导出目录 | `subcli export mihomo --output-dir ./outputs` |
| `--json` | 用 JSON 格式输出 | `subcli doctor --json` |
| `--strict-network` | 网络失败时严格报错 | `subcli sub update --strict-network` |
| `--help` / `-h` | 查看帮助 | `subcli --help` |

## workspace 是什么

workspace 是 subcli 在幕后使用的工作目录，用来存放配置、订阅、模板、资源、缓存、导出文件和运行状态。

普通用户通常只需要运行一次：

```bash
subcli init
```

高级用户可以用下面的命令切换或清除默认 workspace：

```bash
subcli workspace use ./another-workspace
subcli workspace unset
```
```

- [ ] **Step 6: Update README and detailed README**

In `README.md`, change the quick start to:

```markdown
subcli init
subcli doctor --json
subcli sub add --name airport-a --url https://example/sub
subcli sub update
subcli export all --profile bypass-cn
```

Add this link near the docs links:

```markdown
中文命令和选项对照表见 [`docs/cli-glossary.zh-CN.md`](docs/cli-glossary.zh-CN.md)。
```

In `README.subcli.md`, update workspace examples so `workspace init` is not followed by mandatory `workspace use`. Use:

```markdown
subcli init
subcli workspace status
subcli sub add --name airport-a --url https://example/sub
subcli sub update
subcli export mihomo
```

In `docs/config-file.md`, update the workspace precedence wording to state that `init` and `workspace init` persist the default workspace.

- [ ] **Step 7: Run tests and verify pass**

Run:

```bash
cmake --build build -j
ctest --test-dir build -R "subcli_tests|subcli_cli_basic" --output-on-failure
```

Expected: tests pass and CLI smoke still passes with new help text.

- [ ] **Step 8: Commit Task 2**

Run:

```bash
git add src/main.cpp README.md README.subcli.md docs/config-file.md docs/cli-glossary.zh-CN.md tests/subcli_tests.cpp
git commit -m "docs: improve first-use guidance"
```

---

## Task 3: Local HTTP Subscription Server

**Files:**
- Create: `tests/stability_http_server.hpp`
- Create: `tests/stability_http_server.cpp`
- Create: `tests/stability_fixtures/subscriptions/plain.txt`
- Create: `tests/stability_fixtures/subscriptions/base64.txt`
- Create: `tests/stability_fixtures/subscriptions/malformed.txt`
- Create: `tests/stability_fixtures/subscriptions/unicode.txt`
- Create: `tests/stability_fixtures/subscriptions/empty.txt`
- Modify: `CMakeLists.txt`
- Modify: `tests/platform_test_support.hpp`
- Modify: `tests/platform_test_support.cpp`
- Modify: `tests/subcli_tests.cpp`

- [ ] **Step 1: Add fixture files**

Create `tests/stability_fixtures/subscriptions/plain.txt`:

```text
ss://YWVzLTI1Ni1nY206cGFzc0AxMjcuMC4wLjE6ODM4OA#Test%20Node
```

Create `tests/stability_fixtures/subscriptions/malformed.txt`:

```text
not-a-subscription-line
ss://YWVzLTI1Ni1nY206cGFzc0AxMjcuMC4wLjE6ODM4OA#Valid%20After%20Bad
vmess://not-base64
```

Create `tests/stability_fixtures/subscriptions/unicode.txt`:

```text
ss://YWVzLTI1Ni1nY206cGFzc0AxMjcuMC4wLjE6ODM4OA#香港%20节点
```

Create `tests/stability_fixtures/subscriptions/empty.txt` as an empty file.

Create `tests/stability_fixtures/subscriptions/base64.txt` containing the base64 encoding of the two-line plain subscription. Use this exact content:

```text
c3M6Ly9ZV1Z6TFRJMU5pMW5ZMjA2Y0dGemMwQXhNamN1TUM0d0xqRTZPRE00T0EjQmFzZTY0JTIwTm9kZQo=
```

- [ ] **Step 2: Add a loopback client helper for clean server shutdown**

Add this declaration to `tests/platform_test_support.hpp` after `testSupportCreateLoopbackServer()`:

```cpp
int testSupportConnectLoopback(int port);
```

Add this implementation to `tests/platform_test_support.cpp` after `testSupportCreateLoopbackServer()`:

```cpp
int testSupportConnectLoopback(int port) {
    testSupportEnsureSocketsReady();
#ifdef _WIN32
    SOCKET client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET) {
        return -1;
    }
#else
    int client = ::socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) {
        return -1;
    }
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<unsigned short>(port));
    if (::connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#ifdef _WIN32
        closesocket(client);
#else
        close(client);
#endif
        return -1;
    }

#ifdef _WIN32
    return registerClientSocket(client);
#else
    return client;
#endif
}
```

- [ ] **Step 3: Create HTTP server header**

Create `tests/stability_http_server.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <thread>
#include <atomic>

#include "platform_test_support.hpp"

namespace subcli {

class StabilityHttpServer {
public:
    explicit StabilityHttpServer(std::filesystem::path fixtureDir);
    ~StabilityHttpServer();

    StabilityHttpServer(const StabilityHttpServer&) = delete;
    StabilityHttpServer& operator=(const StabilityHttpServer&) = delete;

    void start();
    void stop();
    int port() const;
    std::string url(const std::string& path) const;

private:
    void serveLoop();
    void handleClient(int client);
    std::string responseForPath(const std::string& path);
    std::string readFixture(const std::string& name) const;

    std::filesystem::path fixtureDir_;
    TestTcpServerHandle server_{};
    std::thread thread_;
    std::atomic<bool> running_{false};
};

} // namespace subcli
```

- [ ] **Step 4: Create HTTP server implementation**

Create `tests/stability_http_server.cpp` with:

```cpp
#include "stability_http_server.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace subcli {
namespace {

std::string httpResponse(int status, const std::string& reason, const std::string& body, const std::string& contentType = "text/plain; charset=utf-8") {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << " " << reason << "\r\n";
    out << "Content-Type: " << contentType << "\r\n";
    out << "Content-Length: " << body.size() << "\r\n";
    out << "Connection: close\r\n";
    out << "\r\n";
    out << body;
    return out.str();
}

std::string parsePathFromRequest(const std::string& request) {
    const auto firstSpace = request.find(' ');
    if (firstSpace == std::string::npos) {
        return "/";
    }
    const auto secondSpace = request.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos) {
        return "/";
    }
    return request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
}

std::string largeSubscriptionBody() {
    std::string body;
    body.reserve(1024 * 1024 + 4096);
    const std::string line = "ss://YWVzLTI1Ni1nY206cGFzc0AxMjcuMC4wLjE6ODM4OA#Large%20Node\n";
    while (body.size() < 1024 * 1024 + 4096) {
        body += line;
    }
    return body;
}

} // namespace

StabilityHttpServer::StabilityHttpServer(std::filesystem::path fixtureDir) : fixtureDir_(std::move(fixtureDir)) {}

StabilityHttpServer::~StabilityHttpServer() {
    stop();
}

void StabilityHttpServer::start() {
    if (running_) {
        return;
    }
    server_ = testSupportCreateLoopbackServer();
    running_ = true;
    thread_ = std::thread([this]() { serveLoop(); });
}

void StabilityHttpServer::stop() {
    if (!running_) {
        return;
    }
    const int shutdownPort = server_.port;
    if (shutdownPort > 0) {
        const int client = testSupportConnectLoopback(shutdownPort);
        if (client >= 0) {
            const std::string request = "GET /shutdown HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
            (void)testSupportSend(client, request.data(), static_cast<int>(request.size()));
            char buffer[256] = {};
            (void)testSupportRecv(client, buffer, static_cast<int>(sizeof(buffer)));
            testSupportCloseClient(client);
        }
    }
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    testSupportCloseServer(server_);
}

int StabilityHttpServer::port() const {
    return server_.port;
}

std::string StabilityHttpServer::url(const std::string& path) const {
    return "http://127.0.0.1:" + std::to_string(port()) + path;
}

void StabilityHttpServer::serveLoop() {
    while (running_) {
        int client = testSupportAccept(server_);
        if (client < 0) {
            if (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            continue;
        }
        handleClient(client);
        testSupportCloseClient(client);
    }
}

void StabilityHttpServer::handleClient(int client) {
    char buffer[4096] = {};
    const int received = testSupportRecv(client, buffer, static_cast<int>(sizeof(buffer) - 1));
    if (received <= 0) {
        return;
    }
    const std::string request(buffer, static_cast<size_t>(received));
    const std::string path = parsePathFromRequest(request);
    const std::string response = responseForPath(path);
    (void)testSupportSend(client, response.data(), static_cast<int>(response.size()));
}

std::string StabilityHttpServer::responseForPath(const std::string& path) {
    if (path == "/shutdown") {
        running_ = false;
        return httpResponse(200, "OK", "shutdown\n");
    }
    if (path == "/sub/plain") {
        return httpResponse(200, "OK", readFixture("plain.txt"));
    }
    if (path == "/sub/base64") {
        return httpResponse(200, "OK", readFixture("base64.txt"));
    }
    if (path == "/sub/malformed") {
        return httpResponse(200, "OK", readFixture("malformed.txt"));
    }
    if (path == "/sub/empty") {
        return httpResponse(200, "OK", readFixture("empty.txt"));
    }
    if (path == "/sub/unicode") {
        return httpResponse(200, "OK", readFixture("unicode.txt"));
    }
    if (path == "/sub/large") {
        return httpResponse(200, "OK", largeSubscriptionBody());
    }
    if (path == "/sub/slow") {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return httpResponse(200, "OK", readFixture("plain.txt"));
    }
    if (path == "/sub/500") {
        return httpResponse(500, "Internal Server Error", "server error\n");
    }
    return httpResponse(404, "Not Found", "not found\n");
}

std::string StabilityHttpServer::readFixture(const std::string& name) const {
    std::ifstream in(fixtureDir_ / name, std::ios::binary);
    if (!in) {
        throw std::runtime_error("missing stability HTTP fixture: " + (fixtureDir_ / name).string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace subcli
```

- [ ] **Step 5: Add a server unit test**

Add to `tests/subcli_tests.cpp`:

```cpp
#include "stability_http_server.hpp"
```

Add test:

```cpp
void testStabilityHttpServerStartsAndServesPort() {
    const fs::path fixtureDir = fs::path(SUBCLI_SOURCE_DIR) / "tests/stability_fixtures/subscriptions";
    subcli::StabilityHttpServer server(fixtureDir);
    server.start();
    require(server.port() > 0, "stability HTTP server should bind a dynamic port");
    server.stop();
}
```

Register:

```cpp
runTest("testStabilityHttpServerStartsAndServesPort", testStabilityHttpServerStartsAndServesPort);
```

- [ ] **Step 6: Wire server into CMake**

In `CMakeLists.txt`, add `tests/stability_http_server.cpp` to the `subcli_tests` executable:

```cmake
add_executable(subcli_tests tests/subcli_tests.cpp tests/platform_test_support.cpp tests/stability_http_server.cpp ${SUBCLI_SOURCES})
```

- [ ] **Step 7: Run targeted tests**

Run:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build -R subcli_tests --output-on-failure
```

Expected: `testStabilityHttpServerStartsAndServesPort` passes and all existing unit tests pass.

- [ ] **Step 8: Commit Task 3**

Run:

```bash
git add CMakeLists.txt tests/platform_test_support.hpp tests/platform_test_support.cpp tests/subcli_tests.cpp tests/stability_http_server.hpp tests/stability_http_server.cpp tests/stability_fixtures/subscriptions
git commit -m "test: add local subscription http server"
```

---

## Task 4: Build Binary Stability User Journey

**Files:**
- Create: `tests/stability_runner.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create the failing stability runner skeleton**

Create `tests/stability_runner.cpp`:

```cpp
#include "stability_http_server.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "subcli/platform.hpp"
#include "subcli/util.hpp"

namespace fs = std::filesystem;

namespace {

struct Options {
    std::string mode;
    fs::path subcliBin;
    fs::path sourceDir;
    fs::path testRoot;
};

void fail(const std::string& message) {
    throw std::runtime_error(message);
}

std::string optionValue(int argc, char* argv[], const std::string& key) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] == key) {
            return argv[i + 1];
        }
    }
    return "";
}

Options parseOptions(int argc, char* argv[]) {
    Options options;
    options.mode = optionValue(argc, argv, "--mode");
    options.subcliBin = optionValue(argc, argv, "--subcli-bin");
    options.sourceDir = optionValue(argc, argv, "--source-dir");
    options.testRoot = optionValue(argc, argv, "--test-root");
    if (options.mode.empty() || options.subcliBin.empty() || options.sourceDir.empty() || options.testRoot.empty()) {
        fail("usage: subcli_stability_runner --mode user|package --subcli-bin PATH --source-dir DIR --test-root DIR");
    }
    return options;
}

subcli::ProcessRunResult runSubcli(const Options& options, const std::vector<std::string>& args, int timeoutSec = 20) {
    auto result = subcli::runProcessCapture(options.subcliBin.string(), args, timeoutSec);
    if (!result.started) {
        fail("failed to start subcli: " + result.error);
    }
    return result;
}

std::string runOk(const Options& options, const std::string& label, const std::vector<std::string>& args, int timeoutSec = 20) {
    const auto result = runSubcli(options, args, timeoutSec);
    if (result.exitCode != 0 || result.timedOut) {
        fail(label + " failed\noutput:\n" + result.output + "\nerror:\n" + result.error);
    }
    return result.output;
}

std::string runFail(const Options& options, const std::string& label, const std::vector<std::string>& args, int timeoutSec = 20) {
    const auto result = runSubcli(options, args, timeoutSec);
    if (result.exitCode == 0 && !result.timedOut) {
        fail(label + " unexpectedly succeeded\noutput:\n" + result.output);
    }
    return result.output + result.error;
}

void requireContains(const std::string& haystack, const std::string& needle, const std::string& label) {
    if (haystack.find(needle) == std::string::npos) {
        fail(label + " missing expected text: " + needle + "\nactual:\n" + haystack);
    }
}

void requireFileNonEmpty(const fs::path& path, const std::string& label) {
    std::error_code ec;
    if (!fs::exists(path, ec) || fs::file_size(path, ec) == 0) {
        fail(label + " expected non-empty file: " + path.string());
    }
}

void runJourney(const Options& options) {
    fs::remove_all(options.testRoot);
    fs::create_directories(options.testRoot);
    const fs::path workspace = options.testRoot / "subcli 稳定性 workspace with space";
    const fs::path outputDir = options.testRoot / "outputs";
    fs::create_directories(outputDir);

    const std::string help = runOk(options, "root help", {"--help"});
    requireContains(help, "First use:", "root help");
    requireContains(help, "subcli init", "root help");
    requireContains(help, "does not replace proxy", "root help");

    runOk(options, "init", {"init", workspace.string()});
    const std::string status = runOk(options, "workspace status", {"workspace", "status", "--json"});
    requireContains(status, workspace.string(), "workspace status");

    runOk(options, "doctor", {"doctor", "--json"});
    runOk(options, "config list", {"config", "list"});
    runOk(options, "template list", {"template", "list"});
    runOk(options, "profile list", {"profile", "list"});
    runOk(options, "profile validate", {"profile", "validate", (workspace / "profiles" / "bypass-cn.json").string()});

    subcli::StabilityHttpServer server(options.sourceDir / "tests/stability_fixtures/subscriptions");
    server.start();
    runOk(options, "sub add", {"sub", "add", "--name", "local-http", "--url", server.url("/sub/plain")});
    runOk(options, "sub update", {"sub", "update", "--strict-network"});
    runOk(options, "sub list", {"sub", "list"});
    runOk(options, "export mihomo", {"export", "mihomo", "--output-dir", outputDir.string(), "--strict-network"});
    server.stop();

    requireFileNonEmpty(outputDir / "mihomo.yaml", "mihomo export");
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const Options options = parseOptions(argc, argv);
        runJourney(options);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "stability runner failed: " << ex.what() << "\n";
        return 1;
    }
}
```

- [ ] **Step 2: Wire the runner into CMake**

In `CMakeLists.txt`, add after `subcli_tests`:

```cmake
add_executable(subcli_stability_runner tests/stability_runner.cpp tests/stability_http_server.cpp tests/platform_test_support.cpp ${SUBCLI_SOURCES})
target_include_directories(subcli_stability_runner PRIVATE include src tests)
target_link_libraries(subcli_stability_runner PRIVATE yaml-cpp nlohmann_json::nlohmann_json libcurl)
target_compile_definitions(subcli_stability_runner PRIVATE SUBCLI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(WIN32)
    target_link_libraries(subcli_stability_runner PRIVATE ws2_32)
endif()
```

Add the build-binary journey test:

```cmake
set(SUBCLI_STABILITY_ENV_ROOT "${CMAKE_CURRENT_BINARY_DIR}/subcli-stability-env-$<CONFIG>")
add_test(
    NAME subcli_stability_user_journey
    COMMAND ${CMAKE_COMMAND} -E env
        "--unset=SUBCLI_WORKSPACE"
        "HOME=${SUBCLI_STABILITY_ENV_ROOT}/home"
        "USERPROFILE=${SUBCLI_STABILITY_ENV_ROOT}/home"
        "APPDATA=${SUBCLI_STABILITY_ENV_ROOT}/appdata"
        "LOCALAPPDATA=${SUBCLI_STABILITY_ENV_ROOT}/localappdata"
        "XDG_CONFIG_HOME=${SUBCLI_STABILITY_ENV_ROOT}/xdg-config"
        "XDG_DATA_HOME=${SUBCLI_STABILITY_ENV_ROOT}/xdg-data"
        "XDG_CACHE_HOME=${SUBCLI_STABILITY_ENV_ROOT}/xdg-cache"
        "XDG_STATE_HOME=${SUBCLI_STABILITY_ENV_ROOT}/xdg-state"
        $<TARGET_FILE:subcli_stability_runner>
        --mode user
        --subcli-bin $<TARGET_FILE:subcli>
        --source-dir ${CMAKE_SOURCE_DIR}
        --test-root "${CMAKE_CURRENT_BINARY_DIR}/subcli-stability-user-$<CONFIG>"
)
```

- [ ] **Step 3: Run the new journey and capture the first failure**

Run:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build -R subcli_stability_user_journey --output-on-failure
```

Expected first failure may be one of:

- HTTP server stop hangs because no direct shutdown client exists.
- Export output filename differs from `mihomo.yaml`.
- The subscription fixture is rejected by the parser.

Fix the exact failure by making the runner reflect current CLI behavior while preserving the user journey. If output filename differs, locate the created non-empty `.yaml` file in `outputDir` instead of hardcoding `mihomo.yaml`.

- [ ] **Step 4: Add HTTP boundary checks to the runner**

Add this function to `tests/stability_runner.cpp` and call it after the successful plain subscription journey:

```cpp
void runBoundaryChecks(const Options& options, subcli::StabilityHttpServer& server) {
    runOk(options, "sub add bad500", {"sub", "add", "--name", "bad500", "--url", server.url("/sub/500")});
    const std::string bad500 = runFail(options, "sub update bad500", {"sub", "update", "bad500", "--strict-network"}, 20);
    requireContains(bad500, "500", "HTTP 500 failure");

    runOk(options, "sub add empty", {"sub", "add", "--name", "empty", "--url", server.url("/sub/empty")});
    (void)runFail(options, "sub update empty", {"sub", "update", "empty", "--strict-network"}, 20);

    runOk(options, "sub add malformed", {"sub", "add", "--name", "malformed", "--url", server.url("/sub/malformed")});
    runOk(options, "sub update malformed", {"sub", "update", "malformed"}, 20);

    runOk(options, "sub add unicode", {"sub", "add", "--name", "unicode", "--url", server.url("/sub/unicode")});
    runOk(options, "sub update unicode", {"sub", "update", "unicode", "--strict-network"}, 20);

    runOk(options, "sub add slow", {"sub", "add", "--name", "slow", "--url", server.url("/sub/slow"), "--timeout", "1"});
    (void)runFail(options, "sub update slow", {"sub", "update", "slow", "--strict-network"}, 10);
}
```

Call it before `server.stop()`:

```cpp
runBoundaryChecks(options, server);
```

- [ ] **Step 5: Run full test subset**

Run:

```bash
cmake --build build -j
ctest --test-dir build -R "subcli_tests|subcli_cli_basic|subcli_stability_user_journey" --output-on-failure
```

Expected: all three tests pass.

- [ ] **Step 6: Commit Task 4**

Run:

```bash
git add CMakeLists.txt tests/stability_runner.cpp
git commit -m "test: add stability user journey"
```

---

## Task 5: Package Extraction Stability Journey

**Files:**
- Create: `tests/stability_package_journey.cmake`
- Modify: `CMakeLists.txt`
- Modify: `.github/workflows/release.yml`
- Modify: `.github/workflows/release-validation.yml`

- [ ] **Step 1: Create package journey script**

Create `tests/stability_package_journey.cmake`:

```cmake
cmake_minimum_required(VERSION 3.20)

foreach(_required_var IN ITEMS BUILD_DIR SOURCE_DIR TEST_ROOT STABILITY_RUNNER)
    if(NOT DEFINED ${_required_var} OR "${${_required_var}}" STREQUAL "")
        message(FATAL_ERROR "${_required_var} is required")
    endif()
endforeach()

get_filename_component(BUILD_DIR "${BUILD_DIR}" ABSOLUTE)
get_filename_component(SOURCE_DIR "${SOURCE_DIR}" ABSOLUTE)
get_filename_component(TEST_ROOT "${TEST_ROOT}" ABSOLUTE)
get_filename_component(STABILITY_RUNNER "${STABILITY_RUNNER}" ABSOLUTE)

file(GLOB _packages "${BUILD_DIR}/subcli-*.zip" "${BUILD_DIR}/subcli-*.tar.gz")
list(SORT _packages)
if(NOT _packages)
    message(FATAL_ERROR "No CPack package found in ${BUILD_DIR}. Build the package target before running this test.")
endif()
list(GET _packages 0 _package)

set(_extract_dir "${TEST_ROOT}/extract")
set(_env_root "${TEST_ROOT}/env")
file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${_extract_dir}" "${_env_root}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xzf "${_package}"
    WORKING_DIRECTORY "${_extract_dir}"
    RESULT_VARIABLE _extract_result
    OUTPUT_VARIABLE _extract_stdout
    ERROR_VARIABLE _extract_stderr
)
if(NOT _extract_result STREQUAL "0")
    message(FATAL_ERROR "Failed to extract ${_package}\nstdout:\n${_extract_stdout}\nstderr:\n${_extract_stderr}")
endif()

file(GLOB_RECURSE _subcli_bins "${_extract_dir}/subcli" "${_extract_dir}/subcli.exe")
list(SORT _subcli_bins)
if(NOT _subcli_bins)
    message(FATAL_ERROR "No subcli executable found after extracting ${_package}")
endif()
list(GET _subcli_bins 0 _subcli_bin)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "--unset=SUBCLI_WORKSPACE"
        "HOME=${_env_root}/home"
        "USERPROFILE=${_env_root}/home"
        "APPDATA=${_env_root}/appdata"
        "LOCALAPPDATA=${_env_root}/localappdata"
        "XDG_CONFIG_HOME=${_env_root}/xdg-config"
        "XDG_DATA_HOME=${_env_root}/xdg-data"
        "XDG_CACHE_HOME=${_env_root}/xdg-cache"
        "XDG_STATE_HOME=${_env_root}/xdg-state"
        "${STABILITY_RUNNER}"
        --mode package
        --subcli-bin "${_subcli_bin}"
        --source-dir "${SOURCE_DIR}"
        --test-root "${TEST_ROOT}/runner"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
)
if(NOT _result STREQUAL "0")
    message(FATAL_ERROR "Package journey failed\nPackage: ${_package}\nBinary: ${_subcli_bin}\nstdout:\n${_stdout}\nstderr:\n${_stderr}")
endif()

message(STATUS "Package journey passed for ${_package}")
```

- [ ] **Step 2: Register package journey in CMake**

Add after the stability user journey test:

```cmake
add_test(
    NAME subcli_stability_package_journey
    COMMAND ${CMAKE_COMMAND}
        "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/subcli-stability-package-$<CONFIG>"
        "-DSTABILITY_RUNNER=$<TARGET_FILE:subcli_stability_runner>"
        -P "${CMAKE_SOURCE_DIR}/tests/stability_package_journey.cmake"
)
```

- [ ] **Step 3: Run package journey and verify failure before package exists**

Run:

```bash
ctest --test-dir build -R subcli_stability_package_journey --output-on-failure
```

Expected: fails with `No CPack package found`. This proves the test catches missing package artifacts.

- [ ] **Step 4: Build package and run package journey**

Run:

```bash
cmake --build build --target package
ctest --test-dir build -R subcli_stability_package_journey --output-on-failure
```

Expected: package journey passes. If it fails because the extracted executable is not marked executable on POSIX, add this before invoking the runner in `stability_package_journey.cmake`:

```cmake
if(UNIX)
    execute_process(COMMAND chmod +x "${_subcli_bin}")
endif()
```

- [ ] **Step 5: Update GitHub workflows to package before full test**

In `.github/workflows/release.yml`, change the order to:

```yaml
      - name: Build
        run: cmake --build build --config Release -j

      - name: Package
        run: cmake --build build --config Release --target package

      - name: Test
        run: ctest --test-dir build --build-config Release --output-on-failure
```

Make the same ordering change in `.github/workflows/release-validation.yml`.

- [ ] **Step 6: Run full local package validation**

Run:

```bash
cmake -S . -B build
cmake --build build -j
cmake --build build --target package
ctest --test-dir build --output-on-failure
```

Expected: all CTests pass, including package journey.

- [ ] **Step 7: Commit Task 5**

Run:

```bash
git add CMakeLists.txt tests/stability_package_journey.cmake .github/workflows/release.yml .github/workflows/release-validation.yml
git commit -m "test: validate package first-use journey"
```

---

## Task 6: Platform Boundary Scan and Stability Edge Tightening

**Files:**
- Create: `tests/platform_boundary_scan.cmake`
- Modify: `CMakeLists.txt`
- Modify: `tests/stability_runner.cpp`
- Modify: `tests/subcli_tests.cpp`

- [ ] **Step 1: Create platform boundary scan script**

Create `tests/platform_boundary_scan.cmake`:

```cmake
cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()
get_filename_component(SOURCE_DIR "${SOURCE_DIR}" ABSOLUTE)

set(_tokens
    "#include <windows.h>"
    "#include <winsock2.h>"
    "#include <unistd.h>"
    "#include <sys/wait.h>"
    "#include <sys/socket.h>"
    "CreateProcess"
    "TerminateProcess"
    "WaitForSingleObject"
    "fork("
    "waitpid"
    "kill("
    "socket("
    "bind("
    "listen("
    "accept("
)

set(_allowed
    "src/platform_posix.cpp"
    "src/platform_windows.cpp"
    "tests/platform_test_support.cpp"
    "tests/stability_http_server.cpp"
)

file(GLOB_RECURSE _files
    "${SOURCE_DIR}/include/*.hpp"
    "${SOURCE_DIR}/src/*.cpp"
    "${SOURCE_DIR}/tests/*.cpp"
    "${SOURCE_DIR}/tests/*.hpp"
)

set(_violations "")
foreach(_file IN LISTS _files)
    file(RELATIVE_PATH _rel "${SOURCE_DIR}" "${_file}")
    list(FIND _allowed "${_rel}" _is_allowed)
    if(NOT _is_allowed EQUAL -1)
        continue()
    endif()
    file(READ "${_file}" _content)
    foreach(_token IN LISTS _tokens)
        string(FIND "${_content}" "${_token}" _pos)
        if(NOT _pos EQUAL -1)
            string(APPEND _violations "${_rel}: ${_token}\n")
        endif()
    endforeach()
endforeach()

if(NOT _violations STREQUAL "")
    message(FATAL_ERROR "Platform primitive leakage detected:\n${_violations}")
endif()

message(STATUS "Platform boundary scan passed")
```

- [ ] **Step 2: Register scan in CTest**

In `CMakeLists.txt`, add:

```cmake
add_test(
    NAME subcli_platform_boundary_scan
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/tests/platform_boundary_scan.cmake"
)
```

- [ ] **Step 3: Run scan and adjust allowed list only for boundary files**

Run:

```bash
ctest --test-dir build -R subcli_platform_boundary_scan --output-on-failure
```

Expected: pass. If it fails, inspect each violation. Move platform code into an existing boundary file or the HTTP test server file instead of expanding the allowed list to business code.

- [ ] **Step 4: Add help snapshot checks to stability runner**

In `tests/stability_runner.cpp`, add command-specific help checks in `runJourney` after root help:

```cpp
const std::string initHelp = runOk(options, "init help", {"init", "--help"});
requireContains(initHelp, "remember", "init help");
requireContains(initHelp, "subcli init ./my-subcli", "init help");

const std::string workspaceHelp = runOk(options, "workspace help", {"workspace", "--help"});
requireContains(workspaceHelp, "workspace init initializes", "workspace help");
requireContains(workspaceHelp, "workspace unset", "workspace help");

const std::string subHelp = runOk(options, "sub help", {"sub", "--help"});
requireContains(subHelp, "Subscriptions are URLs", "sub help");
requireContains(subHelp, "Typical flow", "sub help");

const std::string exportHelp = runOk(options, "export help", {"export", "--help"});
requireContains(exportHelp, "Generate native client config files", "export help");
requireContains(exportHelp, "--output-dir", "export help");
```

- [ ] **Step 5: Add workspace override checks to stability runner**

In `tests/stability_runner.cpp`, add after the first `workspace status --json` check:

```cpp
const fs::path overrideWorkspace = options.testRoot / "override workspace";
runOk(options, "workspace init override", {"workspace", "init", overrideWorkspace.string()});
const std::string switched = runOk(options, "workspace status after switch", {"workspace", "status", "--json"});
requireContains(switched, overrideWorkspace.string(), "workspace status after second init");

runOk(options, "workspace use original", {"workspace", "use", workspace.string()});
const std::string restored = runOk(options, "workspace status restored", {"workspace", "status", "--json"});
requireContains(restored, workspace.string(), "workspace status after workspace use");
```

- [ ] **Step 6: Run full test suite**

Run:

```bash
cmake -S . -B build
cmake --build build -j
cmake --build build --target package
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 7: Commit Task 6**

Run:

```bash
git add CMakeLists.txt tests/platform_boundary_scan.cmake tests/stability_runner.cpp tests/subcli_tests.cpp
git commit -m "test: cover stability boundary cases"
```

---

## Task 7: Final Verification and Readiness Evidence

**Files:**
- Create: `docs/superpowers/reviews/2026-05-16-stability-maintenance-readiness.md`

- [ ] **Step 1: Run final local verification**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/stability-maintenance
cmake -S . -B build
cmake --build build -j
cmake --build build --target package
ctest --test-dir build --output-on-failure
```

Expected:

```text
100% tests passed
```

If configure fails due `FetchContent` network errors, retry once after confirming network access. If it still fails before compilation, record the exact network error separately and do not claim code verification passed.

- [ ] **Step 2: Push branch and run GitHub Actions**

Run:

```bash
git push -u origin feat/stability-maintenance
```

Then verify the workflow runs for Linux, macOS, and Windows. Required successful jobs:

```text
Release Validation / Validate linux-x86_64
Release Validation / Validate macos-arm64
Release Validation / Validate windows-x86_64
```

- [ ] **Step 3: Create readiness evidence document**

Create `docs/superpowers/reviews/2026-05-16-stability-maintenance-readiness.md`:

```markdown
# Stability Maintenance Readiness

**Branch:** `feat/stability-maintenance`

**Version policy:** No version bump.

## Local Verification

Command:

```bash
cmake -S . -B build
cmake --build build -j
cmake --build build --target package
ctest --test-dir build --output-on-failure
```

Result:

Write a fenced `text` block containing the exact final lines from Step 1. The block must include the CTest summary line showing all tests passed.

## CI Verification

Workflow run:

Write the GitHub Actions run URL for the successful three-platform validation run.

Results:

- Linux x86_64: success
- macOS arm64: success
- Windows x86_64: success

## Coverage Summary

- `subcli init [DIR]` initializes and remembers the default workspace.
- `subcli workspace init [DIR]` initializes and remembers the default workspace.
- Build-binary user journey passed.
- Package-extraction user journey passed.
- Local HTTP subscription simulation passed.
- HTTP error, empty, malformed, slow, and Unicode boundary cases passed.
- Help text first-use checks passed.
- Chinese command glossary exists.
- Platform boundary scan passed.
```

Before committing, verify the readiness document contains real local verification output and the real CI run URL.

- [ ] **Step 4: Commit readiness evidence**

Run:

```bash
git add docs/superpowers/reviews/2026-05-16-stability-maintenance-readiness.md
git commit -m "docs: record stability maintenance readiness"
```

- [ ] **Step 5: Final status check**

Run:

```bash
git status --short
git log --oneline --decorate -8
```

Expected: `git status --short` prints no tracked or untracked files. The branch should contain the stability maintenance commits after `docs: add stability maintenance design`.

---

## Execution Notes

- The current root checkout has unrelated local state (`.gitignore`, `.pi/`). Continue working only inside `/home/lisztzy/prj/subcli-cpp/.worktrees/stability-maintenance`.
- Use explicit `cd /home/lisztzy/prj/subcli-cpp/.worktrees/stability-maintenance && ...` in shell commands.
- The first configure can require network because `FetchContent` downloads dependencies. A network/TLS failure during dependency download is not a code failure.
- Do not bump the version in `CMakeLists.txt`.
- Do not use Python, Bash, or real external subscription URLs for cross-platform stability tests.
- If a new platform primitive is needed, put it in `src/platform_*.cpp`, `tests/platform_test_support.cpp`, or `tests/stability_http_server.cpp`.
