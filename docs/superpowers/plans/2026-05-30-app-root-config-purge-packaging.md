# App-root Config, Purge, and Three-platform Packaging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将配置初始化、路径解析、下载资源清理、便携式/安装式打包统一到“相对于 app 主程序目录”的三平台模型。

**Architecture:** 保留 `config.yaml` 作为唯一配置入口，但不再把相对路径解析到 `config.yaml` 所在目录，而是解析到可执行文件所在目录 `appDir`。`config.yaml` 只由 `subcli config init` 创建；普通命令在找不到配置时提示初始化，不隐式创建。新增跨平台 `purge` 服务和命令，用同一套删除逻辑清理下载的 geosite/geoip 资源、缓存、输出、状态和配置；包管理脚本在各平台调用或等价实现该清理策略。

**Tech Stack:** C++17, `std::filesystem`, yaml-cpp, CLI11, nlohmann_json, CMake/CPack, POSIX shell, RPM scriptlets, NSIS, PowerShell-compatible Windows paths.

---

## 需求映射

| 用户需求 | 设计结论 | 覆盖任务 |
| --- | --- | --- |
| 1. “所有路径相对于 config.yaml 所在目录解析”改为“相对于 app 主程序”，且 config 由 `config init` 创建 | `EnvironmentInfo` 增加 `appDir`；所有配置路径用 `resolvePathFromAppDir(appDir, value)`；普通命令不自动创建配置；新增 `subcli config init` | Task 1, 2, 3, 4 |
| 2. 确认下载的 geosite 等资源卸载时是否删除 | 当前没有统一卸载清理功能；包脚本也未覆盖三端 | Task 5, 6 |
| 3. 如果不会删除，则添加该功能并三端同步 | 新增 `subcli purge` 跨平台命令；DEB/RPM/NSIS/便携包文档接入 | Task 5, 6, 8 |
| 4. 便携式和安装式三大系统平台全面支持 | 便携包覆盖 Linux/macOS/Windows；安装式覆盖 Linux DEB/RPM、macOS Homebrew、Windows NSIS/Scoop | Task 6, 7, 8 |

---

## File Structure

### Create

- `include/subcli/config_defaults.hpp`  
  生成 portable/FHS/user-local 默认 `AppConfig`，解析配置路径，初始化默认资源 URL/路径。

- `src/config_defaults.cpp`  
  实现 `makeDefaultConfig`、`resolveConfigPathsFromAppDir`、`ensureConfigRuntimeFiles`、`writeInitialConfig`。

- `include/subcli/purge.hpp`  
  定义跨平台清理 API：`PurgeOptions`、`PurgeResult`、`planPurge`、`executePurge`。

- `src/purge.cpp`  
  实现 dry-run、路径去重、安全检查、`remove_all`、资源元数据清理。

- `packaging/fhs/config.yaml`  
  Linux/macOS 安装式默认配置，路径使用绝对路径。

- `packaging/portable/config.yaml`  
  便携式默认配置，路径使用相对于 appDir 的 `./data/...`、`./templates/...`。

- `packaging/deb/postinst`  
  DEB 安装后创建运行目录和初始 `sub.yaml`。

- `packaging/deb/prerm`  
  DEB 卸载/升级前停止 daemon。

- `packaging/deb/postrm`  
  DEB `purge` 时删除 `/var/lib/subcli`、`/var/cache/subcli`、`/var/log/subcli`、`/etc/subcli`。

- `packaging/rpm/postinstall.sh`  
  RPM 安装后创建运行目录和初始 `sub.yaml`。

- `packaging/rpm/preremove.sh`  
  RPM 卸载/升级前停止 daemon。

- `packaging/rpm/postuninstall.sh`  
  RPM 最终卸载时删除运行资源目录。

- `packaging/nsis/uninstall_extra.nsh`  
  Windows NSIS 卸载阶段清理安装目录下的 data/cache/logs/outputs/config。

- `packaging/homebrew/subcli.rb`  
  macOS Homebrew Formula 示例，安装 FHS-like Homebrew 布局。

- `packaging/scoop/subcli.json`  
  Windows Scoop manifest 示例，声明 `persist` 和 `bin`。

### Modify

- `include/subcli/environment.hpp`  
  替换 workspace 模型为 config 模型，新增 `ConfigMode::Missing`、`EnvironmentInfo::appDir`、`resolvePathFromAppDir`。

- `src/environment.cpp`  
  检测顺序改为 `--config > SUBCLI_CONFIG > appDir/config.yaml > FHS config > user-local config > Missing`，并返回 appDir。

- `include/subcli/models.hpp`  
  `AppConfig` 增加 `dataDir`、`cacheDir`、`profileDir`、`stateDir`、`logDir`、`subFile` 字段。

- `include/subcli/store.hpp` / `src/store.cpp`  
  load/save 新增字段；保持现有 `assets.paths` / `assets.urls` schema。

- `src/config_service.cpp`  
  支持新增路径 key；`setConfigValue` 的 resolver 改传 appDir resolver。

- `src/main.cpp`  
  增加全局 `--config`；普通命令找不到配置时失败并提示 `subcli config init`；新增 `config init` 和 `purge` 命令入口；删除或兼容旧 `--workspace` 的实现按本计划统一为 `--config`。

- `src/assets.cpp`  
  不改变下载逻辑；由新的路径解析保证下载写入 appDir 派生路径。

- `src/diagnostic_service.cpp` / `src/commands/doctor_command.cpp`  
  doctor 报告 appDir、configPath、config mode、资源目录可写性。

- `src/cli_completion.cpp`  
  根命令加入 `purge`；config 子命令加入 `init`；全局选项加入 `--config`。

- `CMakeLists.txt`  
  新增源文件、安装布局选项、DEB/RPM/NSIS/TGZ/ZIP CPack 配置。

- `tests/subcli_tests.cpp`  
  新增环境、默认配置、路径解析、purge 单元测试；更新原有 config-relative 测试为 appDir-relative。

- `tests/cli_basic_smoke.cmake`  
  首次使用流程改为 `subcli config init --portable --path <path>`。

- `tests/stability_package_journey.cmake`  
  验证 portable archive 布局包含 exe 同级 `config.yaml`，并验证 purge dry-run。

- `README.md`、`README.subcli.md`、`docs/config-file.md`、`docs/cli-glossary.zh-CN.md`  
  文档改为 appDir 解析规则、`config init` 首次使用、`purge` 清理说明和三平台安装说明。

---

## Task 1: Environment API 改为 appDir + config init 模型

**Files:**
- Modify: `include/subcli/environment.hpp`
- Modify: `src/environment.cpp`
- Modify: `tests/subcli_tests.cpp`

### Step 1: 写失败测试

在 `tests/subcli_tests.cpp` 中现有 environment/path 测试附近添加以下函数：

```cpp
void testEnvironmentResolvesRelativePathsFromAppDir() {
    const fs::path appDir = fs::temp_directory_path() / "subcli-env-appdir-test";
    std::error_code ec;
    fs::remove_all(appDir, ec);
    fs::create_directories(appDir, ec);

    const auto resolved = subcli::resolvePathFromAppDir(appDir, "data/assets/geosite.dat");
    require(resolved == (appDir / "data/assets/geosite.dat").lexically_normal(), "relative config paths must resolve from appDir");

    const fs::path absolute = appDir / "already/absolute.dat";
    const auto kept = subcli::resolvePathFromAppDir(appDir, absolute.string());
    require(kept == absolute.lexically_normal(), "absolute config paths must stay absolute");
}

void testDetectEnvironmentPortableKeepsAppDirSeparateFromConfigDir() {
    const fs::path root = fs::temp_directory_path() / "subcli-env-portable-appdir";
    const fs::path configDir = root / "config-location";
    const fs::path appDir = root / "app";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(configDir, ec);
    fs::create_directories(appDir, ec);
    {
        std::ofstream out(appDir / "config.yaml");
        out << "version: 1\n";
    }

    subcli::EnvironmentDetectInput input;
    input.argv0 = (appDir / "subcli").string();
    input.exeDirOverride = appDir.string();
    input.cwd = configDir.string();
    input.platform = subcli::PlatformKind::Linux;

    const auto info = subcli::detectEnvironment(input);
    require(info.ok, "portable config detection should succeed: " + info.error);
    require(info.mode == subcli::ConfigMode::Portable, "exe-dir config should use portable mode");
    require(info.appDir == appDir.lexically_normal(), "EnvironmentInfo.appDir should be executable directory");
    require(info.configDir == appDir.lexically_normal(), "portable configDir should be exe dir when config is exe-adjacent");
}

void testDetectEnvironmentMissingRequiresConfigInit() {
    const fs::path root = fs::temp_directory_path() / "subcli-env-missing-config";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    subcli::EnvironmentDetectInput input;
    input.argv0 = (root / "subcli").string();
    input.exeDirOverride = root.string();
    input.cwd = root.string();
    input.fhsConfigOverride = (root / "missing-fhs/config.yaml").string();
    input.userConfigOverride = (root / "missing-user/config.yaml").string();
    input.platform = subcli::PlatformKind::Linux;

    const auto info = subcli::detectEnvironment(input);
    require(!info.ok, "missing config should not be treated as ready");
    require(info.mode == subcli::ConfigMode::Missing, "missing config should return ConfigMode::Missing");
    require(info.appDir == root.lexically_normal(), "missing config result should still expose appDir for config init");
    require(info.error.find("subcli config init") != std::string::npos, "missing config error should instruct user to run config init");
}
```

