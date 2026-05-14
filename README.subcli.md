# subcli

`subcli` is a CLI application for proxy subscription management and profile-driven native config export. Its primary workflow is subscription + asset management + native config export for Mihomo, sing-box, and Xray across Linux, macOS, and Windows; profile JSON supplies the generation policy for those exports. Proxy cores are not bundled, and there is no GUI.

Runtime and daemon commands are optional capabilities. The main product guarantee is cross-platform configuration generation and management, not cross-platform core process hosting. In short, runtime and daemon commands are optional.

Profile files are the policy interface. `config.yaml` stores subcli application settings, `profile.json` stores DNS/strategy/routing/default-outbound policy, templates store target skeletons, subscriptions provide nodes, and assets provide rule data files.

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
subcli profile list
subcli profile get bypass-cn
subcli profile validate ./profiles/bypass-cn.json
subcli config set profile_path /path/to/profile.json
subcli template list
subcli asset update
subcli sub add --name airport-a --url https://example/sub
subcli sub update
subcli export all --profile bypass-cn --check
```

The primary workflow ends at exported config files. If you have a local core installed, `check`, `run`, and `daemon` can be used as optional helpers.

## Runtime Directories

`subcli` uses XDG directories on Linux:

- Config: `${XDG_CONFIG_HOME:-~/.config}/subcli/config.yaml`
- Data: `${XDG_DATA_HOME:-~/.local/share}/subcli/sub.yaml`
- Cache: `${XDG_CACHE_HOME:-~/.cache}/subcli/`
- State: `${XDG_STATE_HOME:-~/.local/state}/subcli/`
- Outputs: `${XDG_DATA_HOME:-~/.local/share}/subcli/outputs/`

Persisted relative paths in `config.yaml` are resolved relative to the config directory. CLI path arguments such as `--output-dir`, `--file`, and path-valued `--profile` are resolved relative to the current shell directory.

## Commands

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

subcli daemon once --target all --strict-network   # optional helper

subcli run sing-box                                # optional managed background helper
subcli status sing-box                             # optional helper
subcli logs sing-box --tail 100                    # optional helper
subcli logs sing-box --follow                      # optional helper
subcli restart sing-box --log-file /tmp/subcli-sing-box.log
subcli stop sing-box                               # optional helper

subcli check sing-box --file ./outputs/sing-box.json --timeout 30
subcli completion bash
```

## Subscription Management

## Workspace Management

`subcli` supports workspace-scoped runtime/config/data layout, so multiple independent environments can coexist.

- `subcli workspace init [DIR]`: initialize a workspace layout.
- `subcli workspace status`: show the active workspace and key paths.
- `subcli workspace use <DIR>`: set the active workspace.
- `subcli workspace unset`: clear workspace override and fall back to default XDG layout.
- `subcli workspace migrate [--dry-run] [--overwrite]`: migrate legacy/default files into the active workspace.
- `subcli workspace doctor`: validate workspace structure and required files.

Global workspace selection is also available on any command:

- `--workspace <DIR>`: use a specific workspace for this invocation only.

Environment variable override:

- `SUBCLI_WORKSPACE=<DIR>`: default workspace when `--workspace` is not provided.

Migration notes:

- `--dry-run` prints planned file moves/copies without writing changes.
- `--overwrite` allows replacing existing destination files during migration.

Precedence for workspace selection is `--workspace` > `SUBCLI_WORKSPACE` > persisted workspace selection > default XDG paths.

Subscriptions support normal CRUD through `sub add`, `sub list`, `sub edit`, and `sub remove`. Subscription ids and names must be unique; `sub add` will not overwrite an existing subscription. Use `sub edit <id|name>` for changes.

Supported content includes Mihomo YAML, sing-box JSON, Xray JSON, plain URI lists, and base64 URI lists. URI lists currently support `vmess://`, `vless://`, `trojan://`, `ss://`, `hy2://`, `hysteria2://`, `tuic://`, and `wireguard://` nodes. Mihomo YAML can read inline `proxies`, local `proxy-providers` entries with `type: file`, and remote `proxy-providers` entries with `type: http` plus `url`. Remote providers also support optional `user-agent` and `header` fields; `header` accepts either a map or a list of `Key: Value` strings.

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

Lifecycle backup/import examples:

```bash
subcli sub export --file ./backup/subscriptions.yaml
subcli sub remove airport-a
subcli sub import --file ./backup/subscriptions.yaml --merge
subcli sub export --file ./backup/hk-enabled.yaml --tag hk --enabled true
subcli sub export --file ./backup/default-group.yaml --group default
```

`sub export` writes subcli YAML snapshots. `sub import` accepts those YAML snapshots or plain URI list files.

## Config Management

`config list`, `config get`, `config set`, and `config remove` manage stored config values.

For a dedicated `config.yaml` reference (file responsibilities, path resolution, workspace precedence, common key prefixes, and minimal YAML example), see [`docs/config-file.md`](docs/config-file.md).

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
- `profile_path`
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

