# Config File Reference

`config.yaml` stores subcli application/runtime settings. `profile.json` stores export policy (DNS/groups/rules/default outbound). Keep them separate: `config.yaml` decides how subcli runs; `profile.json` decides what generated configs do.

## Responsibilities

- `config.yaml`: workspace/runtime behavior, paths, template selection, core binary paths, fetch/check limits, grouping rules, and asset path/URL overrides.
- `profile.json`: policy for generated outputs (DNS strategy, groups, routing rules, template merge policy).
- `subcli config ...`: read/write `config.yaml` only.
- `subcli profile ...`: inspect/validate profile JSON files only.

## Path Resolution Rules

- Persisted relative paths in `config.yaml` resolve relative to the directory that contains `config.yaml`.
- CLI path arguments (for example `--output-dir`, `--file`, `--profile /path/...`) resolve relative to current shell working directory.
- Absolute paths stay absolute in both cases.

Practical effect: `subcli config set profile_path ./profiles/work.json` stores a config-relative path; `subcli export all --profile ./profiles/work.json` is cwd-relative for that command.

## Workspace Selection Precedence

Selection order for one invocation:

1. `--workspace <DIR>`
2. `SUBCLI_WORKSPACE=<DIR>`
3. workspace marker discovery from the current directory upward
4. persisted workspace selection
5. platform default paths

`subcli init [DIR]` and `subcli workspace init [DIR]` both initialize a workspace and persist it as the default workspace. If no explicit workspace source is present and no remembered workspace exists, subcli falls back to platform default paths.

---

## Configuration Reference

Below is every recognized key in `config.yaml`, organized by category. Use `subcli config list` to see current values and `subcli config get <key>` to read one.

### Profile & Export Policy

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `profile` | string | `bypass-cn` | Built-in profile name. Options: `bypass-cn` (direct private/LAN/CN traffic, proxy rest), `global` (proxy all), `direct` (direct all), or a custom profile JSON path via `profile_path`. |
| `profile_path` | path | (empty) | Custom profile JSON file path. Overrides `profile` for export. Available profiles are `bypass-cn`, `global`, `direct`, or a path to a custom JSON file. See [`docs/profile-schema.md`](profile-schema.md) for the profile JSON schema. |
| `tun` | bool | `false` | When `true`, exports use the `tun`-kind templates (e.g. `templates.mihomo.tun` instead of `templates.mihomo.normal`). Use `export --tun` for a one-off tun export without changing this setting. |

### Paths

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `output_dir` | path | `./outputs` | Directory where exported config files are written. |
| `template_dir` | path | `./templates` | Directory containing template files for config generation. |
| `asset_dir` | path | `./assets` | Directory for downloaded geo/rule assets (geoip.dat, geosite.dat, etc.). When left as default, subcli resolves it to the platform data directory (`<workspace>/assets/`). |

### Core Binary Paths

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `core_paths.mihomo` | path | (empty) | Path to the Mihomo (mihomo) binary. Used by `export --check`, `run`, and `check` commands. Also resolved from `PATH` when not set. |
| `core_paths.sing_box` | path | (empty) | Path to the sing-box binary. |
| `core_paths.xray` | path | (empty) | Path to the Xray (xray) binary. |

### Network / Fetch Limits

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `parallelism` | integer | `4` | Maximum number of concurrent subscription fetch workers. |
| `timeout` | integer | `15` | HTTP request timeout in seconds. Applies to subscription fetches and asset downloads. |
| `retry` | integer | `2` | Number of retry attempts on transient fetch failures. |
| `fetch_max_bytes` | integer | `10485760` (10 MiB) | Maximum bytes to download per subscription source. Content exceeding this limit is truncated. |

### Logging

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `log_level` | string | `info` | Log verbosity. Supported values: `trace`, `debug`, `info`, `warn`, `error`, `critical`, `off`. |

### Template Paths

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `templates.mihomo.normal` | path | `./templates/mihomo_base.yaml` | Mihomo base template (non-TUN). |
| `templates.mihomo.tun` | path | `./templates/mihomo_tun.yaml` | Mihomo TUN-mode template. |
| `templates.sing-box.normal` | path | `./templates/singbox_base.json` | sing-box base template (non-TUN). |
| `templates.sing-box.tun` | path | `./templates/singbox_tun.json` | sing-box TUN-mode template. |
| `templates.xray.normal` | path | `./templates/xray_base.json` | Xray base template (non-TUN). |
| `templates.xray.tun` | path | `./templates/xray_tun.json` | Xray transparent-proxy template. Note: Xray has no native TUN device; this template generates a transparent-proxy skeleton that still needs OS-level redirect/tproxy/tun2socks plumbing. |

Template paths are easier to manage with `subcli template list|get|set|reset|validate`. See `subcli template --help`.

### Node Management

