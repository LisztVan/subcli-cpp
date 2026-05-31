# subcli

`subcli` 是一个用于代理订阅管理和基于配置文件生成原生配置的 CLI 应用程序。其主要工作流程是订阅 + 资产管理 + 为 Mihomo、sing-box 和 Xray 生成原生配置（支持 Linux、macOS 和 Windows）；profile JSON 为这些导出提供生成策略。代理核心不内置，也没有图形界面。

运行时和守护进程命令为可选功能。产品的主要保证是跨平台配置生成和管理，而非跨平台核心进程托管。简言之，运行时和守护进程命令是可选的。

Profile 文件是策略接口。`config.yaml` 存储 subcli 应用程序设置，`profile.json` 存储 DNS/策略/路由/默认出站策略，模板存储目标骨架，订阅提供节点，资产提供规则数据文件。

## 打包构建

```bash
cmake -S . -B build
cmake --build build -j
cmake --build build --target package
```

生成的归档文件写入 `build/` 目录下。

## 首次使用

```bash
subcli init
subcli doctor --json
subcli sub add --name airport-a --url https://example/sub
subcli sub update
subcli export mihomo
```

`subcli init` 创建并记住一个默认工作区。后续命令将自动使用该工作区。

有用的后续步骤：

```bash
subcli completion bash > ~/.local/share/bash-completion/completions/subcli
subcli config set core_paths.sing_box /path/to/sing-box
subcli config set core_paths.xray /path/to/xray
subcli config set core_paths.mihomo /path/to/mihomo
subcli config set fetch_max_bytes 10485760
subcli config get profile
subcli profile list
subcli profile get bypass-cn
subcli profile validate ./profiles/bypass-cn.json
subcli config set profile_path /path/to/profile.json
subcli template list
subcli asset update
subcli export all --profile bypass-cn --check
```

主要工作流程以导出的配置文件结束。如果您本地安装了核心程序，可以使用 `check`、`run` 和 `daemon` 作为可选辅助命令。

## 运行时目录

`subcli` 以工作区为优先。`subcli init [DIR]` 和 `subcli workspace init [DIR]` 初始化一个工作区，填充内置模板/profile，并将该工作区记为默认工作区。

工作区解析顺序为：

1. `--workspace DIR`
2. `SUBCLI_WORKSPACE`
3. 从当前目录发现标记文件（`.subcli-workspace` 或 `subcli.env.yaml`）
4. 记住的默认工作区
5. 平台默认工作区

当未提供 `DIR` 时，使用平台默认工作区：

- Linux：`${XDG_DATA_HOME:-~/.local/share}/subcli`
- macOS：`~/Library/Application Support/subcli`
- Windows：`%APPDATA%\subcli`

所有运行时路径都位于解析后的工作区根目录下：

- 配置：`<workspace>/config.yaml`
- 订阅：`<workspace>/sub.yaml`
- 缓存：`<workspace>/cache/`
- 状态：`<workspace>/state/`
- 输出：`<workspace>/outputs/`
- 模板：`<workspace>/templates/`
- Profiles：`<workspace>/profiles/`

`config.yaml` 中持久化的相对路径相对于配置目录解析。CLI 路径参数（如 `--output-dir`、`--file` 和路径类型的 `--profile`）相对于当前 shell 目录解析。

## 命令

