# Code Review Evidence (2026-04-30)

## Environment

- Review date: 2026-04-30
- Repository root path: /home/lisztzy/prj/subcli-cpp
- Execution worktree path: /home/lisztzy/prj/subcli-cpp/.worktrees/code-review-2026-04-30

## Command Evidence

### 1) Working tree snapshot

Command:
```bash
git status --short
```

Exact output:
```text
?? docs/superpowers/reviews/
```

Command:
```bash
git log --oneline -8
```

Exact output:
```text
d684ca1 List nested commands in root help
0728816 Cover root help commands
38013ab List config in root help
56c744e Complete CLI11 subcommand parsing and dual help routing
5c88d52 Improve CLI help discoverability for v0.2.3
4738299 Complete workspace metadata, doctor checks, and docs
781a7d5 Merge branch 'feat/v2-ecosystem-impl'
a7fc158 feat: implement v2 ecosystem workspace flow and diagnostics
```

### 2) Build and test baseline

Command:
```bash
cmake -S . -B build && cmake --build build -j
```

Exact output:
```text
CMake Deprecation Warning at build/_deps/openssl-src/CMakeLists.txt:26 (cmake_minimum_required):
  Compatibility with CMake < 3.10 will be removed from a future version of
  CMake.

  Update the VERSION argument <min> value.  Or, use the <min>...<max> syntax
  to tell CMake that the project requires at least <min> but has been updated
  to work with policies introduced by <max> or earlier.


-- OpenSSL version 1.1.1w
-- Using the multi-header code from /home/lisztzy/prj/subcli-cpp/.worktrees/code-review-2026-04-30/build/_deps/nlohmann_json-src/include/
CMake Deprecation Warning at build/_deps/cli11-src/CMakeLists.txt:1 (cmake_minimum_required):
  Compatibility with CMake < 3.10 will be removed from a future version of
  CMake.

  Update the VERSION argument <min> value.  Or, use the <min>...<max> syntax
  to tell CMake that the project requires at least <min> but has been updated
  to work with policies introduced by <max> or earlier.


-- Using CMake version 4.3.2
-- CMake platform flags: UNIX GCC
-- curl version=[8.10.1-DEV]
-- Picky compiler options: -Werror-implicit-function-declaration -W -Wall -pedantic -Wbad-function-cast -Wconversion -Winline -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wno-long-long -Wno-multichar -Wpointer-arith -Wshadow -Wsign-compare -Wundef -Wunused -Wwrite-strings -Waddress -Wattributes -Wcast-align -Wdeclaration-after-statement -Wdiv-by-zero -Wempty-body -Wendif-labels -Wfloat-equal -Wformat-security -Wignored-qualifiers -Wmissing-field-initializers -Wmissing-noreturn -Wno-format-nonliteral -Wno-system-headers -Wold-style-definition -Wredundant-decls -Wsign-conversion -Wno-error=sign-conversion -Wstrict-prototypes -Wtype-limits -Wunreachable-code -Wunused-parameter -Wvla -Wclobbered -Wmissing-parameter-type -Wold-style-declaration -Wstrict-aliasing=3 -Wtrampolines -Wformat=2 -Warray-bounds=2 -ftree-vrp -Wduplicated-cond -Wnull-dereference -fdelete-null-pointer-checks -Wshift-negative-value -Wshift-overflow=2 -Walloc-zero -Wduplicated-branches -Wno-format-overflow -Wformat-truncation=2 -Wimplicit-fallthrough -Wrestrict -Warith-conversion -Wdouble-promotion -Wenum-conversion -Wpragmas -Wunused-const-variable
-- Could NOT find ZLIB (missing: ZLIB_LIBRARY ZLIB_INCLUDE_DIR) 
-- Could NOT find Libssh2 (missing: LIBSSH2_INCLUDE_DIR LIBSSH2_LIBRARY) 
-- Protocols: http https ipfs ipns
-- Features: alt-svc AsynchDNS HSTS HTTPS-proxy IPv6 Largefile NTLM SSL threadsafe UnixSockets
-- Enabled SSL backends: OpenSSL
-- Configuring done (1.1s)
-- Generating done (0.2s)
-- Build files have been written to: /home/lisztzy/prj/subcli-cpp/.worktrees/code-review-2026-04-30/build
[  0%] Built target CLI11
[  0%] Built target curl-generate-mk-ca-bundle.1
[  0%] Built target curl-generate-curl-config.1
[  0%] Built target curl-opts-man
[  4%] Built target yaml-cpp
[  4%] Built target curl-man
[  5%] Built target yaml-cpp-sandbox
[  5%] Built target yaml-cpp-parse
[  5%] Built target yaml-cpp-read
[  5%] Building C object _deps/openssl-build/crypto/CMakeFiles/crypto.dir/cversion.c.o
[  5%] Linking C static library libcrypto.a
[ 71%] Built target crypto
[ 76%] Built target ssl
[ 94%] Built target libcurl_static
[ 94%] Linking CXX executable subcli
[ 94%] Linking CXX executable subcli_tests
[ 97%] Built target subcli_tests
[100%] Built target subcli
```

Command:
```bash
ctest --test-dir build --output-on-failure
```

Exact output:
```text
Test project /home/lisztzy/prj/subcli-cpp/.worktrees/code-review-2026-04-30/build
    Start 1: subcli_tests
1/2 Test #1: subcli_tests .....................   Passed    4.10 sec
    Start 2: subcli_cli_smoke
2/2 Test #2: subcli_cli_smoke .................   Passed    0.36 sec

100% tests passed, 0 tests failed out of 2

Total Test time (real) =   4.46 sec
```

### Baseline verification result

- Configure/build result: pass; last success line: `[100%] Built target subcli`.
- Test suite result: pass; totals: `100% tests passed, 0 tests failed out of 2`.
- Immediate blocker detected: no; baseline configure/build/test completed successfully.

## Reviewed Source References

- Required format: `path:line-ish - reason inspected`
- Minimum per finding: 2 source references
- Example: `src/main.cpp:120-165 - verified CLI option handling and error contract`

## Task 4-6 Evidence Transcript Completeness

- Purpose: auditor-facing status marker for Task 4-6 probe transcript completeness in this evidence file.
- Current status: Task 4, Task 5, and Task 6 command/output transcripts are captured below; remaining work should be logged as new probe blocks rather than treated as missing transcript ingestion.

## Task 3) Feature-completion + documentation-consistency audit

### 3.1 CLI surface

<a id="probe-3-1-root-help"></a>
Command:
```bash
./build/subcli --help
```

