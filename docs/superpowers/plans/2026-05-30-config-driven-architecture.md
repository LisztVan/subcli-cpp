# Config-Driven Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove workspace mode and make subcli run from one config-driven environment where `config.yaml` declares paths, assets, profiles, templates, outputs, state, and package-manager behavior.

**Architecture:** Replace workspace resolution with config detection (`--config`/`SUBCLI_CONFIG`/portable config/FHS config/user-local fallback), then resolve all relative paths from the config file directory. Keep package data read-only under `/usr/share/subcli` for FHS packages and use relative app-root paths for TGZ/ZIP portable packages. Use CMake/CPack for DEB/RPM/TGZ/ZIP outputs, marking `/etc/subcli/config.yaml` as package-managed config and adding purge scripts for config/assets/data cleanup.

**Tech Stack:** C++17, CMake/CPack, CTest, CLI11, yaml-cpp, nlohmann_json, libcurl, Debian/RPM package scriptlets, existing C++ test harness in `tests/subcli_tests.cpp`.

---

## Important Execution Notes

- Start this work in a clean isolated worktree. Do not reuse a dirty checkout containing manually-built `.deb` files or experimental `debian/` files.
- Use the project-required build directory name: `build`.
- The first configure can require network because CMake FetchContent downloads dependencies.
- Keep commits small. Each task below ends with a commit.
- Do not keep any `workspace` user-facing feature, command, option, environment variable, or docs promise.
- Internal variable names may still use `root` for an app/package root, but not for a workspace concept.

---

## File Structure

### New files

- Create `include/subcli/config_defaults.hpp`
  - Owns default FHS config values, default portable config values, default asset URLs, and path resolution from `config.yaml`.
- Create `src/config_defaults.cpp`
  - Implements default config builders and `resolveConfigPathsInPlace()`.
- Create `packaging/fhs/config.yaml`
  - Default FHS config installed to `/etc/subcli/config.yaml` and marked as conffile.
- Create `packaging/portable/config.yaml`
  - Default portable config placed next to the executable in TGZ/ZIP packages.
- Create `packaging/deb/postinst`
  - Creates `/var/lib/subcli`, `/var/cache/subcli`, `/var/log/subcli`, and an empty `/var/lib/subcli/sub.yaml` after install.
- Create `packaging/deb/prerm`
  - Stops/disables the systemd unit before remove/upgrade.
- Create `packaging/deb/postrm`
  - On `purge`, removes `/etc/subcli`, `/var/lib/subcli`, `/var/cache/subcli`, and `/var/log/subcli`.
- Create `packaging/rpm/postinstall.sh`
  - RPM equivalent of DEB postinst.
- Create `packaging/rpm/preremove.sh`
  - RPM equivalent of DEB prerm.
- Create `packaging/rpm/postuninstall.sh`
  - RPM equivalent of DEB postrm purge behavior for final erase.

### Removed files

- Delete `include/subcli/workspace.hpp`
- Delete `src/workspace.cpp`

### Modified production files

- Modify `include/subcli/environment.hpp`
  - Replace workspace structs with `ConfigMode`, `EnvironmentInfo`, and `EnvironmentPaths` based on config.
- Modify `src/environment.cpp`
  - Replace workspace resolution with config detection and path helpers.
- Modify `include/subcli/models.hpp`
  - Add config-driven path fields: `dataDir`, `cacheDir`, `profileDir`, `stateDir`, `logDir`, `subFile`.
  - Replace asset path/url split maps with `assets` map of `{path,url}` entries while preserving helper accessors during migration.
- Modify `include/subcli/store.hpp`
  - Add `loadConfigFromFile()` and `saveConfigToFile()` only if the implementation needs a clearer name; keep existing `loadConfig`/`saveConfig` wrappers for test stability.
- Modify `src/store.cpp`
  - Read/write the new config schema.
  - Resolve old `assets.paths`/`assets.urls` only as a compatibility reader, then save in the new schema.
- Modify `include/subcli/config_service.hpp`
  - Add `defaultDataDir`, `defaultCacheDir`, `defaultProfileDir`, `defaultStateDir`, `defaultLogDir`, and `defaultSubFile` to `ConfigServiceOptions`.
- Modify `src/config_service.cpp`
  - Add config keys for new paths.
  - Update asset keys to `assets.<asset-key>.path` and `assets.<asset-key>.url`.
- Modify `include/subcli/diagnostic_service.hpp`
  - Replace `workspaceRoot` parameter with `EnvironmentPaths` or `configPath`.
- Modify `src/diagnostic_service.cpp`
  - Report `config.resolved`, `data_dir`, `asset_dir`, `template_dir`, `profile_dir`, `output_dir`, `state_dir`, `log_dir`, and `sub_file` instead of `workspace.resolved`.
- Modify `src/registry.cpp`
  - Remove `init` and `workspace` commands.
  - Add `--config` to relevant command metadata if metadata includes global options.
  - Add new config keys.
- Modify `src/main.cpp`
  - Remove workspace command and init command.
  - Add `--config PATH` global option.
  - Use `detectEnvironment()` and `loadConfig()` to build global runtime paths.
  - Add `subcli config init [--portable] [--fhs] [--path PATH]`.
- Modify `src/assets.cpp`, `src/capability_matrix.cpp`, `src/exporter_singbox.cpp`, and any other file using `assetPaths`/`assetUrls`.
- Modify `CMakeLists.txt`
  - Remove workspace source file.
  - Add `src/config_defaults.cpp`.
  - Add FHS/portable install layouts.
  - Add CPack DEB/RPM/TGZ/ZIP configuration.

### Modified tests and docs

- Modify `tests/subcli_tests.cpp`
  - Remove workspace tests and include.
  - Add environment detection, config default, new config schema, config service, registry, and packaging docs tests.
- Modify `tests/cli_basic_smoke.cmake`
  - Replace `workspace init` flow with config-driven flow using `SUBCLI_CONFIG`.
- Modify `tests/stability_runner.cpp`
  - Replace `init`/`workspace` flow with portable config flow.
- Modify `tests/stability_package_journey.cmake`
  - Validate package extraction for both portable archives and DEB/RPM when available.
- Modify `tests/platform_boundary_scan.cmake`
  - Remove deleted workspace files from any allowed/expected references.
- Modify `README.md`, `README.subcli.md`, `docs/config-file.md`, `docs/cli-glossary.zh-CN.md`, `docs/en_us/*`, and `docs/zh_cn/*`.
  - Remove workspace instructions.
  - Document FHS and portable layouts.
  - Document purge behavior and asset lifecycle.

---

## Task 0: Prepare a Clean Implementation Worktree

**Files:**
- No code files changed.

- [ ] **Step 1: Check the current git state**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp
git status --short
```

Expected: You may see local files from earlier packaging experiments. Do not start implementation in this dirty tree.

- [ ] **Step 2: Create a clean worktree for this implementation**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp
git fetch origin
git worktree add .worktrees/config-driven-architecture -b feat/config-driven-architecture master
```

Expected: command creates `.worktrees/config-driven-architecture`.

- [ ] **Step 3: Enter the clean worktree**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
git status --short
```

Expected output is empty.

- [ ] **Step 4: Build baseline**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: current baseline tests pass before refactoring.

---

## Task 1: Add Package Default Config Files and Maintainer Scripts

**Files:**
- Create: `packaging/fhs/config.yaml`
- Create: `packaging/portable/config.yaml`
- Create: `packaging/deb/postinst`
- Create: `packaging/deb/prerm`
- Create: `packaging/deb/postrm`
- Create: `packaging/rpm/postinstall.sh`
- Create: `packaging/rpm/preremove.sh`
- Create: `packaging/rpm/postuninstall.sh`

- [ ] **Step 1: Create package directories**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
mkdir -p packaging/fhs packaging/portable packaging/deb packaging/rpm
```

Expected: directories exist.

- [ ] **Step 2: Create the FHS config file**

Write this exact file to `packaging/fhs/config.yaml`:

```yaml
# subcli system configuration
# This file is installed as /etc/subcli/config.yaml in FHS packages.
# It is marked as a package-manager config file by DEB/RPM packaging.
# Assets are not package config files; they are runtime data under asset_dir.

version: 2

# Paths
# All relative paths are resolved relative to this config file.
data_dir: /var/lib/subcli
cache_dir: /var/cache/subcli
asset_dir: /var/lib/subcli/assets
template_dir: /usr/share/subcli/templates
profile_dir: /usr/share/subcli/profiles
output_dir: /var/lib/subcli/outputs
state_dir: /var/lib/subcli/state
log_dir: /var/log/subcli
sub_file: /var/lib/subcli/sub.yaml

# Export behavior
profile: bypass-cn
profile_path: ""
tun: false
log_level: info

# Network
parallelism: 3
timeout: 15
retry: 2
fetch_max_bytes: 5242880

# External proxy cores. Empty means: search PATH.
core_paths:
  mihomo: ""
  sing_box: ""
  xray: ""

# Geo/rule assets. Paths are relative to asset_dir unless absolute.
assets:
  mihomo.geosite:
    path: mihomo/geosite.dat
    url: https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geosite.dat
  mihomo.geoip:
    path: mihomo/geoip.dat
    url: https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geoip.dat
  sing-box.geosite-cn:
    path: sing-box/geosite-cn.srs
    url: https://github.com/SagerNet/sing-geosite/releases/latest/download/geosite-cn.srs
  sing-box.geoip-cn:
    path: sing-box/geoip-cn.srs
    url: https://github.com/SagerNet/sing-geoip/releases/latest/download/geoip-cn.srs
  xray.geosite:
    path: xray/geosite.dat
    url: https://github.com/v2fly/domain-list-community/releases/latest/download/dlc.dat
  xray.geoip:
    path: xray/geoip.dat
    url: https://github.com/v2fly/geoip/releases/latest/download/geoip.dat

templates:
  mihomo:
    normal: mihomo_base.yaml
    tun: mihomo_tun.yaml
  sing-box:
    normal: singbox_base.json
    tun: singbox_tun.json
  xray:
    normal: xray_base.json
    tun: xray_tun.json

node_management:
  dedupe: true
  rename_template: "{name}"
  include_regex: ""
  exclude_regex: ""
  sort_by: region,name

grouping:
  region_rules:
    HK: "(?i)(hong kong|hongkong|hk|香港)"
    JP: "(?i)(japan|jp|tokyo|osaka|日本)"
    SG: "(?i)(singapore|sg|新加坡)"
    TW: "(?i)(taiwan|tw|台灣|台湾)"
    US: "(?i)(united states|usa|us|america|美国)"
```

- [ ] **Step 3: Create the portable config file**

Write this exact file to `packaging/portable/config.yaml`:

```yaml
# subcli portable configuration
# This file lives next to the subcli executable in TGZ/ZIP packages.
# All relative paths are resolved relative to this file.

version: 2

# Paths
data_dir: ./data
cache_dir: ./cache
asset_dir: ./data/assets
template_dir: ./templates
profile_dir: ./profiles
output_dir: ./outputs
state_dir: ./data/state
log_dir: ./logs
sub_file: ./data/sub.yaml

# Export behavior
profile: bypass-cn
profile_path: ""
tun: false
log_level: info

# Network
parallelism: 3
timeout: 15
retry: 2
fetch_max_bytes: 5242880

# External proxy cores. Empty means: search PATH.
core_paths:
  mihomo: ""
  sing_box: ""
  xray: ""

# Geo/rule assets. Paths are relative to asset_dir unless absolute.
assets:
  mihomo.geosite:
    path: mihomo/geosite.dat
    url: https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geosite.dat
  mihomo.geoip:
    path: mihomo/geoip.dat
    url: https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geoip.dat
  sing-box.geosite-cn:
    path: sing-box/geosite-cn.srs
    url: https://github.com/SagerNet/sing-geosite/releases/latest/download/geosite-cn.srs
  sing-box.geoip-cn:
    path: sing-box/geoip-cn.srs
    url: https://github.com/SagerNet/sing-geoip/releases/latest/download/geoip-cn.srs
  xray.geosite:
    path: xray/geosite.dat
    url: https://github.com/v2fly/domain-list-community/releases/latest/download/dlc.dat
  xray.geoip:
    path: xray/geoip.dat
    url: https://github.com/v2fly/geoip/releases/latest/download/geoip.dat

templates:
  mihomo:
    normal: mihomo_base.yaml
    tun: mihomo_tun.yaml
  sing-box:
    normal: singbox_base.json
    tun: singbox_tun.json
  xray:
    normal: xray_base.json
    tun: xray_tun.json

node_management:
  dedupe: true
  rename_template: "{name}"
  include_regex: ""
  exclude_regex: ""
  sort_by: region,name

grouping:
  region_rules:
    HK: "(?i)(hong kong|hongkong|hk|香港)"
    JP: "(?i)(japan|jp|tokyo|osaka|日本)"
    SG: "(?i)(singapore|sg|新加坡)"
    TW: "(?i)(taiwan|tw|台灣|台湾)"
    US: "(?i)(united states|usa|us|america|美国)"
```