在测试注册区加入：

```cpp
runTest("testEnvironmentResolvesRelativePathsFromAppDir", testEnvironmentResolvesRelativePathsFromAppDir);
runTest("testDetectEnvironmentPortableKeepsAppDirSeparateFromConfigDir", testDetectEnvironmentPortableKeepsAppDirSeparateFromConfigDir);
runTest("testDetectEnvironmentMissingRequiresConfigInit", testDetectEnvironmentMissingRequiresConfigInit);
```

### Step 2: 运行测试并确认失败

Run:

```bash
cmake -S . -B build && cmake --build build -j && ./build/subcli_tests
```

Expected: FAIL，错误包含 `resolvePathFromAppDir`、`EnvironmentDetectInput` 或 `ConfigMode::Missing` 未定义。

### Step 3: 修改 `include/subcli/environment.hpp`

将 workspace 风格定义替换为 config 风格定义。保留 `PlatformKind`，删除 `EnvironmentSource`。文件应包含以下公共接口：

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace subcli {

enum class PlatformKind { Linux, MacOS, Windows };

enum class ConfigMode {
    Explicit,
    Portable,
    FHS,
    UserLocal,
    Missing,
};

struct EnvironmentInfo {
    bool ok = false;
    ConfigMode mode = ConfigMode::Missing;
    std::filesystem::path appDir;
    std::filesystem::path configPath;
    std::filesystem::path configDir;
    std::vector<std::string> trace;
    std::string error;
};

struct EnvironmentDetectInput {
    std::string argv0;
    std::string configOption;
    std::string envConfig;
    std::string cwd;
    std::string exeDirOverride;
    std::string fhsConfigOverride;
    std::string userConfigOverride;
    PlatformKind platform = PlatformKind::Linux;
};

struct EnvironmentPaths {
    std::string root;
    std::string appDir;
    std::string configDir;
    std::string dataDir;
    std::string cacheDir;
    std::string stateDir;
    std::string outputDir;
    std::string templateDir;
    std::string profileDir;
    std::string logDir;
    std::string assetDir;
    std::string subPath;
    std::string configPath;
};

EnvironmentInfo detectEnvironment(const EnvironmentDetectInput& input);
std::filesystem::path resolvePathFromAppDir(const std::filesystem::path& appDir, const std::string& value);
std::filesystem::path platformFhsConfigPath(PlatformKind platform);
std::filesystem::path platformUserConfigPath(PlatformKind platform);
std::string configModeName(ConfigMode mode);

} // namespace subcli
```

### Step 4: 修改 `src/environment.cpp`

实现要点：

```cpp
std::filesystem::path resolvePathFromAppDir(const std::filesystem::path& appDir, const std::string& value) {
    if (value.empty()) {
        return {};
    }
    fs::path path(value);
    if (path.is_absolute()) {
        return path.lexically_normal();
    }
    return (appDir / path).lexically_normal();
}
```

`detectEnvironment()` 顺序必须为：

1. `--config PATH`
2. `SUBCLI_CONFIG=PATH`
3. `<appDir>/config.yaml`
4. FHS config：Linux `/etc/subcli/config.yaml`，macOS `/usr/local/etc/subcli/config.yaml`，Windows 无 FHS
5. user-local config：Linux `~/.config/subcli/config.yaml`，macOS `~/Library/Application Support/subcli/config.yaml`，Windows `%APPDATA%/subcli/config.yaml`
6. Missing：返回 `ok=false`，错误提示 `subcli config init`

缺失配置时的错误字符串使用：

```cpp
info.error = "config.yaml not found; run 'subcli config init --portable' or 'subcli config init --path <path>' first";
```

### Step 5: 运行测试

Run:

```bash
cmake -S . -B build && cmake --build build -j && ./build/subcli_tests
```

Expected: 新增 environment 测试 PASS。旧的 workspace/environment 测试会因接口变更失败，后续任务统一更新。

### Step 6: Commit

```bash
git add include/subcli/environment.hpp src/environment.cpp tests/subcli_tests.cpp
git commit -m "refactor: resolve config paths from app directory"
```

---

## Task 2: 扩展 AppConfig 并保持 YAML schema 可持久化

**Files:**
- Modify: `include/subcli/models.hpp`
- Modify: `include/subcli/store.hpp`
- Modify: `src/store.cpp`
- Modify: `tests/subcli_tests.cpp`

### Step 1: 写失败测试

在 `tests/subcli_tests.cpp` 的 store/config 测试附近添加：

```cpp
void testStorePersistsRuntimePathFields() {
    const fs::path dir = fs::temp_directory_path() / "subcli-config-runtime-paths";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path configPath = dir / "config.yaml";

    subcli::AppConfig config;
    config.dataDir = "./data";
    config.cacheDir = "./cache";
    config.assetDir = "./data/assets";
    config.templateDir = "./templates";
    config.profileDir = "./profiles";
    config.outputDir = "./outputs";
    config.stateDir = "./data/state";
    config.logDir = "./logs";
    config.subFile = "./data/sub.yaml";
    config.assetPaths["xray.geosite"] = "./data/assets/xray/geosite.dat";
    config.assetUrls["xray.geosite"] = "https://example.invalid/geosite.dat";

    subcli::saveConfig(configPath.string(), config);
    const auto loaded = subcli::loadConfig(configPath.string());

    require(loaded.dataDir == "./data", "data_dir should persist");
    require(loaded.cacheDir == "./cache", "cache_dir should persist");
    require(loaded.assetDir == "./data/assets", "asset_dir should persist");
    require(loaded.templateDir == "./templates", "template_dir should persist");
    require(loaded.profileDir == "./profiles", "profile_dir should persist");
    require(loaded.outputDir == "./outputs", "output_dir should persist");
    require(loaded.stateDir == "./data/state", "state_dir should persist");
    require(loaded.logDir == "./logs", "log_dir should persist");
    require(loaded.subFile == "./data/sub.yaml", "sub_file should persist");
    require(loaded.assetPaths.at("xray.geosite") == "./data/assets/xray/geosite.dat", "asset path should persist as app-relative value");
}
```

注册：

```cpp
runTest("testStorePersistsRuntimePathFields", testStorePersistsRuntimePathFields);
```

### Step 2: 运行测试并确认失败

Run:

```bash
cmake -S . -B build && cmake --build build -j && ./build/subcli_tests
```

Expected: FAIL，`AppConfig` 缺少 `dataDir`、`cacheDir`、`profileDir`、`stateDir`、`logDir`、`subFile`。

### Step 3: 修改 `include/subcli/models.hpp`

在 `AppConfig` 中加入路径字段，保持现有字段顺序附近：

```cpp
std::string dataDir = "./data";
std::string cacheDir = "./cache";
std::string templateDir = "./templates";
std::string profileDir = "./profiles";
std::string outputDir = "./outputs";
std::string stateDir = "./data/state";
std::string logDir = "./logs";
std::string subFile = "./data/sub.yaml";
std::string assetDir = "./data/assets";
```

保留既有：

```cpp
std::map<std::string, std::string> assetPaths;
std::map<std::string, std::string> assetUrls;
```

### Step 4: 修改 `src/store.cpp` loadConfig

在读取基础字段的位置加入：

```cpp
c.dataDir = root["data_dir"].as<std::string>("./data");
c.cacheDir = root["cache_dir"].as<std::string>("./cache");
c.templateDir = root["template_dir"].as<std::string>("./templates");
c.profileDir = root["profile_dir"].as<std::string>("./profiles");
c.outputDir = root["output_dir"].as<std::string>("./outputs");
c.stateDir = root["state_dir"].as<std::string>("./data/state");
c.logDir = root["log_dir"].as<std::string>("./logs");
c.subFile = root["sub_file"].as<std::string>("./data/sub.yaml");
c.assetDir = root["asset_dir"].as<std::string>("./data/assets");
```

删除或替换旧的：

```cpp
c.templateDir = root["template_dir"].as<std::string>("./templates");
c.outputDir = root["output_dir"].as<std::string>("./outputs");
c.assetDir = root["asset_dir"].as<std::string>("./assets");
```

### Step 5: 修改 `src/store.cpp` saveConfig

在输出基础字段的位置加入：

```cpp
root["data_dir"] = c.dataDir;
root["cache_dir"] = c.cacheDir;
root["asset_dir"] = c.assetDir;
root["template_dir"] = c.templateDir;
root["profile_dir"] = c.profileDir;
root["output_dir"] = c.outputDir;
root["state_dir"] = c.stateDir;
root["log_dir"] = c.logDir;
root["sub_file"] = c.subFile;
```

确保旧的 `root["asset_dir"]`、`root["template_dir"]`、`root["output_dir"]` 没有重复赋值。

### Step 6: 运行测试

Run:

```bash
cmake -S . -B build && cmake --build build -j && ./build/subcli_tests
```

Expected: `testStorePersistsRuntimePathFields` PASS。

### Step 7: Commit

```bash
git add include/subcli/models.hpp include/subcli/store.hpp src/store.cpp tests/subcli_tests.cpp
git commit -m "feat: persist runtime paths in config"
```

---

## Task 3: 默认配置和 appDir 路径解析服务

**Files:**
- Create: `include/subcli/config_defaults.hpp`
- Create: `src/config_defaults.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/subcli_tests.cpp`

### Step 1: 写失败测试

在 `tests/subcli_tests.cpp` 添加：

```cpp
#include "subcli/config_defaults.hpp"
```

添加测试函数：

```cpp
void testPortableDefaultConfigUsesAppRelativePaths() {
    const auto config = subcli::makeDefaultConfig(subcli::ConfigLayout::Portable, subcli::PlatformKind::Linux);
    require(config.dataDir == "./data", "portable data_dir should be app-relative");
    require(config.cacheDir == "./cache", "portable cache_dir should be app-relative");
    require(config.assetDir == "./data/assets", "portable asset_dir should be app-relative");
    require(config.templateDir == "./templates", "portable template_dir should be app-relative");
    require(config.profileDir == "./profiles", "portable profile_dir should be app-relative");
    require(config.outputDir == "./outputs", "portable output_dir should be app-relative");
    require(config.stateDir == "./data/state", "portable state_dir should be app-relative");
    require(config.logDir == "./logs", "portable log_dir should be app-relative");
    require(config.subFile == "./data/sub.yaml", "portable sub_file should be app-relative");
    require(config.assetPaths.at("xray.geosite") == "./data/assets/xray/geosite.dat", "portable asset path should include asset dir prefix");
    require(config.templateNormal.at("mihomo") == "./templates/mihomo_base.yaml", "portable template path should include template dir prefix");
}

