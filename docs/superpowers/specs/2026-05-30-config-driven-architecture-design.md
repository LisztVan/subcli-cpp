# 配置驱动架构重构设计

**日期**: 2026-05-30
**版本**: subcli v0.3.0（架构变更）
**目标**: 删除 workspace 机制，改为配置驱动的单环境模型；统一三端路径解析；CPack 原生打包支持 DEB/RPM/TGZ/ZIP。

---

## 1. 核心设计原则

1. **配置文件是唯一真相来源** — `config.yaml` 定义所有路径和行为，程序不做隐式路径猜测。
2. **FHS 与便携模式自动切换** — 检测运行环境，FHS 安装用绝对路径，便携包用相对路径。
3. **三端一致** — Linux/macOS/Windows 使用相同的路径解析逻辑，仅默认值不同。
4. **包管理器友好** — conffile 标记、purge 清理、systemd 集成均由 CMake/CPack 声明。

---

## 2. 路径解析模型

### 2.1 运行模式检测

程序启动时按以下顺序判断运行模式：

```
1. 环境变量 SUBCLI_CONFIG 指定 config.yaml 路径 → 直接使用
2. 可执行文件同级目录存在 config.yaml → 便携模式（portable）
3. /etc/subcli/config.yaml 存在（Linux/macOS）→ FHS 模式
4. 均不存在 → 使用平台默认路径生成默认配置
```

判断逻辑伪代码：

```cpp
ConfigMode detectMode(const std::string& argv0) {
    // 1. 环境变量优先
    if (auto env = getenv("SUBCLI_CONFIG"); env && exists(env))
        return {Mode::Explicit, env};

    // 2. 便携模式：exe 同级有 config.yaml
    auto exeDir = parentDir(realpath(argv0));
    if (exists(exeDir / "config.yaml"))
        return {Mode::Portable, exeDir / "config.yaml"};

    // 3. FHS 模式
    auto fhsConfig = platformFhsConfigPath(); // /etc/subcli/config.yaml
    if (exists(fhsConfig))
        return {Mode::FHS, fhsConfig};

    // 4. 平台默认（首次运行）
    return {Mode::PlatformDefault, platformDefaultConfigPath()};
}
```

### 2.2 路径解析规则

config.yaml 中所有路径字段支持：
- **绝对路径**：直接使用
- **相对路径**：相对于 app 主程序所在目录（appDir）解析；config.yaml 由 `subcli config init` 显式创建，普通命令不隐式创建配置。

```yaml
# FHS 模式下的 /etc/subcli/config.yaml
asset_dir: /var/lib/subcli/assets        # 绝对
template_dir: /usr/share/subcli/templates # 绝对
output_dir: /var/lib/subcli/outputs       # 绝对

# 便携模式下的 ./config.yaml（与 subcli 同目录）
asset_dir: ./data/assets                  # 相对于 appDir
template_dir: ./templates                 # 相对于 appDir
output_dir: ./outputs                     # 相对于 appDir
```

### 2.3 三端默认路径

当无 config.yaml 存在时（首次运行），程序生成默认配置：

| 平台 | config.yaml 位置 | 模式 |
|------|-----------------|------|
| Linux (FHS) | `/etc/subcli/config.yaml` | FHS |
| Linux (便携) | `<exe-dir>/config.yaml` | Portable |
| macOS (便携) | `<exe-dir>/config.yaml` | Portable |
| macOS (Homebrew) | `/usr/local/etc/subcli/config.yaml` | FHS |
| Windows | `<exe-dir>/config.yaml` | Portable |

FHS 模式默认值：

```yaml
data_dir: /var/lib/subcli
cache_dir: /var/cache/subcli
asset_dir: /var/lib/subcli/assets
template_dir: /usr/share/subcli/templates
profile_dir: /usr/share/subcli/profiles
output_dir: /var/lib/subcli/outputs
state_dir: /var/lib/subcli/state
log_dir: /var/log/subcli
sub_file: /var/lib/subcli/sub.yaml
```

便携模式默认值：

```yaml
data_dir: ./data
cache_dir: ./cache
asset_dir: ./data/assets
template_dir: ./templates
profile_dir: ./profiles
output_dir: ./outputs
state_dir: ./data/state
log_dir: ./logs
sub_file: ./data/sub.yaml
```

---

## 3. config.yaml 完整 Schema

