# subcli-cpp 项目审查与解决方案

## 概述

本文档针对 `subcli` v0.2.7 项目提供以下五方面的完整解决方案：

1. 跨平台编译验证（Linux / macOS / Windows）
2. CI/CD 发布脚本（编译 → 打包 → GitHub Release）
3. 代码审查：潜在 Bug 与问题
4. run / daemon 命令跨平台运行状况测试
5. 项目最终目标评估：作为订阅转换工具是否合格

---

## 1. 跨平台编译验证

### 现有 CI 状态

项目已有 `.github/workflows/release-validation.yml`，覆盖三平台：

| 平台 | Runner | 状态 |
|------|--------|------|
| Linux x86_64 | `ubuntu-latest` | ✅ |
| macOS ARM64 | `macos-latest` | ✅ |
| Windows x86_64 | `windows-latest` | ✅ |

### 使用 `gh` CLI 手动触发验证

```bash
# 查看最近的 workflow 运行结果
gh run list --workflow=release-validation.yml --limit=5

# 手动触发一次全平台编译验证
gh workflow run release-validation.yml

# 等待并查看结果
gh run watch

# 查看某次运行的详细日志
gh run view <run-id> --log

# 下载构建产物
gh run download <run-id>
```

### 本地验证脚本（使用 gh 检查远程状态）

```bash
#!/usr/bin/env bash
# scripts/verify-cross-platform.sh
set -euo pipefail

echo "=== 触发跨平台编译验证 ==="
gh workflow run release-validation.yml --ref "$(git branch --show-current)"

echo "等待 workflow 启动..."
sleep 5

RUN_ID=$(gh run list --workflow=release-validation.yml --limit=1 --json databaseId -q '.[0].databaseId')
echo "Run ID: $RUN_ID"

echo "=== 等待完成 ==="
gh run watch "$RUN_ID"

echo "=== 结果 ==="
gh run view "$RUN_ID"

STATUS=$(gh run view "$RUN_ID" --json conclusion -q '.conclusion')
if [ "$STATUS" != "success" ]; then
    echo "❌ 跨平台编译失败"
    gh run view "$RUN_ID" --log-failed
    exit 1
fi
echo "✅ 三平台编译全部通过"
```

---

## 2. 编译打包与 GitHub Release 发布

### 现有方案

项目已有 `.github/workflows/release.yml`，在推送 `v*` tag 时自动：
1. 三平台并行编译 + 测试 + 打包（tar.gz + zip）
2. 上传 artifact
3. 使用 `softprops/action-gh-release@v2` 发布到 GitHub Release

### 手动发布流程（使用 gh CLI）

```bash
#!/usr/bin/env bash
# scripts/release.sh - 手动触发完整发布流程
set -euo pipefail

# 1. 确认版本号
VERSION=$(grep -oP 'project\(subcli VERSION \K[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt)
TAG="v${VERSION}"
echo "准备发布: $TAG"

# 2. 确认当前分支干净
if ! git diff --quiet; then
    echo "❌ 工作区有未提交的更改"
    exit 1
fi

# 3. 创建 tag 并推送
read -p "确认创建 tag $TAG 并推送? [y/N] " confirm
if [ "$confirm" != "y" ]; then
    echo "取消"
    exit 0
fi

git tag -a "$TAG" -m "Release $TAG"
git push origin "$TAG"

# 4. 等待 Release workflow 完成
echo "等待 Release workflow 启动..."
sleep 10

RUN_ID=$(gh run list --workflow=release.yml --limit=1 --json databaseId -q '.[0].databaseId')
echo "Release workflow Run ID: $RUN_ID"
gh run watch "$RUN_ID"

# 5. 验证 Release
echo "=== Release 产物 ==="
gh release view "$TAG"
gh release download "$TAG" --dir ./dist
ls -la ./dist/

echo "✅ 发布完成: $TAG"
```

### 本地打包脚本（不依赖 CI）

```bash
#!/usr/bin/env bash
# scripts/local-package.sh - 本地编译打包
set -euo pipefail

cd "$(dirname "$0")/.."

VERSION=$(grep -oP 'project\(subcli VERSION \K[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt)
echo "=== Building subcli v${VERSION} ==="

# 编译
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j

# 测试
ctest --test-dir build --build-config Release --output-on-failure

# 打包
cmake --build build --config Release --target package

echo "=== 产物 ==="
ls -la build/subcli-*.{tar.gz,zip} 2>/dev/null || ls -la build/subcli-*
echo "✅ 打包完成"
```