`profile` selects a built-in profile name such as `bypass-cn`, `global`, or `direct`. `profile_path` selects a custom profile JSON file. `export --profile <path-or-name>` overrides both for the current export only and does not mutate `config.yaml`.

## Profile Management

Profiles define config-generation policy: DNS, strategy groups, routing rules, and default outbound behavior. They are JSON files and are the main interface for advanced routing policy.

```bash
subcli profile list
subcli profile get bypass-cn
subcli profile validate ./profiles/bypass-cn.json
subcli profile validate /path/to/custom-profile.json
subcli config set profile_path /path/to/custom-profile.json
subcli export all --profile /path/to/custom-profile.json
subcli export sing-box --profile global --check
```

Built-in profiles:

- `bypass-cn`: direct private/LAN and China traffic, proxy everything else.
- `global`: proxy traffic by default.
- `direct`: direct traffic by default.

For custom authoring, see [`docs/profile-schema.md`](docs/profile-schema.md). Advanced routing and strategy behavior should now live in profile JSON, not in `config.yaml`. Keep `config.yaml` focused on subcli software settings such as paths, timeouts, core locations, assets, templates, and selected profile path/name.

Profile group member selectors support generated expansion tokens:

- `REGION:*`, `REGION:<name>`
- `NODE:*`
- `SOURCE:*`, `SOURCE:<id>`
- `TAG:<tag>`
- `PROTOCOL:<name>`

Use `subcli profile explain <path-or-name> [--target <all|mihomo|sing-box|xray>]` to inspect effective profile behavior, selector semantics, and per-target capability notes.

Release validation workflow:

```bash
subcli profile explain --target all bypass-cn
subcli export all --profile bypass-cn --json
subcli export all --profile bypass-cn --strict-capabilities
```

- `profile explain --target all` is the pre-export capability interpretation check.
- `export ... --json` is machine-readable evidence of per-target capability findings.
- `--strict-capabilities` blocks degraded or unsupported exports for selected target(s).
- GitHub `release-validation` workflow triggers on `v*` tags, enforces tag == `v<project version>` from `CMakeLists.txt`, and uploads `build/subcli-*.tar.gz`.
- GitHub `release` workflow builds Linux, macOS, and Windows packages on `v*` tags and publishes them as GitHub Release assets.

Advanced template merge behavior is also profile-driven via `template_policy`.

Example:

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

`reject` keeps template content and emits warning `template_policy_reject_preserved` without failing export.

Capability-aware warnings:

- `capability_degraded`: exported with target-specific approximation.
- `capability_unsupported`: node/feature skipped for this target.

Use `--strict-capabilities` to fail export when degraded/unsupported behavior is detected for the selected target(s).

JSON output example:

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

`check` object schema per target in `export --json`:

- `requested` (`bool`): whether `--check` was requested.
- `ok` (`bool|null`): check result when run; `null` when not run.
- `message` (`string`): `check passed`, a check failure message, `check not run`, or `check not requested`.

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

`template set` requires the file to exist. `template validate` returns non-zero if any configured template file is missing or cannot be parsed as a valid target format (Mihomo YAML map, sing-box/Xray JSON object).

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

`doctor --json` returns `{"ok":<bool>,"findings":[...],"failed":<int>,"checks":[...]}`.

For v0.2.5 transition compatibility, legacy `failed` and `checks` are retained while new `ok` and `findings` are added.

- `failed` is the legacy failure count retained for compatibility (`0` means command exits zero, `>0` means command exits nonzero).
- `checks` retains compatibility entries (`name`, `ok`, `path`, `message`).

- `workspace.resolved`
- `config.key.registered`
- `export.target.registered`
- `profile.configured` / `profile.missing`
- `subscription.enabled` / `subscription.disabled`
- `subscription.last_error`

## Shell Completion

Bash completion can be generated with:

```bash
subcli completion bash > ~/.local/share/bash-completion/completions/subcli
```

Reload your shell after installing the generated script.

## Export Behavior

`export` fetches selected enabled subscriptions, parses nodes, filters unsupported protocols per target, loads the selected profile, renders templates, applies profile-driven DNS/groups/routing/default outbound policy, and optionally validates with external cores.

`profile_path` stores the external profile file selected for profile-driven export. Relative `profile_path` values are resolved from the config directory. `export --profile <path-or-name>` can point to a custom JSON file or one of the built-in names.

Supported profiles:

- `bypass-cn` (default): private/LAN and mainland China rules go to `DIRECT`; unmatched traffic goes to `PROXY`.
- `global`: unmatched traffic goes to `PROXY` without injecting bypass-cn direct rules.
- `direct`: unmatched traffic goes to `DIRECT`.
- Custom file path: uses profile JSON for DNS, strategy groups, routing rules, and default outbound.