- [ ] **Step 4: Create DEB postinst**

Write this exact file to `packaging/deb/postinst`:

```sh
#!/bin/sh
set -e

case "$1" in
    configure)
        install -d -m 755 /var/lib/subcli
        install -d -m 755 /var/lib/subcli/assets
        install -d -m 755 /var/lib/subcli/outputs
        install -d -m 755 /var/lib/subcli/state
        install -d -m 755 /var/cache/subcli
        install -d -m 755 /var/log/subcli
        if [ ! -f /var/lib/subcli/sub.yaml ]; then
            printf 'version: 1\nsubscriptions: []\n' > /var/lib/subcli/sub.yaml
            chmod 644 /var/lib/subcli/sub.yaml
        fi
        if [ -d /run/systemd/system ] && command -v systemctl >/dev/null 2>&1; then
            systemctl daemon-reload || true
            systemctl enable subcli-daemon.service >/dev/null 2>&1 || true
        fi
        ;;
    abort-upgrade|abort-remove|abort-deconfigure)
        ;;
    *)
        ;;
esac

exit 0
```

- [ ] **Step 5: Create DEB prerm**

Write this exact file to `packaging/deb/prerm`:

```sh
#!/bin/sh
set -e

case "$1" in
    remove|upgrade|deconfigure)
        if [ -d /run/systemd/system ] && command -v systemctl >/dev/null 2>&1; then
            systemctl stop subcli-daemon.service >/dev/null 2>&1 || true
            systemctl disable subcli-daemon.service >/dev/null 2>&1 || true
        fi
        ;;
    failed-upgrade)
        ;;
    *)
        ;;
esac

exit 0
```

- [ ] **Step 6: Create DEB postrm**

Write this exact file to `packaging/deb/postrm`:

```sh
#!/bin/sh
set -e

case "$1" in
    purge)
        rm -rf /var/lib/subcli
        rm -rf /var/cache/subcli
        rm -rf /var/log/subcli
        rm -rf /etc/subcli
        if [ -d /run/systemd/system ] && command -v systemctl >/dev/null 2>&1; then
            systemctl daemon-reload || true
        fi
        ;;
    remove|upgrade|failed-upgrade|abort-install|abort-upgrade|disappear)
        ;;
    *)
        ;;
esac

exit 0
```

- [ ] **Step 7: Create RPM postinstall script**

Write this exact file to `packaging/rpm/postinstall.sh`:

```sh
#!/bin/sh
set -e

install -d -m 755 /var/lib/subcli
install -d -m 755 /var/lib/subcli/assets
install -d -m 755 /var/lib/subcli/outputs
install -d -m 755 /var/lib/subcli/state
install -d -m 755 /var/cache/subcli
install -d -m 755 /var/log/subcli
if [ ! -f /var/lib/subcli/sub.yaml ]; then
    printf 'version: 1\nsubscriptions: []\n' > /var/lib/subcli/sub.yaml
    chmod 644 /var/lib/subcli/sub.yaml
fi
if [ -d /run/systemd/system ] && command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload || true
    systemctl enable subcli-daemon.service >/dev/null 2>&1 || true
fi

exit 0
```

- [ ] **Step 8: Create RPM preremove script**

Write this exact file to `packaging/rpm/preremove.sh`:

```sh
#!/bin/sh
set -e

if [ "$1" = "0" ]; then
    if [ -d /run/systemd/system ] && command -v systemctl >/dev/null 2>&1; then
        systemctl stop subcli-daemon.service >/dev/null 2>&1 || true
        systemctl disable subcli-daemon.service >/dev/null 2>&1 || true
    fi
fi

exit 0
```

- [ ] **Step 9: Create RPM postuninstall script**

Write this exact file to `packaging/rpm/postuninstall.sh`:

```sh
#!/bin/sh
set -e

if [ "$1" = "0" ]; then
    rm -rf /var/lib/subcli
    rm -rf /var/cache/subcli
    rm -rf /var/log/subcli
    rm -rf /etc/subcli
    if [ -d /run/systemd/system ] && command -v systemctl >/dev/null 2>&1; then
        systemctl daemon-reload || true
    fi
fi

exit 0
```

- [ ] **Step 10: Make scripts executable**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
chmod 755 packaging/deb/postinst packaging/deb/prerm packaging/deb/postrm
chmod 755 packaging/rpm/postinstall.sh packaging/rpm/preremove.sh packaging/rpm/postuninstall.sh
```

Expected: no output.

- [ ] **Step 11: Commit package templates**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
git add packaging/fhs/config.yaml packaging/portable/config.yaml packaging/deb/postinst packaging/deb/prerm packaging/deb/postrm packaging/rpm/postinstall.sh packaging/rpm/preremove.sh packaging/rpm/postuninstall.sh
git commit -m "packaging: add config-driven package defaults"
```

---

## Task 2: Replace Environment API With Config Detection Tests

**Files:**
- Modify: `include/subcli/environment.hpp`
- Modify: `tests/subcli_tests.cpp`

- [ ] **Step 1: Replace environment header declarations first**

Open `include/subcli/environment.hpp` and replace the whole file with:

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
};

struct EnvironmentInfo {
    bool ok = false;
    ConfigMode mode = ConfigMode::UserLocal;
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
std::filesystem::path resolvePathFromBase(const std::filesystem::path& baseDir, const std::string& value);
std::filesystem::path platformFhsConfigPath(PlatformKind platform);
std::filesystem::path platformUserConfigPath(PlatformKind platform);
std::string configModeName(ConfigMode mode);

} // namespace subcli
```

- [ ] **Step 2: Remove workspace include from unit tests**

In `tests/subcli_tests.cpp`, remove this include line:

```cpp
#include "subcli/workspace.hpp"
```

- [ ] **Step 3: Add failing environment detection tests**

Add these tests near the existing environment tests in `tests/subcli_tests.cpp`:

```cpp
void testResolvePathFromBaseKeepsAbsoluteAndResolvesRelative() {
    const fs::path base = fs::path("/tmp/subcli-config");
    require(subcli::resolvePathFromBase(base, "/var/lib/subcli").string() == "/var/lib/subcli", "absolute path should stay absolute");
    require(subcli::resolvePathFromBase(base, "./data/assets").string() == "/tmp/subcli-config/data/assets", "relative path should resolve from config dir");
    require(subcli::resolvePathFromBase(base, "templates").string() == "/tmp/subcli-config/templates", "bare relative path should resolve from config dir");
}

void testDetectEnvironmentPrefersExplicitConfigOption() {
    const fs::path dir = makeUniqueTestDir("subcli-env-explicit-config");
    const fs::path config = dir / "custom.yaml";
    std::ofstream(config) << "version: 2\n";

    subcli::EnvironmentDetectInput input;
    input.configOption = config.string();
    input.envConfig = (dir / "env.yaml").string();
    input.exeDirOverride = (dir / "app").string();
    input.fhsConfigOverride = (dir / "etc" / "config.yaml").string();
    input.userConfigOverride = (dir / "home" / "config.yaml").string();
    input.platform = subcli::PlatformKind::Linux;

    const auto info = subcli::detectEnvironment(input);
    require(info.ok, "explicit config detection should succeed: " + info.error);
    require(info.mode == subcli::ConfigMode::Explicit, "explicit config option should win");
    require(info.configPath == fs::absolute(config).lexically_normal(), "explicit config path should be absolute normalized");
}

void testDetectEnvironmentPrefersSubcliConfigEnvWhenOptionMissing() {
    const fs::path dir = makeUniqueTestDir("subcli-env-env-config");
    const fs::path config = dir / "env.yaml";
    std::ofstream(config) << "version: 2\n";

    subcli::EnvironmentDetectInput input;
    input.envConfig = config.string();
    input.exeDirOverride = (dir / "app").string();
    input.fhsConfigOverride = (dir / "etc" / "config.yaml").string();
    input.userConfigOverride = (dir / "home" / "config.yaml").string();
    input.platform = subcli::PlatformKind::Linux;

    const auto info = subcli::detectEnvironment(input);
    require(info.ok, "env config detection should succeed: " + info.error);
    require(info.mode == subcli::ConfigMode::Explicit, "SUBCLI_CONFIG should use explicit mode");
    require(info.configPath == fs::absolute(config).lexically_normal(), "env config path should be absolute normalized");
}

void testDetectEnvironmentUsesPortableConfigNextToExecutable() {
    const fs::path dir = makeUniqueTestDir("subcli-env-portable");
    const fs::path appDir = dir / "app";
    fs::create_directories(appDir);
    const fs::path config = appDir / "config.yaml";
    std::ofstream(config) << "version: 2\n";

    subcli::EnvironmentDetectInput input;
    input.exeDirOverride = appDir.string();
    input.fhsConfigOverride = (dir / "etc" / "config.yaml").string();
    input.userConfigOverride = (dir / "home" / "config.yaml").string();
    input.platform = subcli::PlatformKind::Linux;

    const auto info = subcli::detectEnvironment(input);
    require(info.ok, "portable config detection should succeed: " + info.error);
    require(info.mode == subcli::ConfigMode::Portable, "exe-dir config should use portable mode");
    require(info.configPath == fs::absolute(config).lexically_normal(), "portable config path should be absolute normalized");
}

void testDetectEnvironmentUsesFhsConfigBeforeUserLocal() {
    const fs::path dir = makeUniqueTestDir("subcli-env-fhs");
    const fs::path fhs = dir / "etc" / "subcli" / "config.yaml";
    const fs::path user = dir / "home" / ".config" / "subcli" / "config.yaml";
    fs::create_directories(fhs.parent_path());
    fs::create_directories(user.parent_path());
    std::ofstream(fhs) << "version: 2\n";
    std::ofstream(user) << "version: 2\n";

    subcli::EnvironmentDetectInput input;
    input.exeDirOverride = (dir / "app").string();
    input.fhsConfigOverride = fhs.string();
    input.userConfigOverride = user.string();
    input.platform = subcli::PlatformKind::Linux;

    const auto info = subcli::detectEnvironment(input);
    require(info.ok, "fhs config detection should succeed: " + info.error);
    require(info.mode == subcli::ConfigMode::FHS, "FHS config should win over user-local config");
    require(info.configPath == fs::absolute(fhs).lexically_normal(), "FHS config path should be absolute normalized");
}

