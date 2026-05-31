# 配置文件架构（Profile Schema）

`subcli` 没有图形界面。Profile JSON 文件是配置生成的策略接口：编辑 profile 文件，对其进行验证，然后为 Mihomo、sing-box 或 Xray 导出原生配置。

诸如 `run` 和 `daemon` 之类的核心进程生命周期命令是可选的辅助工具。主要产品范围是订阅管理、资产管理和原生配置生成。

## 文件角色

- `config.yaml`：subcli 软件设置，例如路径、超时、模板、核心路径、资产位置、节点过滤以及选定的配置文件名称/路径。
- `profile.json`：配置生成策略，例如 DNS 行为、策略组、路由规则和默认出站。
- 模板（Templates）：针对 Mihomo、sing-box 和 Xray 的目标特定输出骨架。导出过程会用节点、组、路由、DNS 和目标特定的生成部分来填充这些骨架。
- 订阅（Subscriptions）：节点来源。它们仅提供代理节点和元数据；路由策略不应放在订阅文件中。
- 资产（Assets）：本地规则数据文件，例如由生成的配置引用的 geosite、geoip 和 sing-box rule-set 文件。

## 内置配置文件

内置配置文件可以通过 `config set profile <name>` 按名称选择，也可以通过 `export --profile <name>` 在导出时覆盖。

- `bypass-cn`：直连私有/局域网和中国流量，通过生成的代理组代理其他所有流量。
- `global`：默认将所有流量发送到代理路径。
- `direct`：将所有流量直接发送。

查看内置配置文件：

```bash
subcli profile list
subcli profile get bypass-cn
subcli profile validate ./profiles/bypass-cn.json
```

通过路径使用自定义配置文件：

```bash
subcli config set profile_path /path/to/profile.json
subcli export all --profile /path/to/profile.json
```

`export --profile` 仅影响该次导出调用；它不会更新 `config.yaml`。

## 顶层架构

```json
{
  "version": 1,
  "name": "custom-name",
  "description": "Human-readable profile description.",
  "default_outbound": "PROXY",
  "dns": {},
  "groups": [],
  "rules": [],
  "template_policy": {}
}
```

- `version`：可选整数。如果存在，必须为 `1`。
- `name`：必填字符串。
- `description`：可选字符串。
- `default_outbound`：可选字符串。当规则未指定最终路由时，用作回退的最终路由；默认为 `PROXY`。
- `dns`：可选对象。
- `groups`：可选策略组对象数组。
- `rules`：可选路由规则对象数组。
- `template_policy`：可选对象，控制生成的部分如何与模板合并。

未知的 JSON 键会被当前加载器忽略。将自定义元数据放在命名清晰的键下，以便未来的架构版本可以在不产生歧义的情况下保留新名称。

## 模板策略（Template Policy）

`template_policy` 是一个用于模板组合的高级控制面。

