# subcli-cpp

> **AI 构建项目声明**
>
> 本项目主要通过 AI 辅助完成设计、实现、测试和文档编写。整个过程均有人类进行指导、审查和验证。

`subcli` 是一个 C++17 命令行工具，用于管理代理订阅并导出经过验证的、基于配置文件的 Mihomo、sing-box 和 Xray 配置文件。它没有图形界面；以 JSON 配置文件（profile）作为生成配置的策略接口。

## 特性

- 通过单一命令行界面管理 HTTP 和 `file://` 订阅。
- 解析常见订阅格式，包括 Mihomo YAML、sing-box/Xray JSON、URI 列表以及 base64 编码的 URI 列表。
- URI 列表支持 `vmess`、`vless`、`trojan`、`ss`、`hy2`/`hysteria2`、`tuic` 和 `wireguard` 链接。
- Mihomo YAML 支持内联 `proxies`、本地 `proxy-providers`（`type: file`）和远程 `proxy-providers`（`type: http`/`url`，可附带 `user-agent` 和 `header` 字段）。
- 导出 Mihomo、sing-box 和 Xray 的 non-`tun` 和 `tun` 模板。
- 根据定义 DNS、策略组、路由和默认出站行为的 JSON 配置文件（profile）生成配置。
- 支持 profile JSON 中的 `template_policy`，提供按目标、按路径的替换/追加/合并/拒绝控制。
- 支持内置的 `bypass-cn`、`global` 和 `direct` 配置文件以及自定义配置文件。
- 为 Mihomo/sing-box 渲染自定义策略组，包括 `fallback` 和 `load-balance` 类型。
- 通过 `subcli asset list|validate|update` 管理 geo/规则资源文件。
- 导入/导出订阅记录，便于便携备份和工作区迁移。
- 通过命令行检查、修剪和批量编辑订阅。
- 使用基于注册表的命令/配置/目标元数据，确保帮助信息、自动补全和文档保持一致。
- 通过 `subcli doctor --json` 运行结构化诊断。
- 通过 `--check` 使用外部核心验证导出的配置。
- 支持 Linux、macOS 和 Windows 构建，通过平台原生进程执行检查、运行时助手和守护进程助手。
- 使用 XDG 运行时目录管理配置、数据、缓存、状态和输出。
- 保留缓存回退可见性，并支持严格网络模式。
- 跳过不支持的目标协议，并发出明确警告，而不是生成无效配置。

## 构建

```bash
cmake -S . -B build
cmake --build build -j
```

打包归档：

```bash
cmake --build build --target package
```

发布验证工作流在 `v*` 标签上触发，强制要求标签与 `CMakeLists.txt` 中的 `<项目版本>` 一致，然后执行配置/构建/测试/打包。

## 快速开始

```bash
subcli init
subcli doctor --json
subcli sub add --name airport-a --url https://example/sub
subcli sub update
subcli export all --profile bypass-cn
```

有用的后续步骤：

```bash
subcli completion bash > ~/.local/share/bash-completion/completions/subcli
subcli config set core_paths.mihomo /path/to/mihomo
subcli config set core_paths.sing_box /path/to/sing-box
subcli config set core_paths.xray /path/to/xray
subcli config set fetch_max_bytes 10485760
subcli template list
subcli asset update
subcli profile list
subcli profile get bypass-cn
subcli profile explain --target all bypass-cn
subcli export all --profile bypass-cn --json
subcli export all --profile bypass-cn --check
subcli export all --profile bypass-cn --strict-capabilities
```

主要工作流程：订阅 + 资源文件 + JSON 配置文件 + 模板 -> 导出的原生配置。还提供 `run` 和 `daemon` 等可选的运行时助手，但跨平台配置生成是核心保障。

代理核心不随本工具打包。需要显式配置核心路径，或确保其位于 `PATH` 环境变量中。

## 运行时路径

`subcli` 以工作区为先。`subcli init [DIR]` 和 `subcli workspace init [DIR]` 可初始化工作区，预置内置模板和配置文件，并将该工作区记录为默认值。

工作区解析顺序如下：

1. `--workspace DIR`
2. `SUBCLI_WORKSPACE` 环境变量
3. 从当前目录发现标记文件（`.subcli-workspace` 或 `subcli.env.yaml`）
4. 已记录的默认工作区
5. 平台默认工作区

当未提供 `DIR` 时，将使用平台默认工作区：

- Linux：`${XDG_DATA_HOME:-~/.local/share}/subcli`
- macOS：`~/Library/Application Support/subcli`
- Windows：`%APPDATA%\subcli`

所有运行时路径均位于解析后的工作区根目录下：

- 配置：`<workspace>/config.yaml`
- 订阅：`<workspace>/sub.yaml`
- 缓存：`<workspace>/cache/`
- 状态：`<workspace>/state/`
- 输出：`<workspace>/outputs/`
- 模板：`<workspace>/templates/`
- 配置文件：`<workspace>/profiles/`

迁移现有数据时仍可能出现旧的 XDG 风格路径，但首次使用时建议优先使用 `subcli init` / `subcli workspace init` 和 `subcli workspace status --json`。

## 工作区模式

`subcli` 支持项目级别的工作区数据根目录，以实现隔离和可复现性。

```bash
subcli workspace init ./my-subcli
subcli workspace status --json
subcli workspace unset
```

`workspace init` 会记住已初始化的工作区。后续如需切换默认工作区，可使用 `workspace use ./other-workspace`。

单条命令级别覆盖：

```bash
subcli --workspace ./my-subcli export all --profile bypass-cn --check
```

## 迁移

将现有 XDG 数据迁移到工作区：

```bash
subcli workspace migrate --to ./my-subcli
subcli workspace init ./my-subcli
subcli doctor --json
```

## 验证

```bash
cmake --build build -j
cmake --build build --target package
ctest --test-dir build --output-on-failure
```

默认的 CTest 测试套件包含 CPack 包的首次使用流程，因此在新的构建目录中运行 `ctest` 前，请先构建好包。

实用的端到端检查是将配置导出并使用对应的核心进行验证：

```bash
subcli export mihomo --check
subcli export sing-box --check
subcli export xray --check
```

使用对应的核心直接运行生成的配置：

```bash
mihomo -f ~/.local/share/subcli/outputs/mihomo.yaml
sing-box run -c ~/.local/share/subcli/outputs/sing-box.json
xray run -config ~/.local/share/subcli/outputs/xray.json
```

Xray 本身不提供原生 TUN 设备。Xray TUN 模板是一个透明代理辅助工具，仍需要操作系统级别的重定向/tproxy/tun2socks 管道配置。

## 文档

参见 [`README.subcli.md`](README.subcli.md) 获取详细的命令示例、发布验证工作流（`profile explain`、`export --json`、`--strict-capabilities`）、缓存行为、故障排除和部署说明。参见 [`docs/config-file.md`](docs/config-file.md) 获取 `config.yaml` 参考以及工作区/路径优先级规则。参见 [`docs/cli-glossary.zh-CN.md`](docs/cli-glossary.zh-CN.md) 获取命令和选项的中文术语表。参见 [`docs/profile-schema.md`](docs/profile-schema.md) 获取 profile JSON 模式及迁移说明。参见 [`docs/capability-matrix.md`](docs/capability-matrix.md) 获取完整的 v2.1 能力矩阵（协议、策略组、DNS、路由映射、资源文件和严格模式行为）。