Controls how parsed proxy nodes are filtered, renamed, deduplicated, and sorted before export.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `node_management.dedupe` | bool | `true` | When `true`, duplicate nodes (same type, server, port, protocol params, and transport/tls settings) are dropped, keeping only the first occurrence. |
| `node_management.rename_template` | string | `{name}` | Template string for renaming nodes. Supports placeholders: `{name}` (original node name), `{region}` (detected region code, e.g. `HK`), `{source}` (subscription source id), `{protocol}` (protocol type, e.g. `vmess`). Example: `[{region}] {name}` renames `NodeA` → `[HK] NodeA` when its region is detected as `HK`. |
| `node_management.include_regex` | string | (empty) | If set, only nodes whose name + region + type + sourceId match this regex are kept. Others are filtered out with a warning. Uses ECMAScript regex (case-sensitive unless `(?i)` prefix is used). |
| `node_management.exclude_regex` | string | (empty) | If set, nodes whose name + region + type + sourceId match this regex are dropped with a warning. |
| `node_management.sort_by` | string | `region,name` | Sort order for nodes in the final export. Supported values: <br>• `region,name` — sort by region rank (as ordered in `grouping.region_rules`), then by name alphabetically. This is the default. <br>• `name` — sort by node name alphabetically. <br>• `source,name` — sort by subscription source id, then by name alphabetically. |

---

## Node Grouping (`grouping`)

The `grouping` section controls how proxy nodes are automatically organized into region-based groups during export. It serves two purposes:

1. **`region_rules`** — regex patterns to detect a node's geographic region from its name.
2. **`strategy_groups`** — (legacy) custom strategy group definitions exported to Mihomo and sing-box. New projects should define strategy groups in `profile.json` instead.

### `region_rules` — How Region Detection Works

Each entry maps a region code (e.g. `HK`, `JP`) to a case-insensitive regex pattern. During node parsing, every proxy node's name is tested against all defined patterns in order. The first matching region wins, and the node is tagged with that region code. Nodes that match no pattern are tagged as `OTHER`.

This region tag then drives the final export structure:

- **Auto-generated groups**: Each region key becomes a proxy group of the same name (e.g. `HK`, `JP`, `SG`), containing all nodes that matched that region. An `OTHER` group catches unmatched nodes.
- **`PROXY` group**: A top-level group containing all nodes (the full pool). Usually set as `select` type for manual switching.
- **`AUTO` group**: A group containing all nodes, typically set as `url-test` for automatic latency-based selection.
- **Sort order**: With `sort_by: region,name` (the default), nodes are sorted by the order their regions appear in `region_rules`, so regions listed first appear earlier in proxy lists.
- **Group expansion in profiles**: Profiles can reference region groups via `REGION:*` or `REGION:HK` selectors.

#### Default Region Rules

If `grouping.region_rules` is omitted, subcli uses these built-in defaults:

```yaml
grouping:
  region_rules:
    HK: "(?i)(hong kong|hongkong|hk|香港)"
    SG: "(?i)(singapore|sg|新加坡)"
    JP: "(?i)(japan|jp|tokyo|osaka|日本)"
    TW: "(?i)(taiwan|tw|台灣|台湾)"
    US: "(?i)(united states|usa|us|america|美国)"
```

#### Customizing Region Rules

You can add regions, remove defaults, or adjust patterns. For example:

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

CLI management:

```bash
# View current rules
subcli config get grouping.region_rules.HK

# Update an existing rule
subcli config set grouping.region_rules.HK '(?i)(hk|hong kong|hongkong|香港)'

# Add a new region
subcli config set grouping.region_rules.KR '(?i)(korea|kr|seoul|韩国)'

# Remove a region
subcli config remove grouping.region_rules.KR
```

The order of keys in `region_rules` matters: it determines group sort order and `regionRank` priority. The first-listed region has the highest priority when sorting nodes.

### `strategy_groups` (Legacy)

Custom strategy groups can be defined directly in `config.yaml`, but **new projects should use `profile.json`** for group definitions. This field is retained for backward compatibility.

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

Supported group `type` values:

| Type | Mihomo | sing-box |
|------|--------|----------|
| `select` | select (manual pick) | selector |
| `url-test` | url-test (auto latency) | urltest |
| `fallback` | fallback | urltest (mapped) |
| `load-balance` | load-balance | selector (mapped) |

---

## Regex Writing Guide

Several config fields use ECMAScript regular expressions (`include_regex`, `exclude_regex`, `region_rules` patterns). Here are the basics:

### Syntax

- **Literal characters** match themselves: `hk` matches the substring "hk".
- **`|`** means "or": `hk|sg` matches "hk" or "sg".
- **`()`** groups alternatives: `(hong kong|hk)` matches either "hong kong" or "hk".
- **`.`** matches any single character.
- **`.*`** matches zero or more of anything (greedy).
- **`\d`** matches a digit, `\w` matches a word character, `\s` matches whitespace.
- **`^`** / **`$`** anchor to start/end of the string.