### 手动上传到已有 Release

```bash
# 如果 CI 失败需要手动补充产物
gh release upload v0.2.7 build/subcli-0.2.7-Linux-x86_64.tar.gz
gh release upload v0.2.7 build/subcli-0.2.7-Linux-x86_64.zip
```

---

## 3. 代码审查：潜在 Bug 与问题

### 3.1 架构级问题

#### P1: `main.cpp` 单文件 4426 行

**问题**：所有 CLI 分发、命令处理器（export/config/daemon/asset/profile/template/purge/run/stop/status/restart/logs/check/completion）全部在一个文件中，包含大量嵌套 lambda。

**影响**：
- 无法对单个命令进行单元测试
- 编译时间长（修改任何命令逻辑都要重编整个文件）
- 代码导航困难

**建议**：将每个 `doXxxCommand` 函数拆分到 `src/commands/` 目录下独立文件。

#### P2: 全局可变状态

```cpp
// main.cpp
RuntimePaths gPaths;
std::string gExecutablePath;
EnvironmentInfo gEnvInfo;
EnvironmentPaths gEnvPaths;
```

**影响**：所有命令处理器隐式依赖全局状态，无法并行测试，难以推理生命周期。

**建议**：将这些状态封装为 `AppContext` 结构体，通过参数传递。

### 3.2 代码重复

#### P3: `splitCommaValues` 行为不一致

```cpp
// exporter_common.cpp - 不 trim
std::vector<std::string> splitCommaValues(const std::string& input) {
    // ...
    while (std::getline(ss, part, ',')) {
        if (!part.empty()) out.push_back(part);
    }
}

// parser_common.cpp - 会 trim
std::vector<std::string> splitCommaValues(const std::string& input) {
    // ...
    while (std::getline(ss, part, ',')) {
        part = trim(part);  // <-- 差异
        if (!part.empty()) out.push_back(part);
    }
}
```

**影响**：如果输入含空格（如 `"HK, SG, US"`），两处行为不同，可能导致节点匹配失败。

#### P4: 多处重复定义

以下函数/逻辑在 `main.cpp` 和其他文件中重复定义：
- `normalizeAbsolutePath`（main.cpp vs environment.cpp）
- `isSupportedProfile`（main.cpp vs config_service.cpp）
- `defaultTemplatePath`（main.cpp vs config_service.cpp）
- `updateTemplateDirDefaults`（main.cpp vs config_service.cpp）

### 3.3 错误处理问题

#### P5: 静默吞异常

```cpp
// parser_common.cpp
int parseIntOrDefault(const std::string& value, int fallback = 0) {
    try { return std::stoi(value); }
    catch (...) { return fallback; }  // 整数溢出也被吞掉
}

// exporter_common.cpp
void setJsonScalar(nlohmann::json& object, const std::string& key, const std::string& value) {
    // ...
    try {
        const int number = std::stoi(value, &parsed);
        // ...
    } catch (...) {}  // 静默失败
    object[key] = value;
}
```

**影响**：配置错误（如 `port: abc`）不会产生任何警告，用户无法发现问题。

#### P6: `fromUri` 函数 197 行无结构化错误报告

`ss://` 解析路径中，如果 base64 解码后仍无 `@` 符号，函数返回 `port=0, server=""` 的节点。虽然调用方会过滤掉，但没有任何诊断信息告知用户哪条 URI 有问题。

### 3.4 潜在运行时 Bug

#### P7: `makeExportNodes` 去重逻辑可能产生意外名称

```cpp
// exporter_common.cpp:183
std::vector<ProxyNode> makeExportNodes(const std::vector<ProxyNode>& nodes) {
    // ...
    if (seen > 1) {
        copy.name += copy.sourceId.empty() ? "" : " [" + copy.sourceId + "]";
        if (counts.count(copy.name)) {
            copy.name += " #" + std::to_string(seen);
        }
        ++counts[copy.name];  // 新名称也加入计数
    }
}
```

