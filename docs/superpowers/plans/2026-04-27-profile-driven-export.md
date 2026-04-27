# Profile-Driven Config Export Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `subcli` a non-GUI, profile-file-driven config generator where `config.yaml` stores only app settings and external JSON profiles define routing, strategy groups, DNS, and target export policy.

**Architecture:** Add a target-neutral profile model loaded from JSON, then render that model into Mihomo, sing-box, and Xray native config semantics during export. Templates remain output skeletons; subscriptions remain node sources; profile JSON becomes the primary policy source. Core `run` lifecycle work is out of scope.

**Tech Stack:** C++17, nlohmann_json, yaml-cpp, existing `subcli` exporter architecture, `tests/subcli_tests.cpp`, `tests/cli_smoke.sh`.

---

## Scope

Implement:
- External JSON profile model and loader.
- Built-in profiles: `bypass-cn`, `global`, `direct`.
- `config.yaml`/profile separation with `profile_path` support.
- Export-time profile resolution and validation.
- Profile-driven strategy groups, routing rules, DNS, and default outbound.
- Target-native rendering for Mihomo, sing-box, and Xray.
- Profile CLI commands and docs.

Do not implement:
- GUI.
- New core `run` lifecycle behavior.
- Full online rule/profile marketplace.
- Visual profile editing.

## Profile JSON Schema, Phase 1

```json
{
  "version": 1,
  "name": "bypass-cn",
  "description": "Direct private and China traffic, proxy everything else",
  "default_outbound": "PROXY",
  "dns": {
    "mode": "fake-ip",
    "strategy": "prefer_ipv4",
    "direct_servers": ["https://dns.alidns.com/dns-query", "https://doh.pub/dns-query"],
    "remote_servers": ["https://1.1.1.1/dns-query"]
  },
  "groups": [
    {
      "tag": "PROXY",
      "type": "select",
      "members": ["AUTO", "DIRECT", "REGION:*"],
      "default": "AUTO"
    },
    {
      "tag": "AUTO",
      "type": "url-test",
      "members": ["REGION:*"],
      "url": "http://www.gstatic.com/generate_204",
      "interval": 300
    }
  ],
  "rules": [
    {"type": "geosite", "value": "private", "outbound": "DIRECT"},
    {"type": "geoip", "value": "private", "outbound": "DIRECT"},
    {"type": "geosite", "value": "cn", "outbound": "DIRECT"},
    {"type": "geoip", "value": "cn", "outbound": "DIRECT"},
    {"type": "final", "outbound": "PROXY"}
  ]
}
```

Supported rule types: `geosite`, `geoip`, `domain`, `domain_suffix`, `domain_keyword`, `ip_cidr`, `port`, `network`, `final`.

Supported group types: `select`, `url-test`, `fallback`, `load-balance`.

Special members:
- `REGION:*`: all generated region groups.
- `REGION:<name>`: one generated region group if present.
- `NODE:*`: all generated node names.
- Other strings are literal outbound/group tags such as `DIRECT`, `PROXY`, `AUTO`.

## File Structure

Create:
- `include/subcli/profile.hpp`: profile structs and loader declarations.
- `src/profile.cpp`: JSON parsing and validation.
- `profiles/bypass-cn.json`: built-in bypass profile.
- `profiles/global.json`: built-in global profile.
- `profiles/direct.json`: built-in direct profile.
- `docs/profile-schema.md`: profile authoring documentation.

Modify:
- `include/subcli/models.hpp`: add `profilePath`; keep legacy fields during migration.
- `src/store.cpp`: load/save `profile_path`.
- `src/main.cpp`: profile path resolution, profile CLI, export override.
- `include/subcli/exporter.hpp`: pass resolved profile into exporters.
- `src/exporter.cpp`: dispatch profile-aware exporters.
- `src/exporter_internal.hpp`: shared profile helpers.
- `src/exporter_common.cpp`: member expansion helper.
- `src/exporter_mihomo.cpp`: profile groups/rules/DNS rendering.
- `src/exporter_singbox.cpp`: profile groups/rules/DNS rendering.
- `src/exporter_xray.cpp`: profile routing/balancer/DNS rendering.
- `CMakeLists.txt`: compile profile loader and install profiles.
- `README.md`, `README.subcli.md`: document config/profile split.
- `tests/subcli_tests.cpp`, `tests/cli_smoke.sh`: coverage.

## Task 1: Add Profile Model And Loader

**Files:**
- Create: `include/subcli/profile.hpp`
- Create: `src/profile.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/subcli_tests.cpp`