void testDetectEnvironmentFallsBackToUserLocalPathWhenNoConfigExists() {
    const fs::path dir = makeUniqueTestDir("subcli-env-user-fallback");
    const fs::path user = dir / "home" / ".config" / "subcli" / "config.yaml";

    subcli::EnvironmentDetectInput input;
    input.exeDirOverride = (dir / "app").string();
    input.fhsConfigOverride = (dir / "etc" / "subcli" / "config.yaml").string();
    input.userConfigOverride = user.string();
    input.platform = subcli::PlatformKind::Linux;

    const auto info = subcli::detectEnvironment(input);
    require(info.ok, "fallback detection should succeed: " + info.error);
    require(info.mode == subcli::ConfigMode::UserLocal, "missing config should choose user-local fallback");
    require(info.configPath == fs::absolute(user).lexically_normal(), "user-local fallback path should be absolute normalized");
}
```

- [ ] **Step 4: Register the new tests**

Near old environment test registrations in `main()`, add:

```cpp
runTest("testResolvePathFromBaseKeepsAbsoluteAndResolvesRelative", testResolvePathFromBaseKeepsAbsoluteAndResolvesRelative);
runTest("testDetectEnvironmentPrefersExplicitConfigOption", testDetectEnvironmentPrefersExplicitConfigOption);
runTest("testDetectEnvironmentPrefersSubcliConfigEnvWhenOptionMissing", testDetectEnvironmentPrefersSubcliConfigEnvWhenOptionMissing);
runTest("testDetectEnvironmentUsesPortableConfigNextToExecutable", testDetectEnvironmentUsesPortableConfigNextToExecutable);
runTest("testDetectEnvironmentUsesFhsConfigBeforeUserLocal", testDetectEnvironmentUsesFhsConfigBeforeUserLocal);
runTest("testDetectEnvironmentFallsBackToUserLocalPathWhenNoConfigExists", testDetectEnvironmentFallsBackToUserLocalPathWhenNoConfigExists);
```

- [ ] **Step 5: Remove old environment/workspace test registrations**

Remove these registration lines from `tests/subcli_tests.cpp`:

```cpp
runTest("testEnvironmentResolutionPrefersCliWorkspaceOverEnvAndPersisted", testEnvironmentResolutionPrefersCliWorkspaceOverEnvAndPersisted);
runTest("testEnvironmentResolutionFailsForInvalidExplicitCliWorkspace", testEnvironmentResolutionFailsForInvalidExplicitCliWorkspace);
runTest("testEnvironmentResolutionUsesEnvWorkspaceWhenCliWorkspaceMissing", testEnvironmentResolutionUsesEnvWorkspaceWhenCliWorkspaceMissing);
runTest("testEnvironmentResolutionPrefersMarkerDiscoveryOverPersistedDefault", testEnvironmentResolutionPrefersMarkerDiscoveryOverPersistedDefault);
runTest("testEnvironmentResolutionUsesPersistedDefaultWithoutCliEnvOrMarker", testEnvironmentResolutionUsesPersistedDefaultWithoutCliEnvOrMarker);
runTest("testPlatformDefaultWorkspaceRootIsNonEmpty", testPlatformDefaultWorkspaceRootIsNonEmpty);
runTest("testWorkspaceSeedBuiltInsCopiesMissingFilesOnly", testWorkspaceSeedBuiltInsCopiesMissingFilesOnly);
runTest("testWorkspaceInitCreatesExpectedTree", testWorkspaceInitCreatesExpectedTree);
runTest("testWorkspaceReadMetadataRejectsUnsupportedEnvVersion", testWorkspaceReadMetadataRejectsUnsupportedEnvVersion);
runTest("testWorkspaceReadMetadataSupportsLegacyMarkerWithoutMetadata", testWorkspaceReadMetadataSupportsLegacyMarkerWithoutMetadata);
runTest("testWorkspaceMigrateCopiesDurableDataOnly", testWorkspaceMigrateCopiesDurableDataOnly);
```

- [ ] **Step 6: Remove old test function definitions**

In `tests/subcli_tests.cpp`, delete the function bodies for the old environment/workspace tests named in Step 5. Delete the entire function from its `void test...() {` line through its matching closing brace.

- [ ] **Step 7: Run build and confirm expected compile failures**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cmake --build build -j
```

Expected: compile fails because `src/environment.cpp`, `src/main.cpp`, and other files still reference old `EnvironmentResolveInput`, `resolveEnvironment`, or workspace symbols.

Do not fix unrelated files in this task.

- [ ] **Step 8: Commit the API and failing tests**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
git add include/subcli/environment.hpp tests/subcli_tests.cpp
git commit -m "test: define config-driven environment detection"
```

---

## Task 3: Implement Config-Driven Environment Detection

**Files:**
- Modify: `src/environment.cpp`
- Modify: `tests/subcli_tests.cpp` only if Step 2 left compile-only cleanup needed

- [ ] **Step 1: Replace environment implementation**

Replace the entire contents of `src/environment.cpp` with:

```cpp
#include "subcli/environment.hpp"

#include <cstdlib>
#include <filesystem>

namespace subcli {

namespace fs = std::filesystem;

namespace {

fs::path normalizeAbsolutePath(const fs::path& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec) {
        return path.lexically_normal();
    }
    return absolute.lexically_normal();
}

std::string getEnvValue(const char* name) {
    const char* raw = std::getenv(name);
    return raw && *raw ? std::string(raw) : std::string();
}

fs::path homeDir(PlatformKind platform) {
#ifdef _WIN32
    const std::string userProfile = getEnvValue("USERPROFILE");
    if (!userProfile.empty()) {
        return fs::path(userProfile);
    }
#endif
    const std::string home = getEnvValue("HOME");
    if (!home.empty()) {
        return fs::path(home);
    }
    if (platform == PlatformKind::Windows) {
        const std::string appData = getEnvValue("APPDATA");
        if (!appData.empty()) {
            return fs::path(appData).parent_path();
        }
    }
    std::error_code ec;
    return fs::current_path(ec);
}

fs::path exeDirFromArgv0(const std::string& argv0, const std::string& overrideDir, const std::string& cwd) {
    if (!overrideDir.empty()) {
        return normalizeAbsolutePath(overrideDir);
    }
    if (!argv0.empty()) {
        fs::path exe(argv0);
        if (exe.has_parent_path()) {
            return normalizeAbsolutePath(exe.parent_path());
        }
    }
    if (!cwd.empty()) {
        return normalizeAbsolutePath(cwd);
    }
    std::error_code ec;
    return normalizeAbsolutePath(fs::current_path(ec));
}

bool existsRegularFile(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path, ec) && !ec && fs::is_regular_file(path, ec) && !ec;
}

EnvironmentInfo makeInfo(ConfigMode mode, const fs::path& configPath, std::vector<std::string> trace) {
    EnvironmentInfo info;
    info.ok = true;
    info.mode = mode;
    info.configPath = normalizeAbsolutePath(configPath);
    info.configDir = info.configPath.parent_path();
    info.trace = std::move(trace);
    return info;
}

} // namespace

std::filesystem::path resolvePathFromBase(const std::filesystem::path& baseDir, const std::string& value) {
    if (value.empty()) {
        return {};
    }
    fs::path path(value);
    if (path.is_absolute()) {
        return path.lexically_normal();
    }
    return (baseDir / path).lexically_normal();
}

std::filesystem::path platformFhsConfigPath(PlatformKind platform) {
    if (platform == PlatformKind::MacOS) {
        return fs::path("/usr/local/etc/subcli/config.yaml");
    }
    if (platform == PlatformKind::Windows) {
        return {};
    }
    return fs::path("/etc/subcli/config.yaml");
}

std::filesystem::path platformUserConfigPath(PlatformKind platform) {
    if (platform == PlatformKind::Windows) {
        const std::string appData = getEnvValue("APPDATA");
        if (!appData.empty()) {
            return fs::path(appData) / "subcli" / "config.yaml";
        }
        return homeDir(platform) / "AppData" / "Roaming" / "subcli" / "config.yaml";
    }
    if (platform == PlatformKind::MacOS) {
        return homeDir(platform) / "Library" / "Application Support" / "subcli" / "config.yaml";
    }
    const std::string xdgConfig = getEnvValue("XDG_CONFIG_HOME");
    if (!xdgConfig.empty()) {
        return fs::path(xdgConfig) / "subcli" / "config.yaml";
    }
    return homeDir(platform) / ".config" / "subcli" / "config.yaml";
}

std::string configModeName(ConfigMode mode) {
    switch (mode) {
    case ConfigMode::Explicit:
        return "explicit";
    case ConfigMode::Portable:
        return "portable";
    case ConfigMode::FHS:
        return "fhs";
    case ConfigMode::UserLocal:
        return "user_local";
    }
    return "unknown";
}

EnvironmentInfo detectEnvironment(const EnvironmentDetectInput& input) {
    std::vector<std::string> trace;
    trace.push_back("resolution order: --config > SUBCLI_CONFIG > exe-dir config.yaml > FHS config > user-local config");

    const std::string explicitPath = !input.configOption.empty() ? input.configOption : input.envConfig;
    if (!explicitPath.empty()) {
        const fs::path configPath = normalizeAbsolutePath(explicitPath);
        if (!existsRegularFile(configPath)) {
            EnvironmentInfo failed;
            failed.ok = false;
            failed.mode = ConfigMode::Explicit;
            failed.configPath = configPath;
            failed.configDir = configPath.parent_path();
            failed.trace = std::move(trace);
            failed.error = "config file does not exist: " + configPath.string();
            return failed;
        }
        trace.push_back(input.configOption.empty() ? "selected SUBCLI_CONFIG" : "selected --config");
        return makeInfo(ConfigMode::Explicit, configPath, std::move(trace));
    }

    const fs::path exeDir = exeDirFromArgv0(input.argv0, input.exeDirOverride, input.cwd);
    const fs::path portableConfig = exeDir / "config.yaml";
    if (existsRegularFile(portableConfig)) {
        trace.push_back("selected portable config next to executable");
        return makeInfo(ConfigMode::Portable, portableConfig, std::move(trace));
    }

    const fs::path fhsConfig = input.fhsConfigOverride.empty() ? platformFhsConfigPath(input.platform) : fs::path(input.fhsConfigOverride);
    if (!fhsConfig.empty() && existsRegularFile(fhsConfig)) {
        trace.push_back("selected FHS config");
        return makeInfo(ConfigMode::FHS, fhsConfig, std::move(trace));
    }

    const fs::path userConfig = input.userConfigOverride.empty() ? platformUserConfigPath(input.platform) : fs::path(input.userConfigOverride);
    trace.push_back("selected user-local fallback config path");
    return makeInfo(ConfigMode::UserLocal, userConfig, std::move(trace));
}

} // namespace subcli
```

- [ ] **Step 2: Run environment unit tests**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cmake --build build -j
ctest --test-dir build -R subcli_tests --output-on-failure
```

Expected: build still may fail because `src/main.cpp` references old environment types. If build fails there, continue to Step 3 and compile only the object mentally is not enough; the next tasks will fix main. If it builds, the new environment tests should pass.

- [ ] **Step 3: Commit environment implementation**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
git add src/environment.cpp
git commit -m "feat: detect config-driven runtime environment"
```

---

## Task 4: Add Config Defaults and Path Resolution Helpers

**Files:**
- Create: `include/subcli/config_defaults.hpp`
- Create: `src/config_defaults.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/subcli_tests.cpp`

- [ ] **Step 1: Add failing tests for defaults and resolved paths**

Add these tests to `tests/subcli_tests.cpp` near config tests:

```cpp
void testPortableDefaultConfigUsesRelativePaths() {
    const auto cfg = subcli::makePortableDefaultConfig();
    require(cfg.dataDir == "./data", "portable data_dir should be relative");
    require(cfg.cacheDir == "./cache", "portable cache_dir should be relative");
    require(cfg.assetDir == "./data/assets", "portable asset_dir should be relative");
    require(cfg.templateDir == "./templates", "portable template_dir should be relative");
    require(cfg.profileDir == "./profiles", "portable profile_dir should be relative");
    require(cfg.outputDir == "./outputs", "portable output_dir should be relative");
    require(cfg.stateDir == "./data/state", "portable state_dir should be relative");
    require(cfg.logDir == "./logs", "portable log_dir should be relative");
    require(cfg.subFile == "./data/sub.yaml", "portable sub_file should be relative");
}

void testFhsDefaultConfigUsesAbsolutePaths() {
    const auto cfg = subcli::makeFhsDefaultConfig();
    require(cfg.dataDir == "/var/lib/subcli", "FHS data_dir should be /var/lib/subcli");
    require(cfg.cacheDir == "/var/cache/subcli", "FHS cache_dir should be /var/cache/subcli");
    require(cfg.assetDir == "/var/lib/subcli/assets", "FHS asset_dir should be /var/lib/subcli/assets");
    require(cfg.templateDir == "/usr/share/subcli/templates", "FHS template_dir should be /usr/share/subcli/templates");
    require(cfg.profileDir == "/usr/share/subcli/profiles", "FHS profile_dir should be /usr/share/subcli/profiles");
    require(cfg.outputDir == "/var/lib/subcli/outputs", "FHS output_dir should be /var/lib/subcli/outputs");
    require(cfg.stateDir == "/var/lib/subcli/state", "FHS state_dir should be /var/lib/subcli/state");
    require(cfg.logDir == "/var/log/subcli", "FHS log_dir should be /var/log/subcli");
    require(cfg.subFile == "/var/lib/subcli/sub.yaml", "FHS sub_file should be /var/lib/subcli/sub.yaml");
}

void testResolveConfigPathsResolvesFromConfigDirAndAssetDir() {
    subcli::AppConfig cfg = subcli::makePortableDefaultConfig();
    cfg.assetDir = "./runtime/assets";
    cfg.templateDir = "./factory/templates";
    cfg.profileDir = "./factory/profiles";
    cfg.outputDir = "./out";
    cfg.cacheDir = "./cache";
    cfg.dataDir = "./data";
    cfg.stateDir = "./data/state";
    cfg.logDir = "./logs";
    cfg.subFile = "./data/sub.yaml";
    cfg.assets["test.asset"].path = "nested/file.dat";
    cfg.assets["test.asset"].url = "file:///tmp/file.dat";
    cfg.templateNormal["mihomo"] = "mihomo_base.yaml";

    const fs::path configDir = fs::path("/tmp/subcli-config-root");
    subcli::resolveConfigPathsInPlace(cfg, configDir);

    require(cfg.assetDir == "/tmp/subcli-config-root/runtime/assets", "asset_dir should resolve from config dir");
    require(cfg.assets["test.asset"].path == "/tmp/subcli-config-root/runtime/assets/nested/file.dat", "asset path should resolve from asset_dir");
    require(cfg.templateDir == "/tmp/subcli-config-root/factory/templates", "template_dir should resolve from config dir");
    require(cfg.templateNormal["mihomo"] == "/tmp/subcli-config-root/factory/templates/mihomo_base.yaml", "template path should resolve from template_dir");
    require(cfg.profileDir == "/tmp/subcli-config-root/factory/profiles", "profile_dir should resolve from config dir");
    require(cfg.outputDir == "/tmp/subcli-config-root/out", "output_dir should resolve from config dir");
}
```

- [ ] **Step 2: Register default config tests**

Add these lines in `main()` test registrations:

```cpp
runTest("testPortableDefaultConfigUsesRelativePaths", testPortableDefaultConfigUsesRelativePaths);
runTest("testFhsDefaultConfigUsesAbsolutePaths", testFhsDefaultConfigUsesAbsolutePaths);
runTest("testResolveConfigPathsResolvesFromConfigDirAndAssetDir", testResolveConfigPathsResolvesFromConfigDirAndAssetDir);
```

- [ ] **Step 3: Add include for new helpers**

At the top of `tests/subcli_tests.cpp`, add:

```cpp
#include "subcli/config_defaults.hpp"
```

- [ ] **Step 4: Create config defaults header**

Write this exact file to `include/subcli/config_defaults.hpp`:

```cpp
#pragma once