```yaml
# === 路径配置 ===
# 所有相对路径相对于本文件所在目录解析
data_dir: /var/lib/subcli           # 运行时数据根目录
cache_dir: /var/cache/subcli        # 可清除缓存
asset_dir: /var/lib/subcli/assets   # geo/rule 资源文件
template_dir: /usr/share/subcli/templates  # 导出模板
profile_dir: /usr/share/subcli/profiles    # profile 定义
output_dir: /var/lib/subcli/outputs        # 导出输出
state_dir: /var/lib/subcli/state           # 运行时状态（pid 等）
log_dir: /var/log/subcli                   # 日志目录
sub_file: /var/lib/subcli/sub.yaml         # 订阅记录文件

# === 导出行为 ===
profile: bypass-cn                  # 默认 profile 名称
# profile_path: /path/to/custom.json  # 自定义 profile 文件（覆盖 profile）
tun: false                          # TUN 模式开关
log_level: info                     # 日志级别: debug/info/warn/error

# === 网络 ===
parallelism: 3                      # 并发订阅更新数
timeout: 15                         # 单次请求超时（秒）
retry: 2                            # 失败重试次数
fetch_max_bytes: 5242880            # 单次下载最大字节数

# === 核心二进制路径 ===
core_paths:
  mihomo: ""                        # 留空则从 PATH 查找
  sing_box: ""
  xray: ""

# === 资源定义 ===
assets:
  mihomo.geosite:
    path: mihomo/geosite.dat        # 相对于 asset_dir
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

# === 模板路径覆盖 ===
templates:
  mihomo:
    normal: mihomo_base.yaml        # 相对于 template_dir
    tun: mihomo_tun.yaml
  sing-box:
    normal: singbox_base.json
    tun: singbox_tun.json
  xray:
    normal: xray_base.json
    tun: xray_tun.json

# === 节点管理 ===
node_management:
  dedupe: true
  rename_template: "{name}"
  include_regex: ""
  exclude_regex: ""
  sort_by: region,name

# === 分组规则 ===
grouping:
  region_rules:
    HK: "(?i)(hong kong|hongkong|hk|香港)"
    JP: "(?i)(japan|jp|tokyo|osaka|日本)"
    SG: "(?i)(singapore|sg|新加坡)"
    TW: "(?i)(taiwan|tw|台灣|台湾)"
    US: "(?i)(united states|usa|us|america|美国)"
```

---

## 4. 删除 Workspace 的影响范围

### 4.1 删除的文件

| 文件 | 原因 |
|------|------|
| `include/subcli/workspace.hpp` | workspace API 定义 |
| `src/workspace.cpp` | workspace 实现（init/use/unset/migrate/status/doctor） |

### 4.2 删除的命令

| 命令 | 替代 |
|------|------|
| `subcli init [DIR]` | 不再需要；首次运行自动按模式生成默认配置 |
| `subcli workspace init/use/unset/migrate/status/doctor` | 全部删除 |
| `--workspace DIR` 全局选项 | 删除；用 `SUBCLI_CONFIG` 环境变量或 `--config PATH` 替代 |

### 4.3 新增的命令/选项

| 命令/选项 | 用途 |
|-----------|------|
| `--config PATH` | 全局选项，指定 config.yaml 路径 |
| `subcli config init` | 在当前目录生成默认 config.yaml（便携模式初始化） |
| `subcli doctor` | 保留，改为检查 config.yaml 中声明的所有路径是否可用 |

### 4.4 修改的文件

| 文件 | 变更 |
|------|------|
| `include/subcli/environment.hpp` | 重写为 `ConfigMode` 检测 + 路径解析 |
| `src/environment.cpp` | 实现新的模式检测和路径解析 |
| `src/main.cpp` | 删除 workspace 相关代码；改用新路径解析；删除 `doInitCommand`/`doWorkspaceCommand` |
| `src/store.cpp` | config 加载适配新 schema（assets 改为 map-of-objects） |
| `include/subcli/models.hpp` | `AppConfig` 结构体适配新字段 |
| `src/config_service.cpp` | 适配新 config schema |
| `src/diagnostic_service.cpp` | doctor 检查适配新路径 |
| `src/registry.cpp` | 删除 workspace 命令注册；更新 config key 注册 |
| `tests/subcli_tests.cpp` | 删除 workspace 测试；新增模式检测测试 |
| `tests/cli_basic_smoke.cmake` | 适配新初始化流程 |
| `tests/stability_runner.cpp` | 适配新初始化流程 |

---

## 5. 打包架构

### 5.1 CPack 多格式输出