Exact output:
```text
Usage:
  subcli [--workspace DIR] <command> [args...]

Global Options:
  --workspace DIR  Use a workspace for this invocation.

Commands:
  init      Create config/data/cache/state/output directories.
  doctor    Check dirs, templates, assets, and core paths.
  sub       list/add/edit/remove/enable/disable/update/validate subscriptions.
  config    list/get/set/remove application settings.
  profile   list/get/validate/explain export profiles.
  template  list/get/set/reset/validate export templates.
  asset     list/status/validate/update geo and rule assets.
  export    all/mihomo/sing-box/xray configs.

Workspace:
  workspace init/status/use/unset/migrate/doctor workspace roots.

Runtime Helpers (optional):
  check     Validate exported config with installed core.
  run       Run one core with an exported config.
  daemon    once/run/start/stop/status periodic helper.
  status    Show helper process status.
  stop      Stop a helper process.
  restart   Restart a helper process.

Shell:
  completion  Generate shell completion scripts.

Examples:
  subcli init
  subcli doctor
  subcli template list
  subcli profile list
  subcli sub add --name airport-a --url https://example/sub
  subcli sub update
  subcli export all --profile bypass-cn --check

Help:
  subcli <command> --help
  subcli template --help
  subcli export --help
```

<a id="probe-3-1-workspace-help"></a>
Command:
```bash
./build/subcli workspace --help
```

Exact output:
```text
Usage:
  subcli workspace init [DIR]
  subcli workspace status [--json]
  subcli workspace use DIR
  subcli workspace unset
  subcli workspace migrate [--to DIR] [--from DIR] [--dry-run] [--overwrite]
  subcli workspace doctor

Examples:
  subcli workspace init ./ws
  subcli workspace use ./ws
  subcli workspace status --json
  subcli workspace migrate --to ./ws --dry-run
  subcli workspace doctor
```

<a id="probe-3-1-sub-help"></a>
Command:
```bash
./build/subcli sub --help
```

Exact output:
```text
Usage:
  subcli sub list [--json]
  subcli sub add --name NAME --url URL [options]
  subcli sub edit <id|name> [options]
  subcli sub remove <id|name>
  subcli sub enable <id|name>
  subcli sub disable <id|name>
  subcli sub update [id-or-name ...] [--tag TAG] [--strict-network]
  subcli sub validate [id-or-name]

Add/Edit Options:
  --id ID
  --name NAME
  --url URL
  --group GROUP
  --format-hint auto|mihomo|sing-box|xray|uri
  --user-agent VALUE
  --timeout SEC
  --retry N
  --priority N
  --update-interval SEC
  --tag TAG
  --tags a,b
  --header 'Key: Value'
  --remove-header Key
  --clear-headers
  --force
  --enable
  --disable

Examples:
  subcli sub list
  subcli sub add --name airport-a --url https://example/sub
  subcli sub edit airport-a --tag hk --priority 20
  subcli sub update --tag hk
  subcli sub validate airport-a
```

<a id="probe-3-1-export-help"></a>
Command:
```bash
./build/subcli export --help
```

Exact output:
```text
Usage:
  subcli export <all|mihomo|sing-box|xray> [options]

Options:
  --tun
  --check
  --check-timeout SEC
  --output-dir DIR
  --profile PATH_OR_NAME
  --sub ID_OR_NAME
  --tag TAG
  --strict-network
  --strict-capabilities
  --download-assets
  --explain-policy
  --json

Examples:
  subcli export all
  subcli export all --profile bypass-cn --check
  subcli export sing-box --output-dir ./outputs --check --check-timeout 30
  subcli export mihomo --tag hk --strict-network
  subcli export xray --profile /path/to/custom-profile.json --json
```

<a id="probe-3-1-profile-help"></a>
Command:
```bash
./build/subcli profile --help
```

Exact output:
```text
Usage:
  subcli profile list
  subcli profile get <bypass-cn|global|direct>
  subcli profile validate <path>
  subcli profile explain <path-or-name> [--target <all|mihomo|sing-box|xray>] [--json]

Built-in Profiles:
  bypass-cn
  global
  direct

Examples:
  subcli profile list
  subcli profile get bypass-cn
  subcli profile validate ./profiles/bypass-cn.json
  subcli profile explain bypass-cn --target all
  subcli profile explain ./profiles/custom.json --json
```

<a id="probe-3-1-template-help"></a>
Command:
```bash
./build/subcli template --help
```

Exact output:
```text
Usage:
  subcli template list [--json]
  subcli template get <target> <kind>
  subcli template set <target> <kind> <path>
  subcli template reset [target] [kind]
  subcli template validate [--json]

Targets:
  mihomo
  sing-box
  xray

Kinds:
  normal
  tun

Commands:
  list      Show template paths and whether files exist.
  get       Print one configured template path.
  set       Set one template path from a local file path.
  reset     Reset all templates, one target, or one target kind.
  validate  Check that template files exist and parse correctly.

Examples:
  subcli template list
  subcli template list --json
  subcli template get sing-box normal
  subcli template set sing-box normal ./templates/singbox_base.json
  subcli template reset xray tun
  subcli template validate

Next:
  subcli config list
  subcli export all --profile bypass-cn
```

<a id="probe-3-1-asset-help"></a>
Command:
```bash
./build/subcli asset --help
```

Exact output:
```text
Usage:
  subcli asset list
  subcli asset status
  subcli asset validate
  subcli asset update [asset-key]

Commands:
  list      Show configured asset key/path/url entries.
  status    Show local status, size, update time, and source.
  validate  Fail if required assets are missing.
  update    Download all assets or one specified asset key.

Examples:
  subcli asset list
  subcli asset status
  subcli asset update
  subcli asset update xray.geoip
```

<a id="probe-3-1-config-help"></a>
Command:
```bash
./build/subcli config --help
```

Exact output:
```text
Usage:
  subcli config list [--json]
  subcli config get <key>
  subcli config set <key> <value>
  subcli config remove <key>

Common Keys:
  tun
  profile
  profile_path
  output_dir
  template_dir
  asset_dir
  parallelism
  timeout
  retry
  fetch_max_bytes
  log_level
  core_paths.mihomo
  core_paths.sing_box
  core_paths.xray
  node_management.dedupe
  node_management.rename_template
  node_management.include_regex
  node_management.exclude_regex
  node_management.sort_by
  grouping.region_rules.<REGION>
  assets.paths.<asset-key>
  assets.urls.<asset-key>
  templates.<target>.<normal|tun>

Examples:
  subcli config list
  subcli config get core_paths.sing_box
  subcli config set profile bypass-cn

Note:
  Template paths are easier to manage with 'subcli template --help'.
```

### 3.2 Path precedence behavior

Reconciliation note:

- `doctor --json` and `workspace status --json` report different semantics, so different roots can both be correct in same worktree run.
- `doctor --json` shows the runtime workspace resolver result for current invocation (`active_workspace_root`) and trace selected `platform default` because no marker was found from cwd upward in that invocation context.
- `workspace status --json` reports workspace command status root (`active_root`) for the current directory/worktree context, which can remain the worktree root even when doctor runtime resolution picked platform default.
- Conclusion for this evidence set: outputs are not contradictory by themselves; they indicate two command-specific views that still leave successful env-override/conflict precedence behavior unverified.

<a id="probe-3-2-doctor-default"></a>
Command:
```bash
./build/subcli doctor --json
```

