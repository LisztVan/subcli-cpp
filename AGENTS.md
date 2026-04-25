# AGENTS.md

## Build And Verify
- Build with `cmake -S . -B build && cmake --build build -j`.
- Run tests with `ctest --test-dir build --output-on-failure` after building.
- This repo has no lint, formatter, or CI config checked in. The practical verification step is to rebuild, run tests, and run the specific CLI command you changed.
- First configure is networked: `CMakeLists.txt` pulls `yaml-cpp`, `nlohmann_json`, `openssl-cmake`, and `curl` via `FetchContent`.

## Repo Shape
- Single C++17 executable: `subcli`.
- Real entrypoints:
  - `src/main.cpp`: CLI wiring for `sub`, `config`, and `export`.
  - `src/fetch.cpp`: `file://` and HTTP fetches, including `ETag` / `Last-Modified` handling.
  - `src/parser.cpp`: parses Mihomo YAML, sing-box/xray-style JSON outbounds, and plain/base64 URI lists (`vmess`, `vless`, `trojan`, `ss`).
  - `src/exporter.cpp`: renders Mihomo, sing-box, and xray outputs from templates.
  - `src/store.cpp`: YAML persistence for `sub.yaml` and `config.yaml`.

## Runtime Files
- Running the binary auto-creates `sub.yaml` and `config.yaml` in the repo root if they do not exist.
- Working data is rooted in the repo, not under `build/`:
  - `sub.yaml`: subscription registry
  - `config.yaml`: app config
  - `cache/subscriptions/*.cache`: fetched subscription cache
  - `outputs/`: exported configs
  - `templates/`: base templates consumed by exporters
- `build/`, `cache/`, `outputs/`, `tools/`, and `sub.yaml` are gitignored.

## Behavior That Is Easy To Miss
- `sub add` validates the subscription immediately unless `--force` is passed.
- `sub update` only processes enabled subscriptions; filters can be IDs/names and repeated `--tag` flags.
- HTTP fetches can fall back to cached content on network/non-2xx failures, but `export` still performs a fresh fetch for each selected subscription before parsing.
- `export` fails if filters select no enabled subscriptions or if selected subscriptions fetch successfully but parse into zero nodes.
- Region grouping is regex-driven from `config.yaml` `grouping.region_rules`; unmatched nodes become `OTHER`.
- Export templates are resolved from explicit `templates.<target>.(normal|tun)` paths. `template_dir` is stored and editable, but current code does not use it to locate templates.
- `config remove templates.<target>.(normal|tun)` does not leave the field empty for supported targets: defaults are reapplied immediately by `applyConfigDefaults()`.

## Useful Commands
- List subscriptions: `./build/subcli sub list`
- Validate one subscription: `./build/subcli sub validate <id-or-name>`
- Update by tag: `./build/subcli sub update --tag hk`
- Inspect effective config: `./build/subcli config list`
- Export one backend without touching default output dir: `./build/subcli export mihomo --output-dir ./outputs`
## build dir
please use `build` name to cmake configure 