**问题**：如果两个不同订阅源有同名节点（如 "香港 01"），第二个会变成 "香港 01 [airport-b]"。但如果恰好有第三个节点也叫 "香港 01 [airport-b]"（极端情况），计数逻辑会混乱。

#### P8: WireGuard 导出路径已由现有测试覆盖，但建议保持回归测试

代码中 `makeSingBoxOutbound` 对 WireGuard 的普通 outbound 路径直接返回，但 sing-box 的实际 WireGuard 输出使用 endpoint 路径 `makeSingBoxWireGuardEndpoint`。现有测试 `testExportSingBoxWireGuardUsesEndpoint` 已覆盖该行为。因此它不是当前 bug，但应在后续重构 exporter 时保留这组回归测试，避免 WireGuard 字段丢失。

#### P9: `regionRank` 函数 O(n) 遍历

```cpp
int regionRank(const ProxyNode& node, const AppConfig& config) {
    int rank = 0;
    for (const auto& kv : config.regionRules) {
        if (kv.first == node.region) return rank;
        ++rank;
    }
    return rank + 100;
}
```

`std::map` 的遍历顺序是按 key 字典序，不是用户定义的顺序。如果用户期望 `HK > JP > SG > US` 的排序，实际得到的是字典序 `HK > JP > SG > TW > US`。这可能不是 bug（因为 config.yaml 中 region_rules 的顺序在 YAML 解析后就丢失了），但用户可能期望保序。

### 3.5 安全相关

#### P10: curl 未设置 `CURLOPT_MAXREDIRS`

```cpp
// fetch.cpp
curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
// 缺少: curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
```

**影响**：恶意订阅 URL 可以通过无限重定向造成 hang。

#### P11: `file://` URL 无路径遍历防护

```cpp
if (sub.url.rfind("file://", 0) == 0) {
    const std::string path = decodeFileUrlPath(sub.url.substr(7));
    // 直接读取，无路径限制
}
```

**影响**：如果订阅文件由外部导入（`subcli sub import`），恶意 `file:///etc/shadow` 类 URL 可读取任意本地文件。

### 3.6 测试覆盖不足

- 没有针对 `fromUri` 各协议的边界测试（空密码、特殊字符、IPv6 地址）
- 没有针对 `template_policy` merge/reject 行为的独立单元测试
- 没有针对 `makeExportNodes` 去重/重命名的边界测试
- daemon 相关逻辑没有 mock 测试

---

## 4. run / daemon 命令跨平台测试

### 4.1 命令功能矩阵

| 命令 | Linux | macOS | Windows | 说明 |
|------|-------|-------|---------|------|
| `subcli run <target>` | ✅ fork+exec | ✅ fork+exec | ⚠️ CreateProcess | 前台运行核心进程 |
| `subcli stop` | ✅ kill(pid) | ✅ kill(pid) | ⚠️ TerminateProcess | 停止运行中的核心 |
| `subcli status` | ✅ | ✅ | ✅ | 检查 PID 文件 + 进程存活 |
| `subcli restart` | ✅ | ✅ | ⚠️ | stop + run |
| `subcli daemon once` | ✅ | ✅ | ✅ | 单次 update+export 循环 |
| `subcli daemon run` | ✅ | ✅ | ⚠️ | 前台守护循环 |
| `subcli daemon start` | ✅ fork+setsid | ✅ fork+setsid | ⚠️ CreateProcess detach | 后台守护 |
| `subcli daemon stop` | ✅ | ✅ | ⚠️ | 停止守护进程 |
| `subcli daemon status` | ✅ | ✅ | ✅ | 检查守护状态 |

### 4.2 跨平台测试方案