Key excerpt (decisive lines):
```text
"active_workspace_root":"/home/lisztzy/.local/share/subcli"
"resolution_source":"platform_default"
"trace":["resolution order: cli --workspace > SUBCLI_WORKSPACE > marker discovery > persisted default > platform default", ... ,"platform default selected: /home/lisztzy/.local/share/subcli"]
```

<a id="probe-3-2-doctor-env-invalid"></a>
Command:
```bash
SUBCLI_WORKSPACE="/tmp/subcli-review-ws" ./build/subcli doctor --json
```

Exact output:
```text
environment resolution failed: invalid SUBCLI_WORKSPACE directory: /tmp/subcli-review-ws
```

<a id="probe-3-2-workspace-status"></a>
Command:
```bash
./build/subcli workspace status --json
```

Exact output:
```json
{"active_root":"/home/lisztzy/prj/subcli-cpp/.worktrees/code-review-2026-04-30","active_root_has_legacy_marker":false,"default_root":"","default_root_exists":false,"default_root_has_marker":false,"has_default":false,"metadata":{"created_at":"","description":"","env_version":0,"error":"","exists":false,"name":"","valid":false},"persisted_path":"/home/lisztzy/.config/subcli/workspace-default"}
```

### 3.3 Doc/code anchors

<a id="probe-3-3-anchor-rg"></a>
Command:
```bash
rg "template_policy|default_outbound|capability_unsupported|capability_degraded|strict" src include tests
```