```bash
subcli --help
subcli --workspace /path/to/ws status
subcli init
subcli doctor

subcli workspace init ./ws
subcli workspace status
subcli workspace use ./ws
subcli workspace unset
subcli workspace migrate --from-legacy --dry-run
subcli workspace migrate --from-legacy --overwrite
subcli workspace doctor

subcli sub add --name airport-a --url https://example/sub
subcli sub add --name local --url file:///abs/path/sub.txt --force
subcli sub list
subcli sub edit airport-a --tags hk,sg --priority 20
subcli sub disable airport-a
subcli sub enable airport-a
subcli sub remove airport-a
subcli sub update
subcli sub update --strict-network
subcli sub validate airport-a
subcli sub check --json
subcli sub prune --disabled --dry-run
subcli sub edit --tag hk --set-group asia
subcli sub export --file ./backup/subs.yaml
subcli sub import --file ./backup/subs.yaml --merge
subcli sub export --file ./backup/enabled-hk.yaml --tag hk --enabled true

subcli config list
subcli config list --json
subcli config get core_paths.sing_box
subcli config set core_paths.sing_box /usr/local/bin/sing-box
subcli config set profile_path /path/to/profile.json
subcli config get profile_path
subcli config remove profile_path
subcli config remove core_paths.sing_box

subcli profile list
subcli profile get bypass-cn
subcli profile validate ./profiles/bypass-cn.json
subcli profile validate /path/to/custom-profile.json

subcli template list
subcli template list --json
subcli template get sing-box normal
subcli template set sing-box normal ./templates/singbox_base.json
subcli template reset sing-box normal
subcli template validate

subcli asset list
subcli asset status
subcli asset validate
subcli asset update
subcli asset update xray.geoip

subcli export all
subcli export all --check
subcli export all --strict-capabilities
subcli export sing-box --output-dir ./outputs --check --check-timeout 30
subcli export mihomo --strict-network
subcli export mihomo --download-assets
subcli export all --profile bypass-cn
subcli export xray --profile /path/to/custom-profile.json --output-dir ./outputs

subcli daemon once --target all --strict-network   # 可选辅助功能

subcli run sing-box                                # 可选的后台管理辅助功能
subcli status sing-box                             # 可选辅助功能
subcli logs sing-box --tail 100                    # 可选辅助功能
subcli logs sing-box --follow                      # 可选辅助功能
subcli restart sing-box --log-file /tmp/subcli-sing-box.log
subcli stop sing-box                               # 可选辅助功能

subcli check sing-box --file ./outputs/sing-box.json --timeout 30
subcli completion bash
```

## 订阅管理

## 工作区管理

`subcli` 支持工作区作用域的运行时/配置/数据布局，因此多个独立环境可以共存。

- `subcli workspace init [DIR]`：初始化一个工作区布局并将其记为默认工作区。
- `subcli workspace status`：显示当前活动工作区及关键路径。
- `subcli workspace use <DIR>`：切换记住的默认工作区。
- `subcli workspace unset`：清除记住的默认工作区，回退到平台默认值。
- `subcli workspace migrate [--dry-run] [--overwrite]`：迁移旧版/默认文件到活动工作区。
- `subcli workspace doctor`：验证工作区结构和必需文件。

全局工作区选择也可在任何命令上使用：

- `--workspace <DIR>`：仅为本次调用使用指定的工作区。

环境变量覆盖：

- `SUBCLI_WORKSPACE=<DIR>`：当未提供 `--workspace` 时的默认工作区。

迁移说明：

- `--dry-run` 打印计划中的文件移动/复制操作但不写入更改。
- `--overwrite` 允许在迁移时替换已有的目标文件。

工作区选择的优先级为：`--workspace` > `SUBCLI_WORKSPACE` > 工作区标记发现 > 持久化的工作区选择 > 平台默认路径。`subcli init [DIR]` 和 `subcli workspace init [DIR]` 都会将初始化的工作区持久化为默认工作区。

订阅通过 `sub add`、`sub list`、`sub edit` 和 `sub remove` 支持常规的增删改查操作。订阅 ID 和名称必须唯一；`sub add` 不会覆盖已有的订阅。请使用 `sub edit <id|name>` 进行修改。

支持的内容格式包括 Mihomo YAML、sing-box JSON、Xray JSON、纯 URI 列表和 base64 URI 列表。URI 列表目前支持 `vmess://`、`vless://`、`trojan://`、`ss://`、`hy2://`、`hysteria2://`、`tuic://` 和 `wireguard://` 节点。Mihomo YAML 可读取内联的 `proxies`、包含 `type: file` 的本地 `proxy-providers` 条目，以及包含 `type: http` 和 `url` 的远程 `proxy-providers` 条目。远程提供者还支持可选的 `user-agent` 和 `header` 字段；`header` 接受映射（map）或 `Key: Value` 字符串列表。

常用字段：

- `--name NAME`：显示名称和默认 ID 来源。
- `--id ID`：显式唯一 ID。
- `--url URL`：HTTP(S) 或 `file://` 订阅 URL。
- `--tag TAG` / `--tags a,b`：用于更新/导出过滤的标签。
- `--header 'Key: Value'`：自定义请求头，可重复。
- `--priority N`：数值越小，处理顺序越靠前。
- `--format-hint auto|mihomo|sing-box|xray|uri`：解析器偏好。
- `--force`：跳过立即抓取/解析验证，直接添加。

请求头示例：

```bash
subcli sub edit airport-a --header 'Authorization: Bearer xxx'
subcli sub edit airport-a --remove-header Authorization
subcli sub edit airport-a --clear-headers
```

