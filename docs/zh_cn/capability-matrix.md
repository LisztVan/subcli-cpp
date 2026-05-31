# 能力矩阵（v2.1）

`subcli` v2.1 使用 profile JSON 作为目标无关的策略源，然后在导出 Mihomo、sing-box 和 Xray 配置时应用按目标的能力检查。

## 范围

本矩阵涵盖了导出时能力解释的以下方面：

- 每个目标的目标协议渲染支持。
- Profile 组类型映射和降级行为。
- DNS 和路由规则映射。
- 依赖资产的功能。
- 严格能力阻塞行为。

它不会重新定义每个目标核心的完整运行时功能集；它记录了 `subcli` 当前导出的内容。

v0.2.5 说明：导出目标元数据在 `TargetRegistry` 中定义；本文档和代码中的目标 ID 必须与 `mihomo`、`sing-box` 和 `xray` 保持一致。

## 等级

- `native`：目标直接支持 profile 行为。
- `degraded`：目标导出一种有意的近似行为，并发出 `capability_degraded` 警告。
- `unsupported`：目标无法导出行为/节点，并发出 `capability_unsupported` 警告。
- `requires_asset`：导出有效，但依赖于规则/地理资产的存在。

## 协议矩阵

| 协议 | Mihomo | sing-box | Xray | 说明 |
| --- | --- | --- | --- | --- |
| `vmess` | native | native | native | 目标模式不同，意图保持一致 |
| `vless` | native | native | native | 目标模式不同，意图保持一致 |
| `trojan` | native | native | native | 目标模式不同，意图保持一致 |
| `ss` | native | native | native | 目标模式不同，意图保持一致 |
| `hysteria2` / `hy2` | native | native | unsupported | Xray 导出跳过节点并发出警告 |
| `tuic` | native | native | unsupported | Xray 导出跳过节点并发出警告 |
| `wireguard` | native | native | native | 所有目标均支持，包含目标特定字段 |

TLS 客户端指纹（`fp` / `client-fingerprint`）说明（v0.2.5）：

- URI 解析器将 VLESS/Trojan 的两个查询别名都保留并规范化为标准化的 TLS 字段。
- 导出器将保留的指纹传播到 Mihomo（`client-fingerprint`）、sing-box（`tls.utls.fingerprint`）和 Xray（`streamSettings.tlsSettings.fingerprint` / 适用时的 reality 指纹）。
- 对于非 TLS 或不支持的协议路径，当其他约束失败时，导出可能会省略指纹并发出常规能力警告。

## 配置组矩阵

| 组类型 | Mihomo | sing-box | Xray | 降级行为 |
| --- | --- | --- | --- | --- |
| `select` | native | native（`selector`） | degraded（`leastPing`） | Xray 发出降级警告 |
| `url-test` / `urltest` | native | native（`urltest`） | degraded（`leastPing`） | Xray 发出降级警告 |
| `fallback` | native | degraded（`urltest`） | degraded（`leastPing` + 可选 `fallbackTag`） | sing-box/Xray 发出降级警告 |
| `load-balance` / `loadbalance` | native | degraded（`selector`） | degraded（`leastLoad`） | sing-box/Xray 发出降级警告 |

Xray 特定组说明：

- 无法解析的组成员将被省略并报告为 `capability_degraded`。
- 如果没有成员可以解析，则会生成一个安全的回退选择器并报告为 `capability_degraded`。
- 回退组降级为 `leastPing`；仅当默认成员解析成功时才设置 `fallbackTag`。
- 订阅节点名称映射为托管出站标签（例如 `SUBCLI_00001`），而不是保留为出站标签名称。

## DNS 矩阵

| Profile DNS 字段 | Mihomo | sing-box | Xray |
| --- | --- | --- | --- |
| `dns.mode` | native（`dns.enhanced-mode`） | degraded/无完全等价项 | degraded/无完全等价项 |
| `dns.strategy` | 目标特定处理 | native 策略映射 | 映射为 Xray 策略（`UseIPv4` 或 `UseIP`） |
| `dns.direct_servers` | native（`dns.nameserver`） | native（`dns-direct` 服务器路径） | native（DNS 服务器条目） |
| `dns.remote_servers` | native（`dns.fallback`） | native（`dns-remote` 服务器路径） | native（DNS 服务器条目） |

## 路由矩阵

| 规则类型 | Mihomo | sing-box | Xray |
| --- | --- | --- | --- |
| `geosite` | native（`GEOSITE`） | requires_asset/按值降级 | native（`geosite:<value>`） |
| `geoip` | native（`GEOIP`） | requires_asset/按值降级 | native（`geoip:<value>`） |
| `domain` / `domain_suffix` / `domain_keyword` | native | native | native |
| `ip_cidr` | native | native | native |
| `port` | native | native | native |
| `network` | native | native | native |
| `final` / `match` | native 全捕获 | native 全捕获 | native 全捕获 |

sing-box geosite/geoip 限制（v2.1）：

- 托管规则集映射在当前实现中仅限于 `cn` 和 `private` 行为。
- 内置托管资产为 `sing-box.geosite-cn` 和 `sing-box.geoip-cn`。
- 其他 geosite/geoip 值不能通过当前的 sing-box 托管资产映射完全移植，在发布预期中应视为能力受限。

## 资产需求

依赖资产的行为在能力检测结果中报告为 `requires_asset`，并可能在导出时文件缺失时发出警告。

常见托管键：

- `mihomo.geosite`
- `mihomo.geoip`
- `sing-box.geosite-cn`
- `sing-box.geoip-cn`
- `xray.geosite`
- `xray.geoip`

在核心运行时检查之前，使用 `subcli asset status`、`subcli asset validate` 和 `subcli asset update` 来满足资产需求。

## TUN 说明

- Mihomo 和 sing-box 具有原生的 TUN 导向模板路径（`normal` / `tun` 变体）。
- Xray `tun` 输出是一个透明代理辅助模板，不是原生的 TUN 设备实现。
- Xray 部署仍然需要操作系统级别的重定向/tproxy/tun2socks 类管道。

## 警告代码

- `capability_degraded`：导出成功，但采用了目标近似行为。
- `capability_unsupported`：节点/功能针对此目标被跳过。
- `template_policy_reject_preserved`：显式的 `template_policy` `reject` 保留了模板内容。

## 严格模式

当导出必须避免降级/不支持的行为时，使用严格能力模式：

```bash
subcli export all --profile bypass-cn --strict-capabilities
```

严格模式行为：

- 任何 `capability_degraded` 或 `capability_unsupported` 的检测结果都会阻止所选目标的导出。
- 被阻止时退出码为非零。
- 此模式适用于发布门禁和 CI 检查，要求原生等价行为。