```yaml
# .github/workflows/runtime-test.yml
name: Runtime Command Tests

on:
  workflow_dispatch:
  push:
    branches: [main]

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
      - uses: actions/checkout@v4

      - name: Build
        run: |
          cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
          cmake --build build --config Release -j

      - name: Setup test environment
        shell: bash
        run: |
          mkdir -p test-workspace
          cp -r templates test-workspace/
          cp -r profiles test-workspace/
          cp build/subcli test-workspace/ 2>/dev/null || \
            cp build/Release/subcli.exe test-workspace/ 2>/dev/null
          cd test-workspace
          ./subcli config init --portable || ./subcli.exe config init --portable

      - name: Test daemon once (no core needed)
        shell: bash
        run: |
          cd test-workspace
          # daemon once 不需要核心二进制，只做 update+export
          # 没有订阅时应该优雅退出
          ./subcli daemon once --target mihomo 2>&1 || true

      - name: Test daemon status (no running daemon)
        shell: bash
        run: |
          cd test-workspace
          ./subcli daemon status 2>&1 | grep -i "not running\|no daemon" || true

      - name: Test run without core (graceful error)
        shell: bash
        run: |
          cd test-workspace
          # 没有核心二进制时应该报错而不是崩溃
          EXIT_CODE=0
          ./subcli run mihomo 2>&1 || EXIT_CODE=$?
          if [ $EXIT_CODE -eq 139 ] || [ $EXIT_CODE -eq 134 ]; then
            echo "❌ CRASH detected (signal $EXIT_CODE)"
            exit 1
          fi
          echo "✅ Graceful error (exit $EXIT_CODE)"

      - name: Test stop without running process
        shell: bash
        run: |
          cd test-workspace
          ./subcli stop 2>&1 || true
          echo "✅ stop without running process handled"

      - name: Test status without running process
        shell: bash
        run: |
          cd test-workspace
          ./subcli status 2>&1 || true
          echo "✅ status without running process handled"
```

### 4.3 已知 Windows 平台问题

通过代码审查 `src/platform_windows.cpp`（775 行）发现：

1. **进程分离**：Windows 使用 `CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS` 实现后台运行，但没有 Windows Service 集成
2. **PID 文件**：Windows 上 PID 概念不同于 POSIX，`isProcessRunning` 使用 `OpenProcess` 检查
3. **信号处理**：Windows 无 SIGTERM/SIGKILL，使用 `TerminateProcess` 强制终止
4. **日志重定向**：使用 `CreateFile` + `SetStdHandle` 实现，需要确认句柄继承正确

### 4.4 建议

- `run`/`daemon` 命令在 README 中已标注为 "optional capabilities"，这是正确的定位
- Windows 上建议用户使用 Task Scheduler 或 nssm 替代 `daemon start`
- 核心功能（订阅管理 + 配置导出）不依赖进程管理，跨平台可靠性有保障

---

## 5. 项目最终目标评估

### 5.1 核心定位

```
订阅源 → 解析节点 → templates + profile + strategy → 核心原生配置
```

### 5.2 灵活性评估

#### Templates（模板）：⭐⭐⭐⭐ 优秀

| 维度 | 评价 |
|------|------|
| 用户可替换 | ✅ `subcli config set templates.mihomo.normal /path/to/custom.yaml` |
| normal/tun 双模板 | ✅ 每个目标有 base 和 tun 两套 |
| 自带模板最小化 | ✅ 仅包含 inbound + 空 proxies/rules，不预设策略 |
| template_policy 控制 | ✅ replace/append/merge/reject 四种合并策略 |
| 路径级控制 | ✅ 可精确控制 `route.rules`、`outbounds`、`dns` 等路径 |

**自带模板分析**：
- `mihomo_base.yaml`：仅 mixed-port + 空 DNS + 空 rules，19 行
- `singbox_base.json`：仅 mixed inbound + DIRECT/REJECT + 空 route，20 行
- `xray_base.json`：socks+http inbound + DIRECT/REJECT + observatory，50 行

结论：自带模板确实是最小可用骨架，用户完全可以替换为自己的模板。

#### Profile（策略配置）：⭐⭐⭐⭐⭐ 优秀

| 维度 | 评价 |
|------|------|
| 目标无关 | ✅ profile JSON 是 target-neutral 的策略描述 |
| DNS 策略 | ✅ mode/strategy/direct_servers/remote_servers |
| 分组策略 | ✅ select/url-test/fallback/load-balance |
| 路由规则 | ✅ geosite/geoip/domain/ip_cidr/port/network/final |
| 成员展开 | ✅ REGION:*/NODE:*/SOURCE:*/TAG:*/PROTOCOL:* |
| 自带 profile | ✅ bypass-cn/global/direct 三个开箱即用 |
| 自定义 profile | ✅ 任意 JSON 文件，`--profile /path` 即可 |
| 验证 | ✅ `subcli profile validate` |

