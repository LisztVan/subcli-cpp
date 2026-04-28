# Template Policy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `template_policy` to profile-driven export so users can control replace/append/merge/reject per target path while preserving backward compatibility.

**Architecture:** Extend `ResolvedProfile` with a typed template policy model, parse and validate it in `profile.cpp`, then apply policy in a shared exporter layer for JSON/YAML operations. Wire each target exporter (sing-box, Xray, Mihomo) to produce generated sections first, then apply policy action by path. Validate unsupported combinations via CLI validation and keep `reject` non-fatal with warnings.

**Tech Stack:** C++17, nlohmann_json, yaml-cpp, CMake, existing `subcli_tests` + CLI smoke.

---

## File Structure (Planned Changes)

- Modify: `include/subcli/profile.hpp`
  - Add template policy structs (`ProfileTemplatePolicy`, target/path map fields).
- Modify: `src/profile.cpp`
  - Parse and validate `template_policy` target/path/action.
- Modify: `src/exporter_internal.hpp`
  - Add action enum + policy resolution/apply declarations.
- Modify: `src/exporter_common.cpp`
  - Implement defaults, path support matrix, JSON/YAML policy apply helpers.
- Modify: `src/exporter_singbox.cpp`
  - Apply per-path policy for `outbounds`, `dns.servers`, `dns.rules`, `route.rules`, `route.rule_set`.
- Modify: `src/exporter_xray.cpp`
  - Apply per-path policy for `outbounds`, `dns.servers`, `routing.rules`, `routing.balancers`.
- Modify: `src/exporter_mihomo.cpp`
  - Apply per-path policy for `proxies`, `proxy-groups`, `dns`, `rules`.
- Modify: `src/main.cpp`
  - Extend `profile validate` and `template validate` checks.
- Modify: `tests/subcli_tests.cpp`
  - Add loader and exporter tests for template policy behavior and warnings.
- Modify: `docs/profile-schema.md`, `README.md`, `README.subcli.md`
  - Document policy schema, action semantics, and examples.

## Task 1: Add Profile Template Policy Model and Loader

**Files:**
- Modify: `include/subcli/profile.hpp`
- Modify: `src/profile.cpp`
- Test: `tests/subcli_tests.cpp`

- [x] **Step 1: Write failing loader tests**

Add tests:
- `testLoadProfileReadsTemplatePolicy`
- `testLoadProfileRejectsUnknownTemplatePolicyTarget`
- `testLoadProfileRejectsUnknownTemplatePolicyPath`
- `testLoadProfileRejectsUnknownTemplatePolicyAction`

Use this profile snippet in tests:

```json
{
  "version": 1,
  "name": "policy-test",
  "template_policy": {
    "targets": {
      "sing-box": {"paths": {"outbounds": "merge", "route.rules": "reject"}},
      "mihomo": {"paths": {"rules": "append"}}
    }
  }
}
```

- [x] **Step 2: Run targeted test and verify failure**

Run:
`ctest --test-dir build --output-on-failure -R subcli_tests`

Expected: new tests fail because policy fields are not parsed/validated yet.

- [x] **Step 3: Implement model in `profile.hpp`**

Add `<map>` include and structs:

```cpp
struct ProfileTemplatePolicyTarget {
    std::map<std::string, std::string> pathActions;
};

struct ProfileTemplatePolicy {
    std::map<std::string, ProfileTemplatePolicyTarget> targets;
};
```

Add field in `ResolvedProfile`:

```cpp
ProfileTemplatePolicy templatePolicy;
```

- [x] **Step 4: Implement parsing in `src/profile.cpp`**

Add helpers:
- `isSupportedTemplatePolicyTarget(...)`
- `isSupportedTemplatePolicyPath(...)`
- `isSupportedTemplatePolicyAction(...)`

Parse path:
- `template_policy` object
- `targets` object
- each target `paths` object
- each value must be string action

Store into:
`parsed.templatePolicy.targets[target].pathActions[path] = action;`

- [x] **Step 5: Re-run tests and verify pass**

