# subcli

`subcli` is a CLI application for proxy subscription management and config export. You add one or more subscriptions, update or validate them, then export complete Mihomo, sing-box, or Xray configuration files from editable templates. Proxy cores are not bundled.

## Package Build

```bash
cmake -S . -B build
cmake --build build -j
cmake --build build --target package
```

The generated archive is written under `build/`.

## First Use

```bash
subcli init
subcli doctor --json
subcli completion bash > ~/.local/share/bash-completion/completions/subcli
subcli config set core_paths.sing_box /path/to/sing-box
subcli config set core_paths.xray /path/to/xray
subcli config set core_paths.mihomo /path/to/mihomo
subcli config set fetch_max_bytes 10485760
subcli config get profile
subcli template list
subcli asset update
subcli sub add --name airport-a --url https://example/sub
subcli sub update
subcli export all --check
```

## Runtime Directories

`subcli` uses XDG directories on Linux:

- Config: `${XDG_CONFIG_HOME:-~/.config}/subcli/config.yaml`
- Data: `${XDG_DATA_HOME:-~/.local/share}/subcli/sub.yaml`
- Cache: `${XDG_CACHE_HOME:-~/.cache}/subcli/`
- State: `${XDG_STATE_HOME:-~/.local/state}/subcli/`
- Outputs: `${XDG_DATA_HOME:-~/.local/share}/subcli/outputs/`

Persisted relative paths in `config.yaml` are resolved relative to the config directory. CLI path arguments such as `--output-dir` and `--file` are resolved relative to the current shell directory.

## Commands

```bash
subcli --help
subcli init
subcli doctor

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

subcli config list
subcli config list --json
subcli config get core_paths.sing_box
subcli config set core_paths.sing_box /usr/local/bin/sing-box
subcli config remove core_paths.sing_box

subcli template list
subcli template list --json
subcli template get sing-box normal
subcli template set sing-box normal ./templates/singbox_base.json
subcli template reset sing-box normal
subcli template validate

subcli asset list
subcli asset validate
subcli asset update

subcli export all
subcli export all --check
subcli export sing-box --output-dir ./outputs --check --check-timeout 30
subcli export mihomo --strict-network

subcli check sing-box --file ./outputs/sing-box.json --timeout 30
subcli completion bash
```

## Subscription Management

Subscriptions support normal CRUD through `sub add`, `sub list`, `sub edit`, and `sub remove`. Subscription ids and names must be unique; `sub add` will not overwrite an existing subscription. Use `sub edit <id|name>` for changes.

Useful fields:

- `--name NAME`: display name and default id source.
- `--id ID`: explicit unique id.
- `--url URL`: HTTP(S) or `file://` subscription URL.
- `--tag TAG` / `--tags a,b`: labels for update/export filtering.
- `--header 'Key: Value'`: custom request header, repeatable.
- `--priority N`: lower values are processed first.
- `--format-hint auto|mihomo|sing-box|xray|uri`: parser preference.
- `--force`: add without immediate fetch/parse validation.

Header examples:

```bash
subcli sub edit airport-a --header 'Authorization: Bearer xxx'
subcli sub edit airport-a --remove-header Authorization
subcli sub edit airport-a --clear-headers
```

## Config Management

`config list`, `config get`, `config set`, and `config remove` manage stored config values.

Common keys:

- `tun`
- `output_dir`
- `template_dir`
- `parallelism`
- `timeout`
- `retry`
- `fetch_max_bytes`
- `log_level`
- `profile`
- `asset_dir`
- `core_paths.mihomo`
- `core_paths.sing_box`
- `core_paths.xray`
- `node_management.dedupe`
- `node_management.rename_template`
- `node_management.include_regex`
- `node_management.exclude_regex`
- `node_management.sort_by`
- `grouping.region_rules.<REGION>`
- `assets.paths.<asset-key>`
- `assets.urls.<asset-key>`

Template paths can still be edited with `config set templates.<target>.<normal|tun> <path>`, but `subcli template ...` is the preferred interface.

Persisted paths are resolved relative to the config directory. Use absolute paths for `core_paths.*` to avoid ambiguity.

## Template Management