### Case-Insensitive Matching

Prefix your pattern with `(?i)` to make it case-insensitive:

```yaml
# Matches "HK", "hk", "Hk", "Hong Kong", "hong kong", etc.
HK: "(?i)(hong kong|hongkong|hk|香港)"

# Without (?i), only lowercase matches
HK: "(hong kong|hongkong|hk|香港)"
```

Region rules typically use `(?i)` because proxy node names often mix cases from different providers.

### Escape Special Characters

Characters like `.`, `(`, `)`, `[`, `+`, `*` need escaping with `\` when used literally. For example, to match a literal dot: `example\.com`.

### Common Patterns

| Purpose | Pattern | Matches |
|---------|---------|---------|
| HK nodes | `(?i)(hong kong\|hongkong\|hk\|香港)` | names containing any of these terms |
| JP nodes | `(?i)(japan\|jp\|tokyo\|osaka)` | Japanese nodes by city/country |
| US nodes | `(?i)(united states\|usa\|us\|america)` | American nodes |
| Exclude test/beta nodes | `(?i)(test\|beta\|trial\|试用)` | nodes with these keywords |
| Include only ss/vmess | `(?i)^.*(\bss\b\|\bvmess\b).*$` | nodes with protocol in name |

---

## Asset Paths & URLs

Rule and geo databases are configured with path/URL pairs. Each asset is identified by a key like `mihomo.geoip`.

### Default Assets

| Key | Description |
|-----|-------------|
| `mihomo.geosite` | Mihomo geosite database (domain rules) |
| `mihomo.geoip` | Mihomo geoip database (IP rules) |
| `sing-box.geosite-cn` | sing-box geosite binary (`.srs` format) |
| `sing-box.geoip-cn` | sing-box geoip binary (`.srs` format) |
| `xray.geosite` | Xray geosite database |
| `xray.geoip` | Xray geoip database |

### Configuration

```yaml
assets:
  paths:
    xray.geoip: ./assets/geoip.dat
    xray.geosite: ./assets/geosite.dat
  urls:
    xray.geoip: https://github.com/v2fly/geoip/releases/latest/download/geoip.dat
    xray.geosite: https://github.com/v2fly/domain-list-community/releases/latest/download/dlc.dat
```

Use `subcli asset update` to download configured URLs into `asset_dir`. Use `subcli asset list` and `subcli asset status` to inspect current assets.

CLI management:

```bash
subcli config set assets.paths.xray.geoip ./assets/geoip.dat
subcli config set assets.urls.xray.geoip 'https://example.invalid/geoip.dat'
subcli config get assets.urls.mihomo.geoip
subcli config remove assets.paths.xray.geoip
```

---

## Minimal Full Example

```yaml
version: 1
profile: bypass-cn
profile_path: ./profiles/custom.json
tun: false

# Paths
output_dir: ./outputs
template_dir: ./templates
asset_dir: ./assets

# Network
parallelism: 4
timeout: 30
retry: 2
fetch_max_bytes: 10485760

# Logging
log_level: info

# Core binaries
core_paths:
  mihomo: /usr/local/bin/mihomo
  sing_box: /usr/local/bin/sing-box
  xray: /usr/local/bin/xray

# Templates
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

# Node processing
node_management:
  dedupe: true
  rename_template: "{name}"
  include_regex: ""
  exclude_regex: ""
  sort_by: region,name

# Region detection & grouping
grouping:
  region_rules:
    HK: "(?i)(hong kong|hongkong|hk|香港)"
    SG: "(?i)(singapore|sg|新加坡)"
    JP: "(?i)(japan|jp|tokyo|osaka|日本)"
    TW: "(?i)(taiwan|tw|台灣|台湾)"
    US: "(?i)(united states|usa|us|america|美国)"

# Rule assets
assets:
  paths:
    xray.geoip: ./assets/geoip.dat
    xray.geosite: ./assets/geosite.dat
  urls:
    xray.geoip: https://example.invalid/geoip.dat
    xray.geosite: https://example.invalid/geosite.dat
```

---

## Basic Workflow

```bash
# List all config keys and current values
subcli config list
subcli config list --json

# Read a single value
subcli config get profile_path
subcli config get node_management.sort_by

# Set values
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

# Remove / reset values
subcli config remove profile_path
subcli config remove grouping.region_rules.KR
```

Use `subcli profile validate <path-or-name>` before export when profile JSON changes.

## Platform Notes

`core_paths.*` accepts absolute paths or binaries discoverable on `PATH`. On Windows, `subcli` also checks executable suffixes from `PATHEXT` when searching `PATH`. Runtime and daemon state stores process ids from the host platform and uses platform-native process APIs for `status`, `stop`, and stale-state cleanup.