- [ ] Write failing tests `testLoadProfileReadsGroupsRulesAndDns`, `testLoadProfileRejectsInvalidJson`, `testLoadProfileRejectsRuleWithoutOutbound`.
- [ ] Run `cmake --build build -j2 && ctest --test-dir build --output-on-failure`; expected failure due missing symbols.
- [ ] Implement profile structs and `bool loadProfile(const std::string& path, ResolvedProfile& profile, std::string& error)`.
- [ ] Validate `version == 1`, non-empty `name`, and every non-final rule has `type` and `outbound`.
- [ ] Add `src/profile.cpp` to both `subcli` and `subcli_tests` targets.
- [ ] Run build/tests; expected pass.
- [ ] Commit with message `Add profile JSON loader`.

## Task 2: Add Built-In Profile Files

**Files:**
- Create: `profiles/bypass-cn.json`
- Create: `profiles/global.json`
- Create: `profiles/direct.json`
- Modify: `CMakeLists.txt`
- Test: `tests/subcli_tests.cpp`

- [ ] Add tests that all three files exist under `SUBCLI_SOURCE_DIR` and load via `loadProfile`.
- [ ] Run tests; expected failure because files do not exist.
- [ ] Add built-in JSON profile files.
- [ ] Install `profiles/` to `${CMAKE_INSTALL_DATADIR}/subcli/profiles`.
- [ ] Run build/tests; expected pass.
- [ ] Commit with message `Add built-in export profiles`.

## Task 3: Separate Config From Profile Policy

**Files:**
- Modify: `include/subcli/models.hpp`
- Modify: `src/store.cpp`
- Modify: `src/main.cpp`
- Modify: `README.subcli.md`
- Test: `tests/subcli_tests.cpp`

- [ ] Add failing test that `profile_path` persists through `saveConfig`/`loadConfig`.
- [ ] Add failing CLI/config tests for `config set profile_path`, `config get profile_path`, and `config remove profile_path`.
- [ ] Add `std::string profilePath` to `AppConfig`.
- [ ] Load/save YAML key `profile_path`.
- [ ] Implement config command handling for `profile_path`; path values resolve from config dir.
- [ ] Keep existing `profile` key supported for built-in profile names.
- [ ] Run build/tests; expected pass.
- [ ] Commit with message `Separate profile path from app config`.

## Task 4: Resolve Profile At Export Time

**Files:**
- Modify: `src/main.cpp`
- Modify: `include/subcli/exporter.hpp`
- Modify: `src/exporter.cpp`
- Modify: `src/exporter_internal.hpp`
- Test: `tests/subcli_tests.cpp`

- [ ] Add tests for missing profile path and invalid profile JSON causing export failure.
- [ ] Implement profile path resolution: explicit `profilePath` wins, otherwise built-in name from `profile` resolves to `profiles/<name>.json`.
- [ ] Load profile once in `doExportCommand` before target rendering.
- [ ] Extend exporter API to accept `ResolvedProfile` while preserving compatibility wrappers during migration.
- [ ] Run build/tests; expected pass.
- [ ] Commit with message `Load export profile before rendering`.

## Task 5: Expand Profile Members

**Files:**
- Modify: `src/exporter_common.cpp`
- Modify: `src/exporter_internal.hpp`
- Test: `tests/subcli_tests.cpp`

- [ ] Add tests for `REGION:*`, `REGION:HK`, `NODE:*`, and literal member expansion.
- [ ] Implement `expandProfileMembers(...)` using generated region order and supported nodes.
- [ ] Preserve literal tags when no special prefix matches.
- [ ] Run build/tests; expected pass.
- [ ] Commit with message `Expand profile group members`.

## Task 6: Render Profile Groups For Mihomo

**Files:**
- Modify: `src/exporter_mihomo.cpp`
- Test: `tests/subcli_tests.cpp`

- [ ] Add tests for Mihomo rendering `select`, `url-test`, `fallback`, and `load-balance` from profile groups.
- [ ] Replace primary `config.strategyGroups` rendering with `profile.groups`.
- [ ] Render `url`/`interval` for `url-test` and `fallback`.
- [ ] Render `strategy` for `load-balance`, defaulting to `round-robin`.
- [ ] Keep generated region groups available when profile references `REGION:*`.
- [ ] Run build/tests; expected pass.
- [ ] Commit with message `Render profile groups for Mihomo`.

## Task 7: Render Profile Rules And DNS For Mihomo

**Files:**
- Modify: `src/exporter_mihomo.cpp`
- Test: `tests/subcli_tests.cpp`

- [ ] Add tests for profile DNS rendering and each supported rule type.
- [ ] Remove hardcoded C++ bypass-cn rule injection from the profile-aware path.
- [ ] Map profile rules to Mihomo rule strings: `GEOSITE`, `GEOIP`, `DOMAIN`, `DOMAIN-SUFFIX`, `DOMAIN-KEYWORD`, `IP-CIDR`, `DST-PORT`, `NETWORK`, `MATCH`.
- [ ] Use profile DNS `mode`, `direct_servers`, and `remote_servers` where applicable.
- [ ] Run build/tests; expected pass.
- [ ] Commit with message `Render profile routing for Mihomo`.