#include <filesystem>

#include "subcli/models.hpp"

namespace subcli {

AppConfig makeFhsDefaultConfig();
AppConfig makePortableDefaultConfig();
void populateDefaultAssets(AppConfig& config);
void populateDefaultTemplates(AppConfig& config);
void populateDefaultRegionRules(AppConfig& config);
void resolveConfigPathsInPlace(AppConfig& config, const std::filesystem::path& configDir);

} // namespace subcli
```

- [ ] **Step 5: Create config defaults implementation**

Write this exact file to `src/config_defaults.cpp`:

```cpp
#include "subcli/config_defaults.hpp"

#include "subcli/environment.hpp"

namespace subcli {

namespace fs = std::filesystem;

namespace {

void populateCommonDefaults(AppConfig& config) {
    config.profile = "bypass-cn";
    config.profilePath.clear();
    config.tun = false;
    config.logLevel = "info";
    config.parallelism = 3;
    config.timeout = 15;
    config.retry = 2;
    config.fetchMaxBytes = 5242880;
    config.mihomoPath.clear();
    config.singBoxPath.clear();
    config.xrayPath.clear();
    config.dedupeNodes = true;
    config.renameTemplate = "{name}";
    config.includeRegex.clear();
    config.excludeRegex.clear();
    config.sortBy = "region,name";
    populateDefaultAssets(config);
    populateDefaultTemplates(config);
    populateDefaultRegionRules(config);
}

std::string resolveFrom(const fs::path& base, const std::string& value) {
    return resolvePathFromBase(base, value).string();
}

} // namespace

void populateDefaultAssets(AppConfig& config) {
    config.assets.clear();
    config.assets["mihomo.geosite"] = {"mihomo/geosite.dat", "https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geosite.dat"};
    config.assets["mihomo.geoip"] = {"mihomo/geoip.dat", "https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geoip.dat"};
    config.assets["sing-box.geosite-cn"] = {"sing-box/geosite-cn.srs", "https://github.com/SagerNet/sing-geosite/releases/latest/download/geosite-cn.srs"};
    config.assets["sing-box.geoip-cn"] = {"sing-box/geoip-cn.srs", "https://github.com/SagerNet/sing-geoip/releases/latest/download/geoip-cn.srs"};
    config.assets["xray.geosite"] = {"xray/geosite.dat", "https://github.com/v2fly/domain-list-community/releases/latest/download/dlc.dat"};
    config.assets["xray.geoip"] = {"xray/geoip.dat", "https://github.com/v2fly/geoip/releases/latest/download/geoip.dat"};
}

void populateDefaultTemplates(AppConfig& config) {
    config.templateNormal.clear();
    config.templateTun.clear();
    config.templateNormal["mihomo"] = "mihomo_base.yaml";
    config.templateTun["mihomo"] = "mihomo_tun.yaml";
    config.templateNormal["sing-box"] = "singbox_base.json";
    config.templateTun["sing-box"] = "singbox_tun.json";
    config.templateNormal["xray"] = "xray_base.json";
    config.templateTun["xray"] = "xray_tun.json";
}

void populateDefaultRegionRules(AppConfig& config) {
    config.regionRules.clear();
    config.regionRules["HK"] = "(?i)(hong kong|hongkong|hk|香港)";
    config.regionRules["JP"] = "(?i)(japan|jp|tokyo|osaka|日本)";
    config.regionRules["SG"] = "(?i)(singapore|sg|新加坡)";
    config.regionRules["TW"] = "(?i)(taiwan|tw|台灣|台湾)";
    config.regionRules["US"] = "(?i)(united states|usa|us|america|美国)";
}

AppConfig makeFhsDefaultConfig() {
    AppConfig config;
    config.dataDir = "/var/lib/subcli";
    config.cacheDir = "/var/cache/subcli";
    config.assetDir = "/var/lib/subcli/assets";
    config.templateDir = "/usr/share/subcli/templates";
    config.profileDir = "/usr/share/subcli/profiles";
    config.outputDir = "/var/lib/subcli/outputs";
    config.stateDir = "/var/lib/subcli/state";
    config.logDir = "/var/log/subcli";
    config.subFile = "/var/lib/subcli/sub.yaml";
    populateCommonDefaults(config);
    return config;
}

AppConfig makePortableDefaultConfig() {
    AppConfig config;
    config.dataDir = "./data";
    config.cacheDir = "./cache";
    config.assetDir = "./data/assets";
    config.templateDir = "./templates";
    config.profileDir = "./profiles";
    config.outputDir = "./outputs";
    config.stateDir = "./data/state";
    config.logDir = "./logs";
    config.subFile = "./data/sub.yaml";
    populateCommonDefaults(config);
    return config;
}

void resolveConfigPathsInPlace(AppConfig& config, const std::filesystem::path& configDir) {
    config.dataDir = resolveFrom(configDir, config.dataDir);
    config.cacheDir = resolveFrom(configDir, config.cacheDir);
    config.assetDir = resolveFrom(configDir, config.assetDir);
    config.templateDir = resolveFrom(configDir, config.templateDir);
    config.profileDir = resolveFrom(configDir, config.profileDir);
    config.outputDir = resolveFrom(configDir, config.outputDir);
    config.stateDir = resolveFrom(configDir, config.stateDir);
    config.logDir = resolveFrom(configDir, config.logDir);
    config.subFile = resolveFrom(configDir, config.subFile);
    if (!config.profilePath.empty()) {
        config.profilePath = resolveFrom(configDir, config.profilePath);
    }
    if (!config.mihomoPath.empty()) {
        config.mihomoPath = resolveFrom(configDir, config.mihomoPath);
    }
    if (!config.singBoxPath.empty()) {
        config.singBoxPath = resolveFrom(configDir, config.singBoxPath);
    }
    if (!config.xrayPath.empty()) {
        config.xrayPath = resolveFrom(configDir, config.xrayPath);
    }
    for (auto& kv : config.assets) {
        if (!kv.second.path.empty()) {
            kv.second.path = resolveFrom(config.assetDir, kv.second.path);
        }
    }
    for (auto& kv : config.templateNormal) {
        if (!kv.second.empty()) {
            kv.second = resolveFrom(config.templateDir, kv.second);
        }
    }
    for (auto& kv : config.templateTun) {
        if (!kv.second.empty()) {
            kv.second = resolveFrom(config.templateDir, kv.second);
        }
    }
}

} // namespace subcli
```

- [ ] **Step 6: Add source file to CMake**

In `CMakeLists.txt`, add `src/config_defaults.cpp` to `SUBCLI_SOURCES` after `src/config_service.cpp`:

```cmake
    src/config_defaults.cpp
```

- [ ] **Step 7: Run targeted tests**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cmake -S . -B build
cmake --build build -j
ctest --test-dir build -R subcli_tests --output-on-failure
```

Expected: may still fail because `AppConfig` does not yet have new fields and `assets` map. Continue to Task 5.

- [ ] **Step 8: Commit config defaults files**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
git add include/subcli/config_defaults.hpp src/config_defaults.cpp CMakeLists.txt tests/subcli_tests.cpp
git commit -m "feat: add config-driven defaults"
```

---

## Task 5: Update AppConfig and Store Schema

**Files:**
- Modify: `include/subcli/models.hpp`
- Modify: `src/store.cpp`
- Modify: `tests/subcli_tests.cpp`

- [ ] **Step 1: Update AppConfig fields**

In `include/subcli/models.hpp`, inside `struct AppConfig`, replace the existing path/asset fields:

```cpp
    std::string templateDir = "./templates";
    std::string outputDir = "./outputs";
    std::string mihomoPath;
    std::string singBoxPath;
    std::string xrayPath;
```

with:

```cpp
    std::string dataDir = "./data";
    std::string cacheDir = "./cache";
    std::string assetDir = "./data/assets";
    std::string templateDir = "./templates";
    std::string profileDir = "./profiles";
    std::string outputDir = "./outputs";
    std::string stateDir = "./data/state";
    std::string logDir = "./logs";
    std::string subFile = "./data/sub.yaml";
    std::string mihomoPath;
    std::string singBoxPath;
    std::string xrayPath;
```

- [ ] **Step 2: Replace asset maps in AppConfig**

Still in `include/subcli/models.hpp`, replace:

```cpp
    std::string assetDir = "./assets";
    std::map<std::string, std::string> assetPaths;
    std::map<std::string, std::string> assetUrls;
```

with:

```cpp
    struct AssetEntry {
        std::string path;
        std::string url;
    };
    std::map<std::string, AssetEntry> assets;
```

- [ ] **Step 3: Add failing store test for new asset schema**

In `tests/subcli_tests.cpp`, add:

```cpp
void testStorePersistsConfigDrivenPathsAndAssets() {
    const fs::path dir = makeUniqueTestDir("subcli-config-driven-store");
    const fs::path configPath = dir / "config.yaml";

    subcli::AppConfig config = subcli::makePortableDefaultConfig();
    config.dataDir = "./runtime-data";
    config.cacheDir = "./runtime-cache";
    config.profileDir = "./profiles-custom";
    config.stateDir = "./state-custom";
    config.logDir = "./logs-custom";
    config.subFile = "./data/subscriptions.yaml";
    config.assets.clear();
    config.assets["custom.asset"] = {"custom/file.dat", "https://example.invalid/file.dat"};

    subcli::saveConfig(configPath.string(), config);
    const auto raw = subcli::readFile(configPath.string());
    require(raw.find("data_dir") != std::string::npos, "config should persist data_dir");
    require(raw.find("cache_dir") != std::string::npos, "config should persist cache_dir");
    require(raw.find("custom.asset") != std::string::npos, "config should persist asset key");
    require(raw.find("path") != std::string::npos, "config should persist asset path field");
    require(raw.find("url") != std::string::npos, "config should persist asset url field");

    const auto loaded = subcli::loadConfig(configPath.string());
    require(loaded.dataDir == "./runtime-data", "loaded data_dir should match saved value");
    require(loaded.cacheDir == "./runtime-cache", "loaded cache_dir should match saved value");
    require(loaded.profileDir == "./profiles-custom", "loaded profile_dir should match saved value");
    require(loaded.stateDir == "./state-custom", "loaded state_dir should match saved value");
    require(loaded.logDir == "./logs-custom", "loaded log_dir should match saved value");
    require(loaded.subFile == "./data/subscriptions.yaml", "loaded sub_file should match saved value");
    require(loaded.assets.at("custom.asset").path == "custom/file.dat", "loaded asset path should match saved value");
    require(loaded.assets.at("custom.asset").url == "https://example.invalid/file.dat", "loaded asset url should match saved value");
}
```

- [ ] **Step 4: Register store schema test**

Add to `main()` test registrations:

```cpp
runTest("testStorePersistsConfigDrivenPathsAndAssets", testStorePersistsConfigDrivenPathsAndAssets);
```

- [ ] **Step 5: Update loadConfig defaults**

In `src/store.cpp`, add this include near existing includes:

```cpp
#include "subcli/config_defaults.hpp"
```

Then in `loadConfig`, replace:

```cpp
    AppConfig c;
    if (!fileExists(path)) {
        return c;
    }
```

with:

```cpp
    AppConfig c = makePortableDefaultConfig();
    if (!fileExists(path)) {
        return c;
    }
```

- [ ] **Step 6: Update loadConfig scalar path reads**

In `src/store.cpp`, inside `loadConfig`, replace the old scalar reads:

```cpp
    c.templateDir = root["template_dir"].as<std::string>("./templates");
    c.outputDir = root["output_dir"].as<std::string>("./outputs");
