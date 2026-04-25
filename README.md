# subcli-cpp

> **AI-BUILT PROJECT NOTICE**
>
> This project was primarily designed, implemented, tested, and documented with AI assistance. Human direction, review, and validation were used throughout the process.

`subcli` is a C++17 command-line tool for managing proxy subscriptions and exporting validated configuration files for Mihomo, sing-box, and Xray.

## Features

- Manage HTTP and `file://` subscriptions from a single CLI.
- Parse common subscription formats including Mihomo YAML, sing-box/Xray JSON, URI lists, and base64 URI lists.
- Export non-`tun` and `tun` templates for Mihomo, sing-box, and Xray.
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
subcli sub add --name airport-a --url https://example/sub
subcli sub update
subcli export all --check
```

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

## Documentation

See [`README.subcli.md`](README.subcli.md) for detailed command examples, cache behavior, troubleshooting, and deployment notes.