生命周期备份/导入示例：

```bash
subcli sub export --file ./backup/subscriptions.yaml
subcli sub remove airport-a
subcli sub import --file ./backup/subscriptions.yaml --merge
subcli sub export --file ./backup/hk-enabled.yaml --tag hk --enabled true
subcli sub export --file ./backup/default-group.yaml --group default
```

`sub export` 写入 subcli YAML 快照。`sub import` 接受这些 YAML 快照或纯 URI 列表文件。

## 配置管理

`config list`、`config get`、`config set` 和 `config remove` 用于管理存储的配置值。

有关每个键的完整参考（包括类型、默认值、描述、正则表达式编写技巧和完整 YAML 示例），请参见 [`docs/config-file.md`](docs/config-file.md)。

### 键类别

**Profile 和导出策略：**

- `profile` — 内置 profile 名称（`bypass-cn`、`global`、`direct`）。
- `profile_path` — 自定义 profile JSON 文件路径。覆盖导出时的 `profile`。
- `tun` — 切换 TUN 模板模式（`true` / `false`，默认为 `false`）。

**路径：** `output_dir`、`template_dir`、`asset_dir`。

**核心二进制文件**（用于 `--check` 和 `run`）：

- `core_paths.mihomo`
- `core_paths.sing_box`
- `core_paths.xray`

**网络限制：** `parallelism`、`timeout`、`retry`、`fetch_max_bytes`、`log_level`。

**模板路径：** `templates.mihomo.normal`、`templates.mihomo.tun`、`templates.sing-box.normal`、`templates.sing-box.tun`、`templates.xray.normal`、`templates.xray.tun`。通过 `subcli template ...` 管理更方便。

**节点管理** — 过滤、重命名、去重和排序已解析的代理节点：

- `node_management.dedupe` — 丢弃重复节点（`true` / `false`，默认为 `true`）。
- `node_management.rename_template` — 重命名模式，支持 `{name}`、`{region}`、`{source}`、`{protocol}` 占位符（默认为 `{name}`）。示例：`[{region}] {name}`。
- `node_management.include_regex` — 仅保留匹配此 ECMAScript 正则表达式的节点。
- `node_management.exclude_regex` — 丢弃匹配此 ECMAScript 正则表达式的节点。
- `node_management.sort_by` — 排序方式：`region,name`（默认，按地区优先级再按名称）、`name`（按字母顺序）或 `source,name`（按订阅来源再按名称）。

**地区检测与分组** — 从名称中自动检测节点地区并创建按地区划分的代理组：

- `grouping.region_rules.<REGION>` — 将地区代码与节点名称匹配的正则表达式模式。每个匹配的节点会被标记为该地区。导出时，subcli 会自动为每个地区创建一个代理组（外加 `PROXY`、`AUTO` 和 `OTHER`）。详细说明和正则表达式编写技巧请参见 [`docs/config-file.md`](docs/config-file.md)。内置默认值覆盖 `HK`、`SG`、`JP`、`TW`、`US`。

**资产路径和 URL：** `assets.paths.<asset-key>`、`assets.urls.<asset-key>`。

所有持久化的相对路径都相对于配置目录解析。请为 `core_paths.*` 使用绝对路径以避免歧义。

`export --profile <path-or-name>` 仅覆盖本次导出的 `profile`/`profile_path`，不会修改 `config.yaml`。

## Profile 管理

Profiles 定义配置生成策略：DNS、策略组、路由规则和默认出站行为。它们以 JSON 文件形式存在，是高级路由策略的主要接口。

```bash
subcli profile list
subcli profile get bypass-cn
subcli profile validate ./profiles/bypass-cn.json
subcli profile validate /path/to/custom-profile.json
subcli config set profile_path /path/to/custom-profile.json
subcli export all --profile /path/to/custom-profile.json
subcli export sing-box --profile global --check
```

内置 profiles：

- `bypass-cn`：私有/局域网和中国流量直连，其余流量走代理。
- `global`：默认所有流量走代理。
- `direct`：默认所有流量直连。

自定义编写请参见 [`docs/profile-schema.md`](docs/profile-schema.md)。高级路由和策略行为现在应放在 profile JSON 中，而非 `config.yaml` 中。保持 `config.yaml` 专注于 subcli 软件设置，如路径、超时、核心位置、资产、模板和选定的 profile 路径/名称。

Profile 组成员选择器支持生成的扩展标记：