```json
{
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

动作：

- `replace`：仅使用生成的字段内容。
- `append`：将生成的条目追加到模板数组。
- `merge`：合并映射或键控数组（根据目标/路径使用 `tag`/`name` 作为键）。
- `reject`：保留模板字段，跳过生成写入，并发出警告代码 `template_policy_reject_preserved`。

### 支持的 target/path 矩阵

- `mihomo`：`proxies`、`proxy-groups`、`rules`、`dns`、`dns.nameserver`、`dns.fallback`
- `sing-box`：`outbounds`、`dns`、`dns.servers`、`dns.rules`、`route.rules`、`route.rule_set`
- `xray`：`outbounds`、`dns`、`dns.servers`、`routing.rules`、`routing.balancers`

`merge` 仅限于安全路径：

- `mihomo`：`proxies`、`proxy-groups`、`dns`
- `sing-box`：`outbounds`、`dns`、`dns.servers`、`route.rule_set`
- `xray`：`outbounds`、`routing.balancers`

不支持的 action/path 组合会导致 `subcli profile validate` 失败。

## DNS 对象

```json
{
  "mode": "fake-ip",
  "strategy": "prefer_ipv4",
  "direct_servers": ["223.5.5.5", "119.29.29.29"],
  "remote_servers": ["1.1.1.1", "8.8.8.8"]
}
```

- `mode`：可选字符串。Mihomo 将其映射到 `dns.enhanced-mode`。在此阶段，sing-box 和 Xray 没有完全等效的配置。
- `strategy`：可选字符串。sing-box 将其写入 DNS 策略；Xray 将 IPv4 优先的值（如 `prefer_ipv4`、`ipv4_only`、`use_ipv4` 和 `useipv4`）映射到 `UseIPv4`，其余映射到 `UseIP`。
- `direct_servers`：可选字符串数组。用作 Mihomo 的 `nameserver`、sing-box 的 `dns-direct` 服务器以及 Xray 的 DNS 服务器。
- `remote_servers`：可选字符串数组。用作 Mihomo 的 `fallback`、sing-box 的 `dns-remote` 服务器以及 Xray 的 DNS 服务器。

## 组（Groups）

```json
{
  "tag": "PROXY",
  "type": "select",
  "members": ["AUTO", "DIRECT", "REGION:*"],
  "default": "AUTO",
  "url": "https://www.gstatic.com/generate_204",
  "interval": 300,
  "strategy": "round-robin"
}
```

- `tag`：必填字符串。这是被其他组和规则引用的出站/组名称。
- `type`：必填字符串。支持的值为 `select`、`url-test`、`urltest`、`fallback`、`load-balance` 和 `loadbalance`。
- `members`：可选字符串数组。成员可以引用生成的组、生成的节点、其他 profile 组或文字目标出站（例如 `DIRECT`）。
- `default`：可选字符串。用于支持或可以近似实现选中/回退默认值的目标。
- `url`：可选字符串。用于健康检查类型的组，如 `url-test` 和 `fallback`（在支持的情况下）。
- `interval`：可选整数，单位为秒。默认为 `300`。
- `strategy`：可选字符串。Mihomo 在 `load-balance` 中使用此值；Xray 根据组类型选择目标原生的均衡器策略。

支持的组类型：

- `select`：手动选择组。
- `url-test` / `urltest`：延迟测试组。
- `fallback`：按顺序回退组（在支持的情况下）。
- `load-balance` / `loadbalance`：负载均衡组（在支持的情况下）。

## 特殊成员

- `REGION:*`：扩展为按配置的区域顺序生成的所有区域组。
- `REGION:<name>`：如果存在，扩展为一个生成的区域组。如果该区域不存在，则对于可以使用文字标签的目标，它保留为文字成员。
- `NODE:*`：扩展为为导出目标选择的所有生成的节点名称。
- `SOURCE:*`：扩展为为导出目标选择的所有生成的节点名称。
- `SOURCE:<id>`：扩展为来自一个订阅源 ID 的生成节点名称。
- `TAG:<tag>`：扩展为来自携带该标签的订阅的生成节点名称。
- `PROTOCOL:<name>`：扩展为匹配该协议（规范名称；诸如 `hy2` 和 `hysteria2` 等别名被等同对待）的生成节点名称。

任何其他字符串都保留为文字成员。对 profile 组标签（如 `AUTO`）、生成的出站（如 `DIRECT`）或模板中已存在的目标特定出站使用文字字符串。

## 规则（Rules）

每个非最终规则都需要 `type` 和 `outbound`。`final` 规则也支持 `outbound`；如果省略，目标渲染器可以使用 `value`，并且当没有最终目标可用时，profile 回退到 `default_outbound`。

```json
{
  "type": "domain_suffix",
  "value": "example.com",
  "outbound": "PROXY",
  "domains": ["example.org"],
  "ip_cidrs": ["10.0.0.0/8"],
  "ports": ["443"],
  "networks": ["tcp"]
}
```

支持的规则类型：

- `geosite`：使用 `value`，例如 `cn` 或 `private`。
- `geoip`：使用 `value`，例如 `cn` 或 `private`。
- `domain`：使用 `value` 和/或 `domains`。
- `domain_suffix`：使用 `value` 和/或 `domains`。
- `domain_keyword`：使用 `value` 和/或 `domains`。
- `ip_cidr`：使用 `value` 和/或 `ip_cidrs`。
- `port`：使用 `value` 和/或 `ports`。
- `network`：使用 `value` 和/或 `networks`，通常为 `tcp` 和 `udp`。
- `final`：设置最终出站。
- `match`：被 sing-box 和 Xray 渲染器接受为 final 风格的别名；对于可移植的 profile，请使用 `final`。

目标映射：

- Mihomo：`geosite` -> `GEOSITE`，`geoip` -> `GEOIP`，`domain` -> `DOMAIN`，`domain_suffix` -> `DOMAIN-SUFFIX`，`domain_keyword` -> `DOMAIN-KEYWORD`，`ip_cidr` -> `IP-CIDR`，`port` -> `DST-PORT`，`network` -> `NETWORK`，`final` -> `MATCH`。
- sing-box：`geosite:cn` 和 `geoip:cn` 使用配置的资产成为本地规则集；`geosite:private` 成为私有域名规则；`geoip:private` 成为 `ip_is_private`；直接的域名、CIDR、端口和网络规则映射到原生的路由规则字段。
- Xray：`geosite` 和 `geoip` 成为 `geosite:<value>` 和 `geoip:<value>` 字段规则；域名规则映射到 Xray 的域名匹配前缀；端口和网络规则成为合并的 Xray 字段；final 添加一个兜底路由。

## 目标特定能力警告

配置文件是目标中立的，但并非每个目标都具有等效的策略行为。

- Mihomo 原生支持 `select`、`url-test`、`fallback` 和 `load-balance`。
- sing-box 将 `select` 映射为 `selector`，`url-test` 映射为 `urltest`，`fallback` 映射为 `urltest`，`load-balance` 映射为 `selector`。有损映射会发出 `capability_degraded` 警告。
- Xray 将 profile 组渲染为均衡器。`url-test` 和 `select` 使用 `leastPing`，`load-balance` 使用 `leastLoad`，`fallback` 通过 `leastPing` 加上可选的 `fallbackTag`（当默认成员解析成功时）进行近似。未解析的成员、未解析的路由目标以及有损映射会发出 `capability_degraded` 警告。
- Xray 不保留订阅节点名称作为出站标签。生成的节点出站使用如 `SUBCLI_00001` 之类的管理标签；profile 成员扩展在内部将节点名称映射到这些标签。
- sing-box 在此阶段仅对当前托管的中文规则集有内置资产映射：`sing-box.geosite-cn` 和 `sing-box.geoip-cn`。
- Xray TUN 输出仍然是一个透明代理辅助工具。Xray 没有原生的 TUN 设备，需要操作系统级别的重定向/tproxy/tun2socks 管道。

导出时会打印警告：

- `capability_degraded`：目标配置已生成，但行为是目标中立配置文件的近似。
- `capability_unsupported`：该目标跳过了节点/功能。

有关完整的能力矩阵（协议、组、DNS、路由、资产要求、严格模式），请参阅 [`docs/capability-matrix.md`](capability-matrix.md)（英文）。

## 自定义配置文件示例

```json
{
  "version": 1,
  "name": "work-and-streaming",
  "description": "Direct private/CN traffic, use regional selectors for work and streaming, proxy everything else.",
  "default_outbound": "PROXY",
  "dns": {
    "mode": "fake-ip",
    "strategy": "prefer_ipv4",
    "direct_servers": ["223.5.5.5", "119.29.29.29"],
    "remote_servers": ["1.1.1.1", "8.8.8.8"]
  },
  "groups": [
    {
      "tag": "PROXY",
      "type": "select",
      "members": ["AUTO", "HK", "SG", "US", "REGION:*", "DIRECT"],
      "default": "AUTO"
    },
    {
      "tag": "AUTO",
      "type": "url-test",
      "members": ["REGION:*"],
      "url": "https://www.gstatic.com/generate_204",
      "interval": 300
    },
    {
      "tag": "STREAMING",
      "type": "fallback",
      "members": ["REGION:HK", "REGION:SG", "PROXY"],
      "default": "REGION:HK",
      "url": "https://www.gstatic.com/generate_204",
      "interval": 300
    },
    {
      "tag": "ALL-NODES",
      "type": "load-balance",
      "members": ["NODE:*"],
      "strategy": "round-robin"
    }
  ],
  "rules": [
    {"type": "geosite", "value": "private", "outbound": "DIRECT"},
    {"type": "geoip", "value": "private", "outbound": "DIRECT"},
    {"type": "geosite", "value": "cn", "outbound": "DIRECT"},
    {"type": "geoip", "value": "cn", "outbound": "DIRECT"},
    {"type": "domain_suffix", "value": "example-work.com", "outbound": "PROXY"},
    {"type": "domain_keyword", "value": "netflix", "outbound": "STREAMING"},
    {"type": "ip_cidr", "value": "203.0.113.0/24", "outbound": "ALL-NODES"},
    {"type": "port", "ports": ["22", "3389"], "outbound": "DIRECT"},
    {"type": "network", "networks": ["udp"], "outbound": "PROXY"},
    {"type": "final", "outbound": "PROXY"}
  ]
}
```

验证并导出：

```bash
subcli profile validate ./profiles/work-and-streaming.json
subcli export mihomo --profile ./profiles/work-and-streaming.json --check
subcli export sing-box --profile ./profiles/work-and-streaming.json --check
subcli export xray --profile ./profiles/work-and-streaming.json --check
```

## 迁移说明

`config.yaml` 中较旧的路由和策略组字段是遗留的迁移支持。对于新的配置文件，请将 DNS、路由、策略组和默认出站行为放在 profile JSON 中，而不是 `config.yaml` 中。

将 `config.yaml` 用于应用程序设置和路径选择：

```bash
subcli config set profile bypass-cn
subcli config set profile_path /path/to/custom-profile.json
subcli config set templates.mihomo.normal /path/to/mihomo_base.yaml
subcli config set asset_dir /path/to/assets
```

模板仅用于目标骨架。除非你确实需要位于可移植 profile 模型之外的目标特定静态规则，否则避免在模板中编码路由策略。