```cmake
# === DEB ===
set(CPACK_DEBIAN_PACKAGE_NAME "subcli")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcurl4, libc6 (>= 2.17)")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "curl")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "mihomo, sing-box, xray")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "subcli developers")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Subscription to proxy client config tool")
set(CPACK_DEBIAN_PACKAGE_CONFFILES "/etc/subcli/config.yaml")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/packaging/deb/postinst"
    "${CMAKE_SOURCE_DIR}/packaging/deb/prerm"
    "${CMAKE_SOURCE_DIR}/packaging/deb/postrm"
)

# === RPM ===
set(CPACK_RPM_PACKAGE_NAME "subcli")
set(CPACK_RPM_PACKAGE_REQUIRES "libcurl")
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
set(CPACK_RPM_USER_FILELIST "%config(noreplace) /etc/subcli/config.yaml")

# === TGZ/ZIP（便携包）===
# 便携包使用相对路径布局，不含 /etc 或 /var
set(CPACK_GENERATOR "DEB;RPM;TGZ;ZIP")
```

### 5.2 FHS 安装布局（DEB/RPM）

```
/etc/subcli/
  config.yaml                    ← conffile，apt/rpm 管理
/usr/bin/
  subcli                         ← 二进制
/usr/share/subcli/
  templates/                     ← 出厂模板（只读）
    mihomo_base.yaml
    mihomo_tun.yaml
    singbox_base.json
    singbox_tun.json
    xray_base.json
    xray_tun.json
  profiles/                      ← 出厂 profile（只读）
    bypass-cn.json
    direct.json
    global.json
/usr/share/doc/subcli/
  README.subcli.md
  copyright
/lib/systemd/system/
  subcli-daemon.service          ← systemd 服务
/var/lib/subcli/                 ← 运行时数据（安装时创建）
  assets/
  outputs/
  state/
  sub.yaml
/var/cache/subcli/               ← 缓存（安装时创建）
/var/log/subcli/                 ← 日志（安装时创建）
```

### 5.3 便携包布局（TGZ/ZIP）

```
subcli-0.3.0-Linux-x86_64/
  subcli                         ← 二进制
  config.yaml                    ← 默认配置（相对路径）
  templates/
    mihomo_base.yaml
    ...
  profiles/
    bypass-cn.json
    ...
  data/                          ← 运行时数据
    assets/
    state/
    sub.yaml
  outputs/
  cache/
  logs/
```

### 5.4 CMake install() 规则

```cmake
# 条件安装：FHS vs Portable 由 CMake 选项控制
option(SUBCLI_PORTABLE "Build portable package layout" OFF)

if(SUBCLI_PORTABLE)
    # 便携模式：所有文件相对于安装前缀
    install(TARGETS subcli RUNTIME DESTINATION .)
    install(DIRECTORY templates/ DESTINATION templates)
    install(DIRECTORY profiles/ DESTINATION profiles)
    install(FILES packaging/portable/config.yaml DESTINATION .)
    # data/cache/outputs/logs 目录由程序首次运行时创建
else()
    # FHS 模式
    install(TARGETS subcli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY templates/ DESTINATION ${CMAKE_INSTALL_DATADIR}/subcli/templates)
    install(DIRECTORY profiles/ DESTINATION ${CMAKE_INSTALL_DATADIR}/subcli/profiles)
    install(FILES packaging/fhs/config.yaml DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/subcli)
    install(FILES packaging/systemd/subcli-daemon.service
            DESTINATION lib/systemd/system)
    install(FILES README.subcli.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()
```

### 5.5 卸载行为

#### DEB purge（完全删除）

`packaging/deb/postrm`:
```bash
#!/bin/sh
set -e
case "$1" in
    purge)
        # 删除运行时数据和资源
        rm -rf /var/lib/subcli
        rm -rf /var/cache/subcli
        rm -rf /var/log/subcli
        # 删除配置目录（dpkg 已处理 conffile 本身）
        rm -rf /etc/subcli
        ;;
    remove)
        # 普通卸载：保留配置和数据
        ;;
esac
```

#### DEB prerm（卸载前）

`packaging/deb/prerm`:
```bash
#!/bin/sh
set -e
case "$1" in
    remove|upgrade|deconfigure)
        if [ -d /run/systemd/system ]; then
            systemctl stop subcli-daemon.service 2>/dev/null || true
            systemctl disable subcli-daemon.service 2>/dev/null || true
        fi
        ;;
esac
```

#### DEB postinst（安装后）