- `REGION:*`、`REGION:<name>`
- `NODE:*`
- `SOURCE:*`、`SOURCE:<id>`
- `TAG:<tag>`
- `PROTOCOL:<name>`

使用 `subcli profile explain <path-or-name> [--target <all|mihomo|sing-box|xray>]` 查看有效的 profile 行为、选择器语义和每个目标的特性说明。

发布验证工作流程：

```bash
subcli profile explain --target all bypass-cn
subcli export all --profile bypass-cn --json
subcli export all --profile bypass-cn --strict-capabilities
```

- `profile explain --target all` 是导出前的特性解释检查。
- `export ... --json` 是每个目标特性发现结果的机器可读输出。
- `--strict-capabilities` 阻止所选目标被降级或不支持的导出。
- GitHub `release-validation` 工作流程在 `v*` 标签上触发，强制标签 == `CMakeLists.txt` 中的 `v<项目版本>`，并上传 `build/subcli-*.tar.gz`。
- GitHub `release` 工作流程在 `v*` 标签上构建 Linux、macOS 和 Windows 包，并将其作为 GitHub Release 资源发布。

高级模板合并行为也通过 profile 的 `template_policy` 驱动。

示例：

```json
{
  "version": 1,
  "name": "policy-demo",
  "template_policy": {
    "targets": {
      "sing-box": {
        "paths": {
          "route.rules": "reject",
          "outbounds": "merge"
        }
      }
    }
  }
}
```

`reject` 保留模板内容并发出警告 `template_policy_reject_preserved`，不会导致导出失败。

特性感知的警告：

- `capability_degraded`：使用目标特定的近似方式导出。
- `capability_unsupported`：跳过该目标不支持的节点/功能。

使用 `--strict-capabilities` 在检测到所选目标存在降级或不支持的行为时使导出失败。

JSON 输出示例：

```bash
subcli export mihomo --profile bypass-cn --json
```

```json
{
  "summary": {
    "success": 1,
    "failed": 0,
    "skipped_nodes": 0
  },
  "targets": [
    {
      "target": "mihomo",
      "ok": true,
      "output": ".../mihomo.yaml",
      "skipped": 0,
      "check": {
        "requested": true,
        "ok": true,
        "message": "check passed"
      },
      "capabilities": {
        "native": 3,
        "degraded": 0,
        "unsupported": 0,
        "requires_asset": 0
      },
      "findings": [
        {
          "level": "native",
          "code": "profile_group_type",
          "subject": "AUTO",
          "message": "group type is natively supported"
        }
      ]
    }
  ]
}
```

`export --json` 中每个目标的 `check` 对象结构：

- `requested`（布尔值）：是否请求了 `--check`。
- `ok`（布尔值或 null）：运行检查的结果；未运行时为 `null`。
- `message`（字符串）：`check passed`、检查失败消息、`check not run` 或 `check not requested`。

## 模板管理

模板定义生成的代理和组周围的基架配置。支持的目标为 `mihomo`、`sing-box` 和 `xray`；支持的种类为 `normal` 和 `tun`。

```bash
subcli template list
subcli template get mihomo normal
subcli template set mihomo normal /path/to/mihomo_base.yaml
subcli template reset mihomo normal
subcli template reset
subcli template validate
```

`template set` 要求文件已存在。`template validate` 如果任何配置的模板文件缺失或无法解析为有效的目标格式（Mihomo YAML 映射、sing-box/Xray JSON 对象），则返回非零退出码。

## 机器可读输出

若干只读命令支持 `--json` 以便脚本使用：

```bash
subcli doctor --json
subcli sub list --json
subcli config list --json
subcli template list --json
subcli template validate --json
```

JSON 输出以单个紧凑 JSON 对象的形式输出到标准输出。警告和失败信息仍包含在 JSON 负载中，而非依赖终端格式。

`doctor --json` 返回 `{"ok":<bool>,"findings":[...],"failed":<int>,"checks":[...]}`。

为兼容 v0.2.5 过渡，保留旧版 `failed` 和 `checks`，同时新增 `ok` 和 `findings`。

- `failed` 是旧版失败计数，为兼容而保留（`0` 表示命令以零退出，`>0` 表示命令以非零退出）。
- `checks` 保留兼容性条目（`name`、`ok`、`path`、`message`）。

- `workspace.resolved`
- `config.key.registered`
- `export.target.registered`
- `profile.configured` / `profile.missing`
- `subscription.enabled` / `subscription.disabled`
- `subscription.last_error`

