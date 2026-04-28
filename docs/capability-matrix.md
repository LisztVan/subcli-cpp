# Capability Matrix (v2.1)

`subcli` v2.1 keeps profile JSON as the single strategy source and adds capability-aware interpretation for each export target.

## Levels

- `native`: target supports the profile behavior directly.
- `degraded`: target renders an approximation.
- `unsupported`: target cannot export this behavior/protocol.
- `requires_asset`: target behavior is valid but needs managed asset files.

## Profile Group Mapping

| Group Type | Mihomo | sing-box | Xray |
| --- | --- | --- | --- |
| `select` | native | native (`selector`) | degraded (`leastPing` balancer) |
| `url-test` | native | native (`urltest`) | degraded (`leastPing` balancer) |
| `fallback` | native | degraded (`urltest`) | degraded (`leastPing` + optional `fallbackTag`) |
| `load-balance` | native | degraded (`selector`) | degraded (`leastLoad` balancer) |

## Protocol Examples

- `hysteria2`: Mihomo native, sing-box native, Xray unsupported.
- `tuic`: Mihomo native, sing-box native, Xray unsupported.
- `wireguard`: exported on all three targets (target-specific schema differences apply).

## Warning Codes

- `capability_degraded`: export generated, but behavior is target-approximate.
- `capability_unsupported`: node/feature skipped for this target.
- `template_policy_reject_preserved`: explicit `template_policy` reject kept template content.

## Strict Mode

Use strict mode when you need only native-equivalent behavior:

```bash
subcli export all --profile bypass-cn --strict-capabilities
```

In strict mode, export fails if the selected target requires degraded behavior or contains unsupported nodes/features.
