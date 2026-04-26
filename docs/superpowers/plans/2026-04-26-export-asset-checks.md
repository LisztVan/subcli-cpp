# Export Asset Checks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `export` surface missing geo/rule assets before writing runnable configs, with an explicit opt-in flag to download them.

**Architecture:** Keep asset validation in `src/assets.cpp` so CLI code can ask for missing records without duplicating path logic. `export` remains conservative by default: it warns when configured assets are missing, and only downloads when `--download-assets` is passed.

**Tech Stack:** C++17, existing `AssetRecord`, libcurl-backed `updateAsset()`, custom `subcli_tests`, `ctest`.

---

## File Structure

- Modify `include/subcli/assets.hpp`: add `missingAssets()` helper.
- Modify `src/assets.cpp`: implement `missingAssets()` using `configuredAssets()`.
- Modify `src/main.cpp`: accept `export --download-assets`, warn for missing assets, optionally update them before export.
- Modify `tests/subcli_tests.cpp`: add unit coverage for `missingAssets()`.
- Modify `README.subcli.md`: document export asset checks and `--download-assets`.

## Task Order

### Task 1: Add Missing Asset Helper With TDD

- [ ] Add test in `tests/subcli_tests.cpp`:

```cpp
void testMissingAssetsReturnsOnlyMissingRecords() {
    const fs::path dir = fs::temp_directory_path() / "subcli-missing-assets-tests";
    fs::create_directories(dir);
    const auto present = dir / "present.dat";
    {
        std::ofstream out(present);
        out << "asset";
    }

    subcli::AppConfig config;
    config.assetPaths["present"] = present.string();
    config.assetPaths["missing"] = (dir / "missing.dat").string();
    config.assetUrls["present"] = "file:///present";
    config.assetUrls["missing"] = "file:///missing";

    const auto missing = subcli::missingAssets(config);
    require(missing.size() == 1, "missingAssets should return only missing assets");
    require(missing[0].key == "missing", "missingAssets should keep missing asset key");

    fs::remove_all(dir);
}
```

- [ ] Call it from `main()` after `testConfiguredAssetsExposePaths()`.
- [ ] Run `cmake --build build -j && ctest --test-dir build --output-on-failure`.
- [ ] Expected: compile fails because `subcli::missingAssets` is undeclared.
- [ ] Add declaration to `include/subcli/assets.hpp`:

```cpp
std::vector<AssetRecord> missingAssets(const AppConfig& config);
```

- [ ] Implement in `src/assets.cpp`:

```cpp
std::vector<AssetRecord> missingAssets(const AppConfig& config) {
    std::vector<AssetRecord> out;
    for (const auto& asset : configuredAssets(config)) {
        if (!asset.exists) {
            out.push_back(asset);
        }
    }
    return out;
}
```

- [ ] Run tests; expected `100% tests passed`.

### Task 2: Wire Export Warning And Opt-In Download

- [ ] Update export option validation in `src/main.cpp` to accept `--download-assets` as a flag.
- [ ] After `applyConfigDefaults(cfg)` in `doExportCommand()`, add:

```cpp
const bool downloadAssets = hasFlag(args, "--download-assets");
const auto missing = missingAssets(cfg);
if (!missing.empty()) {
    if (!downloadAssets) {
        for (const auto& asset : missing) {
            std::cerr << "warning: missing asset: " << asset.key << " at " << asset.path << "\n";
        }
        std::cerr << "warning: run 'subcli asset update' or export with --download-assets before direct core use\n";
    } else {
        int failedAssets = 0;
        for (const auto& asset : missing) {
            std::string assetError;
            if (!updateAsset(asset, cfg.timeout, cfg.fetchMaxBytes, assetError)) {
                ++failedAssets;
                std::cerr << "asset update failed: " << asset.key << ": " << assetError << "\n";
            } else {
                std::cout << "updated asset: " << asset.key << " -> " << asset.path << "\n";
            }
        }
        if (failedAssets > 0) {
            return ExitError;
        }
    }
}
```

- [ ] Run `cmake --build build -j && ctest --test-dir build --output-on-failure`; expected pass.

### Task 3: Document And Verify CLI Behavior

- [ ] Update `README.subcli.md` export examples to include `subcli export mihomo --download-assets`.
- [ ] Add one sentence: export warns on missing assets by default; `--download-assets` downloads missing configured assets before export.
- [ ] Run full verification:

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

- [ ] Run CLI smoke with local file subscription:

```bash
tmpdir=$(mktemp -d)
export XDG_CONFIG_HOME="$tmpdir/config" XDG_DATA_HOME="$tmpdir/data" XDG_CACHE_HOME="$tmpdir/cache" XDG_STATE_HOME="$tmpdir/state"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_STATE_HOME"
./build/subcli init
./build/subcli sub add --name basic --url "file://$PWD/tests/fixtures/basic-sub.txt" --format-hint uri
./build/subcli export mihomo --output-dir "$tmpdir/out" --sub basic --strict-network
```

- [ ] Expected: export succeeds and prints missing asset warnings.
- [ ] Commit changes with message `Check assets during export`.