## Shell 补全

Bash 补全可以通过以下命令生成：

```bash
subcli completion bash > ~/.local/share/bash-completion/completions/subcli
```

安装生成的脚本后请重新加载您的 shell。

## 导出行为

`export` 抓取所选启用的订阅、解析节点、按目标过滤不支持的协议、加载所选 profile、渲染模板、应用 profile 驱动的 DNS/组/路由/默认出站策略，并可选用外部核心进行验证。

`profile_path` 存储为 profile 驱动的导出所选的外部 profile 文件。相对 `profile_path` 值从配置目录解析。`export --profile <path-or-name>` 可以指向自定义 JSON 文件或内置名称之一。

支持的 profiles：

- `bypass-cn`（默认）：私有/局域网和中国大陆规则走 `DIRECT`；未匹配的流量走 `PROXY`。
- `global`：未匹配的流量走 `PROXY`，不注入 bypass-cn 直连规则。
- `direct`：未匹配的流量走 `DIRECT`。
- 自定义文件路径：使用 profile JSON 中的 DNS、策略组、路由规则和默认出站。

高级路由/策略行为应移至 profile 文件。旧版配置字段在迁移期间仍可用，但不再是主要的策略界面：

- `routing.rules` 目前支持 `geosite`、`geoip`、`final` 和 `match` 类型。
- `grouping.strategy_groups` 自定义组会导出到 Mihomo 和 sing-box。
- Mihomo 保留 `select`、`url-test`、`fallback` 和 `load-balance` 组类型。
- sing-box 将 `fallback` 映射为 `urltest`，将 `load-balance` 映射为 `selector`。

- `--sub ID_OR_NAME` 可重复使用以仅导出选定的订阅。
- `--tag TAG` 可重复使用以导出具有匹配标签的订阅。
- `--tun` 选择本次导出的 tun 模板。
- `--output-dir DIR` 覆盖配置的输出目录。
- `--profile PATH_OR_NAME` 仅覆盖本次导出的配置 profile。
- `--check` 在导出后运行相应的外部核心检查。
- `--strict-network` 禁用缓存回退。
- `--download-assets` 在导出前下载缺失的已配置规则资产。

导出在以下情况下失败：未选择任何启用的订阅、选定的订阅解析出零个节点、目标在过滤后没有任何支持的节点，或者必需的模板缺失。

## 守护进程自动化

守护进程模式是一个可选辅助功能，适用于需要本地进程托管的场景。它不是跨平台核心产品保证的一部分。

```bash
subcli daemon once --target all --strict-network
subcli daemon run --interval 1800 --target sing-box --update-assets
subcli daemon start --interval 1800 --target sing-box --update-assets
subcli daemon start --target sing-box --pid-file /tmp/subcli-daemon.pid --log-file /tmp/subcli-daemon.log
subcli daemon start --target sing-box --log-file /tmp/subcli-daemon.log
subcli logs daemon --tail 200
subcli daemon status
subcli daemon stop
```

- `once`：运行一个周期（`sub update` -> `export`）然后退出。
- `run`：按配置的间隔（秒）无限循环运行。
- `start`：派生一个后台守护进程，并将 pid/状态持久化到活动状态目录下。
- `status`：显示管理的守护进程是否正在运行及其配置的 target/interval。
- `stop`：终止管理的后台守护进程。
- `--target`：选择 `all|mihomo|sing-box|xray` 导出目标。
- `--update-assets`：将资产下载行为传递给导出（`--download-assets`）。
- `--strict-network`：在更新和导出中都禁用缓存回退。
- `--check`：在导出后通过核心检查验证导出的配置。
- `--no-restart`：跳过由 `subcli run` 管理的当前运行核心的自动重启。
- `--pid-file`：覆盖默认的守护进程 pid 文件路径。
- `--log-file`：覆盖默认的守护进程日志文件路径。

`daemon status` 还可能显示上一个周期的摘要，格式为 `last=ok` 或 `last=failed(...)`，这反映的是最近一次 `sub update -> export -> restart-running-cores` 的结果，而不仅仅是进程是否仍在运行。
守护进程日志可通过 `subcli logs daemon` 查看；使用 `daemon start --log-file PATH` 选择守护进程日志目标。

### systemd 用户服务示例

一个示例性的 `systemd --user` 单元文件会安装到打包安装的 `share/subcli/systemd/subcli-daemon.service`，源模板位于 `packaging/systemd/subcli-daemon.service`。

典型配置：