## Task 8: Render Profile Groups For sing-box

**Files:**
- Modify: `src/exporter_singbox.cpp`
- Test: `tests/subcli_tests.cpp`

- [ ] Add tests for sing-box rendering profile `select` and `url-test` groups.
- [ ] Add tests that `fallback` degrades to `urltest` with warning `profile_group_degraded`.
- [ ] Add tests that `load-balance` degrades to `selector` with warning `profile_group_degraded`.
- [ ] Render profile groups as sing-box outbounds.
- [ ] Run build/tests; expected pass.
- [ ] Commit with message `Render profile groups for sing-box`.

## Task 9: Render Profile Rules And DNS For sing-box

**Files:**
- Modify: `src/exporter_singbox.cpp`
- Test: `tests/subcli_tests.cpp`

- [ ] Add tests for sing-box profile DNS, final outbound, rule-set, private, domain, CIDR, port, and network rules.
- [ ] Map `geosite:cn` and `geoip:cn` to configured local rule sets.
- [ ] Map private rules to sing-box private domain/IP forms.
- [ ] Map direct literal rule types to sing-box route rules.
- [ ] Run build/tests; expected pass.
- [ ] Commit with message `Render profile routing for sing-box`.

## Task 10: Render Profile Routing For Xray

**Files:**
- Modify: `src/exporter_xray.cpp`
- Test: `tests/subcli_tests.cpp`

- [ ] Add tests for Xray profile DNS, geosite/geoip/domain/IP/final rules.
- [ ] Add tests for Xray balancer generation from profile groups.
- [ ] Map `url-test` to `leastPing`, `load-balance` to `leastLoad`, and unsupported/lossy mappings to explicit warnings.
- [ ] Keep managed node tags as `SUBCLI_00001` style.
- [ ] Run build/tests; expected pass.
- [ ] Commit with message `Render profile routing for Xray`.

## Task 11: Add Profile CLI Commands

**Files:**
- Modify: `src/main.cpp`
- Modify: `tests/cli_smoke.sh`
- Test: `tests/subcli_tests.cpp`

- [ ] Add command forms: `subcli profile list`, `subcli profile get <name>`, `subcli profile validate <path>`, and `subcli export <target> --profile <path-or-name>`.
- [ ] Add smoke tests for `profile list` and `profile validate profiles/bypass-cn.json`.
- [ ] Ensure `--profile` only overrides the current export and does not mutate `config.yaml`.
- [ ] Run build/tests; expected pass.
- [ ] Commit with message `Add profile CLI commands`.

## Task 12: Update Docs And Migration Notes

**Files:**
- Create: `docs/profile-schema.md`
- Modify: `README.md`
- Modify: `README.subcli.md`

- [ ] Document: no GUI; core run lifecycle is secondary/out of this phase.
- [ ] Document separation: `config.yaml` app settings, `profile.json` generation policy, templates skeleton, subscriptions node source, assets rule files.
- [ ] Document built-in profiles and schema.
- [ ] Document target-specific degradation warnings.
- [ ] Add custom profile example with strategy groups and routing.
- [ ] Run build/tests; expected pass.
- [ ] Commit with message `Document profile-driven export`.

## Task 13: End-To-End Verification

**Files:**
- No source edits unless verification finds bugs.

- [ ] Run `cmake --build build -j2`.
- [ ] Run `ctest --test-dir build --output-on-failure`.
- [ ] Run `./build/subcli profile list`.
- [ ] Run `./build/subcli profile validate ./profiles/bypass-cn.json`.
- [ ] Run `./build/subcli export mihomo --profile ./profiles/bypass-cn.json --output-dir ./outputs`.
- [ ] Run `./build/subcli export sing-box --profile ./profiles/bypass-cn.json --output-dir ./outputs`.
- [ ] Run `./build/subcli export xray --profile ./profiles/bypass-cn.json --output-dir ./outputs`.
- [ ] Missing asset warnings are acceptable; command failures are not.
- [ ] Commit fixes if needed.

## Review Checklist

- [ ] `config.yaml` no longer owns strategy/routing as primary source.
- [ ] Built-in profile files generate same-or-better behavior than current hardcoded profiles.
- [ ] Custom profile can express strategy groups and routing without C++ changes.
- [ ] Mihomo output uses Mihomo-native semantics.
- [ ] sing-box output uses sing-box-native semantics or warns on lossy mappings.
- [ ] Xray output uses Xray-native semantics or warns on lossy mappings.
- [ ] No GUI code added.
- [ ] No new core `run` lifecycle behavior added.
- [ ] Existing subscription/export flows still work.
