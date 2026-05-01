# Capability Matrix (v2.1)

`subcli` v2.1 uses profile JSON as the target-neutral policy source, then applies per-target capability checks while exporting Mihomo, sing-box, and Xray configs.

## Scope

This matrix covers export-time capability interpretation for:

- Protocol rendering support per target.
- Profile group type mapping and degradation behavior.
- DNS and route rule mappings.
- Asset-dependent features.
- Strict-capability blocking behavior.

It does not redefine each target core's full runtime feature set; it documents what `subcli` exports today.

v0.2.5 note: export target metadata is defined in `TargetRegistry`; target ids in this document and code must stay aligned with `mihomo`, `sing-box`, and `xray`.

## Levels

- `native`: target supports profile behavior directly.
- `degraded`: target exports a deliberate approximation and emits `capability_degraded`.
- `unsupported`: target cannot export behavior/node and emits `capability_unsupported`.
- `requires_asset`: export is valid but depends on rule/geo assets being present.

## Protocol Matrix

| Protocol | Mihomo | sing-box | Xray | Notes |
| --- | --- | --- | --- | --- |
| `vmess` | native | native | native | target schema differs, intent preserved |
| `vless` | native | native | native | target schema differs, intent preserved |
| `trojan` | native | native | native | target schema differs, intent preserved |
| `ss` | native | native | native | target schema differs, intent preserved |
| `hysteria2` / `hy2` | native | native | unsupported | Xray export skips node with warning |
| `tuic` | native | native | unsupported | Xray export skips node with warning |
| `wireguard` | native | native | native | supported on all targets with target-specific fields |

TLS client fingerprint (`fp` / `client-fingerprint`) note in v0.2.5:

- URI parser preserves both query aliases for VLESS/Trojan into normalized TLS fields.
- Exporters propagate preserved fingerprint to Mihomo (`client-fingerprint`), sing-box (`tls.utls.fingerprint`), and Xray (`streamSettings.tlsSettings.fingerprint` / reality fingerprint when applicable).
- For non-TLS or unsupported protocol paths, export may omit fingerprint and emit regular capability warnings when other constraints fail.

## Profile Group Matrix

| Group Type | Mihomo | sing-box | Xray | Degradation Behavior |
| --- | --- | --- | --- | --- |
| `select` | native | native (`selector`) | degraded (`leastPing`) | Xray emits degraded warning |
| `url-test` / `urltest` | native | native (`urltest`) | degraded (`leastPing`) | Xray emits degraded warning |
| `fallback` | native | degraded (`urltest`) | degraded (`leastPing` + optional `fallbackTag`) | sing-box/Xray emit degraded warning |
| `load-balance` / `loadbalance` | native | degraded (`selector`) | degraded (`leastLoad`) | sing-box/Xray emit degraded warning |

Xray-specific group caveats:

- Group members that cannot be resolved are omitted and reported as `capability_degraded`.
- If no members can be resolved, a safe fallback selector is generated and reported as `capability_degraded`.
- Fallback groups degrade to `leastPing`; `fallbackTag` is only set when default-member resolution succeeds.
- Subscription node names are mapped to managed outbound tags (for example `SUBCLI_00001`) rather than preserved as outbound tag names.

## DNS Matrix

| Profile DNS Field | Mihomo | sing-box | Xray |
| --- | --- | --- | --- |
| `dns.mode` | native (`dns.enhanced-mode`) | degraded/no exact equivalent | degraded/no exact equivalent |
| `dns.strategy` | target-specific handling | native strategy mapping | mapped to Xray strategy (`UseIPv4` or `UseIP`) |
| `dns.direct_servers` | native (`dns.nameserver`) | native (`dns-direct` server path) | native (DNS server entries) |
| `dns.remote_servers` | native (`dns.fallback`) | native (`dns-remote` server path) | native (DNS server entries) |

## Route Matrix

| Rule Type | Mihomo | sing-box | Xray |
| --- | --- | --- | --- |
| `geosite` | native (`GEOSITE`) | requires_asset/degraded by value | native (`geosite:<value>`) |
| `geoip` | native (`GEOIP`) | requires_asset/degraded by value | native (`geoip:<value>`) |
| `domain` / `domain_suffix` / `domain_keyword` | native | native | native |
| `ip_cidr` | native | native | native |
| `port` | native | native | native |
| `network` | native | native | native |
| `final` / `match` | native catch-all | native catch-all | native catch-all |

sing-box geosite/geoip limitation in v2.1:

- Managed rule-set mapping is limited to `cn` and `private` behavior in current implementation.
- Built-in managed assets are `sing-box.geosite-cn` and `sing-box.geoip-cn`.
- Other geosite/geoip values are not fully portable through current sing-box managed asset mapping and should be treated as capability-limited for release expectations.

## Asset Requirements

Asset-dependent behavior is reported as `requires_asset` in capability findings and may warn at export time when files are missing.

Common managed keys:

- `mihomo.geosite`
- `mihomo.geoip`
- `sing-box.geosite-cn`
- `sing-box.geoip-cn`
- `xray.geosite`
- `xray.geoip`

Use `subcli asset status`, `subcli asset validate`, and `subcli asset update` to satisfy asset requirements before core runtime checks.

## TUN Notes

- Mihomo and sing-box have native TUN-oriented template paths (`normal` / `tun` variants).
- Xray `tun` output is a transparent-proxy helper template, not a native TUN device implementation.
- Xray deployments still require OS-level redirect/tproxy/tun2socks-style plumbing.

## Warning Codes

- `capability_degraded`: export succeeded with target-approximate behavior.
- `capability_unsupported`: node/feature skipped for this target.
- `template_policy_reject_preserved`: explicit `template_policy` `reject` preserved template content.

## Strict Mode

Use strict-capability mode when exports must avoid degraded/unsupported behavior:

```bash
subcli export all --profile bypass-cn --strict-capabilities
```

Strict mode behavior:

- Any `capability_degraded` or `capability_unsupported` finding blocks export for the selected target.
- Exit code is non-zero when blocked.
- This mode is intended for release gating and CI checks where native-equivalent behavior is required.