Exact output:
```text
src/exporter_mihomo.cpp:            result.warnings.push_back({"capability_unsupported", node.name + ": " + reason});
include/subcli/daemon.hpp:    bool strictNetwork = false;
src/daemon.cpp:        {"strict_network", status.options.strictNetwork},
src/daemon.cpp:    if (options.strictNetwork) {
src/daemon.cpp:        args.push_back("--strict-network");
src/daemon.cpp:    if (options.strictNetwork) {
src/daemon.cpp:        args.push_back("--strict-network");
src/daemon.cpp:    if (options.strictNetwork) {
src/daemon.cpp:        args.push_back("--strict-network");
src/daemon.cpp:    status.options.strictNetwork = parsed.value("strict_network", false);
src/profile.cpp:    if (root.contains("default_outbound") && root["default_outbound"].is_string()) {
src/profile.cpp:        parsed.defaultOutbound = root["default_outbound"].get<std::string>();
src/profile.cpp:    const auto templatePolicyIt = root.find("template_policy");
src/profile.cpp:            error = "profile template_policy must be an object";
src/profile.cpp:                error = "profile template_policy.targets must be an object";
src/profile.cpp:                    error = "unsupported template_policy target: " + target;
src/profile.cpp:                    error = "profile template_policy.targets." + target + " must be an object";
src/profile.cpp:                    error = "profile template_policy.targets." + target + ".paths must be an object";
src/profile.cpp:                            error = "conflicting template_policy paths for " + target + ": " + seenPath + " and " + path;
src/profile.cpp:                        error = "unsupported template_policy path for " + target + ": " + path;
src/profile.cpp:                        error = "template_policy action must be a string for path: " + path;
src/profile.cpp:                        error = "unsupported template_policy action: " + action;
src/profile.cpp:                        error = "unsupported template_policy action for " + target + " path " + path + ": " + action;
src/exporter_xray.cpp:        warnings.push_back({"capability_degraded", target + ": unresolved route target, using PROXY"});
src/exporter_xray.cpp:    warnings.push_back({"capability_degraded", target + ": unresolved route target, using DIRECT"});
src/exporter_xray.cpp:            result.warnings.push_back({"capability_unsupported", node.name + ": " + reason});
src/exporter_xray.cpp:                result.warnings.push_back({"capability_degraded", group.tag + ": unresolved member " + member + " omitted"});
src/exporter_xray.cpp:                result.warnings.push_back({"capability_degraded", group.tag + ": no resolvable members, using safe fallback selector"});
src/exporter_xray.cpp:                result.warnings.push_back({"capability_degraded", group.tag + ": select rendered as leastPing balancer"});
src/exporter_xray.cpp:                        result.warnings.push_back({"capability_degraded", group.tag + ": unresolved fallback default " + member + " omitted"});
src/exporter_xray.cpp:                        result.warnings.push_back({"capability_degraded", group.tag + ": fallback rendered as leastPing without fallbackTag"});
src/exporter_xray.cpp:                    result.warnings.push_back({"capability_degraded", group.tag + ": fallback rendered as leastPing without fallbackTag"});
tests/subcli_tests.cpp:  "default_outbound": "AUTO",
tests/subcli_tests.cpp:  "template_policy": {
tests/subcli_tests.cpp:    require(subcli::loadProfile(path.string(), profile, error), "loadProfile should read template_policy: " + error);
tests/subcli_tests.cpp:    require(profile.templatePolicy.targets.size() == 3, "template_policy should read target count");
tests/subcli_tests.cpp:    require(profile.templatePolicy.targets.at("sing-box").pathActions.at("outbounds") == "merge", "template_policy sing-box outbounds should be merge");
tests/subcli_tests.cpp:    require(profile.templatePolicy.targets.at("sing-box").pathActions.at("route.rules") == "reject", "template_policy sing-box route.rules should be reject");
tests/subcli_tests.cpp:    require(profile.templatePolicy.targets.at("xray").pathActions.at("routing.balancers") == "append", "template_policy xray routing.balancers should be append");
tests/subcli_tests.cpp:    require(profile.templatePolicy.targets.at("mihomo").pathActions.at("rules") == "replace", "template_policy mihomo rules should be replace");
tests/subcli_tests.cpp:  "template_policy": {
tests/subcli_tests.cpp:    require(!subcli::loadProfile(path.string(), profile, error), "loadProfile should reject unknown template_policy target");
tests/subcli_tests.cpp:    require(error.find("unsupported template_policy target") != std::string::npos, "unknown template_policy target should return clear error");
tests/subcli_tests.cpp:  "template_policy": {
tests/subcli_tests.cpp:    require(!subcli::loadProfile(path.string(), profile, error), "loadProfile should reject unknown template_policy path");
tests/subcli_tests.cpp:    require(error.find("unsupported template_policy path") != std::string::npos, "unknown template_policy path should return clear error");
tests/subcli_tests.cpp:  "template_policy": {
tests/subcli_tests.cpp:    require(!subcli::loadProfile(path.string(), profile, error), "loadProfile should reject unknown template_policy action");
tests/subcli_tests.cpp:    require(error.find("unsupported template_policy action") != std::string::npos, "unknown template_policy action should return clear error");
tests/subcli_tests.cpp:  "template_policy": {
tests/subcli_tests.cpp:    require(error.find("unsupported template_policy action for sing-box path route.rules") != std::string::npos, "merge on sing-box route.rules should return clear error");
tests/subcli_tests.cpp:  "template_policy": {
tests/subcli_tests.cpp:  "template_policy": {
tests/subcli_tests.cpp:    require(error.find("unsupported template_policy action for xray path dns.servers") != std::string::npos, "merge on xray dns.servers should return clear error");
tests/subcli_tests.cpp:  "template_policy": {
tests/subcli_tests.cpp:  "template_policy": {
tests/subcli_tests.cpp:    require(error.find("unsupported template_policy action for mihomo path dns") != std::string::npos, "append on mihomo dns should return clear error");
tests/subcli_tests.cpp:  "template_policy": {
tests/subcli_tests.cpp:  "template_policy": {
tests/subcli_tests.cpp:    require(!subcli::loadProfile(path.string(), profile, error), "loadProfile should reject template_policy parent-child conflict");
tests/subcli_tests.cpp:    require(error.find("conflicting template_policy paths") != std::string::npos, "parent-child conflict should return clear error");
tests/subcli_tests.cpp:  "default_outbound": "DIRECT",
tests/subcli_tests.cpp:  "template_policy": {
tests/subcli_tests.cpp:    require(profile.defaultOutbound == "DIRECT", "extended profile should override default_outbound");
tests/subcli_tests.cpp:        out << R"json({"version":1,"name":"custom-override","default_outbound":"DIRECT"})json";
tests/subcli_tests.cpp:        if (warning.code == "template_policy_reject_preserved" && warning.message.find("route.rules") != std::string::npos) {
tests/subcli_tests.cpp:    require(hasRejectWarning, "reject policy should emit template_policy_reject_preserved warning with path");
tests/subcli_tests.cpp:        if (warning.code == "template_policy_reject_preserved" && warning.message.find("routing.rules") != std::string::npos) {
tests/subcli_tests.cpp:        if (warning.code == "template_policy_reject_preserved" && warning.message.find("rules") != std::string::npos) {
tests/subcli_tests.cpp:        if (warning.code == "template_policy_reject_preserved" && warning.message.find("dns") != std::string::npos) {
tests/subcli_tests.cpp:    require(mihomo.ok, "mihomo export should succeed with template_policy: " + error);
tests/subcli_tests.cpp:    require(sing.ok, "sing-box export should succeed with template_policy: " + error);
tests/subcli_tests.cpp:    require(xray.ok, "xray export should succeed with template_policy: " + error);
tests/subcli_tests.cpp:    options.strictNetwork = true;
tests/subcli_tests.cpp:    require(subArgs.size() == 3, "daemon sub update args should include strict-network");
tests/subcli_tests.cpp:    require(subArgs[2] == "--strict-network", "daemon sub update args should include strict-network");
tests/subcli_tests.cpp:    require(exportArgs[2] == "--strict-network", "daemon export args should include strict-network");
tests/subcli_tests.cpp:        hasStrictNetwork = hasStrictNetwork || runArgs[i] == "--strict-network";
tests/subcli_tests.cpp:    require(hasStrictNetwork, "daemon run args should include strict-network flag");
tests/subcli_tests.cpp:        warned = warned || warning.code == "capability_degraded";
tests/subcli_tests.cpp:        degradedWarnings += warning.code == "capability_degraded" ? 1 : 0;
tests/subcli_tests.cpp:        warned = warned || warning.code == "capability_degraded";
tests/subcli_tests.cpp:        warned = warned || warning.code == "capability_degraded";
tests/subcli_tests.cpp:        warned = warned || warning.code == "capability_degraded";
tests/subcli_tests.cpp:        warned = warned || warning.code == "capability_degraded";
tests/subcli_tests.cpp:        warned = warned || warning.code == "capability_degraded";
tests/cli_smoke.sh:  "template_policy": {
tests/cli_smoke.sh:  "default_outbound": "DIRECT"
tests/cli_smoke.sh:strict_export_json="$({ "$bin" export all --profile bypass-cn --sub explain --strict-capabilities --json; } 2>/dev/null || true)"
tests/cli_smoke.sh:if [[ "$strict_export_json" != *'"strict_capabilities_blocked":true'* || "$strict_export_json" != *'"violations"'* ]]; then
tests/cli_smoke.sh:    printf '%s\n' "$strict_export_json"
tests/cli_smoke.sh:"$bin" export all --profile bypass-cn --sub explain --strict-capabilities >/dev/null 2>&1 && exit 1 || true
src/main.cpp:              << "  subcli sub update [id-or-name ...] [--tag TAG] [--strict-network]\n"
src/main.cpp:              << "  --strict-network\n"
src/main.cpp:              << "  --strict-capabilities\n"
src/main.cpp:              << "  subcli export mihomo --tag hk --strict-network\n"
src/main.cpp:              << "               [--update-assets] [--strict-network] [--check] [--no-restart]\n"
src/main.cpp:              << " [--tun] [--check] [--check-timeout SEC] [--output-dir DIR] [--profile PATH_OR_NAME] [--sub ID_OR_NAME] [--tag TAG] [--strict-network] [--strict-capabilities] [--download-assets] [--explain-policy] [--json]\n";
src/main.cpp:        std::cout << "Usage:\n  subcli sub update [id-or-name ...] [--tag TAG] [--strict-network]\n";
src/main.cpp:                  << " [--interval SEC] [--target all|mihomo|sing-box|xray] [--update-assets] [--strict-network] [--check] [--no-restart]\n";
src/main.cpp:    updateCmd->add_flag("--strict-network", updateStrictNetwork);
src/main.cpp:        const bool strictNetwork = updateStrictNetwork;
src/main.cpp:            fetchResults[idx] = fetchSubscriptionWithRetry(effective, !strictNetwork);
src/main.cpp:                std::cerr << "profile validation failed: unsupported template_policy target: " << targetEntry.first << "\n";
src/main.cpp:                    std::cerr << "profile validation failed: unsupported template_policy action: " << pathEntry.second << "\n";
src/main.cpp:                    std::cerr << "profile validation failed: unsupported template_policy path for "
src/main.cpp:                    std::cerr << "profile validation failed: unsupported template_policy action for "
src/main.cpp:    bool strictNetworkFlag = false;
src/main.cpp:    bool strictCapabilitiesFlag = false;
src/main.cpp:    parser.add_flag("--strict-network", strictNetworkFlag);
src/main.cpp:    parser.add_flag("--strict-capabilities", strictCapabilitiesFlag);
src/main.cpp:    const bool strictNetwork = strictNetworkFlag;
src/main.cpp:    const bool strictCapabilities = strictCapabilitiesFlag;
src/main.cpp:        fetchResults[idx] = fetchSubscriptionWithRetry(effective, !strictNetwork);
src/main.cpp:    if (strictCapabilities && exportProfile != nullptr) {
src/main.cpp:        nlohmann::json strictViolations = nlohmann::json::array();
src/main.cpp:                    strictViolations.push_back(
src/main.cpp:                        std::cerr << "strict-capabilities blocked " << templatePolicyTargetKey(exportTarget) << ": "
src/main.cpp:                    strictViolations.push_back(
src/main.cpp:                        std::cerr << "strict-capabilities blocked " << templatePolicyTargetKey(exportTarget) << ": "
src/main.cpp:                printJsonLine({{"summary", {{"success", 0}, {"failed", 1}, {"skipped_nodes", skippedNodes}}}, {"strict_capabilities_blocked", true}, {"violations", strictViolations}});
src/main.cpp:    bool strictNetwork = false;
src/main.cpp:        sc->add_flag("--strict-network", strictNetwork);
src/main.cpp:    options.strictNetwork = strictNetwork;
src/cli_completion.cpp:            COMPREPLY=( $(compgen -W "--id --name --url --group --format-hint --user-agent --timeout --retry --priority --update-interval --tag --tags --header --force --strict-network --json --enable --disable --clear-headers --remove-header" -- "$cur") )
src/cli_completion.cpp:            COMPREPLY=( $(compgen -W "--tun --check --check-timeout --output-dir --profile --sub --tag --strict-network --download-assets" -- "$cur") )
src/cli_completion.cpp:            COMPREPLY=( $(compgen -W "--interval --target --update-assets --strict-network --check --no-restart" -- "$cur") )
src/exporter_singbox.cpp:            result.warnings.push_back({"capability_unsupported", node.name + ": " + reason});
src/exporter_singbox.cpp:                result.warnings.push_back({"capability_degraded", configured.tag + ": " + configured.type + " rendered as " + normalizeSingBoxGroupType(configured.type)});
src/profile_explain.cpp:        overrides.push_back("default_outbound");
src/profile_explain.cpp:        overrides.push_back("template_policy");
src/profile_explain.cpp:    out << "default_outbound: " << profile.defaultOutbound << "\n\n";
src/profile_explain.cpp:    out << "\ntemplate_policy:\n";
src/profile_explain.cpp:        out << "\ntemplate_policy_effective:\n";
src/profile_explain.cpp:        {"default_outbound", profile.defaultOutbound},
src/profile_explain.cpp:    result["template_policy"] = nlohmann::json::array();
src/profile_explain.cpp:            result["template_policy"].push_back(
src/exporter_common.cpp:        {"template_policy_reject_preserved", "template_policy rejected generated field '" + path + "'; preserved template content"}
```