Advanced routing/strategy behavior should move to profile files. Legacy config fields remain available during migration but are no longer the primary policy surface:

- `routing.rules` currently supports `geosite`, `geoip`, `final`, and `match` types.
- `grouping.strategy_groups` custom groups are exported to Mihomo and sing-box.
- Mihomo keeps `select`, `url-test`, `fallback`, and `load-balance` group types.
- sing-box maps `fallback` -> `urltest` and `load-balance` -> `selector`.

- `--sub ID_OR_NAME` can be repeated to export only selected subscriptions.
- `--tag TAG` can be repeated to export subscriptions with matching tags.
- `--tun` selects tun templates for this export.
- `--output-dir DIR` overrides the configured output directory.
- `--profile PATH_OR_NAME` overrides the configured profile for this export only.
- `--check` runs the corresponding external core check after export.
- `--strict-network` disables cache fallback.
- `--download-assets` downloads missing configured rule assets before export.

Export fails when no enabled subscription is selected, selected subscriptions parse into zero nodes, the target has no supported nodes after filtering, or the required template is missing.

## Daemon Automation

Daemon mode is an optional helper for environments where local process hosting is desired. It is not part of the cross-platform core product guarantee.

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

- `once`: run one cycle (`sub update` -> `export`) and exit.
- `run`: loop forever with the configured interval in seconds.
- `start`: fork a background daemon process and persist pid/state under active state directory.
- `status`: show whether the managed daemon process is running and its configured target/interval.
- `stop`: terminate the managed background daemon process.
- `--target`: choose `all|mihomo|sing-box|xray` export target.
- `--update-assets`: pass asset download behavior through export (`--download-assets`).
- `--strict-network`: disable cache fallback in both update and export.
- `--check`: validate exported config with core checks after export.
- `--no-restart`: skip auto restart of currently running cores managed by `subcli run`.
- `--pid-file`: override the default daemon pid file path.
- `--log-file`: override the default daemon log file path.

`daemon status` may also surface the last cycle summary as `last=ok` or `last=failed(...)`, which reflects the most recent `sub update -> export -> restart-running-cores` result, not just whether the process is still alive.
Daemon logs can be inspected through `subcli logs daemon`; use `daemon start --log-file PATH` to choose the daemon log destination.

### systemd user service example

An example `systemd --user` unit is installed to `share/subcli/systemd/subcli-daemon.service` in packaged installs, and the source template lives at `packaging/systemd/subcli-daemon.service`.

Typical setup:

```bash
mkdir -p ~/.config/systemd/user
cp /usr/local/share/subcli/systemd/subcli-daemon.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now subcli-daemon.service
systemctl --user status subcli-daemon.service
```

Adjust `ExecStart=` in the unit file for your preferred `--target`, `--interval`, `--pid-file`, or `--log-file` values before enabling it.

## Rule Assets

Rule and geo databases are managed separately from subscriptions:

```bash
subcli asset list
subcli asset status
subcli asset validate
subcli asset update
subcli asset update xray.geoip
```

Default asset keys include:

- `mihomo.geosite`
- `mihomo.geoip`
- `sing-box.geosite-cn`
- `sing-box.geoip-cn`
- `xray.geosite`
- `xray.geoip`

`asset update` downloads the configured URLs into `asset_dir` (all assets by default, or one asset key when provided). Downloads are written through a temporary file and then swapped into place to avoid partial files. `asset status` shows presence, size, source URL, and last update time metadata. `asset validate` returns non-zero if a configured asset file is missing. Generated configs can be written before assets exist, but direct core runs need the referenced geo/rule files available at the configured paths. `export` warns when assets are missing; use `--download-assets` to download missing configured assets before export.

## Running Cores

After exporting, run the generated configs directly with the matching core:

```bash
mihomo -f ~/.local/share/subcli/outputs/mihomo.yaml
sing-box run -c ~/.local/share/subcli/outputs/sing-box.json
xray run -config ~/.local/share/subcli/outputs/xray.json
```

You can also let `subcli` manage runtime lifecycle directly, but this remains an optional helper outside the primary profile-driven export scope. By default, `subcli run <target>` starts a managed background process, records runtime state under the active state directory, and uses the generated config for that target. Use `--foreground` to keep the core attached to the current terminal instead of managing it in the background.

Managed runtime and daemon helpers use platform-native process primitives: POSIX `fork`/`exec`/signals on Linux and macOS, and Win32 `CreateProcess`/process handles on Windows. They are helper processes, not OS service integrations. On Windows, `daemon start` starts a detached helper process and `daemon stop` terminates it by process id; it does not install a Windows Service.

```bash
subcli run sing-box
subcli status sing-box
subcli logs sing-box --tail 100
subcli logs sing-box --follow
subcli restart sing-box --log-file /tmp/subcli-sing-box.log
subcli stop sing-box
subcli run sing-box --foreground
```

When a core is running, `subcli status` shows the managed pid, config path, log path, and start time.

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
