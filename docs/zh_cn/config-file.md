# 配置文件参考

`config.yaml` 存储 subcli 应用程序/运行时设置。`profile.json` 存储导出策略（DNS/分组/路由规则/默认出站）。两者分开维护：`config.yaml` 决定 subcli 如何运行；`profile.json` 决定生成的配置做什么。

## 职责划分

- `config.yaml`：工作区/运行时行为、路径、模板选择、核心二进制文件路径、获取/检查限制、分组规则以及资产路径/URL 覆盖。
- `profile.json`：生成输出的策略（DNS 策略、分组、路由规则、模板合并策略）。
- `subcli config ...`：仅读写 `config.yaml`。
- `subcli profile ...`：仅检查/验证 profile JSON 文件。

## 路径解析规则

- `config.yaml` 中持久化的相对路径，相对于包含 `config.yaml` 的目录进行解析。
- CLI 路径参数（例如 `--output-dir`、`--file`、`--profile /path/...`）相对于当前 shell 工作目录进行解析。
- 绝对路径在两种情况下都保持绝对路径。

实际效果：`subcli config set profile_path ./profiles/work.json` 存储的是相对于配置文件的路径；`subcli export all --profile ./profiles/work.json` 对该命令而言是相对于当前工作目录的路径。

## 工作区选择优先级

单次调用的选择顺序：

1. `--workspace <DIR>`
2. `SUBCLI_WORKSPACE=<DIR>`
3. 从当前目录向上查找工作区标记
4. 持久化的工作区选择
5. 平台默认路径

`subcli init [DIR]` 和 `subcli workspace init [DIR]` 都会初始化一个工作区并将其持久化为默认工作区。如果没有显式指定工作区来源且不存在已记录的工作区，subcli 将回退到平台默认路径。

---

## 配置参考

以下是 `config.yaml` 中所有可识别的键，按类别组织。使用 `subcli config list` 查看当前值，使用 `subcli config get <key>` 读取单个值。

### 配置文件与导出策略

| 键 | 类型 | 默认值 | 描述 |
|-----|------|---------|-------------|
| `profile` | 字符串 | `bypass-cn` | 内置配置文件名称。选项：`bypass-cn`（直连私有/局域网/国内流量，代理其余流量）、`global`（全部代理）、`direct`（全部直连），或通过 `profile_path` 指定自定义 profile JSON 路径。 |
| `profile_path` | 路径 | （空） | 自定义 profile JSON 文件路径。覆盖 `profile` 用于导出。可用的配置文件为 `bypass-cn`、`global`、`direct`，或指向自定义 JSON 文件的路径。有关 profile JSON 模式，请参阅 [`docs/profile-schema.md`](profile-schema.md)。 |
| `tun` | 布尔值 | `false` | 当为 `true` 时，导出使用 `tun` 类型的模板（例如 `templates.mihomo.tun` 而非 `templates.mihomo.normal`）。使用 `export --tun` 进行一次性 TUN 导出而无需更改此设置。 |

### 路径

| 键 | 类型 | 默认值 | 描述 |
|-----|------|---------|-------------|
| `output_dir` | 路径 | `./outputs` | 导出的配置文件写入的目录。 |
| `template_dir` | 路径 | `./templates` | 包含用于配置生成的模板文件的目录。 |
| `asset_dir` | 路径 | `./assets` | 下载的 geo/规则资产（geoip.dat、geosite.dat 等）的目录。当保持默认值时，subcli 将其解析为平台数据目录（`<workspace>/assets/`）。 |

### 核心二进制文件路径

| 键 | 类型 | 默认值 | 描述 |
|-----|------|---------|-------------|
| `core_paths.mihomo` | 路径 | （空） | Mihomo (mihomo) 二进制文件的路径。由 `export --check`、`run` 和 `check` 命令使用。未设置时也会从 `PATH` 环境变量中解析。 |
| `core_paths.sing_box` | 路径 | （空） | sing-box 二进制文件的路径。 |
| `core_paths.xray` | 路径 | （空） | Xray (xray) 二进制文件的路径。 |

### 网络 / 获取限制

| 键 | 类型 | 默认值 | 描述 |
|-----|------|---------|-------------|
| `parallelism` | 整数 | `4` | 并发订阅获取工作线程的最大数量。 |
| `timeout` | 整数 | `15` | HTTP 请求超时时间（秒）。适用于订阅获取和资产下载。 |
| `retry` | 整数 | `2` | 瞬时获取失败时的重试次数。 |
| `fetch_max_bytes` | 整数 | `10485760`（10 MiB） | 每个订阅源的最大下载字节数。超出此限制的内容将被截断。 |