<a id="probe-3-3-source-refs"></a>
### Task 3 reviewed source references

- `src/main.cpp:717-719 - verified export help includes --strict-capabilities, --download-assets, --explain-policy`
- `src/main.cpp:942 - verified export usage string includes --strict-network/--strict-capabilities/--download-assets/--explain-policy/--json`
- `src/main.cpp:3380-3382 - verified parser registers strict-capabilities/download-assets/explain-policy flags`
- `src/main.cpp:3687 - verified strict-capabilities failure JSON contract keys strict_capabilities_blocked + violations`
- `src/cli_completion.cpp:56 - inspected export shell completion options; missing --strict-capabilities/--explain-policy/--json`
- `src/profile.cpp:178-179 - verified default_outbound parse path`
- `src/profile.cpp:260-315 - verified template_policy validation branches and explicit error messages`
- `src/exporter_mihomo.cpp:328 - verified capability_unsupported warning emission`
- `src/exporter_singbox.cpp:409,539 - verified capability_unsupported/degraded warning emission`
- `src/exporter_xray.cpp:166-169,479,586-622 - verified degraded/unsupported warning emission paths`
- `tests/subcli_tests.cpp:587,607 - verified profile default_outbound override parse + assertion coverage`
- `tests/subcli_tests.cpp:243-273 - verified template_policy target/path/action parse coverage for profile schema behavior`
- `tests/subcli_tests.cpp:302-303,332-333,362-363 - verified template_policy invalid target/path/action rejection coverage`
- `tests/cli_smoke.sh:250-251 - verified strict-capabilities JSON contract requires strict_capabilities_blocked + violations`
- `tests/subcli_tests.cpp:3210,3285 - verified capability_degraded warning assertions for capability behavior`

## Task 4) Boundary-condition and failure-mode audit

### 4.0 Isolated probe workspace setup

Command:
```bash
mktemp -d /tmp/subcli-review-task4-XXXXXX
```

Exact output:
```text
/tmp/subcli-review-task4-HzjA6c
```

Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws init
```

Exact output:
```text
initialized subcli
config_dir=/tmp/subcli-review-task4-HzjA6c/ws
data_dir=/tmp/subcli-review-task4-HzjA6c/ws
cache_dir=/tmp/subcli-review-task4-HzjA6c/ws/cache
state_dir=/tmp/subcli-review-task4-HzjA6c/ws/state
template_dir=/tmp/subcli-review-task4-HzjA6c/ws/templates
output_dir=/tmp/subcli-review-task4-HzjA6c/ws/outputs
```

Outcome:
- Isolated workspace established via global `--workspace`; normal repo/runtime state not used for probe commands.

### 4.1 Invalid YAML parse/contracts

<a id="probe-4-1-validate-bad-yaml"></a>
Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws sub add --name bad-yaml --url file:///tmp/subcli-review-task4-HzjA6c/fixtures-invalid-yaml.txt --force
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws sub validate bad-yaml
```

Exact output:
```text
added subscription: bad-yaml
invalid: bad-yaml - parse yielded no supported nodes
validate summary: success=0 failed=1 skipped_nodes=0
```

Outcome:
- Malformed YAML input rejected at validate stage with deterministic invalid contract (`parse yielded no supported nodes`).

### 4.2 Invalid JSON parse/contracts

<a id="probe-4-2-validate-bad-json"></a>
Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws sub add --name bad-json --url file:///tmp/subcli-review-task4-HzjA6c/fixtures-invalid-json.txt --force
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws sub validate bad-json
```

Exact output:
```text
added subscription: bad-json
invalid: bad-json - parse yielded no supported nodes
validate summary: success=0 failed=1 skipped_nodes=0
```

Outcome:
- Malformed JSON input rejected with same deterministic invalid contract.

### 4.3 Invalid base64 parse/contracts

<a id="probe-4-3-validate-bad-base64"></a>
Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws sub add --name bad-base64 --url file:///tmp/subcli-review-task4-HzjA6c/fixtures-invalid-base64.txt --force
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws sub validate bad-base64
```

Exact output:
```text
added subscription: bad-base64
invalid: bad-base64 - parse yielded no supported nodes
validate summary: success=0 failed=1 skipped_nodes=0
```

Outcome:
- Malformed base64/plain payload rejected with deterministic invalid contract.

### 4.4 Invalid URI parse/contracts

<a id="probe-4-4-validate-bad-uri"></a>
Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws sub add --name bad-uri --url file:///tmp/subcli-review-task4-HzjA6c/fixtures-invalid-uri.txt --force
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws sub validate bad-uri
```

Exact output:
```text
added subscription: bad-uri
invalid: bad-uri - parse yielded no supported nodes
validate summary: success=0 failed=1 skipped_nodes=3
```

Outcome:
- Invalid URI lines rejected; parser reports skipped nodes and final invalid contract.

### 4.5 Missing assets/templates/profiles failures

<a id="probe-4-5-template-validate-json"></a>
Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws template validate --json
```