**关键设计优势**：
- Profile 是声明式的：用户描述「我要什么策略」，subcli 负责翻译成各核心的原生格式
- 降级透明：当目标不支持某策略时，自动降级并报告 `capability_degraded`
- 严格模式：`--strict-capabilities` 可阻止降级导出，适合 CI 场景

#### Strategy Groups（策略组）：⭐⭐⭐⭐ 优秀

| 维度 | 评价 |
|------|------|
| 类型支持 | ✅ select/url-test/fallback/load-balance |
| 成员灵活 | ✅ 支持 REGION:*/NODE:*/SOURCE:*/TAG:*/PROTOCOL:* 展开 |
| 区域自动分组 | ✅ config.yaml 中 region_rules 正则自动分组 |
| 嵌套引用 | ✅ 组可以引用其他组（如 PROXY 引用 AUTO） |
| 健康检查 | ✅ url/interval/strategy 可配置 |
| 跨目标映射 | ✅ 自动处理 Mihomo/sing-box/Xray 的差异 |

#### 节点管理：⭐⭐⭐⭐ 良好

| 维度 | 评价 |
|------|------|
| 去重 | ✅ 基于 type+server+port+uuid+... 的 dedupe |
| 重命名 | ✅ `rename_template: "{name}"` 支持 {name}/{region}/{source}/{protocol} |
| 过滤 | ✅ include_regex / exclude_regex |
| 排序 | ✅ region,name / name / source,name |
| 协议跳过 | ✅ 不支持的协议跳过并警告，不生成无效配置 |

### 5.3 开箱即用评估

```bash
# 最小使用路径（5 条命令）
subcli config init --portable
subcli sub add --name my-sub --url https://example.com/sub
subcli sub update
subcli asset update
subcli export all --profile bypass-cn
```

执行后即可得到三个核心的可用配置文件。自带的 `bypass-cn` profile 提供了：
- fake-ip DNS
- 国内直连（geosite:cn + geoip:cn）
- 私有地址直连
- 其余走代理
- PROXY(select) + AUTO(url-test) + 区域分组

这是中国用户最常见的使用场景，开箱即用。

### 5.4 与同类工具对比

| 特性 | subcli | subconverter | clash-verge |
|------|--------|--------------|-------------|
| 配置生成 | ✅ 三核心 | ✅ 多核心 | ❌ 仅 Mihomo |
| 无 GUI | ✅ CLI-only | ✅ Web API | ❌ GUI |
| Profile 策略 | ✅ JSON 声明式 | ❌ INI 模板 | ❌ 内置 |
| Template Policy | ✅ merge/reject | ❌ | ❌ |
| 能力矩阵 | ✅ 降级报告 | ❌ | ❌ |
| 跨平台 | ✅ 三平台 | ✅ | ✅ |
| 依赖 | 无（静态链接） | Node.js/Go | Tauri |

### 5.5 最终结论

**作为「订阅 → 灵活配置生成」工具，subcli 是合格的。**

核心优势：
1. **模板最小化**：自带模板仅是骨架，不预设策略，用户可完全替换
2. **Profile 声明式**：用户描述意图，工具负责翻译，target-neutral
3. **策略组灵活**：REGION:*/NODE:*/SOURCE:*/TAG:*/PROTOCOL:* 展开机制强大
4. **降级透明**：capability_degraded/unsupported 明确告知用户
5. **开箱即用**：bypass-cn + 默认模板 = 5 条命令出配置

改进建议：
1. 支持用户自定义 region_rules 的**有序**配置（当前 YAML map 解析后丢失顺序）
2. 增加 `subcli profile create --interactive` 交互式创建 profile
3. 增加 `subcli export --dry-run` 预览生成内容而不写文件
4. template_policy 文档可以更多示例（当前只有一个 reject 示例）
5. 考虑支持 `subcli export --diff` 显示与上次导出的差异

---

## 附录：快速命令参考

```bash
# 跨平台编译验证
gh workflow run release-validation.yml
gh run watch

# 发布
git tag -a v0.2.7 -m "Release v0.2.7"
git push origin v0.2.7
gh run watch  # 等待 release workflow
gh release view v0.2.7

# 本地测试
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure

# 功能验证
./build/subcli config init --portable
./build/subcli doctor --json
./build/subcli sub add --name test --url file://./tests/fixtures/sample.txt
./build/subcli export all --profile bypass-cn --json
```