### 日志

| 键 | 类型 | 默认值 | 描述 |
|-----|------|---------|-------------|
| `log_level` | 字符串 | `info` | 日志详细程度。支持的值：`trace`、`debug`、`info`、`warn`、`error`、`critical`、`off`。 |

### 模板路径

| 键 | 类型 | 默认值 | 描述 |
|-----|------|---------|-------------|
| `templates.mihomo.normal` | 路径 | `./templates/mihomo_base.yaml` | Mihomo 基础模板（非 TUN）。 |
| `templates.mihomo.tun` | 路径 | `./templates/mihomo_tun.yaml` | Mihomo TUN 模式模板。 |
| `templates.sing-box.normal` | 路径 | `./templates/singbox_base.json` | sing-box 基础模板（非 TUN）。 |
| `templates.sing-box.tun` | 路径 | `./templates/singbox_tun.json` | sing-box TUN 模式模板。 |
| `templates.xray.normal` | 路径 | `./templates/xray_base.json` | Xray 基础模板（非 TUN）。 |
| `templates.xray.tun` | 路径 | `./templates/xray_tun.json` | Xray 透明代理模板。注意：Xray 没有原生 TUN 设备；此模板生成一个透明代理骨架，仍然需要 OS 级别的重定向/tproxy/tun2socks 管道。 |

使用 `subcli template list|get|set|reset|validate` 可以更方便地管理模板路径。参见 `subcli template --help`。

### 节点管理

控制解析后的代理节点在导出前如何过滤、重命名、去重和排序。

| 键 | 类型 | 默认值 | 描述 |
|-----|------|---------|-------------|
| `node_management.dedupe` | 布尔值 | `true` | 当为 `true` 时，重复节点（相同类型、服务器、端口、协议参数和传输/tls 设置）将被删除，仅保留第一个出现的节点。 |
| `node_management.rename_template` | 字符串 | `{name}` | 用于重命名节点的模板字符串。支持占位符：`{name}`（原始节点名称）、`{region}`（检测到的区域代码，例如 `HK`）、`{source}`（订阅源 ID）、`{protocol}`（协议类型，例如 `vmess`）。示例：`[{region}] {name}` 在节点区域被检测为 `HK` 时将 `NodeA` 重命名为 `[HK] NodeA`。 |
| `node_management.include_regex` | 字符串 | （空） | 如果设置，仅保留名称+区域+类型+sourceId 匹配此正则表达式的节点。其他节点将被过滤掉并发出警告。使用 ECMAScript 正则表达式（默认区分大小写，使用 `(?i)` 前缀可不区分大小写）。 |
| `node_management.exclude_regex` | 字符串 | （空） | 如果设置，名称+区域+类型+sourceId 匹配此正则表达式的节点将被丢弃并发出警告。 |
| `node_management.sort_by` | 字符串 | `region,name` | 最终导出中节点的排序顺序。支持的值：<br>• `region,name` — 按区域排序（按照 `grouping.region_rules` 中的顺序），然后按名称字母序排序。此为默认值。<br>• `name` — 按节点名称字母序排序。<br>• `source,name` — 按订阅源 ID 排序，然后按名称字母序排序。 |

---

## 节点分组（`grouping`）

`grouping` 部分控制在导出过程中如何将代理节点自动组织成基于区域的组。它有两个用途：

1. **`region_rules`** — 从节点名称检测其地理区域的正则表达式模式。
2. **`strategy_groups`** —（旧版）导出到 Mihomo 和 sing-box 的自定义策略组定义。新项目应在 `profile.json` 中定义策略组。

### `region_rules` — 区域检测的工作原理

每个条目将一个区域代码（例如 `HK`、`JP`）映射到一个不区分大小写的正则表达式模式。在节点解析期间，每个代理节点的名称会按顺序与所有已定义的模式进行匹配。第一个匹配的区域胜出，该节点被打上该区域代码的标签。没有任何模式匹配的节点被标记为 `OTHER`。

此区域标签随后驱动最终的导出结构：

- **自动生成的组**：每个区域键成为一个同名的代理组（例如 `HK`、`JP`、`SG`），包含所有匹配该区域的节点。一个 `OTHER` 组捕获未匹配的节点。
- **`PROXY` 组**：包含所有节点的顶级组（完整池）。通常设置为 `select` 类型用于手动切换。
- **`AUTO` 组**：包含所有节点的组，通常设置为 `url-test` 用于基于延迟的自动选择。
- **排序顺序**：使用 `sort_by: region,name`（默认）时，节点按照区域在 `region_rules` 中出现的顺序排序，因此先列出的区域在代理列表中显示得更靠前。
- **配置文件中的组扩展**：配置文件可以通过 `REGION:*` 或 `REGION:HK` 选择器引用区域组。