`packaging/deb/postinst`:
```bash
#!/bin/sh
set -e
case "$1" in
    configure)
        # 创建运行时目录
        install -d -m 755 /var/lib/subcli/assets
        install -d -m 755 /var/lib/subcli/outputs
        install -d -m 755 /var/lib/subcli/state
        install -d -m 755 /var/cache/subcli
        install -d -m 755 /var/log/subcli
        # 初始化空订阅文件
        [ -f /var/lib/subcli/sub.yaml ] || echo "subscriptions: []" > /var/lib/subcli/sub.yaml
        # 启用 systemd 服务（不自动启动）
        if [ -d /run/systemd/system ]; then
            systemctl daemon-reload
            systemctl enable subcli-daemon.service 2>/dev/null || true
        fi
        ;;
esac
```

#### RPM

RPM 使用 `%config(noreplace)` 标记 config.yaml，卸载时通过 `%postun` scriptlet 执行类似清理。

### 5.5 卸载行为

Downloaded assets (geosite/geoip/rule-set) are runtime data. Three platforms support unified cleanup:

- `subcli purge --assets` removes configured asset files and their `.meta.json` metadata.
- `subcli purge --all --yes` removes assets, cache, outputs, state, logs, and config.
- DEB `purge` (`dpkg --purge`) deletes `/var/lib/subcli`, `/var/cache/subcli`, `/var/log/subcli`, and `/etc/subcli`.
- RPM final-erase scriptlet removes runtime data directories.
- Windows NSIS uninstaller removes app-local data/cache/logs/outputs/config.
- Portable ZIP/TGZ: run `subcli purge --all --yes` or manually delete the extracted directory.

---

## 6. 程序启动流程

```
main(argc, argv)
  │
  ├─ 解析全局选项: --config PATH, --help, --version
  │
  ├─ detectConfigMode(argv[0], configOption)
  │    → 返回 {mode, configPath}
  │
  ├─ loadConfig(configPath)
  │    → 解析 YAML，填充 AppConfig
  │    → 所有相对路径相对于 configPath 父目录解析
  │
  ├─ resolveAllPaths(config, configDir)
  │    → 将 config 中的相对路径转为绝对路径
  │    → 验证关键目录存在（不存在则创建 data/cache/output/state/log）
  │
  ├─ 分发子命令
  │    doctor / sub / config / export / asset / ...
  │
  └─ 退出
```

### 6.1 首次运行行为

当检测不到任何 config.yaml 时：

1. **FHS 环境**（Linux 且 uid==0 或有 /etc 写权限）：
   - 生成 `/etc/subcli/config.yaml`（FHS 默认值）
   - 创建 `/var/lib/subcli/`, `/var/cache/subcli/`, `/var/log/subcli/`
   - 提示：`Generated default config at /etc/subcli/config.yaml`

2. **便携环境**（非 root 或 Windows/macOS）：
   - 在 exe 同级目录生成 `config.yaml`（便携默认值）
   - 提示：`Generated default config at <exe-dir>/config.yaml`

3. **用户级 fallback**（Linux 非 root 且无 /etc 写权限）：
   - 生成 `~/.config/subcli/config.yaml`
   - 数据目录使用 `~/.local/share/subcli/`
   - 提示：`Generated default config at ~/.config/subcli/config.yaml`

---

## 7. 新的 Environment 模块

### 7.1 头文件 `include/subcli/environment.hpp`

```cpp
#pragma once
#include <string>
#include <filesystem>

namespace subcli {

enum class ConfigMode {
    Explicit,        // SUBCLI_CONFIG 或 --config 指定
    Portable,        // exe 同级 config.yaml
    FHS,             // /etc/subcli/config.yaml
    UserLocal,       // ~/.config/subcli/config.yaml
};

struct EnvironmentInfo {
    ConfigMode mode;
    std::filesystem::path configPath;   // config.yaml 绝对路径
    std::filesystem::path configDir;    // config.yaml 所在目录
};

// 检测运行模式并定位 config.yaml
EnvironmentInfo detectEnvironment(
    const std::string& argv0,
    const std::string& configOption  // --config 参数，可为空
);

// 将相对路径相对于 baseDir 解析为绝对路径
std::filesystem::path resolvePath(
    const std::filesystem::path& baseDir,
    const std::string& path
);

} // namespace subcli
```

### 7.2 路径解析实现要点

```cpp
fs::path resolvePath(const fs::path& baseDir, const std::string& path) {
    if (path.empty()) return {};
    fs::path p(path);
    if (p.is_absolute()) return p.lexically_normal();
    return (baseDir / p).lexically_normal();
}
```

所有 `AppConfig` 中的路径字段在加载后统一调用 `resolvePath(configDir, field)` 转为绝对路径。

---

## 8. AppConfig 结构体变更

