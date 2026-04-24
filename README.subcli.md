# subcli

`subcli` is a CLI application for subscription management and config export. It exports full configs for Mihomo, sing-box, and Xray. Proxy cores are not bundled.

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
subcli doctor
subcli config set core_paths.sing_box /path/to/sing-box
subcli config set core_paths.xray /path/to/xray
subcli config set core_paths.mihomo /path/to/mihomo
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
subcli sub update
subcli sub update --strict-network
subcli sub validate airport-a
subcli sub enable airport-a
subcli sub disable airport-a
subcli sub edit airport-a --tags hk,sg --priority 20

subcli config list
subcli config get core_paths.sing_box
subcli config set core_paths.sing_box /usr/local/bin/sing-box
subcli config remove core_paths.sing_box

subcli export all
subcli export all --check
subcli export sing-box --output-dir ./outputs --check --check-timeout 30
subcli export mihomo --strict-network

subcli check sing-box --file ./outputs/sing-box.json --timeout 30
```

## External Core Checks

`subcli` does not include Mihomo, sing-box, or Xray. Checks use configured paths first, then `PATH` discovery:

- `core_paths.mihomo`
- `core_paths.sing_box`
- `core_paths.xray`

`doctor` reports missing cores as warnings because exporting does not require cores. It reports configured but non-executable cores as failures.

## Cache Behavior

`sub update` and `export` can fall back to cached subscription content when network fetches fail. When this happens, `subcli` prints a warning. Use `--strict-network` to disable cache fallback and fail on network errors.

## Troubleshooting

- Run `subcli doctor` first.
- If export fails with missing templates, verify the package contains `share/subcli/templates`.
- If `--check` fails, configure the relevant `core_paths.*` key or install the core in `PATH`.
- If updated nodes look stale, rerun with `--strict-network` to ensure cache fallback is not being used.