#### 默认区域规则

如果省略 `grouping.region_rules`，subcli 使用以下内置默认值：

```yaml
grouping:
  region_rules:
    HK: "(?i)(hong kong|hongkong|hk|香港)"
    SG: "(?i)(singapore|sg|新加坡)"
    JP: "(?i)(japan|jp|tokyo|osaka|日本)"
    TW: "(?i)(taiwan|tw|台灣|台湾)"
    US: "(?i)(united states|usa|us|america|美国)"
```

#### 自定义区域规则

您可以添加区域、删除默认值或调整模式。例如：

```yaml
grouping:
  region_rules:
    HK: "(?i)(hong kong|hongkong|hk|香港)"
    JP: "(?i)(japan|jp|tokyo|osaka|日本|羽田)"
    SG: "(?i)(singapore|sg|新加坡|狮城)"
    KR: "(?i)(korea|kr|seoul|韩国|서울)"
    DE: "(?i)(germany|de|berlin|frankfurt|德国)"
    US: "(?i)(united states|usa|us|america|美国)"
```

CLI 管理：

```bash
# 查看当前规则
subcli config get grouping.region_rules.HK

# 更新现有规则
subcli config set grouping.region_rules.HK '(?i)(hk|hong kong|hongkong|香港)'

# 添加新区域
subcli config set grouping.region_rules.KR '(?i)(korea|kr|seoul|韩国)'

# 删除一个区域
subcli config remove grouping.region_rules.KR
```

`region_rules` 中键的顺序很重要：它决定了组的排序顺序和 `regionRank` 优先级。列表中第一个列出的区域在排序节点时具有最高优先级。

### `strategy_groups`（旧版）

自定义策略组可以直接在 `config.yaml` 中定义，但**新项目应使用 `profile.json`** 来定义组。此字段保留用于向后兼容。

```yaml
grouping:
  strategy_groups:
    - name: PROXY
      type: select
      members: [AUTO, HK, JP, SG, US, OTHER]
    - name: AUTO
      type: url-test
      url: "http://www.gstatic.com/generate_204"
      interval: 300
      default: HK
```

支持的组 `type` 值：

| 类型 | Mihomo | sing-box |
|------|--------|----------|
| `select` | select（手动选择） | selector |
| `url-test` | url-test（自动延迟） | urltest |
| `fallback` | fallback | urltest（映射） |
| `load-balance` | load-balance | selector（映射） |

---

## 正则表达式编写指南

几个配置字段使用 ECMAScript 正则表达式（`include_regex`、`exclude_regex`、`region_rules` 模式）。以下是基础知识：

### 语法

- **字面字符**匹配自身：`hk` 匹配子字符串 "hk"。
- **`|`** 表示"或"：`hk|sg` 匹配 "hk" 或 "sg"。
- **`()`** 分组备选项：`(hong kong|hk)` 匹配 "hong kong" 或 "hk"。
- **`.`** 匹配任意单个字符。
- **`.*`** 匹配零个或多个任意字符（贪婪匹配）。
- **`\d`** 匹配一个数字，`\w` 匹配一个单词字符，`\s` 匹配空白字符。
- **`^`** / **`$`** 分别锚定到字符串的开头/结尾。

### 不区分大小写匹配

在模式前添加 `(?i)` 前缀使其不区分大小写：

```yaml
# 匹配 "HK", "hk", "Hk", "Hong Kong", "hong kong" 等
HK: "(?i)(hong kong|hongkong|hk|香港)"

# 没有 (?i)，仅匹配小写
HK: "(hong kong|hongkong|hk|香港)"
```

区域规则通常使用 `(?i)`，因为代理节点名称经常混用不同提供者的大小写。

### 转义特殊字符

