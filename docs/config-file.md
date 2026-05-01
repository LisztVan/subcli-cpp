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
3. workspace marker / persisted workspace selection
4. default XDG paths

If no explicit workspace source is present, subcli falls back to the default XDG config/data/cache/state locations.

## Common Keys and Prefixes

Common scalar/object keys:

- `profile`
- `profile_path`
- `output_dir`
- `template_dir`
- `parallelism`, `timeout`, `retry`, `fetch_max_bytes`, `log_level`
- `core_paths.mihomo`, `core_paths.sing_box`, `core_paths.xray`
- `node_management.dedupe`, `node_management.rename_template`, `node_management.include_regex`, `node_management.exclude_regex`, `node_management.sort_by`

Common structured prefixes:

- `templates.<target>.<normal|tun>`
- `grouping.region_rules.<REGION>`
- `assets.paths.<asset-key>`
- `assets.urls.<asset-key>`

## Minimal Full Example

```yaml
profile: bypass-cn
profile_path: ./profiles/custom.json
output_dir: ./outputs
parallelism: 4
timeout: 30
fetch_max_bytes: 10485760
core_paths:
  mihomo: /usr/local/bin/mihomo
  sing_box: /usr/local/bin/sing-box
  xray: /usr/local/bin/xray
templates:
  mihomo:
    normal: ./templates/mihomo.yaml
    tun: ./templates/mihomo_tun.yaml
grouping:
  region_rules:
    HK: "(HK|Hong Kong|香港)"
    SG: "(SG|Singapore|新加坡)"
assets:
  paths:
    xray.geoip: ./assets/geoip.dat
    xray.geosite: ./assets/geosite.dat
  urls:
    xray.geoip: https://example.invalid/geoip.dat
    xray.geosite: https://example.invalid/geosite.dat
```

## Basic Workflow

```bash
subcli config list
subcli config get profile_path
subcli config set profile_path ./profiles/work.json
subcli config set templates.sing-box.normal ./templates/singbox_base.json
subcli config set grouping.region_rules.HK '(HK|Hong Kong|香港)'
subcli config set assets.paths.xray.geoip ./assets/geoip.dat
subcli config set assets.urls.xray.geoip https://example.invalid/geoip.dat
subcli config remove profile_path
```

Use `subcli profile validate <path-or-name>` before export when profile JSON changes.
