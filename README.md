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
- Import/export subscription records for portable backups and workspace migration.
- Check, prune, and batch-edit subscriptions from the CLI.
- Use registry-backed command/config/target metadata to keep help, completion, and docs aligned.
- Run structured diagnostics with `subcli doctor --json`.
- Validate exported configs with external cores via `--check`.
- Support Linux, macOS, and Windows builds with platform-native process execution for checks, runtime helpers, and daemon helpers.
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

Release-validation workflow triggers on `v*` tags, enforces tag == `v<project version>` from `CMakeLists.txt`, then runs configure/build/test/package.

## Quick Start

```bash
subcli init
subcli doctor --json
subcli sub add --name airport-a --url https://example/sub
subcli sub update
subcli export all --profile bypass-cn
```

Useful next steps:

```bash
subcli completion bash > ~/.local/share/bash-completion/completions/subcli
subcli config set core_paths.mihomo /path/to/mihomo
subcli config set core_paths.sing_box /path/to/sing-box
subcli config set core_paths.xray /path/to/xray
subcli config set fetch_max_bytes 10485760
subcli template list
subcli asset update
subcli profile list
subcli profile get bypass-cn
subcli profile explain --target all bypass-cn
subcli export all --profile bypass-cn --json
subcli export all --profile bypass-cn --check
subcli export all --profile bypass-cn --strict-capabilities
```

Primary workflow: subscriptions + assets + profile JSON + templates -> exported native configs. Optional runtime helpers such as `run` and `daemon` exist, but cross-platform config generation is the main guarantee.

Proxy cores are not bundled. Configure core paths explicitly or make them available on `PATH`.

## Runtime Paths

`subcli` is workspace-first. `subcli init [DIR]` and `subcli workspace init [DIR]` initialize a workspace, seed built-in templates/profiles, and remember that workspace as the default.

Workspace resolution order is:

1. `--workspace DIR`
2. `SUBCLI_WORKSPACE`
3. marker discovery from the current directory (`.subcli-workspace` or `subcli.env.yaml`)
4. remembered default workspace
5. platform default workspace

When no `DIR` is provided, the platform default workspace is used:

- Linux: `${XDG_DATA_HOME:-~/.local/share}/subcli`
- macOS: `~/Library/Application Support/subcli`
- Windows: `%APPDATA%\subcli`

All runtime paths then live under the resolved workspace root:

- Config: `<workspace>/config.yaml`
- Subscriptions: `<workspace>/sub.yaml`
- Cache: `<workspace>/cache/`
- State: `<workspace>/state/`
- Outputs: `<workspace>/outputs/`
- Templates: `<workspace>/templates/`
- Profiles: `<workspace>/profiles/`

Older XDG-style paths may still appear when migrating existing data, but first-use commands should prefer `subcli init` / `subcli workspace init` and `subcli workspace status --json`.

## Workspace Mode

`subcli` supports project-scoped workspace data roots for isolation and reproducibility.

```bash
subcli workspace init ./my-subcli
subcli workspace status --json
subcli workspace unset
```

`workspace init` remembers the initialized workspace. Use `workspace use ./other-workspace` later when you want to switch defaults explicitly.

One-off per-command override:

```bash
subcli --workspace ./my-subcli export all --profile bypass-cn --check
```

## Migration

Migrate existing XDG data into a workspace:

```bash
subcli workspace migrate --to ./my-subcli
subcli workspace init ./my-subcli
subcli doctor --json
```

## Verification

```bash
cmake --build build -j
cmake --build build --target package
ctest --test-dir build --output-on-failure
```

The default CTest suite includes the CPack package first-use journey, so build packages before running `ctest` in a fresh build directory.

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

See [`README.subcli.md`](README.subcli.md) for detailed command examples, release validation workflow (`profile explain`, `export --json`, `--strict-capabilities`), cache behavior, troubleshooting, and deployment notes. See [`docs/config-file.md`](docs/config-file.md) for `config.yaml` reference and workspace/path precedence rules. See [`docs/cli-glossary.zh-CN.md`](docs/cli-glossary.zh-CN.md) for a Chinese command and option glossary. See [`docs/profile-schema.md`](docs/profile-schema.md) for the profile JSON schema and migration notes. See [`docs/capability-matrix.md`](docs/capability-matrix.md) for the full v2.1 capability matrix (protocols, groups, DNS, route mapping, assets, and strict-mode behavior).