void testResolveConfigPathsFromAppDir() {
    const fs::path appDir = fs::temp_directory_path() / "subcli-resolve-config-appdir";
    subcli::AppConfig config = subcli::makeDefaultConfig(subcli::ConfigLayout::Portable, subcli::PlatformKind::Linux);
    subcli::resolveConfigPathsFromAppDir(config, appDir);

    require(config.dataDir == (appDir / "data").lexically_normal().string(), "data_dir should resolve from appDir");
    require(config.cacheDir == (appDir / "cache").lexically_normal().string(), "cache_dir should resolve from appDir");
    require(config.assetDir == (appDir / "data/assets").lexically_normal().string(), "asset_dir should resolve from appDir");
    require(config.outputDir == (appDir / "outputs").lexically_normal().string(), "output_dir should resolve from appDir");
    require(config.subFile == (appDir / "data/sub.yaml").lexically_normal().string(), "sub_file should resolve from appDir");
    require(config.assetPaths.at("xray.geoip") == (appDir / "data/assets/xray/geoip.dat").lexically_normal().string(), "asset path should resolve from appDir");
    require(config.templateNormal.at("xray") == (appDir / "templates/xray_base.json").lexically_normal().string(), "template path should resolve from appDir");
}
```

注册：

```cpp
runTest("testPortableDefaultConfigUsesAppRelativePaths", testPortableDefaultConfigUsesAppRelativePaths);
runTest("testResolveConfigPathsFromAppDir", testResolveConfigPathsFromAppDir);
```

### Step 2: 运行测试并确认失败

Run:

```bash
cmake -S . -B build && cmake --build build -j && ./build/subcli_tests
```

Expected: FAIL，`subcli/config_defaults.hpp` 不存在。

### Step 3: 创建 `include/subcli/config_defaults.hpp`

```cpp
#pragma once

#include <filesystem>
#include <string>

#include "subcli/environment.hpp"
#include "subcli/models.hpp"

namespace subcli {

enum class ConfigLayout {
    Portable,
    FHS,
    UserLocal,
};

AppConfig makeDefaultConfig(ConfigLayout layout, PlatformKind platform);
void resolveConfigPathsFromAppDir(AppConfig& config, const std::filesystem::path& appDir);
bool ensureConfigRuntimeFiles(const AppConfig& config, std::string& error);
bool writeInitialConfig(const std::filesystem::path& path, const AppConfig& config, bool force, std::string& error);

} // namespace subcli
```

### Step 4: 创建 `src/config_defaults.cpp`

核心实现应包含这些常量和函数：

```cpp
#include "subcli/config_defaults.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>

#include "subcli/store.hpp"

