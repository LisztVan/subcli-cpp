# Profile Schema

`subcli` has no GUI. Profile JSON files are the policy interface for config generation: edit a profile file, validate it, then export native configs for Mihomo, sing-box, or Xray.

Core process lifecycle commands such as `run` and `daemon` are optional helpers. The primary product scope is subscription management, asset management, and native config generation.

## File Roles

- `config.yaml`: subcli software settings such as paths, timeouts, templates, core paths, asset locations, node filtering, and the selected profile name/path.
- `profile.json`: config-generation policy such as DNS behavior, strategy groups, routing rules, and the default outbound.
- Templates: target-specific output skeletons for Mihomo, sing-box, and Xray. Export fills these skeletons with nodes, groups, routing, DNS, and target-specific generated sections.
- Subscriptions: node sources. They provide proxy nodes and metadata only; routing policy does not belong in subscription files.
- Assets: local rule data files such as geosite, geoip, and sing-box rule-set files referenced by generated configs.

## Built-In Profiles

Built-ins can be selected by name with `config set profile <name>` or overridden per export with `export --profile <name>`.

- `bypass-cn`: direct private/LAN and China traffic, proxy everything else through generated proxy groups.
- `global`: send all traffic to the proxy path by default.
- `direct`: send all traffic directly.

Inspect built-ins with:

```bash
subcli profile list
subcli profile get bypass-cn
subcli profile validate ./profiles/bypass-cn.json
```

Use a custom profile by path:

```bash
subcli config set profile_path /path/to/profile.json
subcli export all --profile /path/to/profile.json
```

`export --profile` only affects that export invocation; it does not update `config.yaml`.

## Top-Level Schema

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

- `version`: optional integer. If present, must be `1`.
- `name`: required string.
- `description`: optional string.
- `default_outbound`: optional string. Used as the fallback final route when rules do not specify a final route; defaults to `PROXY`.
- `dns`: optional object.
- `groups`: optional array of strategy group objects.
- `rules`: optional array of routing rule objects.
- `template_policy`: optional object that controls how generated sections are merged with templates.

Unknown JSON keys are ignored by the current loader. Keep custom metadata under clearly named keys so future schema versions can reserve new names without ambiguity.

## Template Policy

`template_policy` is an advanced control surface for template composition.

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

Actions:

- `replace`: use generated field content only.
- `append`: append generated items to template array.
- `merge`: merge maps or keyed arrays (`tag`/`name` depending on target/path).
- `reject`: preserve template field, skip generated write, and emit warning code `template_policy_reject_preserved`.

### Supported target/path matrix

- `mihomo`: `proxies`, `proxy-groups`, `rules`, `dns`, `dns.nameserver`, `dns.fallback`
- `sing-box`: `outbounds`, `dns`, `dns.servers`, `dns.rules`, `route.rules`, `route.rule_set`
- `xray`: `outbounds`, `dns`, `dns.servers`, `routing.rules`, `routing.balancers`

`merge` is restricted to safe paths only:

- `mihomo`: `proxies`, `proxy-groups`, `dns`
- `sing-box`: `outbounds`, `dns`, `dns.servers`, `route.rule_set`
- `xray`: `outbounds`, `routing.balancers`

Unsupported action/path combinations fail `subcli profile validate`.

## DNS Object

```json
{
  "mode": "fake-ip",
  "strategy": "prefer_ipv4",
  "direct_servers": ["223.5.5.5", "119.29.29.29"],
  "remote_servers": ["1.1.1.1", "8.8.8.8"]
}
```

- `mode`: optional string. Mihomo maps this to `dns.enhanced-mode`. sing-box and Xray do not have an exact equivalent in this phase.
- `strategy`: optional string. sing-box writes it to DNS strategy; Xray maps IPv4-preferring values such as `prefer_ipv4`, `ipv4_only`, `use_ipv4`, and `useipv4` to `UseIPv4`, otherwise `UseIP`.
- `direct_servers`: optional string array. Used as Mihomo `nameserver`, sing-box `dns-direct` servers, and Xray DNS servers.
- `remote_servers`: optional string array. Used as Mihomo `fallback`, sing-box `dns-remote` servers, and Xray DNS servers.

## Groups

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

- `tag`: required string. This is the outbound/group name referenced by other groups and rules.
- `type`: required string. Supported values are `select`, `url-test`, `urltest`, `fallback`, `load-balance`, and `loadbalance`.
- `members`: optional string array. Members can reference generated groups, generated nodes, other profile groups, or literal target outbounds such as `DIRECT`.
- `default`: optional string. Used by targets that support or can approximate a selected/fallback default.
- `url`: optional string. Used by health-checking group types such as `url-test` and `fallback` where supported.
- `interval`: optional integer seconds. Defaults to `300`.
- `strategy`: optional string. Mihomo uses this for `load-balance`; Xray chooses target-native balancer strategies from the group type.

Supported group types:

- `select`: manual selection group.
- `url-test` / `urltest`: latency-test group.
- `fallback`: ordered fallback group where supported.
- `load-balance` / `loadbalance`: load-balancing group where supported.

## Special Members

- `REGION:*`: expands to all generated region groups in configured region order.
- `REGION:<name>`: expands to one generated region group if present. If the region is absent, it remains a literal member for targets that can use literal tags.
- `NODE:*`: expands to all generated node names selected for the export target.
- `SOURCE:*`: expands to all generated node names selected for the export target.
- `SOURCE:<id>`: expands to generated node names from one subscription source id.
- `TAG:<tag>`: expands to generated node names from subscriptions carrying that tag.
- `PROTOCOL:<name>`: expands to generated node names matching that protocol (canonical name; aliases such as `hy2` and `hysteria2` are treated equivalently).

Any other string is preserved as a literal member. Use literal strings for profile group tags such as `AUTO`, generated outbounds such as `DIRECT`, or target-specific outbounds that already exist in the template.

## Rules

Each non-final rule requires `type` and `outbound`. `final` rules also support `outbound`; if omitted, `value` can be used by target renderers, and the profile falls back to `default_outbound` when no final target is available.

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

Supported rule types:

- `geosite`: uses `value`, for example `cn` or `private`.
- `geoip`: uses `value`, for example `cn` or `private`.
- `domain`: uses `value` and/or `domains`.
- `domain_suffix`: uses `value` and/or `domains`.
- `domain_keyword`: uses `value` and/or `domains`.
- `ip_cidr`: uses `value` and/or `ip_cidrs`.
- `port`: uses `value` and/or `ports`.
- `network`: uses `value` and/or `networks`, commonly `tcp` and `udp`.
- `final`: sets the final outbound.
- `match`: accepted as a final-style alias by sing-box and Xray renderers; use `final` for portable profiles.

Target mappings:

- Mihomo: `geosite` -> `GEOSITE`, `geoip` -> `GEOIP`, `domain` -> `DOMAIN`, `domain_suffix` -> `DOMAIN-SUFFIX`, `domain_keyword` -> `DOMAIN-KEYWORD`, `ip_cidr` -> `IP-CIDR`, `port` -> `DST-PORT`, `network` -> `NETWORK`, `final` -> `MATCH`.
- sing-box: `geosite:cn` and `geoip:cn` become local rule sets using configured assets; `geosite:private` becomes a private domain rule; `geoip:private` becomes `ip_is_private`; direct domain, CIDR, port, and network rules map to native route rule fields.
- Xray: `geosite` and `geoip` become `geosite:<value>` and `geoip:<value>` field rules; domain rules map to Xray domain match prefixes; port and network rules become joined Xray fields; final adds a catch-all route.

## Target-Specific Degradation Warnings

Profiles are target-neutral, but not every target has equivalent strategy behavior.

- Mihomo supports `select`, `url-test`, `fallback`, and `load-balance` directly.
- sing-box maps `select` to `selector`, `url-test` to `urltest`, `fallback` to `urltest`, and `load-balance` to `selector`. Lossy mappings emit `capability_degraded` warnings.
- Xray renders profile groups as balancers. `url-test` and `select` use `leastPing`, `load-balance` uses `leastLoad`, and `fallback` is approximated with `leastPing` plus `fallbackTag` when the default member can be resolved. Unresolved members, unresolved route targets, and lossy mappings emit `capability_degraded` warnings.
- Xray does not preserve subscription node names as outbound tags. Generated node outbounds use managed tags such as `SUBCLI_00001`; profile member expansion maps node names to those tags internally.
- sing-box only has built-in asset mapping for the current managed China rule sets in this phase: `sing-box.geosite-cn` and `sing-box.geoip-cn`.
- Xray TUN output is still a transparent-proxy helper. Xray has no native TUN device and requires OS-level redirect/tproxy/tun2socks plumbing.

Warnings are printed during export. Treat `capability_degraded` as a signal that the target config was generated, but behavior is an approximation of the target-neutral profile.

## Example Custom Profile

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

Validate and export it:

```bash
subcli profile validate ./profiles/work-and-streaming.json
subcli export mihomo --profile ./profiles/work-and-streaming.json --check
subcli export sing-box --profile ./profiles/work-and-streaming.json --check
subcli export xray --profile ./profiles/work-and-streaming.json --check
```

## Migration Notes

Older `config.yaml` fields for routing and strategy groups are legacy migration support. For new profiles, put DNS, routing, strategy groups, and default outbound behavior in profile JSON instead of `config.yaml`.

Keep `config.yaml` for app settings and path selection:

```bash
subcli config set profile bypass-cn
subcli config set profile_path /path/to/custom-profile.json
subcli config set templates.mihomo.normal /path/to/mihomo_base.yaml
subcli config set asset_dir /path/to/assets
```

Use templates for target skeletons only. Avoid encoding routing policy in templates unless you intentionally need target-specific static rules that sit outside the portable profile model.