```

with:

```cpp
    c.dataDir = root["data_dir"].as<std::string>(c.dataDir);
    c.cacheDir = root["cache_dir"].as<std::string>(c.cacheDir);
    c.assetDir = root["asset_dir"].as<std::string>(c.assetDir);
    c.templateDir = root["template_dir"].as<std::string>(c.templateDir);
    c.profileDir = root["profile_dir"].as<std::string>(c.profileDir);
    c.outputDir = root["output_dir"].as<std::string>(c.outputDir);
    c.stateDir = root["state_dir"].as<std::string>(c.stateDir);
    c.logDir = root["log_dir"].as<std::string>(c.logDir);
    c.subFile = root["sub_file"].as<std::string>(c.subFile);
```

Also delete this later line because `asset_dir` is already loaded above:

```cpp
    c.assetDir = root["asset_dir"].as<std::string>("./assets");
```

- [ ] **Step 7: Update loadConfig asset schema**

In `src/store.cpp`, replace the whole block:

```cpp
    if (root["assets"] && root["assets"].IsMap()) {
        if (root["assets"]["paths"] && root["assets"]["paths"].IsMap()) {
            for (const auto& kv : root["assets"]["paths"]) {
                c.assetPaths[kv.first.as<std::string>()] = kv.second.as<std::string>();
            }
        }
        if (root["assets"]["urls"] && root["assets"]["urls"].IsMap()) {
            for (const auto& kv : root["assets"]["urls"]) {
                c.assetUrls[kv.first.as<std::string>()] = kv.second.as<std::string>();
            }
        }
    }
```

with:

```cpp
    if (root["assets"] && root["assets"].IsMap()) {
        const auto assets = root["assets"];
        if (assets["paths"] || assets["urls"]) {
            if (assets["paths"] && assets["paths"].IsMap()) {
                for (const auto& kv : assets["paths"]) {
                    c.assets[kv.first.as<std::string>()].path = kv.second.as<std::string>();
                }
            }
            if (assets["urls"] && assets["urls"].IsMap()) {
                for (const auto& kv : assets["urls"]) {
                    c.assets[kv.first.as<std::string>()].url = kv.second.as<std::string>();
                }
            }
        } else {
            for (const auto& kv : assets) {
                const std::string key = kv.first.as<std::string>();
                if (kv.second.IsMap()) {
                    c.assets[key].path = kv.second["path"].as<std::string>(c.assets[key].path);
                    c.assets[key].url = kv.second["url"].as<std::string>(c.assets[key].url);
                }
            }
        }
    }
```

- [ ] **Step 8: Update saveConfig path writes**

In `src/store.cpp`, inside `saveConfig`, after `root["fetch_max_bytes"] = c.fetchMaxBytes;`, replace:

```cpp
    root["template_dir"] = c.templateDir;
    root["output_dir"] = c.outputDir;
```

with:

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

Delete the later duplicate line:

```cpp
    root["asset_dir"] = c.assetDir;
```

- [ ] **Step 9: Update saveConfig asset writes**

In `src/store.cpp`, replace:

```cpp
    root["assets"]["paths"] = YAML::Node(YAML::NodeType::Map);
    for (const auto& kv : c.assetPaths) {
        root["assets"]["paths"][kv.first] = kv.second;
    }
    root["assets"]["urls"] = YAML::Node(YAML::NodeType::Map);
    for (const auto& kv : c.assetUrls) {
        root["assets"]["urls"][kv.first] = kv.second;
    }
```

with:

```cpp
    root["assets"] = YAML::Node(YAML::NodeType::Map);
    for (const auto& kv : c.assets) {
        root["assets"][kv.first]["path"] = kv.second.path;
        root["assets"][kv.first]["url"] = kv.second.url;
    }
```

- [ ] **Step 10: Update test helper makeConfig**

In `tests/subcli_tests.cpp`, update `makeConfig()` to set new fields. Add after `config.templateDir = ...;`:

```cpp
    config.profileDir = (root / "profiles").string();
    config.dataDir = (root / "Testing" / "data").string();
    config.cacheDir = (root / "Testing" / "cache").string();
    config.assetDir = (root / "Testing" / "assets").string();
    config.stateDir = (root / "Testing" / "state").string();
    config.logDir = (root / "Testing" / "logs").string();
    config.subFile = (root / "Testing" / "sub.yaml").string();
```

- [ ] **Step 11: Temporarily fix compile errors from old asset maps in tests**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
grep -R "assetPaths\|assetUrls" -n tests src include | head -100
```

For every test line like:

```cpp
config.assetPaths["key"] = "/path";
config.assetUrls["key"] = "url";
```

change it to:

```cpp
config.assets["key"].path = "/path";
config.assets["key"].url = "url";
```

For every test line like:

```cpp
cfg.assetPaths["key"].clear();
cfg.assetPaths.erase("key");
```

change it to:

```cpp
cfg.assets["key"].path.clear();
cfg.assets.erase("key");
```

- [ ] **Step 12: Run targeted store tests**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cmake --build build -j
ctest --test-dir build -R subcli_tests --output-on-failure
```

Expected: build likely still fails in production files that use `assetPaths` or `assetUrls`. Continue to Task 6.

- [ ] **Step 13: Commit model/store schema changes**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
git add include/subcli/models.hpp src/store.cpp tests/subcli_tests.cpp
git commit -m "feat: persist config-driven path schema"
```

---

## Task 6: Update Asset Consumers, Config Service, Registry, and Diagnostics

**Files:**
- Modify: `src/assets.cpp`
- Modify: `src/capability_matrix.cpp`
- Modify: `src/exporter_singbox.cpp`
- Modify: `src/config_service.cpp`
- Modify: `include/subcli/config_service.hpp`
- Modify: `src/registry.cpp`
- Modify: `include/subcli/diagnostic_service.hpp`
- Modify: `src/diagnostic_service.cpp`
- Modify: `tests/subcli_tests.cpp`

- [ ] **Step 1: Update assets.cpp to use new asset entries**

In `src/assets.cpp`, replace the `buildAssetRecords` implementation with:

```cpp
std::vector<AssetRecord> buildAssetRecords(const AppConfig& config) {
    std::vector<AssetRecord> out;
    out.reserve(config.assets.size());
    for (const auto& kv : config.assets) {
        AssetRecord record;
        record.key = kv.first;
        record.path = kv.second.path;
        record.url = kv.second.url;
        out.push_back(record);
    }
    std::sort(out.begin(), out.end(), [](const AssetRecord& a, const AssetRecord& b) { return a.key < b.key; });
    return out;
}
```

If `std::sort` is not currently available in `src/assets.cpp`, add:

```cpp
#include <algorithm>
```

- [ ] **Step 2: Update capability matrix asset lookup**

In `src/capability_matrix.cpp`, replace code using `config.assetPaths.find(assetKey)` with:

```cpp
            const auto it = config.assets.find(assetKey);
            if ((it == config.assets.end() || it->second.path.empty()) && seenRequiredAssets.insert(assetKey).second) {
```

Keep the existing warning/finding body after that line.

- [ ] **Step 3: Update sing-box exporter asset lookup**

In `src/exporter_singbox.cpp`, replace the helper that reads `config.assetPaths` with:

```cpp
std::string assetPathForKey(const AppConfig& config, const std::string& key) {
    const auto it = config.assets.find(key);
    return it == config.assets.end() ? "" : it->second.path;
}
```

- [ ] **Step 4: Update ConfigServiceOptions**

In `include/subcli/config_service.hpp`, replace `ConfigServiceOptions` with:

```cpp
struct ConfigServiceOptions {
    std::string defaultDataDir;
    std::string defaultCacheDir;
    std::string defaultOutputDir;
    std::string defaultAssetDir;
    std::string defaultTemplateDir;
    std::string defaultProfileDir;
    std::string defaultStateDir;
    std::string defaultLogDir;
    std::string defaultSubFile;
};
```

- [ ] **Step 5: Update config service list output**

In `src/config_service.cpp`, inside `listConfigValues`, add path entries after `profile_path`:

```cpp
    entries.push_back({"data_dir", cfg.dataDir});
    entries.push_back({"cache_dir", cfg.cacheDir});
```

Add after `template_dir`:

```cpp
    entries.push_back({"profile_dir", cfg.profileDir});
```

Add after `output_dir`:

```cpp
    entries.push_back({"state_dir", cfg.stateDir});
    entries.push_back({"log_dir", cfg.logDir});
    entries.push_back({"sub_file", cfg.subFile});
```

Replace asset loops:

```cpp
    for (const auto& kv : cfg.assetPaths) {
        entries.push_back({"assets.paths." + kv.first, kv.second});
    }
    for (const auto& kv : cfg.assetUrls) {
        entries.push_back({"assets.urls." + kv.first, kv.second});
    }
```

with:

```cpp
    for (const auto& kv : cfg.assets) {
        entries.push_back({"assets." + kv.first + ".path", kv.second.path});
        entries.push_back({"assets." + kv.first + ".url", kv.second.url});
    }
```

- [ ] **Step 6: Update config service get/set/remove path keys**

In `src/config_service.cpp`, add these scalar get checks after `profile_path`:

```cpp
    if (key == "data_dir") { value = cfg.dataDir; return true; }
    if (key == "cache_dir") { value = cfg.cacheDir; return true; }
    if (key == "profile_dir") { value = cfg.profileDir; return true; }
    if (key == "state_dir") { value = cfg.stateDir; return true; }
    if (key == "log_dir") { value = cfg.logDir; return true; }
    if (key == "sub_file") { value = cfg.subFile; return true; }
```

Add these scalar set checks near existing path setters:

```cpp
    if (key == "data_dir") { cfg.dataDir = resolvePath(value); return true; }
    if (key == "cache_dir") { cfg.cacheDir = resolvePath(value); return true; }
    if (key == "profile_dir") { cfg.profileDir = resolvePath(value); return true; }
    if (key == "state_dir") { cfg.stateDir = resolvePath(value); return true; }
    if (key == "log_dir") { cfg.logDir = resolvePath(value); return true; }
    if (key == "sub_file") { cfg.subFile = resolvePath(value); return true; }
```

Add these remove checks near existing path removals:

```cpp
    if (key == "data_dir") { cfg.dataDir = options.defaultDataDir; return true; }
    if (key == "cache_dir") { cfg.cacheDir = options.defaultCacheDir; return true; }
    if (key == "profile_dir") { cfg.profileDir = options.defaultProfileDir; return true; }
    if (key == "state_dir") { cfg.stateDir = options.defaultStateDir; return true; }
    if (key == "log_dir") { cfg.logDir = options.defaultLogDir; return true; }
    if (key == "sub_file") { cfg.subFile = options.defaultSubFile; return true; }
```

- [ ] **Step 7: Replace asset key parser in config service**

In `src/config_service.cpp`, add this helper in the anonymous namespace:

```cpp
bool parseAssetNestedKey(const std::string& key, std::string& assetKey, std::string& field) {
    constexpr const char* prefix = "assets.";
    if (key.rfind(prefix, 0) != 0) {
        return false;
    }
    const std::string rest = key.substr(std::char_traits<char>::length(prefix));
    const auto dot = rest.rfind('.');
    if (dot == std::string::npos) {
        return false;
    }
    assetKey = rest.substr(0, dot);
    field = rest.substr(dot + 1);
    return !assetKey.empty() && (field == "path" || field == "url");
}
```

Then replace every `assets.paths.` / `assets.urls.` branch in `getConfigValue`, `setConfigValue`, and `removeConfigValue` with this pattern:

```cpp
    std::string assetKey;
    std::string assetField;
    if (parseAssetNestedKey(key, assetKey, assetField)) {
        if (assetField == "path") {
            auto it = cfg.assets.find(assetKey);
            if (it != cfg.assets.end()) {
                value = it->second.path;
                return true;
            }
        } else {
            auto it = cfg.assets.find(assetKey);
            if (it != cfg.assets.end()) {
                value = it->second.url;
                return true;
            }
        }
        error = "unsupported key in v1";
        return false;
    }
```

For `setConfigValue`, use:

```cpp
    std::string assetKey;
    std::string assetField;
    if (parseAssetNestedKey(key, assetKey, assetField)) {
        if (assetField == "path") {
            cfg.assets[assetKey].path = resolvePath(value);
        } else {
            cfg.assets[assetKey].url = value;
        }
        return true;
    }
```

For `removeConfigValue`, use:

```cpp
    std::string assetKey;
    std::string assetField;
    if (parseAssetNestedKey(key, assetKey, assetField)) {
        auto it = cfg.assets.find(assetKey);
        if (it != cfg.assets.end()) {
            if (assetField == "path") {
                it->second.path.clear();
            } else {
                it->second.url.clear();
            }
        }
        return true;
    }
```

- [ ] **Step 8: Update registry config keys and command surface**

In `src/registry.cpp`, remove command descriptors for `init` and `workspace`.

Add config key descriptors for:

```cpp
    {"data_dir", ConfigValueType::Path, "Runtime data directory."},
    {"cache_dir", ConfigValueType::Path, "Runtime cache directory."},
    {"profile_dir", ConfigValueType::Path, "Profile directory root."},
    {"state_dir", ConfigValueType::Path, "Runtime state directory."},
    {"log_dir", ConfigValueType::Path, "Runtime log directory."},
    {"sub_file", ConfigValueType::Path, "Subscription store file."},
```

Replace old asset prefix descriptors:

```cpp
    {"assets.paths.", ConfigValueType::Prefix, "Asset file path namespace."},
    {"assets.urls.", ConfigValueType::Prefix, "Asset URL namespace."},
```

with:

```cpp
    {"assets.", ConfigValueType::Prefix, "Asset path/url namespace, for example assets.mihomo.geoip.path."},
```

- [ ] **Step 9: Update diagnostic service signature**

In `include/subcli/diagnostic_service.hpp`, replace:

```cpp
DiagnosticReport buildDiagnosticReport(
    const AppConfig& config,
    const std::vector<Subscription>& subscriptions,
    const std::string& workspaceRoot
);
```

with:

```cpp
DiagnosticReport buildDiagnosticReport(
    const AppConfig& config,
    const std::vector<Subscription>& subscriptions,
    const EnvironmentPaths& paths
);
```

Add this include to the header:

```cpp
#include "subcli/environment.hpp"
```

- [ ] **Step 10: Update diagnostic implementation**

In `src/diagnostic_service.cpp`, update function signature the same way and replace:

```cpp
    report.findings.push_back({"workspace.resolved", DiagnosticSeverity::Info, "workspace", "workspace path resolved", workspaceRoot});
```

with:

```cpp
    report.findings.push_back({"config.resolved", DiagnosticSeverity::Info, "config", "config file resolved", paths.configPath});
    report.findings.push_back({"path.data_dir", DiagnosticSeverity::Info, "data_dir", "data directory configured", config.dataDir});
    report.findings.push_back({"path.cache_dir", DiagnosticSeverity::Info, "cache_dir", "cache directory configured", config.cacheDir});
    report.findings.push_back({"path.asset_dir", DiagnosticSeverity::Info, "asset_dir", "asset directory configured", config.assetDir});
    report.findings.push_back({"path.template_dir", DiagnosticSeverity::Info, "template_dir", "template directory configured", config.templateDir});
    report.findings.push_back({"path.profile_dir", DiagnosticSeverity::Info, "profile_dir", "profile directory configured", config.profileDir});
    report.findings.push_back({"path.output_dir", DiagnosticSeverity::Info, "output_dir", "output directory configured", config.outputDir});
    report.findings.push_back({"path.state_dir", DiagnosticSeverity::Info, "state_dir", "state directory configured", config.stateDir});
    report.findings.push_back({"path.log_dir", DiagnosticSeverity::Info, "log_dir", "log directory configured", config.logDir});
    report.findings.push_back({"path.sub_file", DiagnosticSeverity::Info, "sub_file", "subscription file configured", config.subFile});
```

Replace asset diagnostic loops with:

```cpp
    for (const auto& kv : config.assets) {
        if (!kv.second.path.empty()) {
            report.findings.push_back({"asset.path.configured", DiagnosticSeverity::Info, kv.first, "asset path configured", kv.second.path});
        }
        if (!kv.second.url.empty()) {
            report.findings.push_back({"asset.url.configured", DiagnosticSeverity::Info, kv.first, "asset url configured", kv.second.url});
        }
    }
```

- [ ] **Step 11: Update diagnostic unit test**

In `tests/subcli_tests.cpp`, find `testDiagnosticServiceReportsConfigAndTargets`. Replace old call:

```cpp
    const auto report = subcli::buildDiagnosticReport(cfg, {enabled, disabled}, "/tmp/ws");
```

with:

```cpp
    subcli::EnvironmentPaths paths;
    paths.configPath = "/tmp/subcli/config.yaml";
    paths.configDir = "/tmp/subcli";
    const auto report = subcli::buildDiagnosticReport(cfg, {enabled, disabled}, paths);
```

Replace variables `hasWorkspace` with `hasConfigResolved` and replace check for `workspace.resolved` with `config.resolved`:

```cpp
    bool hasConfigResolved = false;
```

and:

```cpp
        if (finding.code == "config.resolved") {
            hasConfigResolved = true;
```

and final requirement:

```cpp
    require(hasConfigResolved, "diagnostic report should include resolved config path");
```

- [ ] **Step 12: Update remaining asset references**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
grep -R "assetPaths\|assetUrls" -n include src tests
```

Expected output must be empty. If output exists, replace each use with `assets[...].path` or `assets[...].url`.

- [ ] **Step 13: Run build and subcli_tests**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cmake --build build -j
ctest --test-dir build -R subcli_tests --output-on-failure
```

Expected: may still fail in `src/main.cpp` because main still uses old workspace environment. Continue to Task 7.

- [ ] **Step 14: Commit asset/config service/diagnostic migration**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
git add src/assets.cpp src/capability_matrix.cpp src/exporter_singbox.cpp include/subcli/config_service.hpp src/config_service.cpp src/registry.cpp include/subcli/diagnostic_service.hpp src/diagnostic_service.cpp tests/subcli_tests.cpp
git commit -m "refactor: use config-driven assets and diagnostics"
```

---

## Task 7: Rewire `src/main.cpp` Startup and Remove Workspace Commands

**Files:**
- Modify: `src/main.cpp`
- Modify: `tests/subcli_tests.cpp`

- [ ] **Step 1: Remove workspace include from main**

In `src/main.cpp`, delete:

```cpp
#include "subcli/workspace.hpp"
```

Add:

```cpp
#include "subcli/config_defaults.hpp"
```

- [ ] **Step 2: Update RuntimePaths struct**

In `src/main.cpp`, replace the `RuntimePaths` struct with:

```cpp
struct RuntimePaths {
    std::filesystem::path root;
    std::filesystem::path dataDir;
    std::filesystem::path cacheDir;
    std::filesystem::path stateDir;
    std::filesystem::path logDir;
    std::filesystem::path configDir;
    std::filesystem::path subPath;
    std::filesystem::path configPath;
    std::filesystem::path templateDir;
    std::filesystem::path profileDir;
    std::filesystem::path outputDir;
    std::filesystem::path assetDir;
};
```

- [ ] **Step 3: Replace global environment type**

Replace:

```cpp
EnvironmentResolveResult gEnvResult;
```

with:

```cpp
EnvironmentInfo gEnvInfo;
```

- [ ] **Step 4: Delete old workspace helper functions from main**

Delete these functions entirely from `src/main.cpp`:

```cpp
std::string readPersistedDefaultWorkspace()
std::filesystem::path platformDefaultRoot(...)
std::string platformDefaultWorkspaceRoot(...)
```

Also delete any helper whose only purpose is workspace marker discovery or persisted default workspace behavior.

- [ ] **Step 5: Add RuntimePaths builder from config**

Add this function near existing path helpers in `src/main.cpp`:

```cpp
RuntimePaths buildRuntimePathsFromConfig(const EnvironmentInfo& env, const AppConfig& cfg) {
    RuntimePaths paths;
    paths.root = env.configDir;
    paths.configDir = env.configDir;
    paths.configPath = env.configPath;
    paths.dataDir = cfg.dataDir;
    paths.cacheDir = cfg.cacheDir;
    paths.stateDir = cfg.stateDir;
    paths.logDir = cfg.logDir;
    paths.outputDir = cfg.outputDir;
    paths.templateDir = cfg.templateDir;
    paths.profileDir = cfg.profileDir;
    paths.assetDir = cfg.assetDir;
    paths.subPath = cfg.subFile;
    return paths;
}
```

- [ ] **Step 6: Update ensureDefaults directories**

Find `ensureDefaults()` in `src/main.cpp`. Make sure it creates these directories:

```cpp
std::filesystem::create_directories(gPaths.configDir);
std::filesystem::create_directories(gPaths.dataDir);
std::filesystem::create_directories(gPaths.cacheDir);
std::filesystem::create_directories(gPaths.stateDir);
std::filesystem::create_directories(gPaths.logDir);
std::filesystem::create_directories(gPaths.outputDir);
std::filesystem::create_directories(gPaths.assetDir);
```

Keep existing subscription/config initialization behavior, but write subscriptions to `gPaths.subPath` and config to `gPaths.configPath`.

- [ ] **Step 7: Replace applyConfigDefaults behavior**

In `src/main.cpp`, simplify `applyConfigDefaults(AppConfig& c)` so it no longer guesses workspace paths. Replace the path default logic at the start of the function with:

```cpp
    if (c.dataDir.empty()) c.dataDir = gPaths.dataDir.string();
    if (c.cacheDir.empty()) c.cacheDir = gPaths.cacheDir.string();
    if (c.assetDir.empty()) c.assetDir = gPaths.assetDir.string();
    if (c.templateDir.empty()) c.templateDir = gPaths.templateDir.string();
    if (c.profileDir.empty()) c.profileDir = gPaths.profileDir.string();
    if (c.outputDir.empty()) c.outputDir = gPaths.outputDir.string();
    if (c.stateDir.empty()) c.stateDir = gPaths.stateDir.string();
    if (c.logDir.empty()) c.logDir = gPaths.logDir.string();
    if (c.subFile.empty()) c.subFile = gPaths.subPath.string();
```

Remove logic that rewrites `./templates`, `./outputs`, or `./assets` based on workspace root because path resolution now happens in `resolveConfigPathsInPlace()`.

- [ ] **Step 8: Update built-in asset default fill in main**

If `applyConfigDefaults()` still builds default assets with local maps, replace that with:

```cpp
    if (c.assets.empty()) {
        populateDefaultAssets(c);
        resolveConfigPathsInPlace(c, gPaths.configDir);
    }
```

If `populateDefaultAssets` would overwrite user assets, do not call it when `c.assets` is non-empty.

- [ ] **Step 9: Remove `doInitCommand`**

Delete the whole `int doInitCommand(const std::vector<std::string>& args)` function from `src/main.cpp`.

- [ ] **Step 10: Remove workspace doctor structures and command**

Delete these from `src/main.cpp`:

```cpp
struct WorkspaceDoctorFinding
std::vector<WorkspaceDoctorFinding> buildWorkspaceDoctorFindings(...)
void printWorkspaceUsage()
void printWorkspaceSubcommandUsage(...)
int doWorkspaceCommand(...)
```

Also delete helper functions only used by `doWorkspaceCommand`.

- [ ] **Step 11: Add config init command handler**

Add this helper in `src/main.cpp` near config command helpers:

```cpp
int doConfigInitCommand(const std::vector<std::string>& args) {
    CLI::App parser("config init");
    parser.set_help_flag("");
    parser.allow_extras(false);
    bool portable = false;
    bool fhs = false;
    std::string path;
    parser.add_flag("--portable", portable);
    parser.add_flag("--fhs", fhs);
    parser.add_option("--path", path);

    if (!parseCliArgs(parser, args)) {
        std::cerr << "Usage:\n  subcli config init [--portable|--fhs] [--path PATH]\n";
        return ExitUsage;
    }
    if (portable && fhs) {
        std::cerr << "config init: choose only one of --portable or --fhs\n";
        return ExitUsage;
    }

    AppConfig config = fhs ? makeFhsDefaultConfig() : makePortableDefaultConfig();
    std::filesystem::path outputPath;
    if (!path.empty()) {
        outputPath = normalizeAbsolutePath(path);
    } else if (fhs) {
        outputPath = platformFhsConfigPath(detectPlatformKind());
    } else {
        outputPath = normalizeAbsolutePath(std::filesystem::current_path() / "config.yaml");
    }

    std::error_code ec;
    std::filesystem::create_directories(outputPath.parent_path(), ec);
    if (ec) {
        std::cerr << "config init failed to create directory: " << outputPath.parent_path().string() << "\n";
        return ExitError;
    }
    if (std::filesystem::exists(outputPath, ec) && !ec) {
        std::cerr << "config init refused to overwrite existing file: " << outputPath.string() << "\n";
        return ExitError;
    }

    saveConfig(outputPath.string(), config);
    std::cout << "config initialized: " << outputPath.string() << "\n";
    return ExitOk;
}
```

- [ ] **Step 12: Route `config init` before legacy config command**

In `legacyConfigCommand`, before normal config parsing, add:

```cpp
    if (args.size() >= 2 && args[1] == "init") {
        return doConfigInitCommand(args);
    }
```

- [ ] **Step 13: Update root help**

In `printRootUsage()`, remove references to workspace and init. Make sure the command list includes this exact wording:

```cpp
              << "  config    Init/list/get/set/remove application settings.\n"
              << "  doctor    Check config, directories, templates, assets, and core paths.\n"