namespace subcli {
namespace fs = std::filesystem;

namespace {

void fillCommonDefaults(AppConfig& config) {
    config.profile = "bypass-cn";
    config.tun = false;
    config.logLevel = "info";
    config.parallelism = 4;
    config.timeout = 15;
    config.retry = 2;
    config.fetchMaxBytes = 10 * 1024 * 1024;

    config.templateNormal["mihomo"] = "./templates/mihomo_base.yaml";
    config.templateTun["mihomo"] = "./templates/mihomo_tun.yaml";
    config.templateNormal["sing-box"] = "./templates/singbox_base.json";
    config.templateTun["sing-box"] = "./templates/singbox_tun.json";
    config.templateNormal["xray"] = "./templates/xray_base.json";
    config.templateTun["xray"] = "./templates/xray_tun.json";

    config.assetPaths["mihomo.geosite"] = "./data/assets/mihomo/geosite.dat";
    config.assetPaths["mihomo.geoip"] = "./data/assets/mihomo/geoip.dat";
    config.assetPaths["sing-box.geosite-cn"] = "./data/assets/sing-box/geosite-cn.srs";
    config.assetPaths["sing-box.geoip-cn"] = "./data/assets/sing-box/geoip-cn.srs";
    config.assetPaths["xray.geosite"] = "./data/assets/xray/geosite.dat";
    config.assetPaths["xray.geoip"] = "./data/assets/xray/geoip.dat";

    config.assetUrls["mihomo.geosite"] = "https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geosite.dat";
    config.assetUrls["mihomo.geoip"] = "https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geoip.dat";
    config.assetUrls["sing-box.geosite-cn"] = "https://github.com/SagerNet/sing-geosite/releases/latest/download/geosite-cn.srs";
    config.assetUrls["sing-box.geoip-cn"] = "https://github.com/SagerNet/sing-geoip/releases/latest/download/geoip-cn.srs";
    config.assetUrls["xray.geosite"] = "https://github.com/v2fly/domain-list-community/releases/latest/download/dlc.dat";
    config.assetUrls["xray.geoip"] = "https://github.com/v2fly/geoip/releases/latest/download/geoip.dat";

    config.regionRules = {
        {"HK", "(?i)(hong kong|hongkong|hk|香港)"},
        {"SG", "(?i)(singapore|sg|新加坡)"},
        {"JP", "(?i)(japan|jp|tokyo|osaka|日本)"},
        {"TW", "(?i)(taiwan|tw|台灣|台湾)"},
        {"US", "(?i)(united states|usa|us|america|美国)"},
    };
}

void fillPortablePaths(AppConfig& config) {
    config.dataDir = "./data";
    config.cacheDir = "./cache";
    config.assetDir = "./data/assets";
    config.templateDir = "./templates";
    config.profileDir = "./profiles";
    config.outputDir = "./outputs";
    config.stateDir = "./data/state";
    config.logDir = "./logs";
    config.subFile = "./data/sub.yaml";
}

void fillFhsPaths(AppConfig& config, PlatformKind platform) {
    if (platform == PlatformKind::MacOS) {
        config.dataDir = "/usr/local/var/lib/subcli";
        config.cacheDir = "/usr/local/var/cache/subcli";
        config.assetDir = "/usr/local/var/lib/subcli/assets";
        config.templateDir = "/usr/local/share/subcli/templates";
        config.profileDir = "/usr/local/share/subcli/profiles";
        config.outputDir = "/usr/local/var/lib/subcli/outputs";
        config.stateDir = "/usr/local/var/lib/subcli/state";
        config.logDir = "/usr/local/var/log/subcli";
        config.subFile = "/usr/local/var/lib/subcli/sub.yaml";
    } else {
        config.dataDir = "/var/lib/subcli";
        config.cacheDir = "/var/cache/subcli";
        config.assetDir = "/var/lib/subcli/assets";
        config.templateDir = "/usr/share/subcli/templates";
        config.profileDir = "/usr/share/subcli/profiles";
        config.outputDir = "/var/lib/subcli/outputs";
        config.stateDir = "/var/lib/subcli/state";
        config.logDir = "/var/log/subcli";
        config.subFile = "/var/lib/subcli/sub.yaml";
    }

    config.assetPaths["mihomo.geosite"] = config.assetDir + "/mihomo/geosite.dat";
    config.assetPaths["mihomo.geoip"] = config.assetDir + "/mihomo/geoip.dat";
    config.assetPaths["sing-box.geosite-cn"] = config.assetDir + "/sing-box/geosite-cn.srs";
    config.assetPaths["sing-box.geoip-cn"] = config.assetDir + "/sing-box/geoip-cn.srs";
    config.assetPaths["xray.geosite"] = config.assetDir + "/xray/geosite.dat";
    config.assetPaths["xray.geoip"] = config.assetDir + "/xray/geoip.dat";

    config.templateNormal["mihomo"] = config.templateDir + "/mihomo_base.yaml";
    config.templateTun["mihomo"] = config.templateDir + "/mihomo_tun.yaml";
    config.templateNormal["sing-box"] = config.templateDir + "/singbox_base.json";
    config.templateTun["sing-box"] = config.templateDir + "/singbox_tun.json";
    config.templateNormal["xray"] = config.templateDir + "/xray_base.json";
    config.templateTun["xray"] = config.templateDir + "/xray_tun.json";
}

} // namespace

AppConfig makeDefaultConfig(ConfigLayout layout, PlatformKind platform) {
    AppConfig config;
    fillCommonDefaults(config);
    if (layout == ConfigLayout::FHS) {
        fillFhsPaths(config, platform);
    } else {
        fillPortablePaths(config);
    }
    return config;
}

void resolveConfigPathsFromAppDir(AppConfig& config, const fs::path& appDir) {
    config.dataDir = resolvePathFromAppDir(appDir, config.dataDir).string();
    config.cacheDir = resolvePathFromAppDir(appDir, config.cacheDir).string();
    config.assetDir = resolvePathFromAppDir(appDir, config.assetDir).string();
    config.templateDir = resolvePathFromAppDir(appDir, config.templateDir).string();
    config.profileDir = resolvePathFromAppDir(appDir, config.profileDir).string();
    config.outputDir = resolvePathFromAppDir(appDir, config.outputDir).string();
    config.stateDir = resolvePathFromAppDir(appDir, config.stateDir).string();
    config.logDir = resolvePathFromAppDir(appDir, config.logDir).string();
    config.subFile = resolvePathFromAppDir(appDir, config.subFile).string();
    if (!config.profilePath.empty()) {
        config.profilePath = resolvePathFromAppDir(appDir, config.profilePath).string();
    }
    if (!config.mihomoPath.empty()) {
        config.mihomoPath = resolvePathFromAppDir(appDir, config.mihomoPath).string();
    }
    if (!config.singBoxPath.empty()) {
        config.singBoxPath = resolvePathFromAppDir(appDir, config.singBoxPath).string();
    }
    if (!config.xrayPath.empty()) {
        config.xrayPath = resolvePathFromAppDir(appDir, config.xrayPath).string();
    }
    for (auto& kv : config.assetPaths) {
        kv.second = resolvePathFromAppDir(appDir, kv.second).string();
    }
    for (auto& kv : config.templateNormal) {
        kv.second = resolvePathFromAppDir(appDir, kv.second).string();
    }
    for (auto& kv : config.templateTun) {
        kv.second = resolvePathFromAppDir(appDir, kv.second).string();
    }
}

bool ensureConfigRuntimeFiles(const AppConfig& config, std::string& error) {
    std::error_code ec;
    for (const std::string& dir : {config.dataDir, config.cacheDir, config.assetDir, config.outputDir, config.stateDir, config.logDir}) {
        if (!dir.empty()) {
            fs::create_directories(dir, ec);
            if (ec) {
                error = "failed to create directory: " + dir + ": " + ec.message();
                return false;
            }
        }
    }
    const fs::path subPath(config.subFile);
    fs::create_directories(subPath.parent_path(), ec);
    if (ec) {
        error = "failed to create subscription directory: " + subPath.parent_path().string() + ": " + ec.message();
        return false;
    }
    if (!fs::exists(subPath, ec)) {
        std::ofstream out(subPath);
        if (!out) {
            error = "failed to create subscription file: " + subPath.string();
            return false;
        }
        out << "version: 1\nsubscriptions: []\n";
    }
    return true;
}

bool writeInitialConfig(const fs::path& path, const AppConfig& config, bool force, std::string& error) {
    std::error_code ec;
    if (fs::exists(path, ec) && !force) {
        error = "config already exists: " + path.string();
        return false;
    }
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "failed to create config directory: " + path.parent_path().string() + ": " + ec.message();
        return false;
    }
    try {
        saveConfig(path.string(), config);
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

} // namespace subcli
```

### Step 5: 修改 `CMakeLists.txt`

在 `SUBCLI_SOURCES` 中加入：

```cmake
src/config_defaults.cpp
```

### Step 6: 运行测试

Run:

```bash
cmake -S . -B build && cmake --build build -j && ./build/subcli_tests
```

Expected: `testPortableDefaultConfigUsesAppRelativePaths` 和 `testResolveConfigPathsFromAppDir` PASS。

### Step 7: Commit

```bash
git add include/subcli/config_defaults.hpp src/config_defaults.cpp CMakeLists.txt tests/subcli_tests.cpp
git commit -m "feat: add app-root config defaults"
```

---

## Task 4: 添加 `subcli config init` 并停止普通命令隐式创建 config

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/config_service.cpp`
- Modify: `include/subcli/config_service.hpp`
- Modify: `tests/cli_basic_smoke.cmake`
- Modify: `tests/stability_runner.cpp`
- Modify: `tests/subcli_tests.cpp`

### Step 1: 写 CLI 失败测试

在 `tests/cli_basic_smoke.cmake` 的首次初始化流程中，将旧的 `subcli init` 调用替换为：

```cmake
set(CONFIG_PATH "${TEST_WORK_DIR}/config.yaml")
run_subcli("config init" config init --portable --path "${CONFIG_PATH}")
assert_exists("${CONFIG_PATH}" "config init should create config.yaml")
run_subcli("config list" --config "${CONFIG_PATH}" config list)
```

在该文件内新增一个缺失配置的断言：

```cmake
execute_process(
    COMMAND "${SUBCLI_BIN}" --config "${TEST_WORK_DIR}/missing/config.yaml" doctor
    RESULT_VARIABLE _missing_rc
    OUTPUT_VARIABLE _missing_out
    ERROR_VARIABLE _missing_err
)
if(_missing_rc EQUAL 0)
    message(FATAL_ERROR "doctor with missing config should fail")
endif()
string(FIND "${_missing_err}" "subcli config init" _missing_hint)
if(_missing_hint EQUAL -1)
    message(FATAL_ERROR "missing config error should mention subcli config init; stderr=${_missing_err}")
endif()
```

### Step 2: 运行测试并确认失败

Run:

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure -R subcli_cli_basic
```

Expected: FAIL，`config init` 或 `--config` 未实现。

### Step 3: 修改 root usage 和全局选项

在 `printRootUsage()` 中把全局选项改为：

```cpp
<< "  subcli [--config PATH] <command> [args...]\n"
...
<< "  --config PATH  Use this config.yaml for this invocation.\n"
```

Commands 列表加入：

```cpp
<< "  purge     Remove downloaded assets, cache, outputs, logs, state, or config.\n"
```

`printConfigUsage()` 加入：

```cpp
<< "  subcli config init [--portable|--fhs|--user] [--path PATH] [--force]\n"
```

### Step 4: main() 解析 `--config`

在 CLI11 root parser 中把旧 `--workspace` 替换为：

```cpp
std::string configOption;
cli.add_option("--config", configOption, "Use this config.yaml for this invocation");
```

保留环境变量读取：

```cpp
const std::string envConfig = []() {
    const char* raw = std::getenv("SUBCLI_CONFIG");
    return raw && *raw ? std::string(raw) : std::string();
}();
```

构造 `EnvironmentDetectInput`：

```cpp
EnvironmentDetectInput input;
input.argv0 = gExecutablePath.empty() ? argv[0] : gExecutablePath;
input.configOption = configOption;
input.envConfig = envConfig;
input.cwd = std::filesystem::current_path().string();
input.platform = detectPlatformKind();

gEnvInfo = detectEnvironment(input);
```

在普通命令分发前加入缺失配置处理。`config init` 必须允许在缺失配置时运行：

```cpp
const bool isConfigInit = cmd == "config" && !extra.empty() && extra[0] == "init";
if (!gEnvInfo.ok && !isConfigInit) {
    std::cerr << gEnvInfo.error << "\n";
    return ExitError;
}
```

### Step 5: `doConfigCommand` 增加 init 子命令

在 `doConfigCommand` parser 中新增：

```cpp
bool initPortable = false;
bool initFhs = false;
bool initUser = false;
bool initForce = false;
std::string initPath;

auto* initCmd = parser.add_subcommand("init");
initCmd->add_flag("--portable", initPortable);
initCmd->add_flag("--fhs", initFhs);
initCmd->add_flag("--user", initUser);
initCmd->add_option("--path", initPath);
initCmd->add_flag("--force", initForce);
```

命令判断加入：

```cpp
if (*initCmd) {
    cmd = "init";
}
```

在执行分支中加入：

```cpp
if (cmd == "init") {
    const int selected = (initPortable ? 1 : 0) + (initFhs ? 1 : 0) + (initUser ? 1 : 0);
    if (selected > 1) {
        std::cerr << "config init: choose only one of --portable, --fhs, or --user\n";
        return ExitUsage;
    }
    ConfigLayout layout = ConfigLayout::Portable;
    if (initFhs) {
        if (detectPlatformKind() == PlatformKind::Windows) {
            std::cerr << "config init: --fhs is not supported on Windows\n";
            return ExitUsage;
        }
        layout = ConfigLayout::FHS;
    } else if (initUser) {
        layout = ConfigLayout::UserLocal;
    }

    std::filesystem::path target;
    if (!initPath.empty()) {
        target = std::filesystem::path(initPath).is_absolute()
            ? std::filesystem::path(initPath)
            : normalizeAbsolutePath(std::filesystem::current_path() / initPath);
    } else if (layout == ConfigLayout::FHS) {
        target = platformFhsConfigPath(detectPlatformKind());
    } else if (layout == ConfigLayout::UserLocal) {
        target = platformUserConfigPath(detectPlatformKind());
    } else {
        target = gEnvInfo.appDir / "config.yaml";
    }

    AppConfig initial = makeDefaultConfig(layout, detectPlatformKind());
    std::string error;
    if (!writeInitialConfig(target, initial, initForce, error)) {
        std::cerr << "config init failed: " << error << "\n";
        return ExitError;
    }

    AppConfig resolved = initial;
    const std::filesystem::path appDir = gEnvInfo.appDir.empty() ? normalizeAbsolutePath(gExecutablePath).parent_path() : gEnvInfo.appDir;
    resolveConfigPathsFromAppDir(resolved, appDir);
    if (!ensureConfigRuntimeFiles(resolved, error)) {
        std::cerr << "config init failed while creating runtime files: " << error << "\n";
        return ExitError;
    }

    std::cout << "config initialized: " << target.lexically_normal().string() << "\n";
    std::cout << "path base: " << appDir.lexically_normal().string() << "\n";
    return ExitOk;
}
```

### Step 6: 普通命令加载配置后统一解析

将旧的 `ensureDefaults(); AppConfig cfg = loadConfig(...); applyConfigDefaults(cfg);` 组合替换为：

```cpp
AppConfig cfg = loadConfig(gEnvInfo.configPath.string());
resolveConfigPathsFromAppDir(cfg, gEnvInfo.appDir);
```

`ensureDefaults()` 不再为普通命令创建 `config.yaml`。如果仍需创建目录，调用：

```cpp
std::string ensureError;
if (!ensureConfigRuntimeFiles(cfg, ensureError)) {
    std::cerr << ensureError << "\n";
    return ExitError;
}
```

### Step 7: config set 使用 appDir resolver

把：

```cpp
setConfigValue(cfg, parsedKey, parsedValue, resolveFromConfigDir, error)
```

替换为：

```cpp
setConfigValue(cfg, parsedKey, parsedValue, [&](const std::string& value) {
    return resolvePathFromAppDir(gEnvInfo.appDir, value).string();
}, error)
```

### Step 8: 运行 CLI 测试

Run:

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure -R subcli_cli_basic
```

Expected: PASS。

### Step 9: Commit

```bash
git add src/main.cpp src/config_service.cpp include/subcli/config_service.hpp tests/cli_basic_smoke.cmake tests/stability_runner.cpp tests/subcli_tests.cpp
git commit -m "feat: initialize config explicitly"
```

---

## Task 5: 新增跨平台 purge 服务和 CLI 命令

**Files:**
- Create: `include/subcli/purge.hpp`
- Create: `src/purge.cpp`
- Modify: `src/main.cpp`
- Modify: `src/cli_completion.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/subcli_tests.cpp`

### Step 1: 写失败测试

在 `tests/subcli_tests.cpp` 添加：

```cpp
#include "subcli/purge.hpp"
```

添加测试：

```cpp
void testPurgePlanIncludesAssetsAndMetadata() {
    const fs::path root = fs::temp_directory_path() / "subcli-purge-plan-assets";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "data/assets/xray", ec);

    subcli::AppConfig config;
    config.assetDir = (root / "data/assets").string();
    config.cacheDir = (root / "cache").string();
    config.outputDir = (root / "outputs").string();
    config.stateDir = (root / "data/state").string();
    config.logDir = (root / "logs").string();
    config.subFile = (root / "data/sub.yaml").string();
    config.assetPaths["xray.geosite"] = (root / "data/assets/xray/geosite.dat").string();

    subcli::PurgeOptions options;
    options.assets = true;
    options.dryRun = true;

    const auto plan = subcli::planPurge(config, (root / "config.yaml").string(), options);
    bool hasAsset = false;
    bool hasMeta = false;
    for (const auto& path : plan.paths) {
        hasAsset = hasAsset || path == (root / "data/assets/xray/geosite.dat").lexically_normal().string();
        hasMeta = hasMeta || path == (root / "data/assets/xray/geosite.dat.meta.json").lexically_normal().string();
    }
    require(hasAsset, "asset purge plan should include configured asset file");
    require(hasMeta, "asset purge plan should include asset metadata file");
}

void testExecutePurgeAssetsDeletesOnlyAssets() {
    const fs::path root = fs::temp_directory_path() / "subcli-purge-assets-delete";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "data/assets/xray", ec);
    fs::create_directories(root / "outputs", ec);
    {
        std::ofstream out(root / "data/assets/xray/geosite.dat");
        out << "asset";
    }
    {
        std::ofstream out(root / "data/assets/xray/geosite.dat.meta.json");
        out << "{}";
    }
    {
        std::ofstream out(root / "outputs/mihomo.yaml");
        out << "output";
    }

    subcli::AppConfig config;
    config.assetDir = (root / "data/assets").string();
    config.outputDir = (root / "outputs").string();
    config.assetPaths["xray.geosite"] = (root / "data/assets/xray/geosite.dat").string();

    subcli::PurgeOptions options;
    options.assets = true;
    options.yes = true;

    const auto result = subcli::executePurge(config, (root / "config.yaml").string(), options);
    require(result.ok, "asset purge should succeed: " + result.error);
    require(!fs::exists(root / "data/assets/xray/geosite.dat"), "asset file should be removed");
    require(!fs::exists(root / "data/assets/xray/geosite.dat.meta.json"), "asset metadata should be removed");
    require(fs::exists(root / "outputs/mihomo.yaml"), "asset-only purge should not remove outputs");
}

void testPurgeRejectsDangerousRootPath() {
    subcli::AppConfig config;
    config.assetDir = "/";
    subcli::PurgeOptions options;
    options.assets = true;
    options.yes = true;

    const auto result = subcli::executePurge(config, "/tmp/subcli-config.yaml", options);
    require(!result.ok, "purge should reject filesystem root");
    require(result.error.find("refusing to remove dangerous path") != std::string::npos, "purge should explain dangerous path rejection");
}
```

注册：

```cpp
runTest("testPurgePlanIncludesAssetsAndMetadata", testPurgePlanIncludesAssetsAndMetadata);
runTest("testExecutePurgeAssetsDeletesOnlyAssets", testExecutePurgeAssetsDeletesOnlyAssets);
runTest("testPurgeRejectsDangerousRootPath", testPurgeRejectsDangerousRootPath);
```

### Step 2: 运行测试并确认失败

Run:

```bash
cmake -S . -B build && cmake --build build -j && ./build/subcli_tests
```

Expected: FAIL，`subcli/purge.hpp` 不存在。

### Step 3: 创建 `include/subcli/purge.hpp`

```cpp
#pragma once

#include <string>
#include <vector>

#include "subcli/models.hpp"

namespace subcli {

struct PurgeOptions {
    bool assets = false;
    bool cache = false;
    bool outputs = false;
    bool state = false;
    bool logs = false;
    bool config = false;
    bool all = false;
    bool dryRun = false;
    bool yes = false;
};

struct PurgePlan {
    std::vector<std::string> paths;
};

struct PurgeResult {
    bool ok = false;
    std::vector<std::string> removed;
    std::vector<std::string> skipped;
    std::string error;
};

PurgePlan planPurge(const AppConfig& config, const std::string& configPath, const PurgeOptions& options);
PurgeResult executePurge(const AppConfig& config, const std::string& configPath, const PurgeOptions& options);

} // namespace subcli
```

### Step 4: 创建 `src/purge.cpp`

实现包含：

```cpp
#include "subcli/purge.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <system_error>

namespace subcli {
namespace fs = std::filesystem;

namespace {

std::string normalized(const fs::path& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec) {
        return path.lexically_normal().string();
    }
    return absolute.lexically_normal().string();
}

bool isDangerousPath(const std::string& raw) {
    if (raw.empty()) {
        return true;
    }
    const fs::path path(raw);
    const fs::path norm = fs::path(normalized(path));
    if (norm == norm.root_path()) {
        return true;
    }
#ifdef _WIN32
    if (norm.has_root_name() && norm == norm.root_name() / norm.root_directory()) {
        return true;
    }
#endif
    return false;
}

void addPath(std::set<std::string>& paths, const std::string& path) {
    if (!path.empty()) {
        paths.insert(normalized(path));
    }
}

} // namespace

PurgePlan planPurge(const AppConfig& config, const std::string& configPath, const PurgeOptions& options) {
    std::set<std::string> paths;
    const bool all = options.all;

    if (all || options.assets) {
        addPath(paths, config.assetDir);
        for (const auto& kv : config.assetPaths) {
            addPath(paths, kv.second);
            addPath(paths, kv.second + ".meta.json");
        }
    }
    if (all || options.cache) {
        addPath(paths, config.cacheDir);
    }
    if (all || options.outputs) {
        addPath(paths, config.outputDir);
    }
    if (all || options.state) {
        addPath(paths, config.stateDir);
    }
    if (all || options.logs) {
        addPath(paths, config.logDir);
    }
    if (all || options.config) {
        addPath(paths, configPath);
    }

    PurgePlan plan;
    plan.paths.assign(paths.begin(), paths.end());
    return plan;
}

PurgeResult executePurge(const AppConfig& config, const std::string& configPath, const PurgeOptions& options) {
    PurgeResult result;
    if (!options.assets && !options.cache && !options.outputs && !options.state && !options.logs && !options.config && !options.all) {
        result.error = "purge requires at least one target: --assets, --cache, --outputs, --state, --logs, --config, or --all";
        return result;
    }
    const auto plan = planPurge(config, configPath, options);
    for (const auto& path : plan.paths) {
        if (isDangerousPath(path)) {
            result.error = "refusing to remove dangerous path: " + path;
            return result;
        }
    }
    if (options.dryRun) {
        result.ok = true;
        result.skipped = plan.paths;
        return result;
    }
    if ((options.all || options.config) && !options.yes) {
        result.error = "purge --config or --all requires --yes";
        return result;
    }

    for (const auto& path : plan.paths) {
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            result.skipped.push_back(path);
            continue;
        }
        fs::remove_all(path, ec);
        if (ec) {
            result.error = "failed to remove " + path + ": " + ec.message();
            return result;
        }
        result.removed.push_back(path);
    }
    result.ok = true;
    return result;
}

} // namespace subcli
```

### Step 5: 修改 `CMakeLists.txt`

加入源文件：

```cmake
src/purge.cpp
```

### Step 6: 修改 `src/main.cpp` 增加 purge CLI

新增 usage：

```cpp
void printPurgeUsage() {
    std::cout << "Usage:\n"
              << "  subcli purge (--assets|--cache|--outputs|--state|--logs|--config|--all) [--dry-run] [--yes]\n"
              << "\n"
              << "Examples:\n"
              << "  subcli purge --assets --dry-run\n"
              << "  subcli purge --assets --yes\n"
              << "  subcli purge --all --yes\n";
}
```

新增命令实现：

```cpp
int doPurgeCommand(const std::vector<std::string>& args) {
    if (hasHelp(args)) {
        printPurgeUsage();
        return ExitOk;
    }
    AppConfig cfg = loadConfig(gEnvInfo.configPath.string());
    resolveConfigPathsFromAppDir(cfg, gEnvInfo.appDir);

    CLI::App parser("purge");
    parser.set_help_flag("");
    parser.allow_extras(false);

    PurgeOptions options;
    parser.add_flag("--assets", options.assets);
    parser.add_flag("--cache", options.cache);
    parser.add_flag("--outputs", options.outputs);
    parser.add_flag("--state", options.state);
    parser.add_flag("--logs", options.logs);
    parser.add_flag("--config", options.config);
    parser.add_flag("--all", options.all);
    parser.add_flag("--dry-run", options.dryRun);
    parser.add_flag("--yes", options.yes);

    if (!parseCliArgs(parser, args)) {
        printPurgeUsage();
        return ExitUsage;
    }

    const auto result = executePurge(cfg, gEnvInfo.configPath.string(), options);
    if (!result.ok) {
        std::cerr << "purge failed: " << result.error << "\n";
        return ExitError;
    }
    if (options.dryRun) {
        std::cout << "purge dry-run paths:\n";
        for (const auto& path : result.skipped) {
            std::cout << "  " << path << "\n";
        }
        return ExitOk;
    }
    for (const auto& path : result.removed) {
        std::cout << "removed: " << path << "\n";
    }
    for (const auto& path : result.skipped) {
        std::cout << "missing: " << path << "\n";
    }
    return ExitOk;
}
```

分发区加入：

```cpp
if (cmd == "purge") {
    return doPurgeCommand(buildTail("purge", extra));
}
```

### Step 7: 修改 completion

根命令补全加入 `purge`，purge 选项加入：

```cpp
--assets --cache --outputs --state --logs --config --all --dry-run --yes
```

### Step 8: 运行测试

Run:

```bash
cmake -S . -B build && cmake --build build -j && ./build/subcli_tests
ctest --test-dir build --output-on-failure -R subcli_cli_basic
```

Expected: PASS。

### Step 9: Commit

```bash
git add include/subcli/purge.hpp src/purge.cpp src/main.cpp src/cli_completion.cpp CMakeLists.txt tests/subcli_tests.cpp
git commit -m "feat: add purge command"
```

---

## Task 6: 打包脚本清理 geosite/geoip 等下载资源

**Files:**
- Create: `packaging/deb/postinst`
- Create: `packaging/deb/prerm`
- Create: `packaging/deb/postrm`
- Create: `packaging/rpm/postinstall.sh`
- Create: `packaging/rpm/preremove.sh`
- Create: `packaging/rpm/postuninstall.sh`
- Create: `packaging/nsis/uninstall_extra.nsh`
- Modify: `CMakeLists.txt`
- Modify: `tests/stability_package_journey.cmake`

### Step 1: 创建 DEB 脚本

`packaging/deb/postinst`:

```sh
#!/bin/sh
set -e
case "$1" in
    configure)
        install -d -m 755 /var/lib/subcli/assets
        install -d -m 755 /var/lib/subcli/outputs
        install -d -m 755 /var/lib/subcli/state
        install -d -m 755 /var/cache/subcli
        install -d -m 755 /var/log/subcli
        if [ ! -f /var/lib/subcli/sub.yaml ]; then
            printf 'version: 1\nsubscriptions: []\n' > /var/lib/subcli/sub.yaml
        fi
        if [ -d /run/systemd/system ]; then
            systemctl daemon-reload >/dev/null 2>&1 || true
        fi
        ;;
esac
exit 0
```

`packaging/deb/prerm`:

```sh
#!/bin/sh
set -e
case "$1" in
    remove|upgrade|deconfigure)
        if [ -d /run/systemd/system ]; then
            systemctl stop subcli-daemon.service >/dev/null 2>&1 || true
            systemctl disable subcli-daemon.service >/dev/null 2>&1 || true
        fi
        ;;
esac
exit 0
```

`packaging/deb/postrm`:

```sh
#!/bin/sh
set -e
case "$1" in
    purge)
        rm -rf /var/lib/subcli
        rm -rf /var/cache/subcli
        rm -rf /var/log/subcli
        rm -rf /etc/subcli
        if [ -d /run/systemd/system ]; then
            systemctl daemon-reload >/dev/null 2>&1 || true
        fi
        ;;
    remove)
        ;;
esac
exit 0
```

说明：Debian policy 区分 `remove` 和 `purge`。普通 `apt remove subcli` 保留配置和数据；`apt purge subcli` 删除下载资源、缓存、日志和配置目录。

### Step 2: 创建 RPM 脚本

`packaging/rpm/postinstall.sh`:

```sh
#!/bin/sh
set -e
install -d -m 755 /var/lib/subcli/assets
install -d -m 755 /var/lib/subcli/outputs
install -d -m 755 /var/lib/subcli/state
install -d -m 755 /var/cache/subcli
install -d -m 755 /var/log/subcli
if [ ! -f /var/lib/subcli/sub.yaml ]; then
    printf 'version: 1\nsubscriptions: []\n' > /var/lib/subcli/sub.yaml
fi
if command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload >/dev/null 2>&1 || true
fi
exit 0
```

`packaging/rpm/preremove.sh`:

```sh
#!/bin/sh
set -e
if [ "$1" = "0" ]; then
    if command -v systemctl >/dev/null 2>&1; then
        systemctl stop subcli-daemon.service >/dev/null 2>&1 || true
        systemctl disable subcli-daemon.service >/dev/null 2>&1 || true
    fi
fi
exit 0
```

`packaging/rpm/postuninstall.sh`:

```sh
#!/bin/sh
set -e
if [ "$1" = "0" ]; then
    rm -rf /var/lib/subcli
    rm -rf /var/cache/subcli
    rm -rf /var/log/subcli
    if command -v systemctl >/dev/null 2>&1; then
        systemctl daemon-reload >/dev/null 2>&1 || true
    fi
fi
exit 0
```

说明：RPM `$1 = 0` 表示最终卸载，不是升级。`/etc/subcli/config.yaml` 用 `%config(noreplace)` 管理，修改过的配置可能被 rpm 保存为 `.rpmsave`。

### Step 3: 创建 NSIS 卸载片段

`packaging/nsis/uninstall_extra.nsh`:

```nsis
!macro SUBCLI_UNINSTALL_CLEANUP
  RMDir /r "$INSTDIR\data\assets"
  RMDir /r "$INSTDIR\data\state"
  RMDir /r "$INSTDIR\cache"
  RMDir /r "$INSTDIR\outputs"
  RMDir /r "$INSTDIR\logs"
  Delete "$INSTDIR\data\sub.yaml"
  Delete "$INSTDIR\config.yaml"
  RMDir "$INSTDIR\data"
  RMDir "$INSTDIR"
!macroend
```

### Step 4: 修改 `CMakeLists.txt` 接入脚本

加入：

```cmake
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/packaging/deb/postinst"
    "${CMAKE_SOURCE_DIR}/packaging/deb/prerm"
    "${CMAKE_SOURCE_DIR}/packaging/deb/postrm"
)
set(CPACK_DEBIAN_PACKAGE_CONFFILES "/etc/subcli/config.yaml")

set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/rpm/postinstall.sh")
set(CPACK_RPM_PRE_UNINSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/rpm/preremove.sh")
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/rpm/postuninstall.sh")
set(CPACK_RPM_USER_FILELIST "%config(noreplace) /etc/subcli/config.yaml")

if(WIN32)
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "!insertmacro SUBCLI_UNINSTALL_CLEANUP")
    set(CPACK_NSIS_SCRIPT_TEMPLATE "${CMAKE_SOURCE_DIR}/packaging/nsis/uninstall_extra.nsh")
endif()
```

如果 `CPACK_NSIS_SCRIPT_TEMPLATE` 与项目生成器冲突，使用 CPack 支持的 `CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS` 直接内联：

```cmake
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
    "RMDir /r \"$INSTDIR\\data\\assets\"\nRMDir /r \"$INSTDIR\\data\\state\"\nRMDir /r \"$INSTDIR\\cache\"\nRMDir /r \"$INSTDIR\\outputs\"\nRMDir /r \"$INSTDIR\\logs\"\nDelete \"$INSTDIR\\config.yaml\""
)
```

### Step 5: 添加脚本可执行权限

Run:

```bash
chmod +x packaging/deb/postinst packaging/deb/prerm packaging/deb/postrm packaging/rpm/postinstall.sh packaging/rpm/preremove.sh packaging/rpm/postuninstall.sh
```

### Step 6: 测试脚本存在性

在 `tests/stability_package_journey.cmake` 增加文件存在检查：

```cmake
foreach(_script
    "${SOURCE_DIR}/packaging/deb/postinst"
    "${SOURCE_DIR}/packaging/deb/prerm"
    "${SOURCE_DIR}/packaging/deb/postrm"
    "${SOURCE_DIR}/packaging/rpm/postinstall.sh"
    "${SOURCE_DIR}/packaging/rpm/preremove.sh"
    "${SOURCE_DIR}/packaging/rpm/postuninstall.sh")
    if(NOT EXISTS "${_script}")
        message(FATAL_ERROR "missing package lifecycle script: ${_script}")
    endif()
endforeach()
```

### Step 7: 运行测试

Run:

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure -R subcli_stability_package_journey
```

Expected: PASS，或者在当前平台没有 DEB/RPM/NSIS 生成器时跳过对应包生成但保留脚本存在性检查。

### Step 8: Commit

```bash
git add packaging/deb packaging/rpm packaging/nsis CMakeLists.txt tests/stability_package_journey.cmake
git commit -m "packaging: clean runtime resources on uninstall"
```

---

## Task 7: 三平台便携式和安装式打包布局

**Files:**
- Create: `packaging/fhs/config.yaml`
- Create: `packaging/portable/config.yaml`
- Create: `packaging/homebrew/subcli.rb`
- Create: `packaging/scoop/subcli.json`
- Modify: `CMakeLists.txt`
- Modify: `tests/stability_package_journey.cmake`

### Step 1: 创建 portable 默认配置

`packaging/portable/config.yaml`:

```yaml
version: 1

# subcli portable configuration
# All relative paths are resolved from the application directory, not from this file location.

data_dir: ./data
cache_dir: ./cache
asset_dir: ./data/assets
template_dir: ./templates
profile_dir: ./profiles
output_dir: ./outputs
state_dir: ./data/state
log_dir: ./logs
sub_file: ./data/sub.yaml

profile: bypass-cn
profile_path: ""
tun: false
log_level: info

parallelism: 4
timeout: 15
retry: 2
fetch_max_bytes: 10485760

core_paths:
  mihomo: ""
  sing_box: ""
  xray: ""

templates:
  mihomo:
    normal: ./templates/mihomo_base.yaml
    tun: ./templates/mihomo_tun.yaml
  sing-box:
    normal: ./templates/singbox_base.json
    tun: ./templates/singbox_tun.json
  xray:
    normal: ./templates/xray_base.json
    tun: ./templates/xray_tun.json

assets:
  paths:
    mihomo.geosite: ./data/assets/mihomo/geosite.dat
    mihomo.geoip: ./data/assets/mihomo/geoip.dat
    sing-box.geosite-cn: ./data/assets/sing-box/geosite-cn.srs
    sing-box.geoip-cn: ./data/assets/sing-box/geoip-cn.srs
    xray.geosite: ./data/assets/xray/geosite.dat
    xray.geoip: ./data/assets/xray/geoip.dat
  urls:
    mihomo.geosite: https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geosite.dat
    mihomo.geoip: https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geoip.dat
    sing-box.geosite-cn: https://github.com/SagerNet/sing-geosite/releases/latest/download/geosite-cn.srs
    sing-box.geoip-cn: https://github.com/SagerNet/sing-geoip/releases/latest/download/geoip-cn.srs
    xray.geosite: https://github.com/v2fly/domain-list-community/releases/latest/download/dlc.dat
    xray.geoip: https://github.com/v2fly/geoip/releases/latest/download/geoip.dat

node_management:
  dedupe: true
  rename_template: "{name}"
  include_regex: ""
  exclude_regex: ""
  sort_by: region,name

grouping:
  region_rules:
    HK: "(?i)(hong kong|hongkong|hk|香港)"
    SG: "(?i)(singapore|sg|新加坡)"
    JP: "(?i)(japan|jp|tokyo|osaka|日本)"
    TW: "(?i)(taiwan|tw|台灣|台湾)"
    US: "(?i)(united states|usa|us|america|美国)"
```

### Step 2: 创建 FHS 默认配置

`packaging/fhs/config.yaml`:

```yaml
version: 1

# subcli FHS configuration
# Absolute paths are used for installed packages. Relative paths, if added by users, are resolved from the application directory.

data_dir: /var/lib/subcli
cache_dir: /var/cache/subcli
asset_dir: /var/lib/subcli/assets
template_dir: /usr/share/subcli/templates
profile_dir: /usr/share/subcli/profiles
output_dir: /var/lib/subcli/outputs
state_dir: /var/lib/subcli/state
log_dir: /var/log/subcli
sub_file: /var/lib/subcli/sub.yaml

profile: bypass-cn
profile_path: ""
tun: false
log_level: info

parallelism: 4
timeout: 15
retry: 2
fetch_max_bytes: 10485760

core_paths:
  mihomo: ""
  sing_box: ""
  xray: ""

templates:
  mihomo:
    normal: /usr/share/subcli/templates/mihomo_base.yaml
    tun: /usr/share/subcli/templates/mihomo_tun.yaml
  sing-box:
    normal: /usr/share/subcli/templates/singbox_base.json
    tun: /usr/share/subcli/templates/singbox_tun.json
  xray:
    normal: /usr/share/subcli/templates/xray_base.json
    tun: /usr/share/subcli/templates/xray_tun.json

assets:
  paths:
    mihomo.geosite: /var/lib/subcli/assets/mihomo/geosite.dat
    mihomo.geoip: /var/lib/subcli/assets/mihomo/geoip.dat
    sing-box.geosite-cn: /var/lib/subcli/assets/sing-box/geosite-cn.srs
    sing-box.geoip-cn: /var/lib/subcli/assets/sing-box/geoip-cn.srs
    xray.geosite: /var/lib/subcli/assets/xray/geosite.dat
    xray.geoip: /var/lib/subcli/assets/xray/geoip.dat
  urls:
    mihomo.geosite: https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geosite.dat
    mihomo.geoip: https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geoip.dat
    sing-box.geosite-cn: https://github.com/SagerNet/sing-geosite/releases/latest/download/geosite-cn.srs
    sing-box.geoip-cn: https://github.com/SagerNet/sing-geoip/releases/latest/download/geoip-cn.srs
    xray.geosite: https://github.com/v2fly/domain-list-community/releases/latest/download/dlc.dat
    xray.geoip: https://github.com/v2fly/geoip/releases/latest/download/geoip.dat

node_management:
  dedupe: true
  rename_template: "{name}"
  include_regex: ""
  exclude_regex: ""
  sort_by: region,name

grouping:
  region_rules:
    HK: "(?i)(hong kong|hongkong|hk|香港)"
    SG: "(?i)(singapore|sg|新加坡)"
    JP: "(?i)(japan|jp|tokyo|osaka|日本)"
    TW: "(?i)(taiwan|tw|台灣|台湾)"
    US: "(?i)(united states|usa|us|america|美国)"
```

### Step 3: 修改 CMake 安装布局

在 `CMakeLists.txt` 中加入：

```cmake
option(SUBCLI_PORTABLE "Install a portable layout rooted at the package directory" OFF)

if(SUBCLI_PORTABLE)
    install(TARGETS subcli RUNTIME DESTINATION .)
    install(DIRECTORY templates/ DESTINATION templates)
    install(DIRECTORY profiles/ DESTINATION profiles)
    install(FILES packaging/portable/config.yaml DESTINATION .)
    install(FILES README.subcli.md DESTINATION .)
else()
    install(TARGETS subcli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY templates/ DESTINATION ${CMAKE_INSTALL_DATADIR}/subcli/templates)
    install(DIRECTORY profiles/ DESTINATION ${CMAKE_INSTALL_DATADIR}/subcli/profiles)
    install(FILES packaging/fhs/config.yaml DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/subcli)
    install(FILES packaging/systemd/subcli-daemon.service DESTINATION ${CMAKE_INSTALL_DATADIR}/subcli/systemd)
    install(FILES README.subcli.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

if(SUBCLI_PORTABLE)
    set(CPACK_GENERATOR "TGZ;ZIP")
elseif(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
elseif(APPLE)
    set(CPACK_GENERATOR "TGZ;ZIP")
else()
    set(CPACK_GENERATOR "DEB;RPM;TGZ;ZIP")
endif()
```

### Step 4: 创建 Homebrew Formula 示例

`packaging/homebrew/subcli.rb`:

```ruby
class Subcli < Formula
  desc "Subscription to proxy client config tool"
  homepage "https://github.com/subcli/subcli"
  url "https://github.com/subcli/subcli/archive/refs/tags/v0.3.0.tar.gz"
  sha256 "replace-with-release-sha256"
  license "MIT"

  depends_on "cmake" => :build

  def install
    system "cmake", "-S", ".", "-B", "build",
           "-DCMAKE_INSTALL_PREFIX=#{prefix}",
           "-DCMAKE_INSTALL_SYSCONFDIR=#{etc}",
           "-DSUBCLI_PORTABLE=OFF",
           *std_cmake_args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  def caveats
    <<~EOS
      Initialize a user config if #{etc}/subcli/config.yaml is not used:
        subcli config init --user

      Remove downloaded assets and runtime data before uninstalling if desired:
        subcli purge --all --yes
    EOS
  end

  test do
    system "#{bin}/subcli", "--help"
  end
end
```

发布时必须将 `replace-with-release-sha256` 替换为实际 release tarball 的 SHA256。

### Step 5: 创建 Scoop manifest 示例

`packaging/scoop/subcli.json`:

```json
{
  "version": "0.3.0",
  "description": "Subscription to proxy client config tool",
  "homepage": "https://github.com/subcli/subcli",
  "license": "MIT",
  "architecture": {
    "64bit": {
      "url": "https://github.com/subcli/subcli/releases/download/v0.3.0/subcli-0.3.0-Windows-x86_64.zip",
      "hash": "replace-with-release-sha256"
    }
  },
  "bin": "subcli.exe",
  "persist": [
    "config.yaml",
    "data",
    "cache",
    "outputs",
    "logs"
  ],
  "checkver": "github",
  "autoupdate": {
    "architecture": {
      "64bit": {
        "url": "https://github.com/subcli/subcli/releases/download/v$version/subcli-$version-Windows-x86_64.zip"
      }
    }
  }
}
```

发布时必须将 `replace-with-release-sha256` 替换为实际 ZIP 的 SHA256。

### Step 6: 包布局测试

在 `tests/stability_package_journey.cmake` 中 portable archive 解压后增加：

```cmake
find_file(_portable_config NAMES config.yaml PATHS "${_extract_dir}" PATH_SUFFIXES "" NO_DEFAULT_PATH)
if(NOT _portable_config)
    message(FATAL_ERROR "portable package should contain config.yaml next to executable")
endif()

find_path(_portable_templates NAMES mihomo_base.yaml PATHS "${_extract_dir}" PATH_SUFFIXES templates NO_DEFAULT_PATH)
if(NOT _portable_templates)
    message(FATAL_ERROR "portable package should contain templates directory")
endif()
```

### Step 7: 运行包测试

Run:

```bash
cmake -S . -B build -DSUBCLI_PORTABLE=ON && cmake --build build -j && cpack --config build/CPackConfig.cmake
ctest --test-dir build --output-on-failure -R subcli_stability_package_journey
```

Expected: portable TGZ/ZIP 包含 `config.yaml`、`templates/`、`profiles/`，解压后 `subcli --help` 可运行。

### Step 8: Commit

```bash
git add packaging/fhs/config.yaml packaging/portable/config.yaml packaging/homebrew/subcli.rb packaging/scoop/subcli.json CMakeLists.txt tests/stability_package_journey.cmake
git commit -m "packaging: add three-platform layouts"
```

---

## Task 8: 文档更新和术语同步

**Files:**
- Modify: `README.md`
- Modify: `README.subcli.md`
- Modify: `docs/config-file.md`
- Modify: `docs/cli-glossary.zh-CN.md`
- Modify: `docs/superpowers/specs/2026-05-30-config-driven-architecture-design.md`

### Step 1: 更新 `docs/config-file.md` 路径规则

将 `Path Resolution Rules` 改为：

```markdown
## Path Resolution Rules

- `config.yaml` is created explicitly with `subcli config init`; normal commands do not create it implicitly.
- Persisted relative paths in `config.yaml` resolve relative to the application directory, meaning the directory that contains the running `subcli` executable.
- CLI path arguments (for example `--output-dir`, `--file`, `--profile /path/...`) resolve relative to current shell working directory.
- Absolute paths stay absolute in both cases.

Practical effect: in a portable package, `asset_dir: ./data/assets` resolves to `<app-dir>/data/assets` even if `config.yaml` was passed with `--config` from another directory.
```

### Step 2: 更新首次使用流程

在 `README.md` 和 `README.subcli.md` 将：

```bash
subcli init
```

替换为：

```bash
subcli config init --portable
```

并增加显式配置示例：

```bash
subcli --config /path/to/config.yaml doctor
subcli --config /path/to/config.yaml asset update
```

### Step 3: 增加 purge 文档

在 README 的命令列表中加入：

```markdown
### Cleanup

Downloaded geo/rule assets are stored under `asset_dir`. They are not part of the read-only templates/profiles shipped with subcli.

Use:

```bash
subcli purge --assets --dry-run
subcli purge --assets --yes
subcli purge --all --yes
```

Package behavior:

- DEB: `apt remove subcli` keeps data; `apt purge subcli` removes `/var/lib/subcli`, `/var/cache/subcli`, `/var/log/subcli`, and `/etc/subcli`.
- RPM: final erase removes runtime data directories; modified config files may be saved by rpm config-file policy.
- Windows NSIS: uninstaller removes app-local data/cache/logs/outputs/config.
- Portable ZIP/TGZ: run `subcli purge --all --yes` or delete the extracted directory.
```
```

### Step 4: 更新中文术语表

在 `docs/cli-glossary.zh-CN.md` 中加入：

```markdown
| `config init` | 初始化配置 | 显式创建 `config.yaml`。普通命令不会隐式创建配置。 |
| app directory / appDir | 应用程序目录 | 正在运行的 `subcli` 主程序所在目录。`config.yaml` 中的相对路径均相对于该目录解析。 |
| `purge` | 清理/彻底清理 | 删除下载资源、缓存、输出、日志、运行状态或配置。 |
```

### Step 5: 更新设计规格

在 `docs/superpowers/specs/2026-05-30-config-driven-architecture-design.md` 中替换所有：

```markdown
所有相对路径相对于 config.yaml 所在目录解析
```

为：

```markdown
所有相对路径相对于 app 主程序所在目录解析；config.yaml 由 `subcli config init` 显式创建，普通命令不隐式创建配置。
```

并在卸载章节补充：

```markdown
下载资源（geosite/geoip/rule-set）属于运行时数据。三平台统一支持 `subcli purge --assets` 和 `subcli purge --all --yes`。包管理器卸载脚本在支持生命周期脚本的平台执行等价清理；Debian 系遵循 remove/purge 语义，仅 purge 删除数据。
```

### Step 6: 文档测试

Run:

```bash
cmake -S . -B build && cmake --build build -j && ./build/subcli_tests
```

Expected: 文档相关测试 PASS，包括 README 目标描述、中文术语表存在、首次使用流程中包含 `subcli config init --portable`。

### Step 7: Commit

```bash
git add README.md README.subcli.md docs/config-file.md docs/cli-glossary.zh-CN.md docs/superpowers/specs/2026-05-30-config-driven-architecture-design.md
git commit -m "docs: describe app-root config and purge"
```

---

## Task 9: 最终验证

**Files:**
- No source changes unless verification exposes a defect.

### Step 1: 全量构建

Run:

```bash
cmake -S . -B build && cmake --build build -j
```

Expected: build succeeds.

### Step 2: 全量测试

Run:

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

### Step 3: CLI 手工冒烟测试

Run:

```bash
rm -rf build/manual-appdir-smoke
mkdir -p build/manual-appdir-smoke
cp build/subcli build/manual-appdir-smoke/subcli
cp -R templates profiles build/manual-appdir-smoke/
build/manual-appdir-smoke/subcli config init --portable --path build/manual-appdir-smoke/config.yaml
build/manual-appdir-smoke/subcli --config build/manual-appdir-smoke/config.yaml doctor
build/manual-appdir-smoke/subcli --config build/manual-appdir-smoke/config.yaml asset status
build/manual-appdir-smoke/subcli --config build/manual-appdir-smoke/config.yaml purge --assets --dry-run
```

Expected:

- `config init` prints `config initialized:`。
- `doctor` exits 0。
- `asset status` lists configured assets under `build/manual-appdir-smoke/data/assets`。
- `purge --assets --dry-run` lists asset and `.meta.json` paths without deleting files。

### Step 4: Portable package 验证

Run:

```bash
cmake -S . -B build-portable -DSUBCLI_PORTABLE=ON
cmake --build build-portable -j
cpack --config build-portable/CPackConfig.cmake
```

Expected: generated TGZ/ZIP package contains:

```text
subcli or subcli.exe
config.yaml
templates/
profiles/
README.subcli.md
```

### Step 5: Installed package 配置验证

Run on Linux builders with DEB/RPM generators installed:

```bash
cmake -S . -B build-install -DSUBCLI_PORTABLE=OFF
cmake --build build-install -j
cpack --config build-install/CPackConfig.cmake
```

Expected:

- DEB package contains `/etc/subcli/config.yaml` as conffile。
- RPM package contains `/etc/subcli/config.yaml` as `%config(noreplace)`。
- Package scripts are included in generated metadata。

### Step 6: Commit verification-only updates

If no source changes were needed, do not create a commit. If verification required a fix, commit the exact modified files:

```bash
git add <fixed-files>
git commit -m "test: stabilize app-root config packaging"
```

---

## Self-review Checklist

### Spec coverage

- Requirement 1 is covered by Task 1 through Task 4: appDir is explicit in environment, config paths resolve through `resolvePathFromAppDir`, and config is created only by `config init`.
- Requirement 2 is answered in Task 5 and Task 6: current resources are not deleted by existing code; new purge and package scripts define deletion behavior.
- Requirement 3 is covered by Task 5 and Task 6: `purge` is cross-platform and package uninstall paths are synchronized where supported.
- Requirement 4 is covered by Task 7: portable packages cover Linux/macOS/Windows; installed packages cover Linux DEB/RPM, macOS Homebrew, Windows NSIS/Scoop.

### Placeholder scan

- No implementation step relies on an unnamed function.
- New interfaces are defined before later tasks use them.
- Commands include exact expected behavior and exact verification commands.

### Type consistency

- `ConfigMode`, `EnvironmentInfo`, `EnvironmentDetectInput`, `resolvePathFromAppDir`, `ConfigLayout`, `makeDefaultConfig`, `resolveConfigPathsFromAppDir`, `PurgeOptions`, `PurgePlan`, and `PurgeResult` names are consistent across tasks.
- `AppConfig` path field names are consistent with YAML keys and C++ tests.