Templates define the base config around generated proxies and groups. Supported targets are `mihomo`, `sing-box`, and `xray`; supported kinds are `normal` and `tun`.

```bash
subcli template list
subcli template get mihomo normal
subcli template set mihomo normal /path/to/mihomo_base.yaml
subcli template reset mihomo normal
subcli template reset
subcli template validate
```

`template set` requires the file to exist. `template validate` returns non-zero if any configured template file is missing.

## Machine-Readable Output

Several read-only commands support `--json` for scripts:

```bash
subcli doctor --json
subcli sub list --json
subcli config list --json
subcli template list --json
subcli template validate --json
```

JSON output is emitted as a single compact JSON object on stdout. Warnings and failures remain represented in the JSON payload instead of relying on terminal formatting.

## Shell Completion

Bash completion can be generated with:

```bash
subcli completion bash > ~/.local/share/bash-completion/completions/subcli
```

Reload your shell after installing the generated script.

## Export Behavior

`export` fetches selected enabled subscriptions, parses nodes, filters unsupported protocols per target, renders templates, applies the configured profile, and optionally validates with external cores.

The default profile is `bypass-cn`: LAN/private traffic and mainland China domain/IP rules go to `DIRECT`, and unmatched traffic goes to `PROXY`. The first implementation intentionally supports this one profile well instead of adding half-complete profile names; custom template rules remain available for advanced cases.

- `--sub ID_OR_NAME` can be repeated to export only selected subscriptions.
- `--tag TAG` can be repeated to export subscriptions with matching tags.
- `--tun` selects tun templates for this export.
- `--output-dir DIR` overrides the configured output directory.
- `--check` runs the corresponding external core check after export.
- `--strict-network` disables cache fallback.

Export fails when no enabled subscription is selected, selected subscriptions parse into zero nodes, the target has no supported nodes after filtering, or the required template is missing.

## Rule Assets

Rule and geo databases are managed separately from subscriptions:

```bash
subcli asset list
subcli asset validate
subcli asset update
```

Default asset keys include:

- `mihomo.geosite`
- `mihomo.geoip`
- `sing-box.geosite-cn`
- `sing-box.geoip-cn`
- `xray.geosite`
- `xray.geoip`

`asset update` downloads the configured URLs into `asset_dir`. `asset validate` returns non-zero if a configured asset file is missing. Generated configs can be written before assets exist, but direct core runs need the referenced geo/rule files available at the configured paths.

## Running Cores

After exporting, run the generated configs directly with the matching core:

```bash
mihomo -f ~/.local/share/subcli/outputs/mihomo.yaml
sing-box run -c ~/.local/share/subcli/outputs/sing-box.json
xray run -config ~/.local/share/subcli/outputs/xray.json
```

Mihomo and sing-box TUN configs can be run directly when the core has the required platform permissions. Xray has no native TUN device; `xray_tun.json` is a transparent-proxy helper and still needs OS redirect/tproxy/tun2socks-style plumbing.

## External Core Checks

`subcli` does not include Mihomo, sing-box, or Xray. Checks use configured paths first, then `PATH` discovery:

- `core_paths.mihomo`
- `core_paths.sing_box`
- `core_paths.xray`

`doctor` reports missing cores as warnings because exporting does not require cores. It reports configured but non-executable cores as failures.

## Cache Behavior

`sub update` and `export` can fall back to cached subscription content when network fetches fail. When this happens, `subcli` prints a warning. Use `--strict-network` to disable cache fallback and fail on network errors.

Only `file://`, `http://`, and `https://` subscription URLs are accepted. Subscription content is capped by `fetch_max_bytes`, which defaults to `10485760` bytes.

## Exit Codes

- `0`: command succeeded.
- `1`: runtime, network, config, parse, export, or validation failure.
- `2`: invalid CLI usage such as an unknown command, unsupported target, or extra positional argument.

## Troubleshooting

- Run `subcli doctor` first.
- If export fails with missing templates, verify the package contains `share/subcli/templates`.
- If `--check` fails, configure the relevant `core_paths.*` key or install the core in `PATH`.
- If updated nodes look stale, rerun with `--strict-network` to ensure cache fallback is not being used.