```

Make sure global usage shows:

```cpp
              << "  subcli [--config PATH] <command> [args...]\n"
              << "\n"
              << "Global Options:\n"
              << "  --config PATH  Use a specific config.yaml for this invocation.\n"
```

- [ ] **Step 14: Update main global CLI parsing**

In `main`, replace `cliWorkspace` with:

```cpp
        std::string configOption;
```

Replace:

```cpp
        cli.add_option("--workspace", cliWorkspace, "Use a workspace for this invocation");
```

with:

```cpp
        cli.add_option("--config", configOption, "Use a config.yaml for this invocation");
```

Remove `init` and `workspace` from command member list.

- [ ] **Step 15: Replace environment resolution in main**

Replace the block that builds `EnvironmentResolveInput`, calls `resolveEnvironment`, and maps `gEnvResult.paths` into `gPaths` with:

```cpp
        const std::string envConfig = []() {
            const char* raw = std::getenv("SUBCLI_CONFIG");
            return raw && *raw ? std::string(raw) : std::string();
        }();

        EnvironmentDetectInput input;
        input.argv0 = argv0;
        input.configOption = configOption;
        input.envConfig = envConfig;
        input.cwd = std::filesystem::current_path().string();
        input.platform = detectPlatformKind();

        gEnvInfo = detectEnvironment(input);
        if (!gEnvInfo.ok) {
            std::cerr << "environment detection failed: " << gEnvInfo.error << "\n";
            return ExitError;
        }

        AppConfig startupConfig = loadConfig(gEnvInfo.configPath.string());
        if (!std::filesystem::exists(gEnvInfo.configPath)) {
            startupConfig = gEnvInfo.mode == ConfigMode::FHS ? makeFhsDefaultConfig() : makePortableDefaultConfig();
            std::error_code ec;
            std::filesystem::create_directories(gEnvInfo.configDir, ec);
            if (!ec) {
                saveConfig(gEnvInfo.configPath.string(), startupConfig);
            }
        }
        resolveConfigPathsInPlace(startupConfig, gEnvInfo.configDir);
        gPaths = buildRuntimePathsFromConfig(gEnvInfo, startupConfig);
```

- [ ] **Step 16: Update command dispatch**

Remove this dispatch block:

```cpp
        if (cmd == "init") {
            return doInitCommand(buildTail("init", extra));
        }
```

Remove this dispatch block:

```cpp
        if (cmd == "workspace") {
            return doWorkspaceCommand(buildTail("workspace", extra));
        }
```

Update calls to use `gPaths` converted to `EnvironmentPaths` if the command wrappers still expect `EnvironmentPaths`. Add this helper:

```cpp
EnvironmentPaths toEnvironmentPaths(const RuntimePaths& paths) {
    EnvironmentPaths out;
    out.root = paths.root.string();
    out.configDir = paths.configDir.string();
    out.dataDir = paths.dataDir.string();
    out.cacheDir = paths.cacheDir.string();
    out.stateDir = paths.stateDir.string();
    out.outputDir = paths.outputDir.string();
    out.templateDir = paths.templateDir.string();
    out.profileDir = paths.profileDir.string();
    out.logDir = paths.logDir.string();
    out.assetDir = paths.assetDir.string();
    out.subPath = paths.subPath.string();
    out.configPath = paths.configPath.string();
    return out;
}
```

Then replace `gEnvResult.paths` arguments with `toEnvironmentPaths(gPaths)`.

- [ ] **Step 17: Update doctor command call**

In `legacyDoctorCommand`, replace diagnostic report call with:

```cpp
    const auto report = buildDiagnosticReport(cfg, subs, toEnvironmentPaths(gPaths));
```

- [ ] **Step 18: Update config service defaults options**

Where `ConfigServiceOptions options` is built in `legacyConfigCommand`, include:

```cpp
    options.defaultDataDir = gPaths.dataDir.string();
    options.defaultCacheDir = gPaths.cacheDir.string();
    options.defaultAssetDir = gPaths.assetDir.string();
    options.defaultTemplateDir = gPaths.templateDir.string();
    options.defaultProfileDir = gPaths.profileDir.string();
    options.defaultOutputDir = gPaths.outputDir.string();
    options.defaultStateDir = gPaths.stateDir.string();
    options.defaultLogDir = gPaths.logDir.string();
    options.defaultSubFile = gPaths.subPath.string();
```

- [ ] **Step 19: Run grep for workspace references in main**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
grep -n "workspace\|Workspace\|SUBCLI_WORKSPACE\|--workspace" src/main.cpp
```

Expected: no output. If output appears in user-facing help or old functions, remove it. If output appears in migration docs embedded in comments, remove it.

- [ ] **Step 20: Run build**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cmake --build build -j
```

Expected: compile errors only in files not yet updated for deleted workspace source or tests. Continue to Task 8.

- [ ] **Step 21: Commit main rewiring**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
git add src/main.cpp tests/subcli_tests.cpp
git commit -m "refactor: run from config file instead of workspace"
```

---

## Task 8: Delete Workspace Source and Fix Build Wiring

**Files:**
- Delete: `include/subcli/workspace.hpp`
- Delete: `src/workspace.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/platform_boundary_scan.cmake`
- Modify: any source file still including `workspace.hpp`

- [ ] **Step 1: Remove workspace files from CMake**

In `CMakeLists.txt`, remove this line from `SUBCLI_SOURCES`:

```cmake
    src/workspace.cpp
```

- [ ] **Step 2: Delete workspace source files**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
git rm include/subcli/workspace.hpp src/workspace.cpp
```

- [ ] **Step 3: Remove workspace includes**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
grep -R "workspace.hpp" -n include src tests
```

Expected: no output. If output exists, remove the include line.

- [ ] **Step 4: Remove workspace command registry expectations**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
grep -R "workspace\|--workspace\|SUBCLI_WORKSPACE\|subcli init" -n include src tests | head -100
```

For every source/test hit, remove or rewrite it to config-driven behavior. Keep only docs hits for migration notes until docs task.

- [ ] **Step 5: Build**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cmake -S . -B build
cmake --build build -j
```

Expected: build succeeds or fails on test assertions only. If compile fails, fix the exact symbol named by the compiler.

- [ ] **Step 6: Run unit tests**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
ctest --test-dir build -R subcli_tests --output-on-failure
```

Expected: unit tests pass or fail only due to stale assertions mentioning workspace. Update those assertions to config-driven terminology.

- [ ] **Step 7: Commit workspace removal**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
git add CMakeLists.txt tests/platform_boundary_scan.cmake include src tests
git commit -m "refactor: remove workspace implementation"
```

---

## Task 9: Implement CMake/CPack FHS and Portable Packaging

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `packaging/systemd/subcli-daemon.service`
- Modify: `tests/stability_package_journey.cmake`

- [ ] **Step 1: Update systemd service to packaged binary path**

Replace `packaging/systemd/subcli-daemon.service` with:

```ini
[Unit]
Description=subcli daemon background sync service
Documentation=file:///usr/share/doc/subcli/README.subcli.md
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/subcli daemon run --target all --interval 1800 --update-assets
Restart=on-failure
RestartSec=5
Nice=10
IOSchedulingClass=idle

[Install]
WantedBy=multi-user.target
```

- [ ] **Step 2: Add install layout options to CMake**

In `CMakeLists.txt`, before install rules, add:

```cmake
option(SUBCLI_PORTABLE "Install a portable layout rooted at the package directory" OFF)
```

- [ ] **Step 3: Replace install rules**

Replace current install block:

```cmake
install(TARGETS subcli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY templates/ DESTINATION ${CMAKE_INSTALL_DATADIR}/subcli/templates)
install(DIRECTORY profiles/ DESTINATION ${CMAKE_INSTALL_DATADIR}/subcli/profiles)
install(FILES packaging/systemd/subcli-daemon.service DESTINATION ${CMAKE_INSTALL_DATADIR}/subcli/systemd)
install(FILES README.subcli.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
```

with:

```cmake
if(SUBCLI_PORTABLE)
    install(TARGETS subcli RUNTIME DESTINATION .)
    install(DIRECTORY templates/ DESTINATION templates)
    install(DIRECTORY profiles/ DESTINATION profiles)
    install(FILES packaging/portable/config.yaml DESTINATION .)
    install(FILES README.subcli.md DESTINATION doc)
else()
    install(TARGETS subcli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY templates/ DESTINATION ${CMAKE_INSTALL_DATADIR}/subcli/templates)
    install(DIRECTORY profiles/ DESTINATION ${CMAKE_INSTALL_DATADIR}/subcli/profiles)
    install(FILES packaging/fhs/config.yaml DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/subcli)
    install(FILES packaging/systemd/subcli-daemon.service DESTINATION lib/systemd/system)
    install(FILES README.subcli.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()
```

- [ ] **Step 4: Replace CPack settings**

At the bottom of `CMakeLists.txt`, replace CPack settings with:

```cmake
set(CPACK_PACKAGE_NAME "subcli")
set(CPACK_PACKAGE_VENDOR "subcli")
set(CPACK_PACKAGE_CONTACT "subcli developers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Subscription to proxy client config tool")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "subcli-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

if(SUBCLI_PORTABLE)
    set(CPACK_GENERATOR "TGZ;ZIP")
else()
    if(UNIX AND NOT APPLE)
        set(CPACK_GENERATOR "DEB;RPM;TGZ;ZIP")
    else()
        set(CPACK_GENERATOR "TGZ;ZIP")
    endif()
endif()

set(CPACK_DEBIAN_PACKAGE_NAME "subcli")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "subcli developers")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcurl4, libc6 (>= 2.17), libgcc-s1, libstdc++6")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "curl")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "mihomo, sing-box, xray")
set(CPACK_DEBIAN_PACKAGE_CONFFILES "/etc/subcli/config.yaml")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/packaging/deb/postinst"
    "${CMAKE_SOURCE_DIR}/packaging/deb/prerm"
    "${CMAKE_SOURCE_DIR}/packaging/deb/postrm"
)

set(CPACK_RPM_PACKAGE_NAME "subcli")
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
set(CPACK_RPM_PACKAGE_REQUIRES "libcurl")
set(CPACK_RPM_USER_FILELIST "%config(noreplace) /etc/subcli/config.yaml")
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/rpm/postinstall.sh")
set(CPACK_RPM_PRE_UNINSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/rpm/preremove.sh")
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/rpm/postuninstall.sh")

include(CPack)
```

- [ ] **Step 5: Configure FHS packaging build**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cmake -S . -B build -DSUBCLI_PORTABLE=OFF
cmake --build build -j
cmake --build build --target package
```

Expected: Linux build produces at least `.deb`, `.rpm`, `.tar.gz`, and `.zip` if RPM tooling is available. If RPM tool is missing, CPack may fail; in that case continue but record the exact missing tool in final verification.

- [ ] **Step 6: Verify DEB conffile metadata**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
dpkg-deb --info build/subcli-*.deb | grep -A2 conffiles
dpkg-deb --contents build/subcli-*.deb | grep '/etc/subcli/config.yaml'
```

Expected: output mentions `conffiles` and `/etc/subcli/config.yaml`.

- [ ] **Step 7: Configure portable packaging build**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cmake -S . -B build-portable -DSUBCLI_PORTABLE=ON
cmake --build build-portable -j
cmake --build build-portable --target package
```

Expected: `build-portable/` contains `.tar.gz` and `.zip` with `config.yaml` next to `subcli`.

- [ ] **Step 8: Verify portable archive layout**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cmake -E tar tf build-portable/subcli-*.tar.gz | head -40
```

Expected: output includes:

```text
subcli
config.yaml
templates/mihomo_base.yaml
profiles/bypass-cn.json
```

The archive may include a top-level package directory prefix; that is acceptable if `subcli` and `config.yaml` are in the same extracted directory.

- [ ] **Step 9: Commit CPack packaging**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
git add CMakeLists.txt packaging/systemd/subcli-daemon.service tests/stability_package_journey.cmake
git commit -m "packaging: build fhs and portable packages with cpack"
```

---

## Task 10: Update CLI Smoke and Stability Journey Tests

**Files:**
- Modify: `tests/cli_basic_smoke.cmake`
- Modify: `tests/stability_runner.cpp`
- Modify: `tests/stability_package_journey.cmake`

- [ ] **Step 1: Update CLI basic smoke environment**

In `tests/cli_basic_smoke.cmake`, replace workspace setup with a config path. Add near the top after `_smoke_root` is set:

```cmake
set(_config_path "${_smoke_root}/config.yaml")
```

Add `SUBCLI_CONFIG=${_config_path}` to `_subcli_env`.

- [ ] **Step 2: Replace workspace init commands in CLI smoke**

Replace any command like:

```cmake
run_subcli("workspace init" workspace init "${TEST_WORK_DIR}")
```

with:

```cmake
run_subcli("config init" config init --portable --path "${_config_path}")
```

Remove checks for `workspace status`.

- [ ] **Step 3: Update CLI smoke path references**

Where the smoke test validates built-in profile path, replace workspace profile path:

```cmake
"${TEST_WORK_DIR}/profiles/bypass-cn.json"
```

with source profile path:

```cmake
"${SOURCE_DIR}/profiles/bypass-cn.json"
```

If the smoke test expects seeded templates/profiles in a workspace, remove that expectation because package data now lives in `template_dir` and `profile_dir` from config.

- [ ] **Step 4: Update stability runner setup**

In `tests/stability_runner.cpp`, replace:

```cpp
    const fs::path workspace = options.testRoot / "subcli 稳定性 workspace with space";