Key excerpt (decisive lines):
```text
{"failed":6,"templates":[{"exists":false,..."path":"/tmp/subcli-review-task4-HzjA6c/ws/templates/mihomo_base.yaml"...}, ... ]}
```

<a id="probe-4-5-profile-validate-missing"></a>
Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws profile validate /tmp/subcli-review-task4-HzjA6c/no-such-profile.json
```

Exact output:
```text
profile validation failed: failed to open profile: /tmp/subcli-review-task4-HzjA6c/no-such-profile.json
```

<a id="probe-4-5-asset-validate-missing"></a>
Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws asset validate
```

Key excerpt (decisive lines):
```text
missing asset: mihomo.geoip at /tmp/subcli-review-task4-HzjA6c/ws/assets/mihomo/geoip.dat
missing asset: ...
missing asset: xray.geosite at /tmp/subcli-review-task4-HzjA6c/ws/assets/xray/geosite.dat
```

Outcome:
- Template/profile/asset surfaces all fail fast on missing required files.

### 4.6 Invalid workspace path/marker behavior

<a id="probe-4-6-workspace-path-invalid"></a>
Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/not-a-dir doctor --json
```

Exact output:
```text
environment resolution failed: invalid CLI workspace directory: /tmp/subcli-review-task4-HzjA6c/not-a-dir
```

<a id="probe-4-6-workspace-marker-warn"></a>
Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws workspace doctor
```

Key excerpt (decisive lines):
```text
[WARN] workspace has neither .subcli-workspace nor subcli.env.yaml marker
workspace doctor summary: root=/tmp/subcli-review-task4-HzjA6c/ws ok=11 warn=2 fail=3
```

Outcome:
- CLI workspace override path hard-fails on non-directory input.
- Missing marker condition emits explicit WARN contract (not silent).

### 4.7 Permission denied writes

<a id="probe-4-7-config-write-permission-denied"></a>
Command:
```bash
chmod 444 /tmp/subcli-review-task4-HzjA6c/ws/config.yaml
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws config set timeout 9
```

Exact output:
```text
error: failed to write /tmp/subcli-review-task4-HzjA6c/ws/config.yaml
```

Outcome:
- Deterministic permission-denied write failure reproduced for config write path.

### 4.8 Network failure/timeout strict behavior

<a id="probe-4-8-sub-update-strict-network-timeout"></a>
Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws sub add --name net-fail --url http://127.0.0.1:9/unreachable --timeout 1 --retry 0 --force
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws sub update net-fail --strict-network
```

Exact output:
```text
added subscription: net-fail
update failed for net-fail: network fetch failed: Timeout was reached
update summary: success=0 failed=1 parsed_nodes=0 skipped_nodes=0 skipped_subscriptions=0
```

Outcome:
- `--strict-network` enforces hard failure on timeout; no fallback success reported.

### 4.9 Duplicate/invalid sub headers/tags

<a id="probe-4-9-duplicate-tags-preserved"></a>
Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws sub add --name dup-header --url file:///tmp/subcli-review-task4-HzjA6c/fixtures-invalid-base64.txt --header 'X-Test: one' --header 'X-Test: two' --tag a --tag a --force
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws sub list --json
```

Key excerpt (decisive lines):
```text
added subscription: dup-header
..."id":"dup-header",..."tags":["a","a"],...
```

<a id="probe-4-9-invalid-header-rejected"></a>
Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws sub add --name bad-header --url file:///tmp/subcli-review-task4-HzjA6c/fixtures-invalid-base64.txt --header 'NoColonHeader' --force
```

Exact output:
```text
invalid header, expected 'Key: Value' or 'Key=Value': NoColonHeader
```

Outcome:
- Invalid header format rejected with explicit parse contract.
- Duplicate tags preserved as duplicates (`["a","a"]`), indicating normalization/dedup contract gap.

### 4.10 CLI parse/usage errors

<a id="probe-4-10-export-arity-usage"></a>
Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws export
```

Key excerpt (decisive lines):
```text
Usage:
  subcli export <all|mihomo|sing-box|xray> [options]
```

<a id="probe-4-10-template-set-arity"></a>
Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws template set sing-box normal
```

Exact output:
```text
template set requires <target> <normal|tun> <path>
Run 'subcli template --help' for valid template usage.
```

<a id="probe-4-10-config-get-arity"></a>
Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws config get
```

Exact output:
```text
config get requires <key>
Run 'subcli config --help' to view supported keys and examples.
```

<a id="probe-4-10-sub-list-unknown-flag"></a>
Command:
```bash
./build/subcli --workspace /tmp/subcli-review-task4-HzjA6c/ws sub list --bogus-flag
```

Key excerpt (decisive lines):
```text
Usage:
  subcli sub list [--json]
```

Outcome:
- Representative wrong-arity and unknown-flag paths emit usage/help guidance instead of silent success.

## Task 5) Redundancy/maintainability audit + test-gap audit

### 5.1 Size and orchestration hotspots

<a id="probe-5-1-line-count-hotspots"></a>
Command:
```bash
wc -l src/main.cpp src/parser.cpp src/exporter.cpp src/store.cpp src/profile.cpp tests/subcli_tests.cpp tests/cli_smoke.sh
```

Exact output:
```text
  4393 src/main.cpp
   117 src/parser.cpp
   101 src/exporter.cpp
   294 src/store.cpp
   329 src/profile.cpp
  6072 tests/subcli_tests.cpp
   308 tests/cli_smoke.sh
 11614 总计
```

<a id="probe-5-1-main-function-lengths"></a>
Command:
```bash
python3 -c "import re,pathlib; p=pathlib.Path('src/main.cpp'); lines=p.read_text().splitlines(); defs=[]; pat=re.compile(r'^(?:static )?(?:int|ExitCode) ([A-Za-z0-9_]+)\('); \
for i,l in enumerate(lines,1):\
 m=pat.match(l)\
 if m: defs.append((m.group(1),i))\
for idx,(name,start) in enumerate(defs):\
 end=(defs[idx+1][1]-1) if idx+1<len(defs) else len(lines)\
 print(f'{name}:{start}-{end} ({end-start+1} lines)')"
```

Exact output:
```text
runConfigCheckForTarget:1361-1423 (63 lines)
startRuntimeForTarget:1424-1497 (74 lines)
doRunCommand:1498-1509 (12 lines)
doRestartCommand:1510-1521 (12 lines)
doStopCommand:1522-1550 (29 lines)
printRuntimeStatus:1551-1566 (16 lines)
doStatusCommand:1567-1595 (29 lines)
doInitCommand:1596-1647 (52 lines)
doDoctorCommand:1648-1764 (117 lines)
doCheckCommand:1765-1826 (62 lines)
doCompletionCommand:1827-1848 (22 lines)
doSubCommand:1849-2410 (562 lines)
doConfigCommand:2411-2841 (431 lines)
doTemplateCommand:2842-3049 (208 lines)
doAssetCommand:3050-3160 (111 lines)
doProfileCommand:3161-3336 (176 lines)
doExportCommand:3337-3859 (523 lines)
doWorkspaceCommand:3860-4058 (199 lines)
doDaemonCommand:4059-4241 (183 lines)
main:4242-4393 (152 lines)
```

