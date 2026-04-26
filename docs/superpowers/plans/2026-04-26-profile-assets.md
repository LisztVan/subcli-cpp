# Profile Assets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generate directly runnable Mihomo, sing-box, and Xray configs from subscriptions with a default bypass-CN profile and managed geo/rule assets.

**Architecture:** Keep subscriptions as node sources, add profile and asset configuration to `AppConfig`, and make exporters render target-specific routing/DNS from the same profile intent. Keep the first implementation focused on `bypass-cn`, direct core run readiness, and clear asset management commands instead of building a full client runtime manager.

**Tech Stack:** C++17, CMake, yaml-cpp, nlohmann_json, libcurl, existing custom test binary plus `ctest`.

---

## File Structure

- Modify `include/subcli/models.hpp`: add profile and asset configuration fields to `AppConfig`.
- Modify `src/store.cpp`: persist and load new profile/asset settings.
- Modify `src/main.cpp`: apply defaults, expose `asset` CLI commands, pass profile values through export.
- Modify `src/exporter_mihomo.cpp`: emit bypass-CN DNS/rules/providers suitable for Mihomo.
- Modify `src/exporter_singbox.cpp`: emit bypass-CN DNS/route/rule_set suitable for sing-box.
- Modify `src/exporter_xray.cpp`: emit bypass-CN DNS/routing suitable for Xray.
- Modify `src/exporter_common.cpp`: add small shared helpers only when target exporters need the same profile data.
- Create `include/subcli/assets.hpp` and `src/assets.cpp`: list, validate, and download configured rule assets.
- Modify `CMakeLists.txt`: compile `src/assets.cpp` into app and tests.
- Modify `tests/subcli_tests.cpp`: add TDD coverage for bypass-CN output and asset configuration.
- Modify `README.md` and `README.subcli.md`: document profiles, assets, direct core run commands, and Xray TUN limits.

## Task Order

### Task 1: Save Plan And Establish Failing Tests

- [x] Add tests showing Mihomo export includes `GEOSITE,cn,DIRECT`, `GEOIP,CN,DIRECT`, and `MATCH,PROXY` for default `bypass-cn`.
- [x] Add tests showing sing-box export includes `geosite-cn`, `geoip-cn`, direct routing, and final `PROXY`.
- [x] Add tests showing Xray export includes `geosite:cn`, `geoip:cn`, direct routing, and proxy fallback.
- [x] Run `cmake --build build -j && ctest --test-dir build --output-on-failure` and verify the new tests fail for missing behavior.

### Task 2: Implement Profile Defaults Cleanly

- [x] Add `profile`, `assetDir`, and target asset path fields to `AppConfig`.
- [x] Set default profile to `bypass-cn` in config defaults.
- [x] Persist fields in `store.cpp` using simple YAML keys.
- [x] Run tests and verify profile-related tests now pass where they do not depend on exporter routing.

### Task 3: Rewrite Export Routing Per Target

- [x] Rewrite Mihomo route injection so `bypass-cn` output has LAN/CN direct rules and `MATCH,PROXY` fallback.
- [x] Rewrite sing-box route injection so `bypass-cn` output has local/geosite/geoip direct rules and `final: PROXY`.
- [x] Rewrite Xray route injection so `bypass-cn` output has private/CN direct rules and otherwise proxies.
- [x] Keep exporter code small and target-specific; do not pile feature flags into one giant helper.
- [x] Run tests after each target exporter change.

### Task 4: Add Asset Management

- [x] Add `asset list`, `asset validate`, and `asset update` commands.
- [x] Use existing fetch code for downloads where practical; keep network behavior explicit and readable.
- [x] `asset validate` should report missing configured files and return non-zero when required profile assets are absent.
- [x] Document that `export` can generate config without downloading assets, but direct core run requires assets to be in the configured locations.

### Task 5: Verify And Document Direct Core Use

- [x] Run `cmake -S . -B build && cmake --build build -j`.
- [x] Run `ctest --test-dir build --output-on-failure`.
- [x] Run targeted CLI smoke commands for `export mihomo`, `export sing-box`, `export xray` using a local file subscription.
- [x] Update docs with `mihomo -f`, `sing-box run -c`, and `xray run -config` commands.
- [x] Explicitly document that Xray transparent/TUN behavior needs OS-level redirect plumbing.