像 `.`、`(`、`)`、`[`、`+`、`*` 这样的字符在需要字面匹配时需要用 `\` 转义。例如，匹配一个字面的点号：`example\.com`。

### 常用模式

| 目的 | 模式 | 匹配内容 |
|---------|---------|---------|
| HK 节点 | `(?i)(hong kong\|hongkong\|hk\|香港)` | 名称中包含任一这些词条的节点 |
| JP 节点 | `(?i)(japan\|jp\|tokyo\|osaka)` | 按城市/国家匹配的日本节点 |
| US 节点 | `(?i)(united states\|usa\|us\|america)` | 美国节点 |
| 排除 test/beta 节点 | `(?i)(test\|beta\|trial\|试用)` | 包含这些关键词的节点 |
| 仅包含 ss/vmess | `(?i)^.*(\bss\b\|\bvmess\b).*$` | 名称中包含协议的节点 |

---

## 资产路径与 URL

规则和地理位置数据库以路径/URL 对的形式配置。每个资产由一个键标识，例如 `mihomo.geoip`。

### 默认资产

| 键 | 描述 |
|-----|-------------|
| `mihomo.geosite` | Mihomo geosite 数据库（域名规则） |
| `mihomo.geoip` | Mihomo geoip 数据库（IP 规则） |
| `sing-box.geosite-cn` | sing-box geosite 二进制文件（`.srs` 格式） |
| `sing-box.geoip-cn` | sing-box geoip 二进制文件（`.srs` 格式） |
| `xray.geosite` | Xray geosite 数据库 |
| `xray.geoip` | Xray geoip 数据库 |

### 配置

```yaml
assets:
  paths:
    xray.geoip: ./assets/geoip.dat
    xray.geosite: ./assets/geosite.dat
  urls:
    xray.geoip: https://github.com/v2fly/geoip/releases/latest/download/geoip.dat
    xray.geosite: https://github.com/v2fly/domain-list-community/releases/latest/download/dlc.dat
```

使用 `subcli asset update` 将配置的 URL 下载到 `asset_dir`。使用 `subcli asset list` 和 `subcli asset status` 检查当前资产。

CLI 管理：

```bash
subcli config set assets.paths.xray.geoip ./assets/geoip.dat
subcli config set assets.urls.xray.geoip 'https://example.invalid/geoip.dat'
subcli config get assets.urls.mihomo.geoip
subcli config remove assets.paths.xray.geoip
```

---

## 最小完整示例

```yaml
version: 1
profile: bypass-cn
profile_path: ./profiles/custom.json
tun: false

# 路径
output_dir: ./outputs
template_dir: ./templates
asset_dir: ./assets

# 网络
parallelism: 4
timeout: 30
retry: 2
fetch_max_bytes: 10485760

# 日志
log_level: info

# 核心二进制文件
core_paths:
  mihomo: /usr/local/bin/mihomo
  sing_box: /usr/local/bin/sing-box
  xray: /usr/local/bin/xray

# 模板
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

# 节点处理
node_management:
  dedupe: true
  rename_template: "{name}"
  include_regex: ""
  exclude_regex: ""
  sort_by: region,name

# 区域检测与分组
grouping:
  region_rules:
    HK: "(?i)(hong kong|hongkong|hk|香港)"
    SG: "(?i)(singapore|sg|新加坡)"
    JP: "(?i)(japan|jp|tokyo|osaka|日本)"
    TW: "(?i)(taiwan|tw|台灣|台湾)"
    US: "(?i)(united states|usa|us|america|美国)"

# 规则资产
assets:
  paths:
    xray.geoip: ./assets/geoip.dat
    xray.geosite: ./assets/geosite.dat
  urls:
    xray.geoip: https://example.invalid/geoip.dat
    xray.geosite: https://example.invalid/geosite.dat
```

---

## 基本工作流

```bash
# 列出所有配置键和当前值
subcli config list
subcli config list --json

# 读取单个值
subcli config get profile_path
subcli config get node_management.sort_by

# 设置值
subcli config set profile bypass-cn
subcli config set profile_path ./profiles/work.json
subcli config set core_paths.sing_box /usr/local/bin/sing-box
subcli config set fetch_max_bytes 10485760
subcli config set templates.sing-box.normal ./templates/singbox_base.json
subcli config set grouping.region_rules.HK '(?i)(hk|hong kong|香港)'
subcli config set grouping.region_rules.KR '(?i)(korea|kr|seoul|韩国)'
subcli config set node_management.sort_by name
subcli config set assets.paths.xray.geoip ./assets/geoip.dat
subcli config set assets.urls.xray.geoip https://example.invalid/geoip.dat

# 删除/重置值
subcli config remove profile_path
subcli config remove grouping.region_rules.KR
```

在修改 profile JSON 后，导出前请使用 `subcli profile validate <路径或名称>` 进行验证。

## 平台说明

`core_paths.*` 接受绝对路径或可在 `PATH` 环境变量中找到的二进制文件。在 Windows 上，`subcli` 在搜索 `PATH` 时还会检查 `PATHEXT` 中的可执行文件后缀。运行时和守护进程状态存储主机平台的进程 ID，并使用平台原生的进程 API 进行 `status`、`stop` 和过期状态清理。