Outcome:
- `src/main.cpp` is a large concentration point (4393 lines) with multiple long command handlers (`doSubCommand` 562 lines, `doExportCommand` 523 lines, `doConfigCommand` 431 lines).
- Command orchestration, validation, and output contracts are heavily centralized, which raises coupling/change-surface risk for CLI behavior updates.

### 5.2 Redundant patterns and warning formatting

<a id="probe-5-2-warning-duplication"></a>
Command:
```bash
rg -n "warning:" src/main.cpp
```

Exact output:
```text
2082:                std::cerr << "warning: " << s.id << " - " << warning.message << "\n";
2253:                std::cerr << "warning: " << s.id << " - " << warning.message << "\n";
2360:                std::cerr << "warning: " << s.id << " used cached subscription content (" << fr.cacheReason << ")\n";
2377:                std::cerr << "warning: " << s.id << " - " << warning.message << "\n";
3407:                std::cerr << "warning: missing asset: " << asset.key << " at " << asset.path << "\n";
3409:            std::cerr << "warning: run 'subcli asset update' or export with --download-assets before direct core use\n";
3510:            std::cerr << "warning: " << s.id << " used cached subscription content (" << fr.cacheReason << ")\n";
3518:            std::cerr << "warning: " << s.id << " - " << warning.message << "\n";
```

<a id="probe-5-2-dispatch-branch-density"></a>
Command:
```bash
rg -n "if \(cmd == \"" src/main.cpp
```

Key excerpt (decisive lines):
```text
946:    if (cmd == "list") {
...snip...
4380:        if (cmd == "workspace") {
```

Outcome:
- Warning formatting/prefixing is repeated in multiple command paths (`sub update`, `sub validate`, `export`, runtime asset checks) instead of routed through one formatter helper.
- Large `if (cmd == ...)` chains appear in both help routing and runtime dispatch, creating a repeated parse/dispatch shape that is harder to evolve safely than table-driven routing.

### 5.3 Broad catch concentration and error handling spread

<a id="probe-5-3-catch-concentration"></a>
Command:
```bash
rg -n "catch\s*\(" src/main.cpp src/parser.cpp src/store.cpp src/profile.cpp
```

Exact output:
```text
src/profile.cpp:127:    } catch (const json::parse_error& ex) {
src/store.cpp:90:    } catch (const std::exception& ex) {
src/store.cpp:128:    } catch (const std::exception& ex) {
src/parser.cpp:23:    } catch (...) {
src/main.cpp:1108:    } catch (...) {
src/main.cpp:1152:    } catch (const CLI::ParseError&) {
src/main.cpp:3013:                } catch (const std::exception& ex) {
src/main.cpp:3308:        } catch (const std::exception&) {
src/main.cpp:4293:        } catch (const CLI::ParseError& ex) {
src/main.cpp:4386:    } catch (const std::exception& ex) {
src/main.cpp:4389:    } catch (...) {
```

Outcome:
- Broad catch-all handlers exist in parser helper and top-level CLI (`src/parser.cpp:23`, `src/main.cpp:1108`, `src/main.cpp:4389`), which can mask root-cause detail and make regression diagnosis harder.
- Error contracts are mostly user-friendly, but exception handling policy is distributed and inconsistent in granularity (typed catches mixed with catch-all paths).

### 5.4 Test-gap evidence mapping (critical behavior coverage)

<a id="probe-5-4-test-gap-anchor-map"></a>
Command:
```bash
rg -n "resolveEnvironment|EnvironmentResolveInput|SUBCLI_WORKSPACE|resolution_source|strict-capabilities|strict_capabilities_blocked|export mihomo|no subscriptions selected|no nodes parsed" tests/subcli_tests.cpp tests/cli_smoke.sh src/main.cpp
```

Exact output:
```text
tests/cli_smoke.sh:173:doctor_env_json="$({ SUBCLI_WORKSPACE="$workspace_root" "$bin" doctor --json; } 2>&1 || true)"
tests/cli_smoke.sh:187:if env.get("resolution_source") != "env_var":
tests/cli_smoke.sh:244:export_json="$({ "$bin" export mihomo --profile bypass-cn --sub explain --json; } 2>/dev/null || true)"
tests/cli_smoke.sh:250:strict_export_json="$({ "$bin" export all --profile bypass-cn --sub explain --strict-capabilities --json; } 2>/dev/null || true)"
tests/cli_smoke.sh:251:if [[ "$strict_export_json" != *'"strict_capabilities_blocked":true'* || "$strict_export_json" != *'"violations"'* ]]; then
tests/subcli_tests.cpp:5671:    subcli::EnvironmentResolveInput input;
tests/subcli_tests.cpp:5678:    const auto out = subcli::resolveEnvironment(input);
tests/subcli_tests.cpp:5717:    require(out.source == subcli::EnvironmentSource::EnvVar, "SUBCLI_WORKSPACE should win when --workspace is absent");
tests/subcli_tests.cpp:5742:    require(out.source == subcli::EnvironmentSource::MarkerDiscovery, "marker-discovered workspace should win over persisted default");
tests/subcli_tests.cpp:5762:    require(out.source == subcli::EnvironmentSource::PersistedDefault, "persisted default should be used without cli/env/marker");
src/main.cpp:3482:        std::cerr << "no subscriptions selected for export (check --sub/--tag filters)\n";
src/main.cpp:3529:        std::cerr << "no nodes parsed from enabled subscriptions\n";
src/main.cpp:3687:                printJsonLine({{"summary", {{"success", 0}, {"failed", 1}, {"skipped_nodes", skippedNodes}}}, {"strict_capabilities_blocked", true}, {"violations", strictViolations}});
```

Outcome:
- Covered directly by tests: workspace precedence/resolution source paths (unit + smoke), export JSON structure happy path, strict-capabilities blocked JSON contract.
- Still missing direct tests for export failure contracts keyed by `no subscriptions selected for export` and `no nodes parsed from enabled subscriptions` despite explicit runtime strings in `src/main.cpp`.

## Task 6) Release/packaging risk audit evidence

### 6.1 Version, install payload, and CPack generator in CMake

<a id="probe-6-1-cmake-release-packaging"></a>
Command:
```bash
rg -n "project\(|install\(|CPACK_|include\(CPack\)" CMakeLists.txt
```