Run:
`cmake --build build -j && ctest --test-dir build --output-on-failure`

Expected: all tests pass.

## Task 2: Add Shared Template Policy Engine

**Files:**
- Modify: `src/exporter_internal.hpp`
- Modify: `src/exporter_common.cpp`
- Test: `tests/subcli_tests.cpp`

- [x] **Step 1: Add failing integration tests for reject warning shape**

Add at least one export test asserting:
- export succeeds
- warning code includes `template_policy_reject_preserved`
- warning message contains rejected path

- [x] **Step 2: Add action enum and policy API declarations**

In `src/exporter_internal.hpp` add:

```cpp
enum class TemplatePolicyAction { Replace, Append, Merge, Reject };
```

Plus declarations for:
- action parse
- target/path support check
- default action resolution
- apply JSON/YAML helpers

- [x] **Step 3: Implement path support matrix and default actions**

Implement in `src/exporter_common.cpp`:
- support matrix by target/path
- defaults:
  - sing-box: merge for `outbounds`,`dns.servers`,`route.rule_set`; replace for rules arrays
  - xray: merge for `outbounds`,`routing.balancers`; replace for `dns.servers`,`routing.rules`
  - mihomo: merge for `proxies`,`proxy-groups`,`dns`; replace for `rules`

- [x] **Step 4: Implement JSON apply helpers**

Implement helpers for:
- replace object/array field
- append array
- merge keyed array by key (`tag`/`name`)
- reject (skip write + warning)

- [x] **Step 5: Implement YAML apply helpers**

Implement helpers for:
- replace/merge map
- append sequence
- merge keyed sequence
- reject behavior with warning

- [x] **Step 6: Re-run tests**

Run:
`cmake --build build -j && ctest --test-dir build --output-on-failure`

Expected: all tests pass with shared helpers compiled and linked.

## Task 3: Integrate Policy into sing-box Exporter

**Files:**
- Modify: `src/exporter_singbox.cpp`
- Test: `tests/subcli_tests.cpp`

- [x] **Step 1: Add failing sing-box policy tests**

Add tests:
- `testSingBoxTemplatePolicyMergeOutbounds`
- `testSingBoxTemplatePolicyRejectOutboundsPreservesTemplate`
- `testSingBoxTemplatePolicyAppendDnsRules`
- `testSingBoxTemplatePolicyRejectRouteRulesPreservesTemplate`
- `testSingBoxTemplatePolicyMergeRuleSetByTag`
- `testSingBoxTemplatePolicyMergeDnsServersKeepsUntaggedTemplateEntries`

- [x] **Step 2: Refactor exporter to materialize generated sections first**

Create local generated values before writing root:
- generated outbounds
- generated dns servers/rules
- generated route rules/rule_set

- [x] **Step 3: Apply policy actions by path**

Apply engine by path:
- `outbounds`
- `dns.servers`
- `dns.rules`
- `route.rules`
- `route.rule_set`

Special merge rule for `dns.servers`:
- merge by `tag` when present
- preserve template entries without `tag`

- [x] **Step 4: Re-run tests**

Run:
`cmake --build build -j && ctest --test-dir build --output-on-failure`

Expected: sing-box policy tests pass; no regressions.

## Task 4: Integrate Policy into Xray Exporter

**Files:**
- Modify: `src/exporter_xray.cpp`
- Test: `tests/subcli_tests.cpp`

- [x] **Step 1: Add failing Xray policy tests**

Add tests:
- `testXrayTemplatePolicyMergeOutboundsByTag`
- `testXrayTemplatePolicyMergeBalancersByTag`
- `testXrayTemplatePolicyRejectRoutingRulesPreservesTemplate`
- `testXrayTemplatePolicyReplaceDnsServers`
- `testXrayTemplatePolicyRejectOutboundsPreservesTemplate`

- [x] **Step 2: Refactor exporter to materialize generated sections first**

Build generated values for:
- `outbounds`
- `routing.rules`
- `routing.balancers`
- `dns.servers`

- [x] **Step 3: Apply policy actions by path**