```bash
mkdir -p ~/.config/systemd/user
cp /usr/local/share/subcli/systemd/subcli-daemon.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now subcli-daemon.service
systemctl --user status subcli-daemon.service
```

在启用之前，请根据您的偏好调整单元文件中的 `ExecStart=` 值（包括 `--target`、`--interval`、`--pid-file` 或 `--log-file`）。

## 规则资产

规则和地理数据库与订阅分开管理：

```bash
subcli asset list
subcli asset status
subcli asset validate
subcli asset update
subcli asset update xray.geoip
```

默认资产键包括：

- `mihomo.geosite`
- `mihomo.geoip`
- `sing-box.geosite-cn`
- `sing-box.geoip-cn`
- `xray.geosite`
- `xray.geoip`

`asset update` 将配置的 URL 下载到 `asset_dir`（默认下载所有资产，或指定一个资产键时仅下载该资产）。下载通过临时文件写入，然后交换到位，以避免文件不完整。`asset status` 显示存在性、大小、源 URL 和最后更新时间元数据。`asset validate` 如果配置的资产文件缺失则返回非零退出码。生成的配置可以在资产存在之前写入，但直接运行核心需要在配置路径中存在引用的 geo/规则文件。当资产缺失时 `export` 会发出警告；使用 `--download-assets` 在导出前下载缺失的已配置资产。

## 运行核心

导出后，直接使用匹配的核心运行生成的配置：

```bash
mihomo -f ~/.local/share/subcli/outputs/mihomo.yaml
sing-box run -c ~/.local/share/subcli/outputs/sing-box.json
xray run -config ~/.local/share/subcli/outputs/xray.json
```

您也可以让 `subcli` 直接管理运行时生命周期，但这仍然是主要 profile 驱动导出范围之外的可选辅助功能。默认情况下，`subcli run <target>` 启动一个受管理的后台进程，将运行时状态记录到活动状态目录下，并使用为该目标生成的配置。使用 `--foreground` 将核心保持在前台终端，而非在后台管理。

受管理的运行时和守护进程辅助功能使用平台原生的进程原语：Linux 和 macOS 上使用 POSIX `fork`/`exec`/信号，Windows 上使用 Win32 `CreateProcess`/进程句柄。它们是辅助进程，而非操作系统服务集成。在 Windows 上，`daemon start` 启动一个分离的辅助进程，`daemon stop` 通过进程 ID 终止它；它不会安装 Windows 服务。

```bash
subcli run sing-box
subcli status sing-box
subcli logs sing-box --tail 100
subcli logs sing-box --follow
subcli restart sing-box --log-file /tmp/subcli-sing-box.log
subcli stop sing-box
subcli run sing-box --foreground
```

当核心正在运行时，`subcli status` 显示受管理的 pid、配置路径、日志路径和启动时间。

Mihomo 和 sing-box 的 TUN 配置可以在核心具有所需平台权限时直接运行。Xray 没有原生的 TUN 设备；`xray_tun.json` 是一个透明代理辅助文件，仍需要操作系统层面的路由/tproxy/tun2socks 管道。

## 外部核心检查

`subcli` 不包含 Mihomo、sing-box 或 Xray。检查优先使用配置的路径，然后通过 `PATH` 发现：

- `core_paths.mihomo`
- `core_paths.sing_box`
- `core_paths.xray`

`doctor` 将缺失的核心报告为警告，因为导出不需要核心。它将配置了但不可执行的核心报告为失败。

## 缓存行为

当网络抓取失败时，`sub update` 和 `export` 可以回退到缓存的订阅内容。发生这种情况时，`subcli` 会打印一条警告。使用 `--strict-network` 禁用缓存回退并在网络错误时失败。

只接受 `file://`、`http://` 和 `https://` 订阅 URL。订阅内容受 `fetch_max_bytes` 限制，默认为 `10485760` 字节。

## 退出码

- `0`：命令成功。
- `1`：运行时、网络、配置、解析、导出或验证失败。
- `2`：无效的 CLI 用法，例如未知命令、不支持的目标或多余的位置参数。

## 故障排除

- 首先运行 `subcli doctor`。
- 如果导出因模板缺失而失败，请验证包是否包含 `share/subcli/templates`。
- 如果 `--check` 失败，请配置相应的 `core_paths.*` 键或将核心安装到 `PATH` 中。
- 如果更新的节点看起来过时，请使用 `--strict-network` 重新运行以确保没有使用缓存回退。