Exact output:
```text
5:project(subcli VERSION 0.1.0 LANGUAGES C CXX)
122:install(TARGETS subcli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
123:install(DIRECTORY templates/ DESTINATION ${CMAKE_INSTALL_DATADIR}/subcli/templates)
124:install(DIRECTORY profiles/ DESTINATION ${CMAKE_INSTALL_DATADIR}/subcli/profiles)
125:install(FILES packaging/systemd/subcli-daemon.service DESTINATION ${CMAKE_INSTALL_DATADIR}/subcli/systemd)
126:install(FILES README.subcli.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
138:set(CPACK_PACKAGE_NAME "subcli")
139:set(CPACK_PACKAGE_VENDOR "subcli")
140:set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Subscription CLI exporter")
141:set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
142:set(CPACK_PACKAGE_FILE_NAME "subcli-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
143:set(CPACK_GENERATOR "TGZ")
144:include(CPack)
```

Outcome:
- Project version is defined in CMake as `0.1.0` and package generator is `TGZ`.
- Install payload is explicitly declared for binary, templates, profiles, systemd unit file, and docs.

### 6.2 Release workflow file presence/absence

<a id="probe-6-2-release-workflow-presence"></a>
Command:
```bash
ls .github/workflows/release*
```

Exact output:
```text
ls: 无法访问 '.github/workflows/release*': 没有那个文件或目录
```

Outcome:
- No `release*` workflow file exists under `.github/workflows` in this worktree snapshot.

### 6.3 Release/packaging claims in docs vs implementation anchors

<a id="probe-6-3-release-claim-scan"></a>
Command:
```bash
rg -n "cmake --build build --target package|systemd --user unit is installed|share/subcli/templates|release validation workflow" README.md README.subcli.md
```

Exact output:
```text
README.subcli.md:14:cmake --build build --target package
README.subcli.md:521:- If export fails with missing templates, verify the package contains `share/subcli/templates`.
README.md:36:cmake --build build --target package
README.md:134:See [`README.subcli.md`](README.subcli.md) for detailed command examples, release validation workflow (`profile explain`, `export --json`, `--strict-capabilities`), cache behavior, troubleshooting, and deployment notes. See [`docs/profile-schema.md`](docs/profile-schema.md) for the profile JSON schema and migration notes. See [`docs/capability-matrix.md`](docs/capability-matrix.md) for the full v2.1 capability matrix (protocols, groups, DNS, route mapping, assets, and strict-mode behavior).
```

Outcome:
- Docs claim packaging path (`cmake --build build --target package`) and package payload expectation (`share/subcli/templates`).
- These claims are consistent with CMake install/CPack declarations (`#probe-6-1-cmake-release-packaging`).

### 6.4 Optional package target execution and artifact naming

<a id="probe-6-4-package-target-run"></a>
Command:
```bash
cmake --build build --target package && ls build
```

Exact output:
```text
[ 66%] Built target crypto
[ 71%] Built target ssl
[ 89%] Built target libcurl_static
[ 93%] Built target yaml-cpp
[ 93%] Built target CLI11
[ 96%] Built target subcli
[ 99%] Built target subcli_tests
[100%] Built target yaml-cpp-sandbox
[100%] Built target yaml-cpp-parse
[100%] Built target yaml-cpp-read
[100%] Built target curl-generate-curl-config.1
[100%] Built target curl-generate-mk-ca-bundle.1
[100%] Built target curl-opts-man
[100%] Built target curl-man
Run CPack packaging tool...
CPack: Create package using TGZ
CPack: Install projects
CPack: - Run preinstall target for: subcli
CPack: - Install project: subcli []
CPack: Create package
CPack: - package: /home/lisztzy/prj/subcli-cpp/.worktrees/code-review-2026-04-30/build/subcli-0.1.0-Linux-x86_64.tar.gz generated.
CMakeCache.txt
CMakeFiles
cmake_install.cmake
CPackConfig.cmake
_CPack_Packages
CPackSourceConfig.cmake
CTestTestfile.cmake
_deps
install_manifest.txt
Makefile
subcli
subcli-0.1.0-Linux-x86_64.tar.gz
subcli_tests
Testing
```

Outcome:
- Packaging target succeeds in current environment.
- Generated artifact name matches CPack template and project version: `subcli-0.1.0-Linux-x86_64.tar.gz`.

### 6.5 Final report/checklist completeness checks

<a id="probe-6-5-report-severity-order"></a>
Command:
```bash
rg -n "^- Severity: " docs/superpowers/reviews/2026-04-30-code-review-report.md
```

Exact output:
```text
25:- Severity: Medium
32:- Severity: Medium
39:- Severity: Medium
46:- Severity: Low
53:- Severity: Low
```

<a id="probe-6-5-checklist-status-contract"></a>
Command:
```bash
rg -n "Status semantics rule|\| Workspace precedence \||\| Export guarantees \||\| Release claims \|" docs/superpowers/reviews/2026-04-30-code-review-checklists.md
```

Exact output:
```text
3:Status semantics rule: plan contract tracks `pass`/`fail`/`unknown`; this execution uses `Pass`/`Partial`/`Fail` where `Partial` is treated as `unknown` with anchored partial evidence. Impact triage (`Blocker`/`High`/`Medium`/`Low`) is tracked separately in the report severity model.
10:| Workspace precedence | Partial | Mixed evidence: runtime default path and invalid env override rejection are directly probed (`./build/subcli doctor --json` -> `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-3-2-doctor-default`; `SUBCLI_WORKSPACE="/tmp/subcli-review-ws" ./build/subcli doctor --json` -> `#probe-3-2-doctor-env-invalid`; `./build/subcli workspace status --json` -> `#probe-3-2-workspace-status`), and unit/smoke tests assert precedence ordering (`rg -n "resolveEnvironment|EnvironmentResolveInput|SUBCLI_WORKSPACE|resolution_source" tests/subcli_tests.cpp tests/cli_smoke.sh src/main.cpp` -> `#probe-5-4-test-gap-anchor-map`). No direct runtime probe transcript yet for successful env override in this review pass, so keep Partial. |
11:| Export guarantees | Partial | Partial evidence exists via direct command/source anchors: `./build/subcli export mihomo --profile bypass-cn --sub explain --json` and `./build/subcli export all --profile bypass-cn --sub explain --strict-capabilities --json` are covered in `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-5-4-test-gap-anchor-map` (`tests/cli_smoke.sh:244`, `tests/cli_smoke.sh:250-251`, `src/main.cpp:3687`); missing direct probes remain for empty-selection and zero-node-failure contracts (`src/main.cpp:3482`, `src/main.cpp:3529`). |
14:| Release claims | Partial | Claim map includes direct release/packaging probes (CMake version/install/CPack lines, docs claim scan, package target run) at `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-6-1-cmake-release-packaging`, `#probe-6-3-release-claim-scan`, and `#probe-6-4-package-target-run`; external dependency for release confidence remains export guarantees failure-contract closure anchored at `docs/superpowers/reviews/2026-04-30-code-review-evidence.md#probe-5-4-test-gap-anchor-map` (`src/main.cpp:3482`, `src/main.cpp:3529`). |
```

Outcome:
- Findings are currently ordered by severity band (Medium entries before Low entries).
- Checklist status contract and partial-evidence states are explicit and anchored for workspace precedence, export guarantees, and release claims.
