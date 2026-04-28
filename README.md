# subcli-cpp

> **AI-BUILT PROJECT NOTICE**
>
> This project was primarily designed, implemented, tested, and documented with AI assistance. Human direction, review, and validation were used throughout the process.

`subcli` is a C++17 command-line tool for managing proxy subscriptions and exporting validated, profile-driven configuration files for Mihomo, sing-box, and Xray. It has no GUI; profile JSON files are the policy interface for generated configs.

## Features

- Manage HTTP and `file://` subscriptions from a single CLI.
- Parse common subscription formats including Mihomo YAML, sing-box/Xray JSON, URI lists, and base64 URI lists.
- URI lists support `vmess`, `vless`, `trojan`, `ss`, `hy2`/`hysteria2`, `tuic`, and `wireguard` links.
- Mihomo YAML supports inline `proxies`, local `proxy-providers` with `type: file`, and remote `proxy-providers` with `type: http`/`url` plus optional `user-agent` and `header` fields.
- Export non-`tun` and `tun` templates for Mihomo, sing-box, and Xray.
- Generate configs from profile JSON policy files that define DNS, strategy groups, routing, and default outbound behavior.
- Support `template_policy` in profile JSON for per-target, per-path replace/append/merge/reject control.
- Support built-in `bypass-cn`, `global`, and `direct` profiles plus custom profile files.
- Render custom strategy groups for Mihomo/sing-box, including `fallback` and `load-balance` types.
- Manage geo/rule assets with `subcli asset list|validate|update`.
- Validate exported configs with external cores via `--check`.
- Use XDG runtime directories for config, data, cache, state, and outputs.
- Preserve cache fallback visibility and support strict network mode.
- Skip unsupported target protocols with explicit warnings instead of generating invalid configs.

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

Package archive:

```bash
cmake --build build --target package
```

## Quick Start

```bash
subcli init
subcli doctor --json
subcli completion bash > ~/.local/share/bash-completion/completions/subcli
subcli config set core_paths.mihomo /path/to/mihomo
subcli config set core_paths.sing_box /path/to/sing-box
subcli config set core_paths.xray /path/to/xray
subcli config set fetch_max_bytes 10485760
subcli template list
subcli asset update
subcli profile list
subcli profile get bypass-cn
subcli config set profile_path /path/to/profile.json
subcli sub add --name airport-a --url https://example/sub
subcli sub update
subcli export all --profile bypass-cn --check
subcli export all --profile bypass-cn --strict-capabilities
```

Primary workflow: subscriptions + assets + profile JSON + templates -> exported native configs. Optional runtime helpers such as `run` and `daemon` exist, but cross-platform config generation is the main guarantee.

Proxy cores are not bundled. Configure core paths explicitly or make them available on `PATH`.

## Runtime Paths

On Linux, `subcli` uses XDG directories:

- Config: `${XDG_CONFIG_HOME:-~/.config}/subcli/config.yaml`
- Data: `${XDG_DATA_HOME:-~/.local/share}/subcli/sub.yaml`
- Cache: `${XDG_CACHE_HOME:-~/.cache}/subcli/`
- State: `${XDG_STATE_HOME:-~/.local/state}/subcli/`
- Outputs: `${XDG_DATA_HOME:-~/.local/share}/subcli/outputs/`

## Verification

```bash
cmake --build build -j
cd build && ctest --output-on-failure
```

The practical end-to-end check is exporting a config and validating it with the corresponding core:

```bash
subcli export mihomo --check
subcli export sing-box --check
subcli export xray --check
```

Run generated configs directly with the matching core:

```bash
mihomo -f ~/.local/share/subcli/outputs/mihomo.yaml
sing-box run -c ~/.local/share/subcli/outputs/sing-box.json
xray run -config ~/.local/share/subcli/outputs/xray.json
```

Xray does not provide a native TUN device. The Xray TUN template is a transparent-proxy helper and still needs OS-level redirect/tproxy/tun2socks plumbing.

## Documentation

See [`README.subcli.md`](README.subcli.md) for detailed command examples, cache behavior, troubleshooting, and deployment notes. See [`docs/profile-schema.md`](docs/profile-schema.md) for the profile JSON schema and migration notes. See [`docs/capability-matrix.md`](docs/capability-matrix.md) for target-native/degraded/unsupported mapping in v2.1.