Apply:
- merge-enabled: `outbounds`, `routing.balancers`
- no-merge v1: `dns.servers`, `routing.rules` (replace/append/reject only)

- [x] **Step 4: Re-run tests**

Run:
`cmake --build build -j && ctest --test-dir build --output-on-failure`

Expected: Xray policy tests pass; no regressions.

## Task 5: Integrate Policy into Mihomo Exporter

**Files:**
- Modify: `src/exporter_mihomo.cpp`
- Test: `tests/subcli_tests.cpp`

- [x] **Step 1: Add failing Mihomo policy tests**

Add tests:
- `testMihomoTemplatePolicyMergeProxiesByName`
- `testMihomoTemplatePolicyMergeProxyGroupsByName`
- `testMihomoTemplatePolicyRejectRulesPreservesTemplate`
- `testMihomoTemplatePolicyRejectDnsPreservesTemplate`
- `testMihomoTemplatePolicyAppendRules`

- [x] **Step 2: Refactor exporter to materialize generated YAML sections first**

Materialize:
- proxies
- proxy-groups
- dns map
- rules sequence

- [x] **Step 3: Apply policy actions by path**

Apply:
- keyed merge: `proxies`, `proxy-groups` by `name`
- map merge: `dns`
- arrays: `rules` replace/append/reject

- [x] **Step 4: Re-run tests**

Run:
`cmake --build build -j && ctest --test-dir build --output-on-failure`

Expected: Mihomo policy tests pass; no regressions.

## Task 6: Extend Validation Commands

**Files:**
- Modify: `src/main.cpp`
- Test: `tests/subcli_tests.cpp`

- [x] **Step 1: Add failing validation tests**

Add tests for:
- `profile validate` rejects unsupported target/path/action
- `profile validate` rejects invalid `merge` on non-merge paths
- `template validate` rejects invalid JSON/YAML template syntax

- [x] **Step 2: Implement `profile validate` policy checks**

Use shared support matrix to validate:
- target/path known
- action known
- action compatible with path type

- [x] **Step 3: Implement `template validate` format checks**

Validate that:
- sing-box/Xray template parses as JSON object
- Mihomo template parses as YAML node/map

- [x] **Step 4: Re-run tests**

Run:
`cmake --build build -j && ctest --test-dir build --output-on-failure`

Expected: validation tests pass.

## Task 7: Update Docs

**Files:**
- Modify: `docs/profile-schema.md`
- Modify: `README.md`
- Modify: `README.subcli.md`

- [x] **Step 1: Document `template_policy` schema and examples**

Document:
- top-level shape
- action semantics
- reject warning behavior
- per-target path/action matrix
- examples for each target

- [x] **Step 2: Cross-check docs with implemented behavior**

Ensure docs reflect v1 constraints:
- Xray `dns.servers` no merge in v1
- sing-box `dns.servers` merge keeps untagged template entries

## Task 8: Full Verification and Regression Sweep

**Files:**
- Verify working tree changes only in expected files above.

- [x] **Step 1: Full build and tests**

Run:
`cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`

- [x] **Step 2: Manual CLI verification**

Run:
- `./build/subcli profile validate ./profiles/bypass-cn.json`
- `./build/subcli template validate`

Create one temporary profile with `reject` paths and verify:
- export succeeds
- warning appears per path

- [x] **Step 3: Final checklist**

Confirm:
- no behavior change without `template_policy`
- `reject` is non-fatal and preserves template content
- unsupported merge combinations fail at validate stage

## Spec Coverage Self-Review

- `template_policy` in profile schema: covered by Task 1 + Task 7.
- shared strategy layer: covered by Task 2.
- sing-box/Xray/Mihomo integration: covered by Tasks 3/4/5.
- `reject` warning per path: covered by Tasks 2/3/4/5/8.
- stronger validate flow: covered by Task 6.
- backward compatibility/default behavior: covered by Tasks 2 and 8.

No placeholders (`TODO`/`TBD`) remain; all tasks specify files, tests, and commands.