```

with:

```cpp
    const fs::path configDir = options.testRoot / "subcli 稳定性 config with space";
    const fs::path configPath = configDir / "config.yaml";
```

Create the config directory:

```cpp
    fs::create_directories(configDir);
```

- [ ] **Step 5: Make runner pass --config to subcli**

Change `runSubcli()` in `tests/stability_runner.cpp` to prepend `--config configPath`. Modify `Options`:

```cpp
struct Options {
    std::string mode;
    fs::path subcliBin;
    fs::path sourceDir;
    fs::path testRoot;
    fs::path configPath;
};
```

After parsing options in `main`, set:

```cpp
        Options options = parseOptions(argc, argv);
        options.configPath = options.testRoot / "subcli-config" / "config.yaml";
```

Then in `runSubcli`, build args like this:

```cpp
subcli::ProcessRunResult runSubcli(const Options& options, const std::vector<std::string>& args, int timeoutSec = 20) {
    std::vector<std::string> fullArgs;
    fullArgs.push_back("--config");
    fullArgs.push_back(options.configPath.string());
    fullArgs.insert(fullArgs.end(), args.begin(), args.end());
    auto result = subcli::runProcessCapture(options.subcliBin.string(), fullArgs, timeoutSec);
    if (!result.started) {
        fail("failed to start subcli: " + result.error);
    }
    return result;
}
```

- [ ] **Step 6: Replace init/workspace journey with config init**

In `runJourney`, replace:

```cpp
    runOk(options, "init", {"init", workspace.string()});
    const std::string status = runOk(options, "workspace status", {"workspace", "status", "--json"});
    requireContains(status, workspace.string(), "workspace status");
```

with:

```cpp
    runOk(options, "config init", {"config", "init", "--portable", "--path", options.configPath.string()});
    const std::string configList = runOk(options, "config list", {"config", "list"});
    requireContains(configList, "asset_dir", "config list");
    requireContains(configList, "template_dir", "config list");
```

Remove all later workspace override checks.

- [ ] **Step 7: Update profile validate path in runner**

Replace:

```cpp
runOk(options, "profile validate", {"profile", "validate", (workspace / "profiles" / "bypass-cn.json").string()});
```

with:

```cpp
runOk(options, "profile validate", {"profile", "validate", (options.sourceDir / "profiles" / "bypass-cn.json").string()});
```

- [ ] **Step 8: Run smoke and stability tests**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cmake -S . -B build
cmake --build build -j
ctest --test-dir build -R "subcli_cli_basic|subcli_stability_user_journey" --output-on-failure
```

Expected: both pass.

- [ ] **Step 9: Commit updated smoke tests**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
git add tests/cli_basic_smoke.cmake tests/stability_runner.cpp tests/stability_package_journey.cmake
git commit -m "test: exercise config-driven first use"
```

---

## Task 11: Update Documentation and Command Metadata Tests

**Files:**
- Modify: `README.md`
- Modify: `README.subcli.md`
- Modify: `docs/config-file.md`
- Modify: `docs/cli-glossary.zh-CN.md`
- Modify: `docs/en_us/README.md`
- Modify: `docs/en_us/README.subcli.md`
- Modify: `docs/en_us/config-file.md`
- Modify: `docs/zh_cn/README.md`
- Modify: `docs/zh_cn/README.subcli.md`
- Modify: `docs/zh_cn/config-file.md`
- Modify: `tests/subcli_tests.cpp`

- [ ] **Step 1: Replace quick start in README.md**

In `README.md`, replace `subcli init` quick start with:

```bash
subcli config init --portable
subcli doctor --json
subcli sub add --name airport-a --url https://example/sub
subcli sub update
subcli export all --profile bypass-cn
```

Add a paragraph:

```markdown
Linux DEB/RPM packages install `/etc/subcli/config.yaml` as a package-manager managed configuration file. Portable TGZ/ZIP packages place `config.yaml` next to the `subcli` executable and use relative paths by default.
```

- [ ] **Step 2: Remove workspace docs from README.subcli.md**

In `README.subcli.md`, replace every workflow using `subcli init`, `subcli workspace init`, `subcli workspace status`, `subcli workspace use`, or `--workspace` with `subcli config init --portable` or `subcli --config PATH`.

Use this replacement first-use block:

```markdown
## First Use

Portable package:

```bash
subcli config init --portable
subcli doctor
subcli sub add --name airport-a --url https://example/sub
subcli sub update
subcli export mihomo
```

FHS package (`apt`/`rpm`):

```bash
subcli doctor
subcli sub add --name airport-a --url https://example/sub
subcli sub update
subcli export mihomo
```

DEB/RPM packages install `/etc/subcli/config.yaml` as the package-managed config file. Runtime data, assets, outputs, state, and subscriptions are stored under the paths declared in that config.
```

- [ ] **Step 3: Replace docs/config-file.md**

Rewrite `docs/config-file.md` around these sections:

```markdown
# subcli config.yaml Reference

`config.yaml` is the single source of truth for paths and runtime behavior.

## Path Resolution

All relative paths are resolved from the directory containing `config.yaml`.

## FHS Layout

- Config: `/etc/subcli/config.yaml`
- Data: `/var/lib/subcli`
- Assets: `/var/lib/subcli/assets`
- Cache: `/var/cache/subcli`
- Logs: `/var/log/subcli`
- Templates: `/usr/share/subcli/templates`
- Profiles: `/usr/share/subcli/profiles`

## Portable Layout

- Config: `<app>/config.yaml`
- Data: `<app>/data`
- Assets: `<app>/data/assets`
- Cache: `<app>/cache`
- Logs: `<app>/logs`
- Templates: `<app>/templates`
- Profiles: `<app>/profiles`

## Package Manager Behavior

DEB/RPM packages mark `/etc/subcli/config.yaml` as package-managed configuration. Assets are runtime data and are removed only on package purge/final erase.
```

Then include the full schema from the design spec.

- [ ] **Step 4: Update Chinese glossary**

In `docs/cli-glossary.zh-CN.md`, replace workspace sections with:

```markdown
## 第一次使用

便携包：

```bash
subcli config init --portable
subcli doctor
subcli sub add --name my-sub --url <你的订阅链接>
subcli sub update
subcli export mihomo
```

apt/rpm 安装包：

```bash
subcli doctor
subcli sub add --name my-sub --url <你的订阅链接>
subcli sub update
subcli export mihomo
```

apt/rpm 安装包会把 `/etc/subcli/config.yaml` 标记为包管理器配置文件。资源文件由 `asset_dir` 决定，默认在 `/var/lib/subcli/assets`。
```

- [ ] **Step 5: Mirror docs into en_us and zh_cn directories**

Copy the updated main docs into localized directories:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cp README.md docs/en_us/README.md
cp README.subcli.md docs/en_us/README.subcli.md
cp docs/config-file.md docs/en_us/config-file.md
cp README.md docs/zh_cn/README.md
cp README.subcli.md docs/zh_cn/README.subcli.md
cp docs/config-file.md docs/zh_cn/config-file.md
```

If you maintain true translations later, do that in a separate docs-only pass. For this implementation, keep content consistent.

- [ ] **Step 6: Add doc assertions**

In `tests/subcli_tests.cpp`, update doc tests so they assert:

```cpp
require(readme.find("subcli config init --portable") != std::string::npos, "README should show config init first-use flow");
require(configDocs.find("/etc/subcli/config.yaml") != std::string::npos, "config docs should mention FHS config path");
require(configDocs.find("/var/lib/subcli/assets") != std::string::npos, "config docs should mention default FHS asset path");
require(configDocs.find("<app>/config.yaml") != std::string::npos, "config docs should mention portable config path");
```

Remove doc assertions that require workspace wording.

- [ ] **Step 7: Run docs-related unit tests**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cmake --build build -j
ctest --test-dir build -R subcli_tests --output-on-failure
```

Expected: `subcli_tests` passes.

- [ ] **Step 8: Commit docs**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
git add README.md README.subcli.md docs/config-file.md docs/cli-glossary.zh-CN.md docs/en_us docs/zh_cn tests/subcli_tests.cpp
git commit -m "docs: document config-driven layouts"
```

---

## Task 12: Final Full Verification

**Files:**
- No planned source edits unless verification finds a bug.

- [ ] **Step 1: Search for removed workspace surface**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
grep -R "subcli init\|workspace init\|workspace status\|--workspace\|SUBCLI_WORKSPACE\|workspace migrate" -n README.md README.subcli.md docs include src tests CMakeLists.txt
```

Expected: no output. If output exists, rewrite it to config-driven wording.

- [ ] **Step 2: Search for removed workspace source files**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
find include src -name '*workspace*' -print
```

Expected: no output.

- [ ] **Step 3: Search for old asset schema**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
grep -R "assetPaths\|assetUrls\|assets.paths\|assets.urls" -n include src tests docs README.md README.subcli.md
```

Expected: no output. If docs mention old schema for migration, rewrite it to say old schema is read for compatibility but new saved config uses `assets.<key>.path` and `assets.<key>.url`.

- [ ] **Step 4: Build normal FHS mode**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cmake -S . -B build -DSUBCLI_PORTABLE=OFF
cmake --build build -j
```

Expected: build succeeds.

- [ ] **Step 5: Build packages**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cmake --build build --target package
```

Expected on Linux with package tooling installed: `.deb`, `.rpm`, `.tar.gz`, `.zip` are created in `build/`. If RPM tools are absent, `.deb` and archives must still be validated by configuring generator fallback or documenting the missing tool.

- [ ] **Step 6: Run all CTests**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 7: Build portable packages**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
cmake -S . -B build-portable -DSUBCLI_PORTABLE=ON
cmake --build build-portable -j
cmake --build build-portable --target package
```

Expected: `build-portable/` contains `.tar.gz` and `.zip` with `subcli` and `config.yaml` in the same extracted directory.

- [ ] **Step 8: Verify DEB conffile**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
dpkg-deb --info build/subcli-*.deb | grep -A3 conffiles
dpkg-deb --contents build/subcli-*.deb | grep -E '/etc/subcli/config.yaml|/usr/bin/subcli|/usr/share/subcli/templates|/usr/share/subcli/profiles|/lib/systemd/system/subcli-daemon.service'
```

Expected: `/etc/subcli/config.yaml` appears in conffiles and contents.

- [ ] **Step 9: Verify portable archive layout**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
rm -rf build-portable/extract-check
mkdir -p build-portable/extract-check
cmake -E tar xzf build-portable/subcli-*.tar.gz --directory build-portable/extract-check
find build-portable/extract-check -maxdepth 3 -type f | sort | grep -E 'subcli$|config.yaml|templates/mihomo_base.yaml|profiles/bypass-cn.json'
```

Expected: output contains the executable, config, template, and profile files.

- [ ] **Step 10: Final status check**

Run:

```bash
cd /home/lisztzy/prj/subcli-cpp/.worktrees/config-driven-architecture
git status --short
git log --oneline --decorate -8
```

Expected: no uncommitted files. Recent log shows this implementation's commits.

---

## Self-Review Checklist

- Spec coverage:
  - CMake/CPack publishing is covered in Task 9 and Task 12.
  - Package-managed config file is covered by `packaging/fhs/config.yaml`, CPack conffile settings, and verification commands.
  - Purge removal of config/assets/data is covered by DEB/RPM scripts and verification steps.
  - Workspace removal is covered in Tasks 7 and 8.
  - Config-driven paths/assets/profile/templates are covered in Tasks 4, 5, and 6.
  - FHS and portable layouts are covered in Tasks 1, 9, and 10.
- Placeholder scan: no task relies on unspecified code generation; every new file has explicit content or exact replacement instructions.
- Type consistency: `ConfigMode`, `EnvironmentInfo`, `EnvironmentDetectInput`, `EnvironmentPaths`, `AppConfig::AssetEntry`, `makeFhsDefaultConfig`, `makePortableDefaultConfig`, and `resolveConfigPathsInPlace` names are consistent across tasks.