```cpp
struct AppConfig {
    // === 路径（加载后已解析为绝对路径）===
    std::string dataDir;
    std::string cacheDir;
    std::string assetDir;
    std::string templateDir;
    std::string profileDir;
    std::string outputDir;
    std::string stateDir;
    std::string logDir;
    std::string subFile;

    // === 导出行为 ===
    std::string profile = "bypass-cn";
    std::string profilePath;
    bool tun = false;
    std::string logLevel = "info";

    // === 网络 ===
    int parallelism = 3;
    int timeout = 15;
    int retry = 2;
    int fetchMaxBytes = 5242880;

    // === 核心路径 ===
    std::string mihomoPath;
    std::string singBoxPath;
    std::string xrayPath;

    // === 资源定义 ===
    struct AssetEntry {
        std::string path;  // 相对于 assetDir
        std::string url;
    };
    std::map<std::string, AssetEntry> assets;

    // === 模板路径覆盖 ===
    std::map<std::string, std::string> templateNormal;
    std::map<std::string, std::string> templateTun;

    // === 节点管理 ===
    bool dedupeNodes = true;
    std::string renameTemplate = "{name}";
    std::string includeRegex;
    std::string excludeRegex;
    std::string sortBy = "region,name";

    // === 分组 ===
    std::map<std::string, std::string> regionRules;

    // 删除: workspace 相关字段
    // 删除: EnvironmentSource / persistedWorkspace 等
};
```

---

## 9. 命令变更总结

### 保留命令（行为不变或微调）

| 命令 | 变更 |
|------|------|
| `subcli doctor` | 检查 config.yaml 中所有路径是否存在/可写 |
| `subcli sub add/update/list/remove/...` | 不变，sub_file 路径从 config 读取 |
| `subcli config list/get/set/remove` | 操作 config.yaml |
| `subcli export` | 不变 |
| `subcli asset list/status/validate/update` | 不变，asset_dir 从 config 读取 |
| `subcli profile list/get/validate/explain` | 不变，profile_dir 从 config 读取 |
| `subcli template list/get/set/reset/validate` | 不变 |
| `subcli check/run/daemon/status/stop/restart/logs` | 不变 |
| `subcli completion` | 不变 |

### 新增命令

| 命令 | 用途 |
|------|------|
| `subcli config init [--portable]` | 在当前目录或系统位置生成默认 config.yaml |

### 删除命令

| 命令 | 原因 |
|------|------|
| `subcli init` | workspace 概念删除 |
| `subcli workspace *` | workspace 概念删除 |

### 全局选项变更

| 选项 | 变更 |
|------|------|
| `--workspace DIR` | **删除** |
| `--config PATH` | **新增**，指定 config.yaml 路径 |

---

## 10. 测试策略

### 10.1 单元测试

- `detectEnvironment()` 在各种文件布局下的模式检测
- `resolvePath()` 相对/绝对路径解析
- config.yaml 加载与默认值填充
- asset path 解析（相对于 asset_dir）

### 10.2 集成测试

- FHS 模式冒烟测试：模拟 /etc/subcli/ 布局
- 便携模式冒烟测试：exe 同级 config.yaml
- `config init` 生成正确默认配置
- `doctor` 检测缺失目录并报告
- `asset update` 下载到 config 指定的 asset_dir
- `export` 输出到 config 指定的 output_dir

### 10.3 打包测试

- DEB 安装后 `/etc/subcli/config.yaml` 存在且标记为 conffile
- DEB purge 后 `/var/lib/subcli/` 和 `/etc/subcli/` 被清除
- TGZ 解压后便携模式可直接运行
- RPM 安装/卸载行为正确

---

## 11. 迁移说明

对于现有用户（从 workspace 模式迁移）：

1. 旧 workspace 中的 `config.yaml` 内容可直接复制到新位置
2. 旧 workspace 中的 `sub.yaml` 复制到 config 中 `sub_file` 指定的位置
3. 旧 workspace 中的 `assets/` 复制到 config 中 `asset_dir` 指定的位置
4. 旧 workspace 中的 `outputs/` 复制到 config 中 `output_dir` 指定的位置

不提供自动迁移工具（workspace 功能删除后无法检测旧数据位置）。README 中给出手动迁移步骤。

---

## 12. 实现优先级

1. **Phase 1**: 新 environment 模块 + config 加载重写 + 删除 workspace 代码
2. **Phase 2**: CMake install 规则重写 + CPack DEB/RPM 配置
3. **Phase 3**: 打包脚本（postinst/prerm/postrm）+ conffile 标记
4. **Phase 4**: 测试适配 + 新集成测试
5. **Phase 5**: 文档更新 + CI 适配
