#include <filesystem>
#include <fstream>
#include <future>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "exporter_internal.hpp"
#include "subcli/assets.hpp"
#include "subcli/capabilities.hpp"
#include "subcli/cli_completion.hpp"
#include "subcli/cli_output.hpp"
#include "subcli/daemon.hpp"
#include "subcli/core_runtime.hpp"
#include "subcli/exporter.hpp"
#include "subcli/fetch.hpp"
#include "subcli/node.hpp"
#include "subcli/parser.hpp"
#include "subcli/profile.hpp"
#include "subcli/protocol_registry.hpp"
#include "subcli/store.hpp"
#include "subcli/util.hpp"

namespace fs = std::filesystem;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

subcli::AppConfig makeConfig() {
    subcli::AppConfig config;
    const fs::path root = fs::path(SUBCLI_SOURCE_DIR);
    config.templateDir = (root / "templates").string();
    config.templateNormal["mihomo"] = (root / "templates/mihomo_base.yaml").string();
    config.templateTun["mihomo"] = (root / "templates/mihomo_tun.yaml").string();
    config.templateNormal["sing-box"] = (root / "templates/singbox_base.json").string();
    config.templateTun["sing-box"] = (root / "templates/singbox_tun.json").string();
    config.templateNormal["xray"] = (root / "templates/xray_base.json").string();
    config.templateTun["xray"] = (root / "templates/xray_tun.json").string();
    config.regionRules = {
        {"HK", "(?i)(hk|hong kong|香港)"},
        {"JP", "(?i)(jp|japan|日本)"},
    };
    return config;
}

bool containsProtocol(const std::vector<std::string>& protocols, const std::string& protocol) {
    for (const auto& item : protocols) {
        if (item == protocol) {
            return true;
        }
    }
    return false;
}

subcli::ProxyNode makeExportReadyShadowsocksNode(const std::string& name);

void testLoadProfileReadsGroupsRulesAndDns() {
    const fs::path dir = fs::temp_directory_path() / "subcli-profile-loader-tests";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path path = dir / "profile.json";
    {
        std::ofstream out(path);
        out << R"json({
  "version": 1,
  "name": "work",
  "description": "daily profile",
  "default_outbound": "AUTO",
  "dns": {
    "mode": "fakeip",
    "strategy": "prefer_ipv4",
    "direct_servers": ["223.5.5.5"],
    "remote_servers": ["1.1.1.1"]
  },
  "groups": [
    {
      "tag": "AUTO",
      "type": "url-test",
      "members": ["HK", "JP"],
      "default": "HK",
      "url": "https://example.com/generate_204",
      "strategy": "latency"
    }
  ],
  "rules": [
    {"type": "geosite", "value": "cn", "outbound": "DIRECT"}
  ]
})json";
    }

    subcli::ResolvedProfile profile;
    std::string error;
    require(subcli::loadProfile(path.string(), profile, error), "loadProfile should read valid profile: " + error);
    require(profile.version == 1, "profile version should be read");
    require(profile.name == "work", "profile name should be read");
    require(profile.description == "daily profile", "profile description should be read");
    require(profile.defaultOutbound == "AUTO", "profile default outbound should be read");
    require(profile.dns.mode == "fakeip", "profile dns mode should be read");
    require(profile.dns.strategy == "prefer_ipv4", "profile dns strategy should be read");
    require(profile.dns.directServers.size() == 1 && profile.dns.directServers[0] == "223.5.5.5", "direct dns server should be read");
    require(profile.dns.remoteServers.size() == 1 && profile.dns.remoteServers[0] == "1.1.1.1", "remote dns server should be read");
    require(profile.groups.size() == 1, "profile group should be read");
    require(profile.groups[0].tag == "AUTO", "profile group tag should be read");
    require(profile.groups[0].type == "url-test", "profile group type should be read");
    require(profile.groups[0].members.size() == 2 && profile.groups[0].members[1] == "JP", "profile group members should be read");
    require(profile.groups[0].defaultMember == "HK", "profile group default member should be read");
    require(profile.groups[0].url == "https://example.com/generate_204", "profile group url should be read");
    require(profile.groups[0].interval == 300, "profile group interval should default to 300");
    require(profile.groups[0].strategy == "latency", "profile group strategy should be read");
    require(profile.rules.size() == 1, "profile rule should be read");
    require(profile.rules[0].type == "geosite", "profile rule type should be read");
    require(profile.rules[0].value == "cn", "profile rule value should be read");
    require(profile.rules[0].outbound == "DIRECT", "profile rule outbound should be read");

    fs::remove_all(dir);
}

void testLoadProfileRejectsInvalidJson() {
    const fs::path dir = fs::temp_directory_path() / "subcli-profile-invalid-json-tests";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path path = dir / "profile.json";
    {
        std::ofstream out(path);
        out << "{ invalid json";
    }

    subcli::ResolvedProfile profile;
    std::string error;
    require(!subcli::loadProfile(path.string(), profile, error), "loadProfile should reject invalid JSON");
    require(!error.empty(), "invalid JSON should produce an error");

    fs::remove_all(dir);
}

void testLoadProfileRejectsRuleWithoutOutbound() {
    const fs::path dir = fs::temp_directory_path() / "subcli-profile-rule-outbound-tests";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path path = dir / "profile.json";
    {
        std::ofstream out(path);
        out << R"json({
  "version": 1,
  "name": "work",
  "rules": [
    {"type": "geosite", "value": "cn"}
  ]
})json";
    }

    subcli::ResolvedProfile profile;
    std::string error;
    require(!subcli::loadProfile(path.string(), profile, error), "loadProfile should reject rule without outbound");
    require(!error.empty(), "rule without outbound should produce an error");

    fs::remove_all(dir);
}

void testLoadProfileAcceptsRuleWithoutValueOrLists() {
    const fs::path dir = fs::temp_directory_path() / "subcli-profile-rule-minimal-tests";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path path = dir / "profile.json";
    {
        std::ofstream out(path);
        out << R"json({
  "version": 1,
  "name": "work",
  "rules": [
    {"type": "domain", "outbound": "DIRECT"}
  ]
})json";
    }

    subcli::ResolvedProfile profile;
    std::string error;
    require(subcli::loadProfile(path.string(), profile, error), "loadProfile should accept rule with type and outbound only: " + error);
    require(profile.rules.size() == 1, "minimal profile rule should be read");
    require(profile.rules[0].type == "domain", "minimal profile rule type should be read");
    require(profile.rules[0].outbound == "DIRECT", "minimal profile rule outbound should be read");
    require(profile.rules[0].value.empty(), "minimal profile rule value should remain empty");

    fs::remove_all(dir);
}

void testLoadProfileAcceptsFinalRuleWithoutOutbound() {
    const fs::path dir = fs::temp_directory_path() / "subcli-profile-final-rule-tests";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path path = dir / "profile.json";
    {
        std::ofstream out(path);
        out << R"json({
  "version": 1,
  "name": "work",
  "rules": [
    {"type": "final"}
  ]
})json";
    }

    subcli::ResolvedProfile profile;
    std::string error;
    require(subcli::loadProfile(path.string(), profile, error), "loadProfile should accept final rule without outbound: " + error);
    require(profile.rules.size() == 1, "final profile rule should be read");
    require(profile.rules[0].type == "final", "final profile rule type should be read");
    require(profile.rules[0].outbound.empty(), "final profile rule outbound should remain empty");

    fs::remove_all(dir);
}

void testBuiltInProfilesExistAndLoad() {
    const fs::path profilesDir = fs::path(SUBCLI_SOURCE_DIR) / "profiles";
    const fs::path bypassCnPath = profilesDir / "bypass-cn.json";
    const fs::path globalPath = profilesDir / "global.json";
    const fs::path directPath = profilesDir / "direct.json";

    require(fs::exists(bypassCnPath), "bypass-cn profile should exist");
    require(fs::exists(globalPath), "global profile should exist");
    require(fs::exists(directPath), "direct profile should exist");

    subcli::ResolvedProfile bypassCn;
    subcli::ResolvedProfile global;
    subcli::ResolvedProfile direct;
    std::string error;
    require(subcli::loadProfile(bypassCnPath.string(), bypassCn, error), "bypass-cn profile should load: " + error);
    require(subcli::loadProfile(globalPath.string(), global, error), "global profile should load: " + error);
    require(subcli::loadProfile(directPath.string(), direct, error), "direct profile should load: " + error);

    require(bypassCn.name == "bypass-cn", "bypass-cn profile name should be set");
    require(bypassCn.defaultOutbound == "PROXY", "bypass-cn default outbound should be PROXY");
    require(bypassCn.rules.size() == 5, "bypass-cn should define four bypass rules and a final rule");
    require(global.defaultOutbound == "PROXY", "global default outbound should be PROXY");
    require(direct.defaultOutbound == "DIRECT", "direct default outbound should be DIRECT");
    require(direct.groups.empty(), "direct profile should not define groups");
}

void testExportProfileResolverLoadsBuiltInProfile() {
    auto config = makeConfig();
    config.profile = "global";

    subcli::ResolvedProfile profile;
    std::string error;
    require(
        subcli::loadExportProfile(config, (fs::path(SUBCLI_SOURCE_DIR) / "profiles").string(), profile, error),
        "export profile resolver should load built-in profile: " + error
    );
    require(profile.name == "global", "export profile resolver should load profile selected by config name");

    const fs::path outDir = fs::temp_directory_path() / "subcli-profile-export-tests";
    fs::create_directories(outDir);
    auto result = subcli::exportForTarget(
        subcli::ExportTarget::Mihomo,
        {makeExportReadyShadowsocksNode("HK Node")},
        config,
        false,
        &profile,
        (outDir / "mihomo-global.yaml").string(),
        error
    );
    require(result.ok, "export should accept a loaded resolved profile: " + error);

    fs::remove_all(outDir);
}

void testExportProfileResolverFailsForMissingProfilePath() {
    auto config = makeConfig();
    config.profilePath = (fs::temp_directory_path() / "subcli-missing-profile.json").string();

    subcli::ResolvedProfile profile;
    std::string error;
    require(
        !subcli::loadExportProfile(config, (fs::path(SUBCLI_SOURCE_DIR) / "profiles").string(), profile, error),
        "export profile resolver should fail missing explicit profile path"
    );
    require(error.find("failed to load export profile") != std::string::npos, "missing profile error should identify export profile load failure");
}

void testExportProfileResolverFailsForInvalidProfileJson() {
    const fs::path dir = fs::temp_directory_path() / "subcli-export-invalid-profile-tests";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path path = dir / "profile.json";
    {
        std::ofstream out(path);
        out << "{ invalid json";
    }

    auto config = makeConfig();
    config.profilePath = path.string();
    subcli::ResolvedProfile profile;
    std::string error;
    require(
        !subcli::loadExportProfile(config, (fs::path(SUBCLI_SOURCE_DIR) / "profiles").string(), profile, error),
        "export profile resolver should fail invalid explicit profile JSON"
    );
    require(error.find("invalid profile JSON") != std::string::npos, "invalid profile error should include JSON parse failure");

    fs::remove_all(dir);
}

void testBuiltInAutoGroupsDoNotReferenceProxy() {
    const fs::path profilesDir = fs::path(SUBCLI_SOURCE_DIR) / "profiles";
    const std::vector<fs::path> profilePaths = {
        profilesDir / "bypass-cn.json",
        profilesDir / "global.json",
        profilesDir / "direct.json",
    };

    for (const auto& path : profilePaths) {
        subcli::ResolvedProfile profile;
        std::string error;
        require(subcli::loadProfile(path.string(), profile, error), "built-in profile should load: " + path.string() + ": " + error);
        for (const auto& group : profile.groups) {
            if (group.tag != "AUTO") {
                continue;
            }
            for (const auto& member : group.members) {
                require(member != "PROXY", "built-in AUTO group must not reference PROXY: " + profile.name);
            }
        }
    }
}

void testProtocolRegistryCoversOfficialTargets() {
    require(subcli::canonicalProtocolName("ss") == "shadowsocks", "ss should normalize to shadowsocks");
    require(subcli::canonicalProtocolName("hy2") == "hysteria2", "hy2 should normalize to hysteria2");

    const auto mihomo = subcli::officialProtocolsForTarget(subcli::ExportTarget::Mihomo);
    const auto singBox = subcli::officialProtocolsForTarget(subcli::ExportTarget::SingBox);
    const auto xray = subcli::officialProtocolsForTarget(subcli::ExportTarget::Xray);

    require(containsProtocol(mihomo, "tuic"), "mihomo registry should include tuic");
    require(containsProtocol(mihomo, "trusttunnel"), "mihomo registry should include trusttunnel");
    require(containsProtocol(singBox, "shadowtls"), "sing-box registry should include shadowtls");
    require(containsProtocol(singBox, "tor"), "sing-box registry should include tor");
    require(containsProtocol(xray, "wireguard"), "xray registry should include wireguard");
    require(containsProtocol(xray, "hysteria"), "xray registry should include hysteria");
    require(!subcli::isOfficialProtocolSupported(subcli::ExportTarget::Xray, "hysteria2"), "xray should not support hysteria2");
    require(subcli::canonicalProtocolName("direct") == "direct", "direct should remain direct canonical protocol");
    require(subcli::canonicalProtocolName("freedom") == "freedom", "freedom should remain freedom canonical protocol");
    require(subcli::isProtocolExportReady(subcli::ExportTarget::Xray, "freedom"), "xray freedom should be export-ready");
    require(subcli::isProtocolExportReady(subcli::ExportTarget::Xray, "blackhole"), "xray blackhole should be export-ready");
    require(subcli::isProtocolExportReady(subcli::ExportTarget::Xray, "dns"), "xray dns should be export-ready");
}

void testCliOutputStatusJson() {
    auto json = subcli::makeStatusJson("ok", "initialized");
    require(json.value("status", "") == "ok", "status json should contain status");
    require(json.value("message", "") == "initialized", "status json should contain message");
}

void testCliOutputDiagnosticsJson() {
    std::vector<subcli::DiagnosticMessage> warnings = {{"invalid_include_regex", "include_regex is invalid and was ignored"}};
    auto json = subcli::diagnosticMessagesToJson(warnings);
    require(json.is_array(), "diagnostics json should be array");
    require(json.size() == 1, "diagnostics json should include one warning");
    require(json[0].value("code", "") == "invalid_include_regex", "diagnostic code should be preserved");
}

void testBashCompletionContainsCommands() {
    const auto script = subcli::generateBashCompletion();
    require(script.find("_subcli_completion") != std::string::npos, "completion should define function");
    require(script.find("init doctor sub config template asset export daemon run stop status restart check completion") != std::string::npos, "completion should include root commands");
    require(script.find("add remove list update enable disable edit validate") != std::string::npos, "completion should include sub commands");
    require(script.find("once run start stop status") != std::string::npos, "completion should include daemon modes");
}

void testDaemonBuildsExpectedArgs() {
    subcli::DaemonOptions options;
    options.exportTarget = "sing-box";
    options.updateAssets = true;
    options.strictNetwork = true;
    options.check = true;
    options.pidFile = "/tmp/subcli-daemon.pid";
    options.logFile = "/tmp/subcli-daemon.log";

    const auto subArgs = subcli::buildDaemonSubUpdateArgs(options);
    require(subArgs.size() == 3, "daemon sub update args should include strict-network");
    require(subArgs[0] == "sub" && subArgs[1] == "update", "daemon sub update args should keep base command");
    require(subArgs[2] == "--strict-network", "daemon sub update args should include strict-network");

    const auto exportArgs = subcli::buildDaemonExportArgs(options);
    require(exportArgs.size() == 5, "daemon export args should include selected flags");
    require(exportArgs[0] == "export" && exportArgs[1] == "sing-box", "daemon export args should target configured backend");
    require(exportArgs[2] == "--strict-network", "daemon export args should include strict-network");
    require(exportArgs[3] == "--download-assets", "daemon export args should include download-assets");
    require(exportArgs[4] == "--check", "daemon export args should include check");

    const auto runArgs = subcli::buildDaemonRunArgs(options);
    require(runArgs.size() == 13, "daemon run args should include mode, flags, pid, and log paths");
    require(runArgs[0] == "daemon" && runArgs[1] == "run", "daemon run args should start from daemon run");
    require(runArgs[2] == "--interval" && runArgs[3] == "3600", "daemon run args should include default interval");
    require(runArgs[4] == "--target" && runArgs[5] == "sing-box", "daemon run args should include export target");
    bool hasUpdateAssets = false;
    bool hasStrictNetwork = false;
    bool hasCheck = false;
    bool hasPidFile = false;
    bool hasLogFile = false;
    for (size_t i = 0; i < runArgs.size(); ++i) {
        hasUpdateAssets = hasUpdateAssets || runArgs[i] == "--update-assets";
        hasStrictNetwork = hasStrictNetwork || runArgs[i] == "--strict-network";
        hasCheck = hasCheck || runArgs[i] == "--check";
        hasPidFile = hasPidFile || (runArgs[i] == "--pid-file" && i + 1 < runArgs.size() && runArgs[i + 1] == "/tmp/subcli-daemon.pid");
        hasLogFile = hasLogFile || (runArgs[i] == "--log-file" && i + 1 < runArgs.size() && runArgs[i + 1] == "/tmp/subcli-daemon.log");
    }
    require(hasUpdateAssets, "daemon run args should include update-assets flag");
    require(hasStrictNetwork, "daemon run args should include strict-network flag");
    require(hasCheck, "daemon run args should include check flag");
    require(hasPidFile, "daemon run args should include pid file");
    require(hasLogFile, "daemon run args should include log file");
}

void testDaemonProcessLifecycle() {
    const fs::path stateDir = fs::temp_directory_path() / "subcli-daemon-lifecycle-tests";
    fs::remove_all(stateDir);
    fs::create_directories(stateDir);

    subcli::DaemonOptions options;
    options.intervalSec = 120;
    options.exportTarget = "sing-box";
    options.updateAssets = true;
    std::string error;

    const bool started = subcli::startDaemonProcess(stateDir, "/bin/sleep", {"30"}, options, error);
    require(started, "startDaemonProcess should start daemon process: " + error);

    auto status = subcli::inspectDaemonProcess(stateDir, error);
    require(error.empty(), "inspectDaemonProcess should not fail when state is valid: " + error);
    require(status.hasState, "inspectDaemonProcess should find daemon state");
    require(status.running, "inspectDaemonProcess should report running process");
    require(status.pid > 0, "inspectDaemonProcess should expose pid");
    require(status.intervalSec == 120, "inspectDaemonProcess should expose interval");
    require(status.exportTarget == "sing-box", "inspectDaemonProcess should expose export target");

    const bool duplicate = subcli::startDaemonProcess(stateDir, "/bin/sleep", {"30"}, options, error);
    require(!duplicate, "startDaemonProcess should reject duplicate start");

    const bool stopped = subcli::stopDaemonProcess(stateDir, 1, error);
    require(stopped, "stopDaemonProcess should stop daemon process: " + error);

    status = subcli::inspectDaemonProcess(stateDir, error);
    require(error.empty(), "inspectDaemonProcess should not fail after stop: " + error);
    require(!status.hasState, "inspectDaemonProcess should remove state after stop");
    require(!status.running, "inspectDaemonProcess should report stopped process");

    fs::remove_all(stateDir);
}

void testDaemonProcessLifecycleWithCustomFiles() {
    const fs::path dir = fs::temp_directory_path() / "subcli-daemon-custom-files-tests";
    fs::remove_all(dir);
    fs::create_directories(dir);

    subcli::DaemonOptions options;
    options.intervalSec = 60;
    options.exportTarget = "mihomo";
    options.pidFile = (dir / "daemon.pid").string();
    options.logFile = (dir / "daemon.log").string();
    std::string error;

    const bool started = subcli::startDaemonProcess(dir, "/bin/sleep", {"30"}, options, error);
    require(started, "startDaemonProcess should support custom file paths: " + error);
    require(fs::exists(options.pidFile), "custom pid file should be created");
    require(fs::exists(options.logFile), "custom log file should be created");

    const auto status = subcli::inspectDaemonProcess(dir, error);
    require(status.options.pidFile == options.pidFile, "daemon status should persist custom pid file");
    require(status.options.logFile == options.logFile, "daemon status should persist custom log file");

    require(subcli::stopDaemonProcess(dir, 1, error), "stopDaemonProcess should stop custom daemon files case: " + error);
    require(!fs::exists(options.pidFile), "pid file should be removed after stop");

    fs::remove_all(dir);
}

void testStartDaemonProcessFailsWhenExecCannotStart() {
    const fs::path stateDir = fs::temp_directory_path() / "subcli-daemon-start-fail-tests";
    fs::remove_all(stateDir);
    fs::create_directories(stateDir);

    subcli::DaemonOptions options;
    std::string error;

    const bool started = subcli::startDaemonProcess(
        stateDir,
        "/definitely/missing/subcli-binary",
        {"daemon", "run"},
        options,
        error
    );

    require(!started, "startDaemonProcess should fail when exec target is missing");
    require(!error.empty(), "startDaemonProcess should report exec failure");
    const auto status = subcli::inspectDaemonProcess(stateDir, error);
    require(!status.hasState, "failed daemon start should not leave daemon state");

    fs::remove_all(stateDir);
}

void testRunDaemonCycleWithStateUpdatesSuccessSummary() {
    const fs::path stateDir = fs::temp_directory_path() / "subcli-daemon-run-summary-tests";
    fs::remove_all(stateDir);
    fs::create_directories(stateDir);

    subcli::DaemonOptions options;
    subcli::DaemonCallbacks callbacks;
    callbacks.runSubCommand = [&](const std::vector<std::string>&) { return 0; };
    callbacks.runExportCommand = [&](const std::vector<std::string>&) { return 0; };
    callbacks.isCoreRunning = [&](const std::string&, std::string& error) {
        error.clear();
        return false;
    };
    callbacks.runRestartCommand = [&](const std::vector<std::string>&) { return 0; };

    require(subcli::runDaemonCycleWithState(stateDir, options, callbacks) == 0, "cycle should succeed");

    std::string error;
    const auto status = subcli::inspectDaemonProcess(stateDir, error);
    require(status.lastCycleMessage == "ok", "successful cycle should persist ok summary");
    require(status.lastCycleExitCode == 0, "successful cycle should persist zero exit code");
    require(!status.lastCycleAt.empty(), "successful cycle should persist timestamp");

    fs::remove_all(stateDir);
}

void testInspectDaemonProcessExposesLastCycleFailure() {
    const fs::path stateDir = fs::temp_directory_path() / "subcli-daemon-status-tests";
    fs::remove_all(stateDir);
    fs::create_directories(stateDir);

    subcli::DaemonOptions options;
    std::string error;
    require(subcli::startDaemonProcess(stateDir, "/bin/sleep", {"30"}, options, error), "daemon start should succeed");

    subcli::DaemonCallbacks callbacks;
    callbacks.runSubCommand = [&](const std::vector<std::string>&) { return 1; };
    callbacks.runExportCommand = [&](const std::vector<std::string>&) { return 0; };
    callbacks.isCoreRunning = [&](const std::string&, std::string& err) {
        err.clear();
        return false;
    };
    callbacks.runRestartCommand = [&](const std::vector<std::string>&) { return 0; };

    require(subcli::runDaemonCycleWithState(stateDir, options, callbacks) != 0, "cycle should fail");

    const auto status = subcli::inspectDaemonProcess(stateDir, error);
    require(status.running, "daemon process should still be running");
    require(status.lastCycleExitCode != 0, "status should expose failed last cycle");
    require(status.lastCycleMessage == "sub update failed", "status should expose failure summary");

    require(subcli::stopDaemonProcess(stateDir, 1, error), "daemon stop should succeed");
    fs::remove_all(stateDir);
}

void testDaemonCycleRestartsOnlyRunningCores() {
    subcli::DaemonOptions options;
    options.exportTarget = "all";

    bool subCalled = false;
    bool exportCalled = false;
    std::vector<std::string> restarted;
    subcli::DaemonCallbacks callbacks;
    callbacks.runSubCommand = [&](const std::vector<std::string>&) {
        subCalled = true;
        return 0;
    };
    callbacks.runExportCommand = [&](const std::vector<std::string>&) {
        exportCalled = true;
        return 0;
    };
    callbacks.isCoreRunning = [&](const std::string& target, std::string& error) {
        error.clear();
        return target == "sing-box";
    };
    callbacks.runRestartCommand = [&](const std::vector<std::string>& args) {
        restarted.push_back(args.size() >= 2 ? args[1] : "");
        return 0;
    };

    const int rc = subcli::runDaemonCycle(options, callbacks);
    require(rc == 0, "daemon cycle should succeed when callbacks succeed");
    require(subCalled, "daemon cycle should run sub update first");
    require(exportCalled, "daemon cycle should run export after update");
    require(restarted.size() == 1 && restarted[0] == "sing-box", "daemon cycle should restart only running targets");
}

void testDaemonCycleStopsOnUpdateFailure() {
    subcli::DaemonOptions options;
    bool exportCalled = false;
    subcli::DaemonCallbacks callbacks;
    callbacks.runSubCommand = [&](const std::vector<std::string>&) { return 1; };
    callbacks.runExportCommand = [&](const std::vector<std::string>&) {
        exportCalled = true;
        return 0;
    };
    callbacks.isCoreRunning = [&](const std::string&, std::string& error) {
        error.clear();
        return false;
    };
    callbacks.runRestartCommand = [&](const std::vector<std::string>&) { return 0; };

    const int rc = subcli::runDaemonCycle(options, callbacks);
    require(rc != 0, "daemon cycle should fail when update fails");
    require(!exportCalled, "daemon cycle should not run export after update failure");
}

void testCapabilityWarningsAreSpecific() {
    subcli::ProxyNode node;
    node.type = "hysteria2";
    node.name = "HY2";
    node.server = "hy.example.com";
    node.port = 443;
    node.protocol.password = "pass";
    node.normalize();

    std::string reason;
    require(!subcli::supportsNode(subcli::ExportTarget::Xray, node, reason), "xray should skip hysteria2");
    require(reason.find("hysteria2") != std::string::npos, "skip reason should name protocol");
    require(reason.find("xray") != std::string::npos, "skip reason should name target");

    require(subcli::supportsNode(subcli::ExportTarget::Mihomo, node, reason), "mihomo should now support hysteria2 export");
}

void testLegacyBridgeBuildsStructuredCoreOptions() {
    subcli::ProxyNode ss;
    ss.type = "ss";
    ss.name = "SS";
    ss.server = "ss.example.com";
    ss.port = 8388;
    ss.protocol.cipher = "2022-blake3-aes-128-gcm";
    ss.protocol.password = "password";

    const auto structured = subcli::legacyToStructuredNode(ss);
    require(structured.protocol == "shadowsocks", "legacy bridge should canonicalize ss protocol");
    require(structured.server.host == "ss.example.com", "legacy bridge should keep server host");
    const auto* options = std::get_if<subcli::ShadowsocksOptions>(&structured.options);
    require(options != nullptr, "legacy bridge should produce shadowsocks options");
    require(options->method == "2022-blake3-aes-128-gcm", "legacy bridge should keep shadowsocks method");
    require(options->password == "password", "legacy bridge should keep shadowsocks password");
}

void testStructuredWritersPreserveVlessEncryption() {
    subcli::ProxyNode node;
    node.type = "vless";
    node.name = "VLESS";
    node.server = "vless.example.com";
    node.port = 443;
    node.protocol.uuid = "11111111-1111-1111-1111-111111111111";
    node.protocol.flow = "xtls-rprx-vision";
    node.protocol.values["encryption"] = "none";
    node.normalize();

    const auto mihomo = subcli::makeMihomoProxy(node);
    require(mihomo["type"].as<std::string>("") == "vless", "mihomo writer should output vless type");
    require(mihomo["uuid"].as<std::string>("") == node.protocol.uuid, "mihomo writer should output vless uuid");
    require(mihomo["flow"].as<std::string>("") == "xtls-rprx-vision", "mihomo writer should output vless flow");
    require(mihomo["encryption"].as<std::string>("") == "none", "mihomo writer should output vless encryption");

    const auto xray = subcli::makeXrayOutbound(node);
    require(xray.value("protocol", "") == "vless", "xray writer should output vless protocol");
    const auto& user = xray["settings"]["vnext"][0]["users"][0];
    require(user.value("id", "") == node.protocol.uuid, "xray writer should output vless id");
    require(user.value("flow", "") == "xtls-rprx-vision", "xray writer should output vless flow");
    require(user.value("encryption", "") == "none", "xray writer should output vless encryption");
}

void testStructuredWritersNormalizeShadowsocksNames() {
    subcli::ProxyNode node;
    node.type = "ss";
    node.name = "SS";
    node.server = "ss.example.com";
    node.port = 8388;
    node.protocol.cipher = "chacha20-ietf-poly1305";
    node.protocol.password = "password";
    node.normalize();

    const auto mihomo = subcli::makeMihomoProxy(node);
    require(mihomo["type"].as<std::string>("") == "ss", "mihomo writer should use ss type");
    require(mihomo["cipher"].as<std::string>("") == "chacha20-ietf-poly1305", "mihomo writer should output cipher");

    const auto singBox = subcli::makeSingBoxOutbound(node);
    require(singBox.value("type", "") == "shadowsocks", "sing-box writer should use shadowsocks type");
    require(singBox.value("method", "") == "chacha20-ietf-poly1305", "sing-box writer should output method");

    const auto xray = subcli::makeXrayOutbound(node);
    require(xray.value("protocol", "") == "shadowsocks", "xray writer should use shadowsocks protocol");
    const auto& server = xray["settings"]["servers"][0];
    require(server.value("method", "") == "chacha20-ietf-poly1305", "xray writer should output method");
}

subcli::ProxyNode makeWireGuardNode() {
    subcli::ProxyNode node;
    node.type = "wireguard";
    node.name = "WG";
    node.server = "wg.example.com";
    node.port = 51820;
    node.protocol.values["private_key"] = "private-key";
    node.protocol.values["local_address"] = "10.0.0.2/32,fd00::2/128";
    node.protocol.values["peer_public_key"] = "peer-public-key";
    node.protocol.values["pre_shared_key"] = "psk";
    node.protocol.values["peer_allowed_ips"] = "0.0.0.0/0,::/0";
    node.protocol.values["reserved"] = "1,2,3";
    node.protocol.values["mtu"] = "1408";
    node.region = "HK";
    node.normalize();
    return node;
}

void testWireGuardWritersUseTargetSchemas() {
    const auto node = makeWireGuardNode();

    const auto mihomo = subcli::makeMihomoProxy(node);
    require(mihomo["type"].as<std::string>("") == "wireguard", "mihomo should output wireguard type");
    require(mihomo["private-key"].as<std::string>("") == "private-key", "mihomo should output private-key");
    require(mihomo["public-key"].as<std::string>("") == "peer-public-key", "mihomo should output public-key");
    require(mihomo["allowed-ips"].IsSequence(), "mihomo should output allowed-ips sequence");

    const auto endpoint = subcli::makeSingBoxWireGuardEndpoint(node);
    require(endpoint.value("type", "") == "wireguard", "sing-box endpoint should be wireguard");
    require(endpoint.value("tag", "") == "WG", "sing-box endpoint should keep tag");
    require(endpoint.value("private_key", "") == "private-key", "sing-box endpoint should output private_key");
    require(endpoint["peers"][0].value("public_key", "") == "peer-public-key", "sing-box endpoint should output peer key");

    const auto xray = subcli::makeXrayOutbound(node);
    require(xray.value("protocol", "") == "wireguard", "xray should output wireguard protocol");
    require(xray["settings"].value("secretKey", "") == "private-key", "xray should output secretKey");
    require(xray["settings"]["peers"][0].value("publicKey", "") == "peer-public-key", "xray should output peer publicKey");
}

void testExportSingBoxWireGuardUsesEndpoint() {
    auto config = makeConfig();
    const fs::path outDir = fs::temp_directory_path() / "subcli-tests";
    fs::create_directories(outDir);

    std::string error;
    auto result = subcli::exportForTarget(subcli::ExportTarget::SingBox, {makeWireGuardNode()}, config, false, (outDir / "sing-box-wg.json").string(), error);
    require(result.ok, "sing-box wireguard export should succeed: " + error);

    std::ifstream in(outDir / "sing-box-wg.json");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto json = nlohmann::json::parse(content);
    require(json.contains("endpoints") && json["endpoints"].is_array(), "sing-box wireguard export should include endpoints");
    require(json["endpoints"].size() == 1, "sing-box wireguard export should create one endpoint");
    require(json["endpoints"][0].value("type", "") == "wireguard", "sing-box wireguard endpoint should be present");
    require(json["endpoints"][0].value("private_key", "") == "private-key", "sing-box wireguard endpoint should include private_key");
    for (const auto& outbound : json["outbounds"]) {
        require(outbound.value("type", "") != "wireguard", "sing-box wireguard must not be emitted as deprecated outbound");
    }
}

void testModernUdpWritersUseTargetSchemas() {
    subcli::ProxyNode hy2;
    hy2.type = "hysteria2";
    hy2.name = "HY2";
    hy2.server = "hy2.example.com";
    hy2.port = 443;
    hy2.protocol.password = "hy-pass";
    hy2.protocol.values["ports"] = "443-8443";
    hy2.protocol.values["hop_interval"] = "30";
    hy2.protocol.values["up"] = "30 Mbps";
    hy2.protocol.values["down"] = "200 Mbps";
    hy2.protocol.values["obfs_type"] = "salamander";
    hy2.protocol.values["obfs_password"] = "obfs-pass";
    hy2.normalize();

    const auto mihomoHy2 = subcli::makeMihomoProxy(hy2);
    require(mihomoHy2["type"].as<std::string>("") == "hysteria2", "mihomo should output hysteria2 type");
    require(mihomoHy2["password"].as<std::string>("") == "hy-pass", "mihomo hy2 should output password");
    require(mihomoHy2["ports"].as<std::string>("") == "443-8443", "mihomo hy2 should output ports");
    require(mihomoHy2["obfs"].as<std::string>("") == "salamander", "mihomo hy2 should output obfs");

    subcli::ProxyNode tuic;
    tuic.type = "tuic";
    tuic.name = "TUIC";
    tuic.server = "tuic.example.com";
    tuic.port = 443;
    tuic.protocol.uuid = "22222222-2222-2222-2222-222222222222";
    tuic.protocol.password = "tuic-pass";
    tuic.protocol.values["congestion_control"] = "bbr";
    tuic.protocol.values["udp_relay_mode"] = "native";
    tuic.protocol.values["zero_rtt_handshake"] = "true";
    tuic.normalize();

    const auto mihomoTuic = subcli::makeMihomoProxy(tuic);
    require(mihomoTuic["type"].as<std::string>("") == "tuic", "mihomo should output tuic type");
    require(mihomoTuic["uuid"].as<std::string>("") == tuic.protocol.uuid, "mihomo tuic should output uuid");
    require(mihomoTuic["congestion-controller"].as<std::string>("") == "bbr", "mihomo tuic should output congestion-controller");
    require(mihomoTuic["reduce-rtt"].as<bool>(false), "mihomo tuic should map zero-rtt to reduce-rtt");

    subcli::ProxyNode hy;
    hy.type = "hysteria";
    hy.name = "HY";
    hy.server = "hy.example.com";
    hy.port = 443;
    hy.protocol.values["version"] = "2";
    hy.normalize();

    const auto xrayHy = subcli::makeXrayOutbound(hy);
    require(xrayHy.value("protocol", "") == "hysteria", "xray should output hysteria protocol");
    require(xrayHy["settings"].value("address", "") == "hy.example.com", "xray hysteria should output address");
    require(xrayHy["settings"].value("port", 0) == 443, "xray hysteria should output port");
    require(xrayHy["settings"].value("version", 0) == 2, "xray hysteria should output version");
}

void testSingBoxModernUdpWritersUseStructuredFields() {
    subcli::ProxyNode hy2;
    hy2.type = "hysteria2";
    hy2.name = "HY2";
    hy2.server = "hy2.example.com";
    hy2.port = 443;
    hy2.protocol.password = "hy-pass";
    hy2.protocol.values["ports"] = "443-8443";
    hy2.protocol.values["hop_interval"] = "30s";
    hy2.protocol.values["hop_interval_max"] = "60s";
    hy2.protocol.values["up_mbps"] = "30";
    hy2.protocol.values["down_mbps"] = "200";
    hy2.protocol.values["obfs_type"] = "salamander";
    hy2.protocol.values["obfs_password"] = "obfs-pass";
    hy2.normalize();

    const auto singHy2 = subcli::makeSingBoxOutbound(hy2);
    require(singHy2.value("type", "") == "hysteria2", "sing-box should output hysteria2 type");
    require(singHy2["server_ports"][0].get<std::string>() == "443-8443", "sing-box hy2 should output server_ports");
    require(singHy2.value("hop_interval", "") == "30s", "sing-box hy2 should output hop_interval");
    require(singHy2.value("hop_interval_max", "") == "60s", "sing-box hy2 should output hop_interval_max");
    require(singHy2["obfs"].value("password", "") == "obfs-pass", "sing-box hy2 should output obfs password");

    subcli::ProxyNode tuic;
    tuic.type = "tuic";
    tuic.name = "TUIC";
    tuic.server = "tuic.example.com";
    tuic.port = 443;
    tuic.protocol.uuid = "22222222-2222-2222-2222-222222222222";
    tuic.protocol.password = "tuic-pass";
    tuic.protocol.values["congestion_control"] = "bbr";
    tuic.protocol.values["udp_relay_mode"] = "native";
    tuic.protocol.values["zero_rtt_handshake"] = "true";
    tuic.protocol.values["heartbeat"] = "10s";
    tuic.normalize();

    const auto singTuic = subcli::makeSingBoxOutbound(tuic);
    require(singTuic.value("type", "") == "tuic", "sing-box should output tuic type");
    require(singTuic.value("zero_rtt_handshake", false), "sing-box tuic should output zero_rtt_handshake");
    require(singTuic.value("heartbeat", "") == "10s", "sing-box tuic should output heartbeat");
}

void testXrayBuiltInOutboundsParseAndExport() {
    const std::string content = R"JSON({
      "outbounds": [
        {"tag": "FREE", "protocol": "freedom", "settings": {}},
        {"tag": "DROP", "protocol": "blackhole", "settings": {}},
        {"tag": "DNS", "protocol": "dns", "settings": {}},
        {"tag": "LOOP", "protocol": "loopback", "settings": {}},
        {"tag": "HTTP", "protocol": "http", "settings": {"servers": [{"address": "proxy.example.com", "port": 8080}]}}
      ]
    })JSON";
    auto result = subcli::parseSubscription(content, "fixture", "xray", makeConfig());
    require(result.nodes.size() == 5, "xray built-in/proxy outbounds should parse");
    std::string reason;
    require(subcli::supportsNode(subcli::ExportTarget::Xray, result.nodes[0], reason), "xray freedom should be supported");

    const auto freedom = subcli::makeXrayOutbound(result.nodes[0]);
    require(freedom.value("protocol", "") == "freedom", "xray freedom should export protocol");
    require(freedom["settings"].is_object(), "xray freedom should export settings object");
    const auto http = subcli::makeXrayOutbound(result.nodes[4]);
    require(http.value("protocol", "") == "http", "xray http should export protocol");
    require(http["settings"]["servers"][0].value("address", "") == "proxy.example.com", "xray http should export server address");
}

void testNativeLongTailPassthroughWriters() {
    const std::string mihomo = R"YAML(proxies:
  - name: SSR
    type: ssr
    server: ssr.example.com
    port: 8388
    cipher: aes-128-gcm
    password: password
    obfs: tls1.2_ticket_auth
    protocol: auth_sha1_v4
)YAML";
    auto mihomoResult = subcli::parseSubscription(mihomo, "fixture", "mihomo", makeConfig());
    require(mihomoResult.nodes.size() == 1, "mihomo ssr should parse");
    std::string reason;
    require(subcli::supportsNode(subcli::ExportTarget::Mihomo, mihomoResult.nodes[0], reason), "mihomo ssr should be export-ready");
    const auto mihomoOut = subcli::makeMihomoProxy(mihomoResult.nodes[0]);
    require(mihomoOut["type"].as<std::string>("") == "ssr", "mihomo ssr should export type");
    require(mihomoOut["password"].as<std::string>("") == "password", "mihomo ssr should preserve password");
    require(mihomoOut["obfs"].as<std::string>("") == "tls1.2_ticket_auth", "mihomo ssr should preserve raw obfs");
    require(mihomoOut["protocol"].as<std::string>("") == "auth_sha1_v4", "mihomo ssr should preserve raw protocol");

    const std::string sing = R"JSON({
      "outbounds": [
        {"type": "shadowtls", "tag": "STLS", "server": "stls.example.com", "server_port": 443, "password": "password", "version": 3}
      ]
    })JSON";
    auto singResult = subcli::parseSubscription(sing, "fixture", "sing-box", makeConfig());
    require(singResult.nodes.size() == 1, "sing-box shadowtls should parse");
    require(subcli::supportsNode(subcli::ExportTarget::SingBox, singResult.nodes[0], reason), "sing-box shadowtls should be export-ready");
    const auto singOut = subcli::makeSingBoxOutbound(singResult.nodes[0]);
    require(singOut.value("type", "") == "shadowtls", "sing-box shadowtls should export type");
    require(singOut.value("password", "") == "password", "sing-box shadowtls should preserve password");
    require(singOut.value("version", 0) == 3, "sing-box shadowtls should preserve raw version");
}

void testModernUdpXraySkipsUnsupportedProtocols() {
    subcli::ProxyNode hy2;
    hy2.type = "hysteria2";
    hy2.name = "HY2";
    hy2.server = "hy2.example.com";
    hy2.port = 443;
    hy2.protocol.password = "hy-pass";
    hy2.normalize();

    std::string reason;
    require(!subcli::supportsNode(subcli::ExportTarget::Xray, hy2, reason), "xray should skip hysteria2");
    require(reason.find("hysteria2") != std::string::npos, "xray skip reason should name hysteria2");

    subcli::ProxyNode tuic;
    tuic.type = "tuic";
    tuic.name = "TUIC";
    tuic.server = "tuic.example.com";
    tuic.port = 443;
    tuic.normalize();
    require(!subcli::supportsNode(subcli::ExportTarget::Xray, tuic, reason), "xray should skip tuic");
    require(reason.find("tuic") != std::string::npos, "xray skip reason should name tuic");
}

void testParseXrayJson() {
    const std::string content = R"JSON({
      "outbounds": [
        {
          "tag": "JP Node",
          "protocol": "vless",
          "settings": {
            "vnext": [
              {
                "address": "example.com",
                "port": 443,
                "users": [
                  {"id": "11111111-1111-1111-1111-111111111111", "flow": "xtls-rprx-vision"}
                ]
              }
            ]
          },
          "streamSettings": {
            "network": "grpc",
            "security": "reality",
            "realitySettings": {
              "serverName": "www.google.com",
              "publicKey": "pubkey",
              "shortId": "abcd",
              "spiderX": "/"
            },
            "grpcSettings": {
              "serviceName": "grpc-service",
              "authority": "grpc.example.com"
            }
          }
        }
      ]
    })JSON";

    auto result = subcli::parseSubscription(content, "fixture", makeConfig());
    require(result.nodes.size() == 1, "expected one parsed xray node");
    const auto& node = result.nodes.front();
    require(node.server == "example.com", "xray parser should read server");
    require(node.port == 443, "xray parser should read port");
    require(node.transport.network == "grpc", "xray parser should read grpc network");
    require(node.tlsConfig.reality.enabled, "xray parser should read reality");
    require(node.tlsConfig.reality.publicKey == "pubkey", "xray parser should read reality public key");
    require(node.transport.serviceName == "grpc-service", "xray parser should read grpc service name");
}

void testParseUriIgnoresInvalidVmessPort() {
    const std::string content =
        "vmess://eyJhZGQiOiJ2bS5leGFtcGxlLmNvbSIsInBvcnQiOiJub3QtYS1udW1iZXIiLCJpZCI6IjExMTExMTExLTExMTEtMTExMS0xMTExLTExMTExMTExMTExMSIsInBzIjoiQmFkIFZNZXNzIn0=\n"
        "vless://11111111-1111-1111-1111-111111111111@good.example.com:443?security=tls&type=ws#Good%20Node\n";

    auto result = subcli::parseSubscription(content, "fixture", "uri", makeConfig());
    require(result.nodes.size() == 1, "invalid vmess row should be skipped while valid vless row remains");
    require(result.skipped == 1, "invalid uri row should increment skipped count");
}

void testParseModernUdpUriLinks() {
    const std::string content =
        "hy2://hy-pass@hy2.example.com:443?sni=www.google.com&insecure=1&obfs=salamander&obfs-password=obfs-pass#HK%20HY2\n"
        "tuic://22222222-2222-2222-2222-222222222222:tuic-pass@tuic.example.com:8443?sni=www.google.com&congestion_control=bbr&udp_relay_mode=native#JP%20TUIC\n"
        "wireguard://private-key@wg.example.com:51820?publickey=peer-public-key&address=10.0.0.2%2F32&allowedips=0.0.0.0%2F0&reserved=1,2,3#WG%20Node\n";

    auto result = subcli::parseSubscription(content, "fixture", "uri", makeConfig());
    require(result.nodes.size() == 3, "modern UDP URI links should parse");

    const auto& hy2 = result.nodes[0];
    require(hy2.type == "hysteria2", "hy2 URI should parse as hysteria2");
    require(hy2.server == "hy2.example.com", "hy2 URI should parse server");
    require(hy2.port == 443, "hy2 URI should parse port");
    require(hy2.protocol.password == "hy-pass", "hy2 URI should parse password");
    require(hy2.tlsConfig.sni == "www.google.com", "hy2 URI should parse SNI");
    require(hy2.tlsConfig.allowInsecure, "hy2 URI should parse insecure flag");
    require(hy2.protocol.values.at("obfs_type") == "salamander", "hy2 URI should parse obfs type");
    require(hy2.protocol.values.at("obfs_password") == "obfs-pass", "hy2 URI should parse obfs password");

    const auto& tuic = result.nodes[1];
    require(tuic.type == "tuic", "tuic URI should parse as tuic");
    require(tuic.protocol.uuid == "22222222-2222-2222-2222-222222222222", "tuic URI should parse uuid");
    require(tuic.protocol.password == "tuic-pass", "tuic URI should parse password");
    require(tuic.protocol.values.at("congestion_control") == "bbr", "tuic URI should parse congestion control");
    require(tuic.protocol.values.at("udp_relay_mode") == "native", "tuic URI should parse UDP relay mode");

    const auto& wg = result.nodes[2];
    require(wg.type == "wireguard", "wireguard URI should parse as wireguard");
    require(wg.protocol.values.at("private_key") == "private-key", "wireguard URI should parse private key");
    require(wg.protocol.values.at("peer_public_key") == "peer-public-key", "wireguard URI should parse peer public key");
    require(wg.protocol.values.at("local_address") == "10.0.0.2/32", "wireguard URI should parse local address");
    require(wg.protocol.values.at("peer_allowed_ips") == "0.0.0.0/0", "wireguard URI should parse allowed IPs");
}

void testUnknownFormatHintFallsBackToAuto() {
    const std::string content = "vless://11111111-1111-1111-1111-111111111111@example.com:443?security=tls#Node\n";
    auto result = subcli::parseSubscription(content, "fixture", "mystery-format", makeConfig());
    require(result.nodes.size() == 1, "unknown format hint should fall back to auto parser");
    bool hasUnknownWarning = false;
    for (const auto& warning : result.warnings) {
        if (warning.code == "unknown_format_hint") {
            hasUnknownWarning = true;
            break;
        }
    }
    require(hasUnknownWarning, "unknown format hint should emit warning");
}

void testHintParseFailedFallsBackToAuto() {
    const std::string content = R"JSON({
      "outbounds": [
        {
          "tag": "JP Node",
          "protocol": "vless",
          "settings": {
            "vnext": [
              {
                "address": "example.com",
                "port": 443,
                "users": [
                  {"id": "11111111-1111-1111-1111-111111111111"}
                ]
              }
            ]
          }
        }
      ]
    })JSON";
    auto result = subcli::parseSubscription(content, "fixture", "uri", makeConfig());
    require(result.nodes.size() == 1, "hint parser fallback should still parse xray json");
    bool hasFallbackWarning = false;
    for (const auto& warning : result.warnings) {
        if (warning.code == "hint_parse_failed_fallback_to_auto") {
            hasFallbackWarning = true;
            break;
        }
    }
    require(hasFallbackWarning, "hint parse failure should emit fallback warning");
}

void testParseMihomoHttpOpts() {
    const std::string content = R"YAML(proxies:
  - name: HTTP Node
    type: vless
    server: http.example.com
    port: 443
    uuid: 11111111-1111-1111-1111-111111111111
    network: http
    http-opts:
      path: /api
      headers:
        Host:
          - h1.example.com
          - h2.example.com
)YAML";

    auto result = subcli::parseSubscription(content, "fixture", "mihomo", makeConfig());
    require(result.nodes.size() == 1, "mihomo http node should parse");
    const auto& node = result.nodes.front();
    require(node.transport.network == "http", "mihomo parser should keep http network");
    require(node.transport.path == "/api", "mihomo parser should parse http-opts path");
    require(node.transport.host == "h1.example.com,h2.example.com", "mihomo parser should parse http host list");
}

void testParseMihomoH2Opts() {
    const std::string content = R"YAML(proxies:
  - name: H2 Node
    type: vmess
    server: h2.example.com
    port: 443
    uuid: 22222222-2222-2222-2222-222222222222
    network: h2
    h2-opts:
      path: /h2
      host:
        - a.example.com
        - b.example.com
)YAML";

    auto result = subcli::parseSubscription(content, "fixture", "mihomo", makeConfig());
    require(result.nodes.size() == 1, "mihomo h2 node should parse");
    const auto& node = result.nodes.front();
    require(node.transport.network == "h2", "mihomo parser should keep h2 network");
    require(node.transport.path == "/h2", "mihomo parser should parse h2-opts path");
    require(node.transport.host == "a.example.com,b.example.com", "mihomo parser should parse h2 host list");
}

void testParseMihomoLocalProxyProvider() {
    const fs::path dir = fs::temp_directory_path() / "subcli-provider-tests";
    fs::create_directories(dir);
    const fs::path providerPath = dir / "provider.yaml";
    {
        std::ofstream provider(providerPath);
        provider << R"YAML(proxies:
  - name: Provider HK
    type: ss
    server: provider.example.com
    port: 8388
    cipher: chacha20-ietf-poly1305
    password: password
)YAML";
    }

    const std::string content = std::string(R"YAML(proxy-providers:
  airport:
    type: file
    path: ")YAML") + providerPath.string() + R"YAML("
)YAML";

    auto result = subcli::parseSubscription(content, "fixture", "mihomo", makeConfig());
    require(result.nodes.size() == 1, "mihomo local proxy-provider should expand one node");
    require(result.nodes[0].name == "Provider HK", "provider node name should parse");
    require(result.nodes[0].server == "provider.example.com", "provider node server should parse");
    require(result.nodes[0].sourceId == "fixture", "provider node source id should remain subscription source");

    fs::remove_all(dir);
}

void testParseMihomoRemoteProxyProvider() {
    const fs::path dir = fs::temp_directory_path() / "subcli-remote-provider-tests";
    fs::create_directories(dir);
    const fs::path providerPath = dir / "remote-provider.yaml";
    {
        std::ofstream provider(providerPath);
        provider << R"YAML(proxies:
  - name: Remote Provider HK
    type: ss
    server: remote-provider.example.com
    port: 8388
    cipher: chacha20-ietf-poly1305
    password: password
)YAML";
    }

    const std::string content = std::string(R"YAML(proxy-providers:
  airport:
    type: http
    url: "file://)YAML") + providerPath.string() + R"YAML("
)YAML";

    auto result = subcli::parseSubscription(content, "fixture", "mihomo", makeConfig());
    require(result.nodes.size() == 1, "mihomo remote proxy-provider should expand one node");
    require(result.nodes[0].name == "Remote Provider HK", "remote provider node name should parse");
    require(result.nodes[0].server == "remote-provider.example.com", "remote provider node server should parse");

    fs::remove_all(dir);
}

void testParseMihomoRemoteProxyProviderWithHttpFields() {
    const int serverFd = ::socket(AF_INET, SOCK_STREAM, 0);
    require(serverFd >= 0, "test server socket should create");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    require(::bind(serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0, "test server bind should succeed");
    require(::listen(serverFd, 1) == 0, "test server listen should succeed");

    sockaddr_in bound{};
    socklen_t boundLen = sizeof(bound);
    require(::getsockname(serverFd, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0, "test server should report bound port");
    const int port = ntohs(bound.sin_port);

    std::promise<std::string> requestPromise;
    auto requestFuture = requestPromise.get_future();
    std::thread serverThread([serverFd, p = std::move(requestPromise)]() mutable {
        int client = ::accept(serverFd, nullptr, nullptr);
        if (client < 0) {
            p.set_value("");
            ::close(serverFd);
            return;
        }
        std::string request;
        char buf[1024];
        while (request.find("\r\n\r\n") == std::string::npos) {
            const ssize_t n = ::recv(client, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            request.append(buf, static_cast<size_t>(n));
        }
        const std::string body = R"YAML(proxies:
  - name: Header Provider HK
    type: ss
    server: header-provider.example.com
    port: 8388
    cipher: chacha20-ietf-poly1305
    password: password
)YAML";
        const std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: " +
            std::to_string(body.size()) +
            "\r\n"
            "Connection: close\r\n\r\n" +
            body;
        (void)::send(client, response.c_str(), response.size(), 0);
        ::close(client);
        ::close(serverFd);
        p.set_value(request);
    });

    const std::string content = std::string(R"YAML(proxy-providers:
  airport:
    type: http
    url: "http://127.0.0.1:)YAML") + std::to_string(port) + R"YAML(/provider"
    user-agent: "SubCLI-Provider-UA/1.0"
    header:
      Authorization: "Bearer provider-token"
)YAML";

    auto result = subcli::parseSubscription(content, "fixture", "mihomo", makeConfig());
    serverThread.join();
    const auto request = requestFuture.get();

    require(result.nodes.size() == 1, "mihomo remote provider with http fields should expand one node");
    require(result.nodes[0].name == "Header Provider HK", "remote provider node name should parse with http fields");
    require(result.nodes[0].server == "header-provider.example.com", "remote provider node server should parse with http fields");
    require(request.find("Authorization: Bearer provider-token\r\n") != std::string::npos, "provider request should include Authorization header");
    require(request.find("User-Agent: SubCLI-Provider-UA/1.0\r\n") != std::string::npos, "provider request should include user-agent");
}

void testParseMihomoHighFrequencyOptionalFields() {
    const std::string content = R"YAML(proxies:
  - name: VLESS XHTTP
    type: vless
    server: vless.example.com
    port: 443
    uuid: 11111111-1111-1111-1111-111111111111
    flow: xtls-rprx-vision
    encryption: none
    packet-encoding: xudp
    network: xhttp
  - name: SS UOT
    type: ss
    server: ss.example.com
    port: 8388
    cipher: chacha20-ietf-poly1305
    password: password
    udp-over-tcp: true
    udp-over-tcp-version: 2
)YAML";

    auto result = subcli::parseSubscription(content, "fixture", "mihomo", makeConfig());
    require(result.nodes.size() == 2, "mihomo optional field fixture should parse two nodes");
    require(result.nodes[0].protocol.values.at("encryption") == "none", "mihomo should parse vless encryption");
    require(result.nodes[0].protocol.values.at("packet_encoding") == "xudp", "mihomo should parse packet encoding");
    require(result.nodes[1].protocol.values.at("udp_over_tcp") == "true", "mihomo should parse ss udp-over-tcp");
    require(result.nodes[1].protocol.values.at("udp_over_tcp_version") == "2", "mihomo should parse ss udp-over-tcp-version");
}

void testParseSingBoxTlsDisabledObject() {
    const std::string content = R"JSON({
      "outbounds": [
        {
          "type": "vless",
          "tag": "No TLS",
          "server": "vless.example.com",
          "server_port": 443,
          "uuid": "33333333-3333-3333-3333-333333333333",
          "tls": {"enabled": false, "server_name": "example.com"}
        }
      ]
    })JSON";

    auto result = subcli::parseSubscription(content, "fixture", "sing-box", makeConfig());
    require(result.nodes.size() == 1, "sing-box node should parse");
    const auto& node = result.nodes.front();
    require(!node.tlsConfig.enabled, "tls.enabled false should remain disabled");
    require(!node.tls, "legacy tls flag should remain false");
}

void testParseSingBoxHighFrequencyOptionalFields() {
    const std::string content = R"JSON({
      "outbounds": [
        {
          "type": "vmess",
          "tag": "VMess",
          "server": "vmess.example.com",
          "server_port": 443,
          "uuid": "22222222-2222-2222-2222-222222222222",
          "security": "auto",
          "alter_id": 1,
          "packet_encoding": "packetaddr",
          "global_padding": true,
          "authenticated_length": true
        },
        {
          "type": "vless",
          "tag": "VLESS",
          "server": "vless.example.com",
          "server_port": 443,
          "uuid": "33333333-3333-3333-3333-333333333333",
          "encryption": "none",
          "packet_encoding": "xudp"
        }
      ]
    })JSON";

    auto result = subcli::parseSubscription(content, "fixture", "sing-box", makeConfig());
    require(result.nodes.size() == 2, "sing-box optional field fixture should parse two nodes");
    require(result.nodes[0].protocol.values.at("alter_id") == "1", "sing-box should parse vmess alter_id");
    require(result.nodes[0].protocol.values.at("packet_encoding") == "packetaddr", "sing-box should parse packet_encoding");
    require(result.nodes[0].protocol.values.at("authenticated_length") == "true", "sing-box should parse authenticated_length");
    require(result.nodes[1].protocol.values.at("encryption") == "none", "sing-box should parse vless encryption");
}

void testParseXrayHighFrequencyOptionalFields() {
    const std::string content = R"JSON({
      "outbounds": [
        {
          "tag": "VLESS",
          "protocol": "vless",
          "settings": {
            "vnext": [{
              "address": "vless.example.com",
              "port": 443,
              "users": [{"id": "44444444-4444-4444-4444-444444444444", "encryption": "none", "flow": "xtls-rprx-vision"}]
            }]
          }
        },
        {
          "tag": "SS",
          "protocol": "shadowsocks",
          "settings": {
            "servers": [{"address": "ss.example.com", "port": 8388, "method": "chacha20-ietf-poly1305", "password": "password", "uot": true, "UoTVersion": 2}]
          }
        }
      ]
    })JSON";

    auto result = subcli::parseSubscription(content, "fixture", "xray", makeConfig());
    require(result.nodes.size() == 2, "xray optional field fixture should parse two nodes");
    require(result.nodes[0].protocol.values.at("encryption") == "none", "xray should parse vless encryption");
    require(result.nodes[1].protocol.values.at("udp_over_tcp") == "true", "xray should parse shadowsocks uot");
    require(result.nodes[1].protocol.values.at("udp_over_tcp_version") == "2", "xray should parse shadowsocks UoTVersion");
}

void testParseWireGuardNativeShapes() {
    const std::string sing = R"JSON({
      "endpoints": [
        {
          "type": "wireguard",
          "tag": "WG EP",
          "address": ["10.0.0.2/32"],
          "private_key": "private-key",
          "peers": [{"address": "wg.example.com", "port": 51820, "public_key": "peer-public-key", "allowed_ips": ["0.0.0.0/0"], "reserved": [1, 2, 3]}]
        }
      ]
    })JSON";
    auto singResult = subcli::parseSubscription(sing, "fixture", "sing-box", makeConfig());
    require(singResult.nodes.size() == 1, "sing-box endpoint wireguard should parse");
    require(singResult.nodes[0].server == "wg.example.com", "sing-box endpoint peer address should parse");
    require(singResult.nodes[0].protocol.values.at("private_key") == "private-key", "sing-box endpoint private key should parse");
    require(singResult.nodes[0].protocol.values.at("peer_public_key") == "peer-public-key", "sing-box endpoint peer key should parse");

    const std::string xray = R"JSON({
      "outbounds": [
        {
          "tag": "WG",
          "protocol": "wireguard",
          "settings": {
            "secretKey": "private-key",
            "address": ["10.0.0.2/32"],
            "peers": [{"endpoint": "wg.example.com:51820", "publicKey": "peer-public-key", "allowedIPs": ["0.0.0.0/0"]}]
          }
        }
      ]
    })JSON";
    auto xrayResult = subcli::parseSubscription(xray, "fixture", "xray", makeConfig());
    require(xrayResult.nodes.size() == 1, "xray wireguard should parse");
    require(xrayResult.nodes[0].server == "wg.example.com", "xray wireguard endpoint host should parse");
    require(xrayResult.nodes[0].port == 51820, "xray wireguard endpoint port should parse");
    require(xrayResult.nodes[0].protocol.values.at("private_key") == "private-key", "xray wireguard secretKey should parse");
}

void testParseModernUdpNativeShapes() {
    const std::string mihomo = R"YAML(proxies:
  - name: HY2
    type: hysteria2
    server: hy2.example.com
    port: 443
    ports: 443-8443
    hop-interval: 30
    password: hy-pass
    up: 30 Mbps
    down: 200 Mbps
    obfs: salamander
    obfs-password: obfs-pass
  - name: TUIC
    type: tuic
    server: tuic.example.com
    port: 443
    uuid: 22222222-2222-2222-2222-222222222222
    password: tuic-pass
    congestion-controller: bbr
    udp-relay-mode: native
    reduce-rtt: true
)YAML";
    auto mihomoResult = subcli::parseSubscription(mihomo, "fixture", "mihomo", makeConfig());
    require(mihomoResult.nodes.size() == 2, "mihomo modern udp fixture should parse two nodes");
    require(mihomoResult.nodes[0].protocol.values.at("ports") == "443-8443", "mihomo hy2 ports should parse");
    require(mihomoResult.nodes[0].protocol.values.at("obfs_type") == "salamander", "mihomo hy2 obfs should parse");
    require(mihomoResult.nodes[1].protocol.values.at("congestion_control") == "bbr", "mihomo tuic congestion should parse");

    const std::string xray = R"JSON({
      "outbounds": [
        {"tag": "HY", "protocol": "hysteria", "settings": {"version": 2, "address": "hy.example.com", "port": 443}}
      ]
    })JSON";
    auto xrayResult = subcli::parseSubscription(xray, "fixture", "xray", makeConfig());
    require(xrayResult.nodes.size() == 1, "xray hysteria should parse");
    require(xrayResult.nodes[0].server == "hy.example.com", "xray hysteria address should parse");
    require(xrayResult.nodes[0].protocol.values.at("version") == "2", "xray hysteria version should parse");
}

void testExportSingBox() {
    auto config = makeConfig();
    subcli::ProxyNode node;
    node.type = "ss";
    node.name = "HK Node";
    node.server = "1.2.3.4";
    node.port = 1234;
    node.protocol.password = "password";
    node.protocol.cipher = "chacha20-ietf-poly1305";
    node.region = "HK";
    node.syncLegacyFromStructured();
    node.syncStructuredFromLegacy();

    const fs::path outDir = fs::temp_directory_path() / "subcli-tests";
    fs::create_directories(outDir);
    std::string error;
    auto result = subcli::exportForTarget(subcli::ExportTarget::SingBox, {node}, config, false, (outDir / "sing-box.json").string(), error);
    require(result.ok, "sing-box export should succeed: " + error);
    std::ifstream in(outDir / "sing-box.json");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    require(content.find("\"tag\": \"PROXY\"") != std::string::npos, "export should contain PROXY selector");
    require(content.find("\"tag\": \"HK\"") != std::string::npos, "export should contain dynamic HK selector");
}

void testParseSingBoxExtendedProtocols() {
    const std::string content = R"JSON({
      "outbounds": [
        {
          "type": "hysteria2",
          "tag": "HK Hy2",
          "server": "hy.example.com",
          "server_port": 443,
          "password": "hy-pass",
          "up_mbps": 100,
          "down_mbps": 200,
          "obfs": {"type": "salamander", "password": "obfs-pass"},
          "tls": {"enabled": true, "server_name": "google.com"}
        },
        {
          "type": "tuic",
          "tag": "JP Tuic",
          "server": "tuic.example.com",
          "server_port": 443,
          "uuid": "22222222-2222-2222-2222-222222222222",
          "password": "tuic-pass",
          "congestion_control": "bbr",
          "tls": {"enabled": true, "server_name": "www.google.com"}
        },
        {
          "type": "wireguard",
          "tag": "WG Node",
          "server": "wg.example.com",
          "server_port": 51820,
          "private_key": "private-key",
          "peer_public_key": "peer-public-key",
          "local_address": ["10.0.0.2/32"],
          "peer_allowed_ips": ["0.0.0.0/0"],
          "reserved": [1, 2, 3]
        }
      ]
    })JSON";

    auto result = subcli::parseSubscription(content, "fixture", makeConfig());
    require(result.nodes.size() == 3, "expected three parsed extended sing-box nodes");
    require(result.nodes[0].type == "hysteria2", "should parse hysteria2 type");
    require(result.nodes[0].protocol.values.at("obfs_type") == "salamander", "should parse hysteria2 obfs type");
    require(result.nodes[1].type == "tuic", "should parse tuic type");
    require(result.nodes[1].protocol.values.at("congestion_control") == "bbr", "should parse tuic congestion control");
    require(result.nodes[2].type == "wireguard", "should parse wireguard type");
    require(result.nodes[2].protocol.values.at("peer_public_key") == "peer-public-key", "should parse wireguard peer key");
}

void testExportFilteringByTarget() {
    auto config = makeConfig();
    subcli::ProxyNode hy2;
    hy2.type = "hysteria2";
    hy2.name = "HK Hy2";
    hy2.server = "hy.example.com";
    hy2.port = 443;
    hy2.protocol.password = "hy-pass";
    hy2.protocol.values["up_mbps"] = "100";
    hy2.protocol.values["down_mbps"] = "200";
    hy2.tlsConfig.enabled = true;
    hy2.tlsConfig.sni = "google.com";
    hy2.region = "HK";
    hy2.syncLegacyFromStructured();

    const fs::path outDir = fs::temp_directory_path() / "subcli-tests";
    fs::create_directories(outDir);
    std::string error;
    auto sing = subcli::exportForTarget(subcli::ExportTarget::SingBox, {hy2}, config, false, (outDir / "sing-box-hy2.json").string(), error);
    require(sing.ok, "sing-box should export hysteria2");
    std::ifstream singIn(outDir / "sing-box-hy2.json");
    std::string singContent((std::istreambuf_iterator<char>(singIn)), std::istreambuf_iterator<char>());
    require(singContent.find("\"type\": \"hysteria2\"") != std::string::npos, "sing-box export should keep hysteria2 outbound");

    auto xray = subcli::exportForTarget(subcli::ExportTarget::Xray, {hy2}, config, false, (outDir / "xray-hy2.json").string(), error);
    require(!xray.ok, "xray export should fail when every node is unsupported");
    require(xray.skipped == 1, "xray export should skip unsupported hysteria2 node");
    require(error.find("no supported nodes") != std::string::npos, "xray error should explain that no supported nodes remain");
}

void testExportXrayUsesStableRandomBalancer() {
    auto config = makeConfig();
    subcli::ProxyNode node;
    node.type = "vless";
    node.name = "JP Xray";
    node.server = "example.com";
    node.port = 443;
    node.protocol.uuid = "44444444-4444-4444-4444-444444444444";
    node.region = "JP";
    node.normalize();

    const fs::path outDir = fs::temp_directory_path() / "subcli-tests";
    fs::create_directories(outDir);
    std::string error;
    auto xray = subcli::exportForTarget(subcli::ExportTarget::Xray, {node}, config, false, (outDir / "xray-random.json").string(), error);
    require(xray.ok, "xray export should succeed: " + error);

    std::ifstream in(outDir / "xray-random.json");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    require(content.find("\"balancerTag\": \"PROXY\"") != std::string::npos, "xray export should route catch-all via PROXY balancer");
    require(content.find("\"type\": \"leastPing\"") != std::string::npos, "xray export should default to leastPing balancer strategy");
    require(content.find("\"fallbackTag\": \"DIRECT\"") != std::string::npos, "xray export should have fallbackTag DIRECT");
    require(content.find("\"observatory\"") != std::string::npos, "xray export should include observatory config");
}

void testExportXraySupportsLeastLoadStrategyFromTemplate() {
    auto config = makeConfig();
    const fs::path outDir = fs::temp_directory_path() / "subcli-tests";
    fs::create_directories(outDir);
    const fs::path customTpl = outDir / "xray-custom-template.json";
    {
        std::ofstream tpl(customTpl);
        tpl << R"JSON({
  "inbounds": [],
  "outbounds": [
    {"tag": "DIRECT", "protocol": "freedom", "settings": {}},
    {"tag": "REJECT", "protocol": "blackhole", "settings": {}}
  ],
  "routing": {
    "rules": [],
    "balancers": [
      {"tag": "PROXY", "selector": ["foo"], "strategy": {"type": "leastLoad"}}
    ]
  }
})JSON";
    }

    config.templateNormal["xray"] = customTpl.string();

    subcli::ProxyNode node;
    node.type = "vless";
    node.name = "JP Xray";
    node.server = "example.com";
    node.port = 443;
    node.protocol.uuid = "55555555-5555-5555-5555-555555555555";
    node.region = "JP";
    node.normalize();

    std::string error;
    auto xray = subcli::exportForTarget(subcli::ExportTarget::Xray, {node}, config, false, (outDir / "xray-leastload.json").string(), error);
    require(xray.ok, "xray leastLoad export should succeed: " + error);

    std::ifstream in(outDir / "xray-leastload.json");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    require(content.find("\"type\": \"leastLoad\"") != std::string::npos, "xray export should preserve leastLoad strategy");
    require(content.find("\"burstObservatory\"") != std::string::npos, "xray leastLoad should include burstObservatory");
}

void testExportSingBoxDnsRuleUsesRouteActionWithPort53() {
    auto config = makeConfig();
    subcli::ProxyNode node;
    node.type = "ss";
    node.name = "HK Node";
    node.server = "1.2.3.4";
    node.port = 443;
    node.protocol.password = "password";
    node.protocol.cipher = "chacha20-ietf-poly1305";
    node.region = "HK";
    node.normalize();

    const fs::path outDir = fs::temp_directory_path() / "subcli-tests";
    fs::create_directories(outDir);
    std::string error;
    auto result = subcli::exportForTarget(subcli::ExportTarget::SingBox, {node}, config, false, (outDir / "sing-box-dns-rule.json").string(), error);
    require(result.ok, "sing-box export should succeed: " + error);

    std::ifstream in(outDir / "sing-box-dns-rule.json");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    require(content.find("\"action\": \"route\"") != std::string::npos, "sing-box route rule should use route action");
    require(content.find("\"port\": 53") != std::string::npos, "sing-box dns direct rule should match port 53");
}

subcli::ProxyNode makeExportReadyShadowsocksNode(const std::string& name) {
    subcli::ProxyNode node;
    node.type = "ss";
    node.name = name;
    node.server = "1.2.3.4";
    node.port = 443;
    node.protocol.password = "password";
    node.protocol.cipher = "chacha20-ietf-poly1305";
    node.region = "HK";
    node.normalize();
    return node;
}

void testMihomoExportIncludesBypassCnProfileRules() {
    auto config = makeConfig();
    const fs::path outDir = fs::temp_directory_path() / "subcli-tests";
    fs::create_directories(outDir);

    std::string error;
    auto result = subcli::exportForTarget(
        subcli::ExportTarget::Mihomo,
        {makeExportReadyShadowsocksNode("HK Node")},
        config,
        false,
        (outDir / "mihomo-bypass-cn.yaml").string(),
        error
    );
    require(result.ok, "mihomo bypass-cn export should succeed: " + error);

    std::ifstream in(outDir / "mihomo-bypass-cn.yaml");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    require(content.find("GEOSITE,cn,DIRECT") != std::string::npos, "mihomo should bypass mainland domains");
    require(content.find("GEOIP,CN,DIRECT") != std::string::npos, "mihomo should bypass mainland IPs");
    require(content.find("MATCH,PROXY") != std::string::npos, "mihomo should proxy unmatched traffic");
}

void testSingBoxExportIncludesBypassCnProfileRules() {
    auto config = makeConfig();
    const fs::path outDir = fs::temp_directory_path() / "subcli-tests";
    fs::create_directories(outDir);

    std::string error;
    auto result = subcli::exportForTarget(
        subcli::ExportTarget::SingBox,
        {makeExportReadyShadowsocksNode("HK Node")},
        config,
        false,
        (outDir / "sing-box-bypass-cn.json").string(),
        error
    );
    require(result.ok, "sing-box bypass-cn export should succeed: " + error);

    std::ifstream in(outDir / "sing-box-bypass-cn.json");
    const auto json = nlohmann::json::parse(in);
    require(json["route"].value("final", "") == "PROXY", "sing-box should proxy unmatched traffic");
    require(json["route"].contains("rule_set"), "sing-box route should declare rule_set assets");

    bool hasGeositeCnRule = false;
    bool hasGeoipCnRule = false;
    for (const auto& rule : json["route"]["rules"]) {
        if (rule.value("outbound", "") != "DIRECT") {
            continue;
        }
        if (rule.contains("rule_set") && rule["rule_set"].is_array()) {
            for (const auto& name : rule["rule_set"]) {
                hasGeositeCnRule = hasGeositeCnRule || name.get<std::string>() == "geosite-cn";
                hasGeoipCnRule = hasGeoipCnRule || name.get<std::string>() == "geoip-cn";
            }
        }
    }
    require(hasGeositeCnRule, "sing-box should bypass mainland domain rule set");
    require(hasGeoipCnRule, "sing-box should bypass mainland IP rule set");
}

void testXrayExportIncludesBypassCnProfileRules() {
    auto config = makeConfig();
    const fs::path outDir = fs::temp_directory_path() / "subcli-tests";
    fs::create_directories(outDir);

    std::string error;
    auto result = subcli::exportForTarget(
        subcli::ExportTarget::Xray,
        {makeExportReadyShadowsocksNode("HK Node")},
        config,
        false,
        (outDir / "xray-bypass-cn.json").string(),
        error
    );
    require(result.ok, "xray bypass-cn export should succeed: " + error);

    std::ifstream in(outDir / "xray-bypass-cn.json");
    const auto json = nlohmann::json::parse(in);
    bool hasDirectCnRule = false;
    bool hasProxyFallback = false;
    for (const auto& rule : json["routing"]["rules"]) {
        if (rule.value("outboundTag", "") == "DIRECT") {
            const auto domainText = rule.contains("domain") ? rule["domain"].dump() : "";
            const auto ipText = rule.contains("ip") ? rule["ip"].dump() : "";
            hasDirectCnRule = hasDirectCnRule || domainText.find("geosite:cn") != std::string::npos ||
                              ipText.find("geoip:cn") != std::string::npos;
        }
        hasProxyFallback = hasProxyFallback || rule.value("balancerTag", "") == "PROXY";
    }
    require(hasDirectCnRule, "xray should bypass mainland geosite/geoip rules");
    require(hasProxyFallback, "xray should proxy unmatched traffic through PROXY balancer");
}

void testMihomoExportSupportsGlobalAndDirectProfiles() {
    auto config = makeConfig();
    const fs::path outDir = fs::temp_directory_path() / "subcli-tests-profile-mihomo";
    fs::create_directories(outDir);

    config.profile = "global";
    std::string error;
    auto global = subcli::exportForTarget(
        subcli::ExportTarget::Mihomo,
        {makeExportReadyShadowsocksNode("HK Node")},
        config,
        false,
        (outDir / "mihomo-global.yaml").string(),
        error
    );
    require(global.ok, "mihomo global export should succeed: " + error);
    {
        std::ifstream in(outDir / "mihomo-global.yaml");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        require(content.find("MATCH,PROXY") != std::string::npos, "mihomo global profile should proxy unmatched traffic");
        require(content.find("GEOSITE,cn,DIRECT") == std::string::npos, "mihomo global profile should not inject bypass-cn rules");
    }

    config.profile = "direct";
    auto direct = subcli::exportForTarget(
        subcli::ExportTarget::Mihomo,
        {makeExportReadyShadowsocksNode("HK Node")},
        config,
        false,
        (outDir / "mihomo-direct.yaml").string(),
        error
    );
    require(direct.ok, "mihomo direct export should succeed: " + error);
    {
        std::ifstream in(outDir / "mihomo-direct.yaml");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        require(content.find("MATCH,DIRECT") != std::string::npos, "mihomo direct profile should direct unmatched traffic");
        require(content.find("MATCH,PROXY") == std::string::npos, "mihomo direct profile should not proxy unmatched traffic");
    }
}

void testSingBoxAndXrayExportSupportDirectProfile() {
    auto config = makeConfig();
    config.profile = "direct";
    const fs::path outDir = fs::temp_directory_path() / "subcli-tests-profile-json";
    fs::create_directories(outDir);
    std::string error;

    auto sing = subcli::exportForTarget(
        subcli::ExportTarget::SingBox,
        {makeExportReadyShadowsocksNode("HK Node")},
        config,
        false,
        (outDir / "sing-direct.json").string(),
        error
    );
    require(sing.ok, "sing-box direct export should succeed: " + error);
    {
        std::ifstream in(outDir / "sing-direct.json");
        const auto json = nlohmann::json::parse(in);
        require(json["route"].value("final", "") == "DIRECT", "sing-box direct profile should direct unmatched traffic");
    }

    auto xray = subcli::exportForTarget(
        subcli::ExportTarget::Xray,
        {makeExportReadyShadowsocksNode("HK Node")},
        config,
        false,
        (outDir / "xray-direct.json").string(),
        error
    );
    require(xray.ok, "xray direct export should succeed: " + error);
    {
        std::ifstream in(outDir / "xray-direct.json");
        const auto json = nlohmann::json::parse(in);
        bool hasFinalDirect = false;
        for (const auto& rule : json["routing"]["rules"]) {
            hasFinalDirect = hasFinalDirect || (rule.value("outboundTag", "") == "DIRECT" && rule.value("network", "") == "tcp,udp");
        }
        require(hasFinalDirect, "xray direct profile should direct unmatched traffic");
    }
}

void testStorePersistsOverrideFlags() {
    const fs::path path = fs::temp_directory_path() / "subcli-tests-subs.yaml";

    subcli::Subscription sub;
    sub.id = "a";
    sub.name = "A";
    sub.url = "file:///tmp/a";
    sub.timeout = 9;
    sub.timeoutOverride = true;
    sub.retry = 1;
    sub.retryOverride = true;

    subcli::saveSubscriptions(path.string(), {sub});
    auto loaded = subcli::loadSubscriptions(path.string());
    require(loaded.size() == 1, "expected one loaded subscription");
    require(loaded[0].timeout == 9, "timeout should persist");
    require(loaded[0].retry == 1, "retry should persist");
    require(loaded[0].timeoutOverride, "timeout_override should persist");
    require(loaded[0].retryOverride, "retry_override should persist");

    fs::remove(path);
}

void testStorePersistsFetchMaxBytes() {
    const fs::path path = fs::temp_directory_path() / "subcli-tests-fetch-config.yaml";
    subcli::AppConfig config;
    config.fetchMaxBytes = 4096;

    subcli::saveConfig(path.string(), config);
    auto loaded = subcli::loadConfig(path.string());
    require(loaded.fetchMaxBytes == 4096, "fetch_max_bytes should persist");

    fs::remove(path);
}

void testStorePersistsProfileAndAssets() {
    const fs::path path = fs::temp_directory_path() / "subcli-tests-profile-config.yaml";
    subcli::AppConfig config;
    config.profile = "bypass-cn";
    config.assetDir = "/tmp/subcli-assets";
    config.assetPaths["sing-box.geosite-cn"] = "/tmp/subcli-assets/sing-box/geosite-cn.srs";
    config.assetUrls["sing-box.geosite-cn"] = "file:///tmp/geosite-cn.srs";

    subcli::saveConfig(path.string(), config);
    auto loaded = subcli::loadConfig(path.string());
    require(loaded.profile == "bypass-cn", "profile should persist");
    require(loaded.assetDir == "/tmp/subcli-assets", "asset_dir should persist");
    require(loaded.assetPaths["sing-box.geosite-cn"] == "/tmp/subcli-assets/sing-box/geosite-cn.srs", "asset path should persist");
    require(loaded.assetUrls["sing-box.geosite-cn"] == "file:///tmp/geosite-cn.srs", "asset URL should persist");

    fs::remove(path);
}

void testStorePersistsProfilePath() {
    const fs::path path = fs::temp_directory_path() / "subcli-tests-profile-path-config.yaml";
    subcli::AppConfig config;
    config.profile = "global";
    config.profilePath = "/tmp/subcli-profiles/work.json";

    subcli::saveConfig(path.string(), config);
    auto loaded = subcli::loadConfig(path.string());
    require(loaded.profile == "global", "built-in profile should remain supported");
    require(loaded.profilePath == "/tmp/subcli-profiles/work.json", "profile_path should persist");

    fs::remove(path);
}

void testStoreDefaultsMissingProfilePathToEmpty() {
    const fs::path path = fs::temp_directory_path() / "subcli-tests-missing-profile-path-config.yaml";
    {
        std::ofstream out(path);
        out << "version: 1\n";
        out << "profile: global\n";
    }

    auto loaded = subcli::loadConfig(path.string());
    require(loaded.profile == "global", "profile should load when profile_path is absent");
    require(loaded.profilePath.empty(), "missing profile_path should default to empty");

    fs::remove(path);
}

void testStorePersistsRoutingRules() {
    const fs::path path = fs::temp_directory_path() / "subcli-tests-routing-config.yaml";
    subcli::AppConfig config;
    config.routingRules.push_back({"geosite", "cn", "PROXY"});
    config.routingRules.push_back({"geoip", "cn", "PROXY"});
    config.routingRules.push_back({"final", "", "DIRECT"});

    subcli::saveConfig(path.string(), config);
    auto loaded = subcli::loadConfig(path.string());
    require(loaded.routingRules.size() == 3, "routing rules should persist");
    require(loaded.routingRules[0].type == "geosite", "routing rule type should persist");
    require(loaded.routingRules[0].value == "cn", "routing rule value should persist");
    require(loaded.routingRules[0].outbound == "PROXY", "routing rule outbound should persist");

    fs::remove(path);
}

void testStorePersistsStrategyGroups() {
    const fs::path path = fs::temp_directory_path() / "subcli-tests-groups-config.yaml";
    subcli::AppConfig config;
    subcli::AppConfig::StrategyGroup proxy;
    proxy.name = "PROXY";
    proxy.type = "select";
    proxy.members = {"AUTO", "DIRECT", "HK"};
    proxy.defaultMember = "AUTO";
    config.strategyGroups.push_back(proxy);

    subcli::AppConfig::StrategyGroup autoGroup;
    autoGroup.name = "AUTO";
    autoGroup.type = "url-test";
    autoGroup.url = "http://www.gstatic.com/generate_204";
    autoGroup.interval = 300;
    autoGroup.members = {"HK"};
    config.strategyGroups.push_back(autoGroup);

    subcli::saveConfig(path.string(), config);
    auto loaded = subcli::loadConfig(path.string());
    require(loaded.strategyGroups.size() == 2, "strategy groups should persist");
    require(loaded.strategyGroups[0].name == "PROXY", "strategy group name should persist");
    require(loaded.strategyGroups[0].members.size() == 3, "strategy group members should persist");
    require(loaded.strategyGroups[1].type == "url-test", "strategy group type should persist");

    fs::remove(path);
}

void testCustomStrategyGroupsRenderForMihomoAndSingBox() {
    auto config = makeConfig();
    config.strategyGroups.clear();

    subcli::AppConfig::StrategyGroup proxy;
    proxy.name = "PROXY";
    proxy.type = "select";
    proxy.members = {"AUTO", "DIRECT", "HK"};
    proxy.defaultMember = "AUTO";
    config.strategyGroups.push_back(proxy);

    subcli::AppConfig::StrategyGroup autoGroup;
    autoGroup.name = "AUTO";
    autoGroup.type = "url-test";
    autoGroup.url = "http://www.gstatic.com/generate_204";
    autoGroup.interval = 300;
    autoGroup.members = {"HK"};
    config.strategyGroups.push_back(autoGroup);

    subcli::AppConfig::StrategyGroup hk;
    hk.name = "HK";
    hk.type = "select";
    hk.members = {"DIRECT"};
    hk.defaultMember = "DIRECT";
    config.strategyGroups.push_back(hk);

    const fs::path outDir = fs::temp_directory_path() / "subcli-tests-custom-groups";
    fs::create_directories(outDir);
    const auto node = makeExportReadyShadowsocksNode("HK Node");
    std::string error;

    auto mihomo = subcli::exportForTarget(subcli::ExportTarget::Mihomo, {node}, config, false, (outDir / "mihomo-groups.yaml").string(), error);
    require(mihomo.ok, "mihomo custom groups export should succeed: " + error);
    {
        std::ifstream in(outDir / "mihomo-groups.yaml");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        require(content.find("name: PROXY") != std::string::npos, "mihomo should include PROXY group");
        require(content.find("type: url-test") != std::string::npos, "mihomo should include url-test group");
    }

    auto sing = subcli::exportForTarget(subcli::ExportTarget::SingBox, {node}, config, false, (outDir / "sing-groups.json").string(), error);
    require(sing.ok, "sing-box custom groups export should succeed: " + error);
    {
        std::ifstream in(outDir / "sing-groups.json");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        require(content.find("\"tag\": \"PROXY\"") != std::string::npos, "sing-box should include PROXY group");
        require(content.find("\"type\": \"urltest\"") != std::string::npos, "sing-box should include urltest group");
    }
}

void testFallbackAndLoadBalanceGroupsRenderForMihomoAndSingBox() {
    auto config = makeConfig();
    config.strategyGroups.clear();

    subcli::AppConfig::StrategyGroup fallback;
    fallback.name = "FAILOVER";
    fallback.type = "fallback";
    fallback.members = {"DIRECT", "HK"};
    fallback.url = "http://www.gstatic.com/generate_204";
    fallback.interval = 300;
    config.strategyGroups.push_back(fallback);

    subcli::AppConfig::StrategyGroup lb;
    lb.name = "BALANCE";
    lb.type = "load-balance";
    lb.members = {"HK", "DIRECT"};
    lb.defaultMember = "HK";
    config.strategyGroups.push_back(lb);

    const fs::path outDir = fs::temp_directory_path() / "subcli-tests-extended-groups";
    fs::create_directories(outDir);
    const auto node = makeExportReadyShadowsocksNode("HK Node");
    std::string error;

    auto mihomo = subcli::exportForTarget(subcli::ExportTarget::Mihomo, {node}, config, false, (outDir / "mihomo-extended-groups.yaml").string(), error);
    require(mihomo.ok, "mihomo extended groups export should succeed: " + error);
    {
        std::ifstream in(outDir / "mihomo-extended-groups.yaml");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        require(content.find("name: FAILOVER") != std::string::npos, "mihomo should include FAILOVER group");
        require(content.find("type: fallback") != std::string::npos, "mihomo should render fallback group type");
        require(content.find("name: BALANCE") != std::string::npos, "mihomo should include BALANCE group");
        require(content.find("type: load-balance") != std::string::npos, "mihomo should render load-balance group type");
    }

    auto sing = subcli::exportForTarget(subcli::ExportTarget::SingBox, {node}, config, false, (outDir / "sing-extended-groups.json").string(), error);
    require(sing.ok, "sing-box extended groups export should succeed: " + error);
    {
        std::ifstream in(outDir / "sing-extended-groups.json");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        require(content.find("\"tag\": \"FAILOVER\"") != std::string::npos, "sing-box should include FAILOVER group");
        require(content.find("\"type\": \"urltest\"") != std::string::npos, "sing-box should degrade fallback to urltest");
        require(content.find("\"tag\": \"BALANCE\"") != std::string::npos, "sing-box should include BALANCE group");
        require(content.find("\"type\": \"selector\"") != std::string::npos, "sing-box should degrade load-balance to selector");
    }
}

void testMihomoProfileGroupsRenderAndExpandMembers() {
    auto config = makeConfig();
    config.strategyGroups.clear();

    subcli::ResolvedProfile profile;
    profile.name = "custom";

    subcli::ProfileGroup proxy;
    proxy.tag = "PROXY";
    proxy.type = "select";
    proxy.members = {"AUTO", "DIRECT", "REGION:*"};
    profile.groups.push_back(proxy);

    subcli::ProfileGroup autoGroup;
    autoGroup.tag = "AUTO";
    autoGroup.type = "url-test";
    autoGroup.members = {"NODE:*"};
    autoGroup.url = "https://example.com/generate_204";
    autoGroup.interval = 180;
    profile.groups.push_back(autoGroup);

    subcli::ProfileGroup fallback;
    fallback.tag = "FAILOVER";
    fallback.type = "fallback";
    fallback.members = {"REGION:HK", "REGION:JP"};
    fallback.url = "https://example.com/health";
    fallback.interval = 90;
    profile.groups.push_back(fallback);

    subcli::ProfileGroup balance;
    balance.tag = "BALANCE";
    balance.type = "load-balance";
    balance.members = {"REGION:*"};
    balance.strategy = "consistent-hashing";
    profile.groups.push_back(balance);

    subcli::ProfileGroup empty;
    empty.tag = "EMPTY";
    empty.type = "select";
    profile.groups.push_back(empty);

    auto hk = makeExportReadyShadowsocksNode("HK Node");
    auto jp = makeExportReadyShadowsocksNode("JP Node");
    jp.region = "JP";
    jp.normalize();

    const fs::path outDir = fs::temp_directory_path() / "subcli-tests-profile-groups-mihomo";
    fs::create_directories(outDir);
    std::string error;
    auto result = subcli::exportForTarget(
        subcli::ExportTarget::Mihomo,
        {hk, jp},
        config,
        false,
        &profile,
        (outDir / "mihomo-profile-groups.yaml").string(),
        error
    );
    require(result.ok, "mihomo profile groups export should succeed: " + error);

    std::ifstream in(outDir / "mihomo-profile-groups.yaml");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto yaml = YAML::Load(content);

    auto findGroup = [&](const std::string& name) -> YAML::Node {
        YAML::Node found;
        if (!yaml["proxy-groups"] || !yaml["proxy-groups"].IsSequence()) {
            return found;
        }
        for (const auto& group : yaml["proxy-groups"]) {
            if (group["name"].as<std::string>("") == name) {
                return YAML::Node(group);
            }
        }
        return found;
    };

    const auto balanceGroup = findGroup("BALANCE");

    require(content.find("name: PROXY") != std::string::npos, "mihomo profile groups should include PROXY");
    require(content.find("type: select") != std::string::npos, "mihomo profile select group should render as select");
    require(content.find("name: AUTO") != std::string::npos, "mihomo profile groups should include AUTO");
    require(content.find("type: url-test") != std::string::npos, "mihomo profile url-test group should render");
    require(content.find("https://example.com/generate_204") != std::string::npos, "mihomo profile url-test should keep custom URL");
    require(content.find("interval: 180") != std::string::npos, "mihomo profile url-test should keep custom interval");
    require(content.find("name: FAILOVER") != std::string::npos, "mihomo profile groups should include FAILOVER");
    require(content.find("type: fallback") != std::string::npos, "mihomo profile fallback group should render");
    require(content.find("https://example.com/health") != std::string::npos, "mihomo profile fallback should keep custom URL");
    require(content.find("interval: 90") != std::string::npos, "mihomo profile fallback should keep custom interval");
    require(content.find("name: BALANCE") != std::string::npos, "mihomo profile groups should include BALANCE");
    require(content.find("type: load-balance") != std::string::npos, "mihomo profile load-balance group should render");
    require(content.find("strategy: consistent-hashing") != std::string::npos, "mihomo profile load-balance should keep strategy");
    require(content.find("name: EMPTY") != std::string::npos, "mihomo profile groups should include EMPTY");
    require(content.find("- DIRECT") != std::string::npos, "mihomo profile empty expanded members should fall back to DIRECT");
    require(content.find("REGION:*") == std::string::npos, "mihomo profile groups should expand REGION:* tokens");
    require(balanceGroup && balanceGroup["proxies"] && balanceGroup["proxies"].IsSequence(), "mihomo BALANCE group should include proxies list");
    require(balanceGroup["proxies"].size() == 3, "mihomo BALANCE REGION:* should expand to all generated regions");
    require(balanceGroup["proxies"][0].as<std::string>("") == "HK", "mihomo BALANCE REGION:* should include HK region tag first");
    require(balanceGroup["proxies"][1].as<std::string>("") == "JP", "mihomo BALANCE REGION:* should include JP region tag second");
    require(balanceGroup["proxies"][2].as<std::string>("") == "OTHER", "mihomo BALANCE REGION:* should include OTHER region tag third");
}

void testMihomoWithEmptyProfileGroupsKeepsLegacyGroupBehavior() {
    auto config = makeConfig();
    config.strategyGroups.clear();

    subcli::AppConfig::StrategyGroup fallback;
    fallback.name = "FAILOVER";
    fallback.type = "fallback";
    fallback.members = {"DIRECT", "HK"};
    config.strategyGroups.push_back(fallback);

    subcli::ResolvedProfile profile;
    profile.name = "empty-groups";

    const fs::path outDir = fs::temp_directory_path() / "subcli-tests-empty-profile-groups-mihomo";
    fs::create_directories(outDir);
    std::string error;
    auto result = subcli::exportForTarget(
        subcli::ExportTarget::Mihomo,
        {makeExportReadyShadowsocksNode("HK Node")},
        config,
        false,
        &profile,
        (outDir / "mihomo-empty-profile-groups.yaml").string(),
        error
    );
    require(result.ok, "mihomo export with empty profile groups should succeed: " + error);

    std::ifstream in(outDir / "mihomo-empty-profile-groups.yaml");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    require(content.find("name: FAILOVER") != std::string::npos, "mihomo should keep legacy strategy groups when profile groups are empty");
    require(content.find("type: fallback") != std::string::npos, "mihomo should keep legacy fallback type when profile groups are empty");
}

void testMihomoProfileGroupsSynthesizeMissingProxyAndAuto() {
    auto config = makeConfig();
    config.profile = "global";
    config.strategyGroups.clear();

    subcli::ResolvedProfile profile;
    profile.name = "missing-proxy-auto";
    subcli::ProfileGroup failover;
    failover.tag = "FAILOVER";
    failover.type = "fallback";
    failover.members = {"REGION:*"};
    profile.groups.push_back(failover);

    const fs::path outDir = fs::temp_directory_path() / "subcli-tests-profile-groups-fallbacks-mihomo";
    fs::create_directories(outDir);
    std::string error;
    auto result = subcli::exportForTarget(
        subcli::ExportTarget::Mihomo,
        {makeExportReadyShadowsocksNode("HK Node")},
        config,
        false,
        &profile,
        (outDir / "mihomo-profile-missing-groups.yaml").string(),
        error
    );
    require(result.ok, "mihomo export should synthesize missing PROXY/AUTO groups: " + error);

    std::ifstream in(outDir / "mihomo-profile-missing-groups.yaml");
    const auto yaml = YAML::Load(in);

    YAML::Node proxyGroup;
    YAML::Node autoGroup;
    for (const auto& group : yaml["proxy-groups"]) {
        const auto name = group["name"].as<std::string>("");
        if (name == "PROXY") {
            proxyGroup = YAML::Node(group);
        }
        if (name == "AUTO") {
            autoGroup = YAML::Node(group);
        }
    }

    require(proxyGroup && proxyGroup["proxies"] && proxyGroup["proxies"].IsSequence(), "mihomo should synthesize PROXY group proxies list");
    require(proxyGroup["type"].as<std::string>("") == "select", "synthesized PROXY group should be select type");
    require(proxyGroup["proxies"][0].as<std::string>("") == "AUTO", "synthesized PROXY group should reference AUTO first");
    require(proxyGroup["proxies"][1].as<std::string>("") == "DIRECT", "synthesized PROXY group should reference DIRECT second");

    require(autoGroup && autoGroup["proxies"] && autoGroup["proxies"].IsSequence(), "mihomo should synthesize AUTO group proxies list");
    require(autoGroup["type"].as<std::string>("") == "url-test", "synthesized AUTO group should be url-test type");
    require(autoGroup["proxies"].size() > 0, "synthesized AUTO group should include usable members");
}

void testMihomoProfileDnsAndRulesRenderFromProfile() {
    auto config = makeConfig();
    config.profile = "bypass-cn";

    subcli::ResolvedProfile profile;
    profile.name = "custom-routing";
    profile.defaultOutbound = "PROXY";
    profile.dns.mode = "fake-ip";
    profile.dns.directServers = {"223.5.5.5", "119.29.29.29"};
    profile.dns.remoteServers = {"1.1.1.1", "8.8.8.8"};
    profile.rules.push_back({"geosite", "private", "DIRECT"});
    profile.rules.push_back({"geoip", "private", "DIRECT"});
    profile.rules.push_back({"domain", "example.com", "DIRECT"});
    profile.rules.push_back({"domain_suffix", "example.org", "PROXY"});
    profile.rules.push_back({"domain_keyword", "video", "PROXY"});
    profile.rules.push_back({"ip_cidr", "10.0.0.0/8", "DIRECT"});
    subcli::ProfileRule portRule;
    portRule.type = "port";
    portRule.outbound = "DIRECT";
    portRule.ports = {"53", "123"};
    profile.rules.push_back(portRule);
    subcli::ProfileRule networkRule;
    networkRule.type = "network";
    networkRule.outbound = "PROXY";
    networkRule.networks = {"tcp", "udp"};
    profile.rules.push_back(networkRule);
    profile.rules.push_back({"final", "", "DIRECT"});

    const fs::path outDir = fs::temp_directory_path() / "subcli-tests-profile-routing-mihomo";
    fs::create_directories(outDir);
    std::string error;
    auto result = subcli::exportForTarget(
        subcli::ExportTarget::Mihomo,
        {makeExportReadyShadowsocksNode("HK Node")},
        config,
        false,
        &profile,
        (outDir / "mihomo-profile-routing.yaml").string(),
        error
    );
    require(result.ok, "mihomo profile routing export should succeed: " + error);

    std::ifstream in(outDir / "mihomo-profile-routing.yaml");
    const auto yaml = YAML::Load(in);
    require(yaml["dns"] && yaml["dns"].IsMap(), "mihomo profile should render dns map");
    require(yaml["dns"]["enable"].as<bool>(false), "mihomo profile dns should be enabled");
    require(yaml["dns"]["enhanced-mode"].as<std::string>("") == "fake-ip", "mihomo profile dns should use profile mode");
    require(yaml["dns"]["nameserver"] && yaml["dns"]["nameserver"].IsSequence(), "mihomo profile dns should render direct nameservers");
    require(yaml["dns"]["nameserver"][0].as<std::string>("") == "223.5.5.5", "mihomo profile dns should keep direct server order");
    require(yaml["dns"]["fallback"] && yaml["dns"]["fallback"].IsSequence(), "mihomo profile dns should render remote fallback servers");
    require(yaml["dns"]["fallback"][1].as<std::string>("") == "8.8.8.8", "mihomo profile dns should keep remote server order");

    std::vector<std::string> rules;
    for (const auto& rule : yaml["rules"]) {
        rules.push_back(rule.as<std::string>(""));
    }
    const std::vector<std::string> expected = {
        "GEOSITE,private,DIRECT",
        "GEOIP,private,DIRECT",
        "DOMAIN,example.com,DIRECT",
        "DOMAIN-SUFFIX,example.org,PROXY",
        "DOMAIN-KEYWORD,video,PROXY",
        "IP-CIDR,10.0.0.0/8,DIRECT",
        "DST-PORT,53,DIRECT",
        "DST-PORT,123,DIRECT",
        "NETWORK,tcp,PROXY",
        "NETWORK,udp,PROXY",
        "MATCH,DIRECT",
    };
    require(rules.size() == expected.size(), "mihomo profile rules should replace template and legacy rules");
    for (size_t i = 0; i < expected.size(); ++i) {
        require(rules[i] == expected[i], "mihomo profile rule order and mapping should match profile");
    }
}

void testCustomRoutingRulesMapToAllTargets() {
    auto config = makeConfig();
    config.profile = "custom";
    config.routingRules = {
        {"geosite", "cn", "PROXY"},
        {"geoip", "cn", "PROXY"},
        {"final", "", "DIRECT"},
    };

    const fs::path outDir = fs::temp_directory_path() / "subcli-tests-routing";
    fs::create_directories(outDir);
    const auto node = makeExportReadyShadowsocksNode("HK Node");
    std::string error;

    auto mihomo = subcli::exportForTarget(subcli::ExportTarget::Mihomo, {node}, config, false, (outDir / "mihomo-routing.yaml").string(), error);
    require(mihomo.ok, "mihomo custom routing export should succeed: " + error);
    {
        std::ifstream in(outDir / "mihomo-routing.yaml");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        require(content.find("GEOSITE,cn,PROXY") != std::string::npos, "mihomo should map custom geosite rule");
        require(content.find("GEOIP,cn,PROXY") != std::string::npos, "mihomo should map custom geoip rule");
        require(content.find("MATCH,DIRECT") != std::string::npos, "mihomo should map custom final rule");
    }

    auto sing = subcli::exportForTarget(subcli::ExportTarget::SingBox, {node}, config, false, (outDir / "sing-routing.json").string(), error);
    require(sing.ok, "sing-box custom routing export should succeed: " + error);
    {
        std::ifstream in(outDir / "sing-routing.json");
        const auto json = nlohmann::json::parse(in);
        require(json["route"].value("final", "") == "DIRECT", "sing-box should map custom final rule");
        bool hasGeositeProxy = false;
        bool hasGeoipProxy = false;
        for (const auto& rule : json["route"]["rules"]) {
            if (rule.value("outbound", "") != "PROXY" || !rule.contains("rule_set")) {
                continue;
            }
            for (const auto& name : rule["rule_set"]) {
                hasGeositeProxy = hasGeositeProxy || name.get<std::string>() == "geosite-cn";
                hasGeoipProxy = hasGeoipProxy || name.get<std::string>() == "geoip-cn";
            }
        }
        require(hasGeositeProxy, "sing-box should map geosite cn to PROXY");
        require(hasGeoipProxy, "sing-box should map geoip cn to PROXY");
    }

    auto xray = subcli::exportForTarget(subcli::ExportTarget::Xray, {node}, config, false, (outDir / "xray-routing.json").string(), error);
    require(xray.ok, "xray custom routing export should succeed: " + error);
    {
        std::ifstream in(outDir / "xray-routing.json");
        const auto json = nlohmann::json::parse(in);
        bool hasGeositeProxy = false;
        bool hasGeoipProxy = false;
        bool hasFinalDirect = false;
        for (const auto& rule : json["routing"]["rules"]) {
            const auto domainText = rule.contains("domain") ? rule["domain"].dump() : "";
            const auto ipText = rule.contains("ip") ? rule["ip"].dump() : "";
            hasGeositeProxy = hasGeositeProxy || (rule.value("outboundTag", "") == "PROXY" && domainText.find("geosite:cn") != std::string::npos);
            hasGeoipProxy = hasGeoipProxy || (rule.value("outboundTag", "") == "PROXY" && ipText.find("geoip:cn") != std::string::npos);
            hasFinalDirect = hasFinalDirect || (rule.value("outboundTag", "") == "DIRECT" && rule.value("network", "") == "tcp,udp");
        }
        require(hasGeositeProxy, "xray should map geosite cn to PROXY");
        require(hasGeoipProxy, "xray should map geoip cn to PROXY");
        require(hasFinalDirect, "xray should map final rule to DIRECT");
    }
}

void testMatchRoutingRuleMapsToAllTargets() {
    auto config = makeConfig();
    config.profile = "custom";
    config.routingRules = {
        {"match", "", "DIRECT"},
    };

    const fs::path outDir = fs::temp_directory_path() / "subcli-tests-match-routing";
    fs::create_directories(outDir);
    const auto node = makeExportReadyShadowsocksNode("HK Node");
    std::string error;

    auto mihomo = subcli::exportForTarget(subcli::ExportTarget::Mihomo, {node}, config, false, (outDir / "mihomo-match.yaml").string(), error);
    require(mihomo.ok, "mihomo match routing export should succeed: " + error);
    {
        std::ifstream in(outDir / "mihomo-match.yaml");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        require(content.find("MATCH,DIRECT") != std::string::npos, "mihomo should map match rule to DIRECT");
    }

    auto sing = subcli::exportForTarget(subcli::ExportTarget::SingBox, {node}, config, false, (outDir / "sing-match.json").string(), error);
    require(sing.ok, "sing-box match routing export should succeed: " + error);
    {
        std::ifstream in(outDir / "sing-match.json");
        const auto json = nlohmann::json::parse(in);
        require(json["route"].value("final", "") == "DIRECT", "sing-box should map match rule to DIRECT final");
    }

    auto xray = subcli::exportForTarget(subcli::ExportTarget::Xray, {node}, config, false, (outDir / "xray-match.json").string(), error);
    require(xray.ok, "xray match routing export should succeed: " + error);
    {
        std::ifstream in(outDir / "xray-match.json");
        const auto json = nlohmann::json::parse(in);
        bool hasFinalDirect = false;
        for (const auto& rule : json["routing"]["rules"]) {
            hasFinalDirect = hasFinalDirect || (rule.value("outboundTag", "") == "DIRECT" && rule.value("network", "") == "tcp,udp");
        }
        require(hasFinalDirect, "xray should map match rule to DIRECT final");
    }
}

void testPrivateRoutingRulesMapToAllTargets() {
    auto config = makeConfig();
    config.profile = "custom";
    config.routingRules = {
        {"geosite", "private", "DIRECT"},
        {"geoip", "private", "DIRECT"},
        {"final", "", "PROXY"},
    };

    const fs::path outDir = fs::temp_directory_path() / "subcli-tests-private-routing";
    fs::create_directories(outDir);
    const auto node = makeExportReadyShadowsocksNode("HK Node");
    std::string error;

    auto mihomo = subcli::exportForTarget(subcli::ExportTarget::Mihomo, {node}, config, false, (outDir / "mihomo-private.yaml").string(), error);
    require(mihomo.ok, "mihomo private routing export should succeed: " + error);
    {
        std::ifstream in(outDir / "mihomo-private.yaml");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        require(content.find("GEOSITE,private,DIRECT") != std::string::npos, "mihomo should map geosite private");
        require(content.find("GEOIP,private,DIRECT") != std::string::npos, "mihomo should map geoip private");
    }

    auto sing = subcli::exportForTarget(subcli::ExportTarget::SingBox, {node}, config, false, (outDir / "sing-private.json").string(), error);
    require(sing.ok, "sing-box private routing export should succeed: " + error);
    {
        std::ifstream in(outDir / "sing-private.json");
        const auto json = nlohmann::json::parse(in);
        bool hasPrivateDirect = false;
        for (const auto& rule : json["route"]["rules"]) {
            const auto domainText = rule.contains("domain") ? rule["domain"].dump() : "";
            const auto ipText = rule.contains("ip_cidr") ? rule["ip_cidr"].dump() : "";
            hasPrivateDirect = hasPrivateDirect || (rule.value("outbound", "") == "DIRECT" && (domainText.find("private") != std::string::npos || ipText.find("private") != std::string::npos));
        }
        require(hasPrivateDirect, "sing-box should map private direct rule");
    }

    auto xray = subcli::exportForTarget(subcli::ExportTarget::Xray, {node}, config, false, (outDir / "xray-private.json").string(), error);
    require(xray.ok, "xray private routing export should succeed: " + error);
    {
        std::ifstream in(outDir / "xray-private.json");
        const auto json = nlohmann::json::parse(in);
        bool hasPrivateDirect = false;
        for (const auto& rule : json["routing"]["rules"]) {
            const auto domainText = rule.contains("domain") ? rule["domain"].dump() : "";
            const auto ipText = rule.contains("ip") ? rule["ip"].dump() : "";
            hasPrivateDirect = hasPrivateDirect || (rule.value("outboundTag", "") == "DIRECT" && (domainText.find("geosite:private") != std::string::npos || ipText.find("geoip:private") != std::string::npos));
        }
        require(hasPrivateDirect, "xray should map private direct rule");
    }
}

void testAssetRecordsExposeConfiguredFiles() {
    subcli::AppConfig config;
    config.assetPaths["xray.geoip"] = "/tmp/subcli-assets/xray/geoip.dat";
    config.assetUrls["xray.geoip"] = "file:///tmp/geoip.dat";

    const auto records = subcli::configuredAssets(config);
    bool found = false;
    for (const auto& record : records) {
        if (record.key == "xray.geoip") {
            found = record.path == "/tmp/subcli-assets/xray/geoip.dat" && record.url == "file:///tmp/geoip.dat";
        }
    }
    require(found, "configuredAssets should expose configured xray geoip asset");
}

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

void testUpdateAssetPersistsMetadata() {
    const fs::path dir = fs::temp_directory_path() / "subcli-asset-update-tests";
    fs::remove_all(dir);
    fs::create_directories(dir);

    const fs::path source = dir / "source.dat";
    {
        std::ofstream out(source);
        out << "new-asset";
    }
    const fs::path target = dir / "geoip.dat";

    subcli::AssetRecord asset;
    asset.key = "xray.geoip";
    asset.path = target.string();
    asset.url = "file://" + source.string();

    std::string error;
    require(subcli::updateAsset(asset, 5, 1024 * 1024, error), "updateAsset should succeed: " + error);
    require(subcli::readFile(target.string()) == "new-asset", "updateAsset should write downloaded content");

    subcli::AppConfig config;
    config.assetPaths[asset.key] = asset.path;
    config.assetUrls[asset.key] = asset.url;
    auto records = subcli::configuredAssets(config);
    require(records.size() == 1, "configuredAssets should include updated asset");
    require(records[0].sizeBytes == 9, "configuredAssets should expose stored asset size");
    require(records[0].sourceUrl == asset.url, "configuredAssets should expose metadata source url");
    require(!records[0].updatedAt.empty(), "configuredAssets should expose metadata updated time");

    fs::remove_all(dir);
}

void testUpdateAssetFailureKeepsPreviousFile() {
    const fs::path dir = fs::temp_directory_path() / "subcli-asset-failure-tests";
    fs::remove_all(dir);
    fs::create_directories(dir);

    const fs::path target = dir / "geoip.dat";
    {
        std::ofstream out(target);
        out << "old-asset";
    }

    subcli::AssetRecord asset;
    asset.key = "xray.geoip";
    asset.path = target.string();
    asset.url = "file://" + (dir / "missing.dat").string();

    std::string error;
    require(!subcli::updateAsset(asset, 5, 1024 * 1024, error), "updateAsset should fail for missing source");
    require(subcli::readFile(target.string()) == "old-asset", "failed update should keep previous asset content");

    fs::remove_all(dir);
}

void testSystemdUserServiceExampleExists() {
    const fs::path servicePath = fs::path(SUBCLI_SOURCE_DIR) / "packaging/systemd/subcli-daemon.service";
    require(fs::exists(servicePath), "systemd user service example should exist");

    std::ifstream in(servicePath);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    require(content.find("[Unit]") != std::string::npos, "systemd service should include Unit section");
    require(content.find("ExecStart=") != std::string::npos, "systemd service should include ExecStart");
    require(content.find("daemon run") != std::string::npos, "systemd service should launch daemon run mode");
}

void testReadmeDeclaresConfigGenerationAsPrimaryGoal() {
    const fs::path path = fs::path(SUBCLI_SOURCE_DIR) / "README.subcli.md";
    require(fs::exists(path), "README.subcli.md should exist");

    std::ifstream in(path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    require(content.find("primary workflow is subscription + asset management + native config export") != std::string::npos,
            "README should declare config generation as the primary workflow");
    require(content.find("runtime and daemon commands are optional") != std::string::npos,
            "README should declare runtime and daemon as optional capabilities");
    require(content.find("profile_path") != std::string::npos,
            "README should document profile_path as the external profile selector");
    require(content.find("routing/strategy") != std::string::npos && content.find("profile files") != std::string::npos,
            "README should direct advanced routing and strategy policy to profile files");
}

void testFetchRejectsUnsupportedScheme() {
    subcli::Subscription sub;
    sub.id = "bad";
    sub.name = "bad";
    sub.url = "ftp://example.com/sub";

    auto result = subcli::fetchSubscription(sub, false);
    require(!result.ok, "unsupported scheme fetch should fail");
    require(result.error.find("unsupported subscription url scheme") != std::string::npos, "unsupported scheme error should be clear");
}

void testFetchFileHonorsMaxBytes() {
    const fs::path path = fs::temp_directory_path() / "subcli-tests-large-sub.txt";
    {
        std::ofstream out(path);
        out << "1234567890";
    }

    subcli::Subscription sub;
    sub.id = "large";
    sub.name = "large";
    sub.url = "file://" + path.string();
    sub.fetchMaxBytes = 5;

    auto result = subcli::fetchSubscription(sub, false);
    require(!result.ok, "oversized file fetch should fail");
    require(result.error.find("fetch_max_bytes") != std::string::npos, "oversized fetch error should mention fetch_max_bytes");

    fs::remove(path);
}

void testFetchFileUrlDecodesPercentEscapes() {
    const fs::path dir = fs::temp_directory_path() / "subcli-file-url-space-tests";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path path = dir / "with space.txt";
    {
        std::ofstream out(path);
        out << "ok";
    }

    subcli::Subscription sub;
    sub.id = "space";
    sub.name = "space";
    sub.url = "file://" + dir.string() + "/with%20space.txt";

    auto result = subcli::fetchSubscription(sub, false);
    require(result.ok, "percent-encoded file url should succeed");
    require(result.content == "ok", "percent-encoded file url should read content");

    fs::remove_all(dir);
}

void testDecodeFileUrlPathPreservesWindowsStyleDrivePath() {
    const std::string decoded = subcli::decodeFileUrlPath("/C:/Users/test/with%20space.txt");
    require(decoded == "/C:/Users/test/with space.txt", "decodeFileUrlPath should preserve Windows drive-style path text");
}

void testDecodeFileUrlPathPreservesUncStylePath() {
    const std::string decoded = subcli::decodeFileUrlPath("//server/share/with%20space.txt");
    require(decoded == "//server/share/with space.txt", "decodeFileUrlPath should preserve UNC-style path text");
}

void testResolveAgainstConfigDirUsesConfigBase() {
    const fs::path base = fs::temp_directory_path() / "subcli-config-base-tests";
    const auto resolved = subcli::resolveAgainstBaseForTest(base.string(), "assets/geoip.dat");
    require(resolved == (base / "assets/geoip.dat").lexically_normal().string(), "config-relative path should resolve against config dir base");
}

void testResolveAgainstCliCwdUsesCwdBase() {
    const fs::path base = fs::temp_directory_path() / "subcli-cwd-base-tests";
    const auto resolved = subcli::resolveAgainstBaseForTest(base.string(), "outputs/mihomo.yaml");
    require(resolved == (base / "outputs/mihomo.yaml").lexically_normal().string(), "cli relative path should resolve against cwd base");
}

void testResolveAgainstBaseKeepsAbsolutePath() {
    const fs::path base = fs::temp_directory_path() / "subcli-absolute-base-tests";
    const fs::path absolute = fs::temp_directory_path() / "already/absolute.txt";
    const auto resolved = subcli::resolveAgainstBaseForTest(base.string(), absolute.string());
    require(resolved == absolute.lexically_normal().string(), "absolute path should remain absolute");
}

void testStructuredFieldsSurviveExportNormalization() {
    auto config = makeConfig();
    subcli::ProxyNode node;
    node.type = "vless";
    node.name = "JP Structured";
    node.server = "example.com";
    node.port = 443;
    node.transport.network = "grpc";
    node.transport.serviceName = "svc";
    node.transport.authority = "auth.example.com";
    node.tlsConfig.enabled = true;
    node.tlsConfig.sni = "www.google.com";
    node.tlsConfig.alpn = {"h2", "http/1.1"};
    node.protocol.uuid = "33333333-3333-3333-3333-333333333333";
    node.protocol.flow = "xtls-rprx-vision";
    node.region = "JP";

    const fs::path outDir = fs::temp_directory_path() / "subcli-tests";
    fs::create_directories(outDir);
    std::string error;
    auto result = subcli::exportForTarget(subcli::ExportTarget::SingBox, {node}, config, false, (outDir / "sing-box-structured.json").string(), error);
    require(result.ok, "structured sing-box export should succeed: " + error);
    std::ifstream in(outDir / "sing-box-structured.json");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    require(content.find("\"service_name\": \"svc\"") != std::string::npos, "grpc service name should survive export normalization");
    require(content.find("\"authority\": \"auth.example.com\"") != std::string::npos, "grpc authority should survive export normalization");
    require(content.find("\"h2\"") != std::string::npos && content.find("\"http/1.1\"") != std::string::npos, "multi ALPN should survive export normalization");
}

void testNodeManagementPreprocess() {
    auto config = makeConfig();
    config.renameTemplate = "[{region}] {name}";
    config.excludeRegex = "Drop";
    config.sortBy = "name";
    config.dedupeNodes = true;

    subcli::ProxyNode keepA;
    keepA.type = "ss";
    keepA.name = "NodeA";
    keepA.server = "1.1.1.1";
    keepA.port = 1000;
    keepA.protocol.password = "p";
    keepA.protocol.cipher = "aes-128-gcm";
    keepA.region = "HK";
    keepA.normalize();

    subcli::ProxyNode dup = keepA;
    dup.name = "NodeA-dup";
    dup.normalize();

    subcli::ProxyNode filtered = keepA;
    filtered.name = "DropMe";
    filtered.server = "2.2.2.2";
    filtered.normalize();

    subcli::ProxyNode keepB = keepA;
    keepB.name = "NodeB";
    keepB.server = "3.3.3.3";
    keepB.region = "JP";
    keepB.normalize();

    std::vector<subcli::DiagnosticMessage> warnings;
    auto processed = subcli::preprocessNodes({keepB, dup, filtered, keepA}, config, warnings);
    require(processed.size() == 2, "preprocess should keep two nodes after filter and dedupe");
    require(processed[0].name == "[HK] NodeA-dup", "preprocess should keep first duplicate candidate after rename");
    require(processed[1].name == "[JP] NodeB", "preprocess should rename and sort second node");
    require(warnings.size() == 2, "preprocess should emit filter and dedupe warnings");
}

void testExpandProfileMembersExpandsRegionWildcardInOrder() {
    subcli::GroupData groups;
    groups.regionOrder = {"HK", "JP", "OTHER"};

    const std::vector<subcli::ProxyNode> exportNodes;
    const auto expanded = subcli::expandProfileMembers({"REGION:*"}, groups, exportNodes);

    require(expanded.size() == 3, "REGION:* should expand every generated region");
    require(expanded[0] == "HK", "REGION:* should keep generated region order");
    require(expanded[1] == "JP", "REGION:* should keep generated region order");
    require(expanded[2] == "OTHER", "REGION:* should keep generated region order");
}

void testExpandProfileMembersExpandsSingleRegionOnlyWhenPresent() {
    subcli::GroupData groups;
    groups.regionOrder = {"HK", "JP", "OTHER"};

    const std::vector<subcli::ProxyNode> exportNodes;
    const auto expanded = subcli::expandProfileMembers({"REGION:HK", "REGION:US"}, groups, exportNodes);

    require(expanded.size() == 2, "REGION:<name> should keep unmatched tokens as literals");
    require(expanded[0] == "HK", "REGION:<name> should include the matched region");
    require(expanded[1] == "REGION:US", "REGION:<name> should preserve unmatched literal tokens");
}

void testExpandProfileMembersExpandsNodeWildcard() {
    subcli::GroupData groups;
    groups.regionOrder = {"HK", "OTHER"};

    subcli::ProxyNode a;
    a.name = "HK Node";
    subcli::ProxyNode b;
    b.name = "JP Node";
    const std::vector<subcli::ProxyNode> exportNodes = {a, b};

    const auto expanded = subcli::expandProfileMembers({"NODE:*"}, groups, exportNodes);
    require(expanded.size() == 2, "NODE:* should expand all export node names");
    require(expanded[0] == "HK Node", "NODE:* should preserve export node order");
    require(expanded[1] == "JP Node", "NODE:* should preserve export node order");
}

void testExpandProfileMembersKeepsLiteralMembersAndDedupes() {
    subcli::GroupData groups;
    groups.regionOrder = {"HK", "JP", "OTHER"};

    subcli::ProxyNode a;
    a.name = "HK Node";
    const std::vector<subcli::ProxyNode> exportNodes = {a};

    const auto expanded = subcli::expandProfileMembers(
        {"DIRECT", "PROXY", "REGION:HK", "NODE:*", "REGION:*", "DIRECT", "HK", "HK Node", "CUSTOM"},
        groups,
        exportNodes
    );

    require(expanded.size() == 7, "expanded members should dedupe while preserving first-seen order");
    require(expanded[0] == "DIRECT", "literal DIRECT should be preserved");
    require(expanded[1] == "PROXY", "literal PROXY should be preserved");
    require(expanded[2] == "HK", "expanded region should be deduped with later duplicates");
    require(expanded[3] == "HK Node", "expanded node should be deduped with later duplicates");
    require(expanded[4] == "JP", "REGION:* should add missing regions in order");
    require(expanded[5] == "OTHER", "REGION:* should include OTHER when generated");
    require(expanded[6] == "CUSTOM", "custom literal should be preserved");
}

void testInvalidRegexIsIgnored() {
    auto config = makeConfig();
    config.includeRegex = "(";
    config.excludeRegex = "(";

    subcli::ProxyNode node;
    node.type = "ss";
    node.name = "HK Node";
    node.server = "1.2.3.4";
    node.port = 443;
    node.protocol.password = "password";
    node.protocol.cipher = "chacha20-ietf-poly1305";
    node.region = "HK";
    node.normalize();

    std::vector<subcli::DiagnosticMessage> warnings;
    auto processed = subcli::preprocessNodes({node}, config, warnings);
    require(processed.size() == 1, "invalid regex should be ignored and keep nodes");

    bool includeWarned = false;
    bool excludeWarned = false;
    for (const auto& warning : warnings) {
        if (warning.code == "invalid_include_regex") {
            includeWarned = true;
        }
        if (warning.code == "invalid_exclude_regex") {
            excludeWarned = true;
        }
    }
    require(includeWarned, "invalid include_regex should emit warning");
    require(excludeWarned, "invalid exclude_regex should emit warning");
}

void testWriteFileCreatesParentDirectories() {
    const fs::path base = fs::temp_directory_path() / "subcli-tests-write";
    const fs::path path = base / "nested/deeper/file.txt";
    fs::remove_all(base);

    std::string error;
    require(subcli::writeFile(path.string(), "ok", error), "writeFile should create parent directories: " + error);
    require(fs::exists(path), "writeFile should create target file");

    fs::remove_all(base);
}

void testLoadConfigMalformedYamlThrows() {
    const fs::path path = fs::temp_directory_path() / "subcli-tests-config-bad.yaml";
    {
        std::ofstream out(path);
        out << "tun: [";
    }

    bool thrown = false;
    try {
        (void)subcli::loadConfig(path.string());
    } catch (const std::runtime_error&) {
        thrown = true;
    }
    require(thrown, "malformed config yaml should throw runtime_error");

    fs::remove(path);
}

void testCoreRuntimeLifecycle() {
    const fs::path stateDir = fs::temp_directory_path() / "subcli-tests-runtime";
    fs::remove_all(stateDir);
    fs::create_directories(stateDir);

    std::string error;
    const bool started = subcli::startCoreRuntime(
        stateDir,
        "sing-box",
        "/bin/sleep",
        {"30"},
        "/tmp/subcli-tests-runtime-config.json",
        error
    );
    require(started, "startCoreRuntime should start process: " + error);

    auto status = subcli::inspectCoreRuntime(stateDir, "sing-box", error);
    require(error.empty(), "inspectCoreRuntime should not fail for running process: " + error);
    require(status.hasState, "inspectCoreRuntime should find saved runtime state");
    require(status.running, "inspectCoreRuntime should report running process");
    require(status.pid > 0, "inspectCoreRuntime should report pid");

    const bool secondStart = subcli::startCoreRuntime(
        stateDir,
        "sing-box",
        "/bin/sleep",
        {"30"},
        "/tmp/subcli-tests-runtime-config.json",
        error
    );
    require(!secondStart, "startCoreRuntime should reject duplicate start");

    const bool stopped = subcli::stopCoreRuntime(stateDir, "sing-box", 1, error);
    require(stopped, "stopCoreRuntime should stop running process: " + error);

    status = subcli::inspectCoreRuntime(stateDir, "sing-box", error);
    require(error.empty(), "inspectCoreRuntime should not fail after stop: " + error);
    require(!status.hasState, "inspectCoreRuntime should remove state after stop");
    require(!status.running, "inspectCoreRuntime should report stopped process");

    fs::remove_all(stateDir);
}

} // namespace

int main() {
    testLoadProfileReadsGroupsRulesAndDns();
    testLoadProfileRejectsInvalidJson();
    testLoadProfileRejectsRuleWithoutOutbound();
    testLoadProfileAcceptsRuleWithoutValueOrLists();
    testLoadProfileAcceptsFinalRuleWithoutOutbound();
    testBuiltInProfilesExistAndLoad();
    testExportProfileResolverLoadsBuiltInProfile();
    testExportProfileResolverFailsForMissingProfilePath();
    testExportProfileResolverFailsForInvalidProfileJson();
    testBuiltInAutoGroupsDoNotReferenceProxy();
    testProtocolRegistryCoversOfficialTargets();
    testCliOutputStatusJson();
    testCliOutputDiagnosticsJson();
    testBashCompletionContainsCommands();
    testDaemonBuildsExpectedArgs();
    testDaemonProcessLifecycle();
    testDaemonProcessLifecycleWithCustomFiles();
    testStartDaemonProcessFailsWhenExecCannotStart();
    testRunDaemonCycleWithStateUpdatesSuccessSummary();
    testInspectDaemonProcessExposesLastCycleFailure();
    testDaemonCycleRestartsOnlyRunningCores();
    testDaemonCycleStopsOnUpdateFailure();
    testCapabilityWarningsAreSpecific();
    testLegacyBridgeBuildsStructuredCoreOptions();
    testStructuredWritersPreserveVlessEncryption();
    testStructuredWritersNormalizeShadowsocksNames();
    testWireGuardWritersUseTargetSchemas();
    testExportSingBoxWireGuardUsesEndpoint();
    testModernUdpWritersUseTargetSchemas();
    testSingBoxModernUdpWritersUseStructuredFields();
    testModernUdpXraySkipsUnsupportedProtocols();
    testXrayBuiltInOutboundsParseAndExport();
    testNativeLongTailPassthroughWriters();
    testParseXrayJson();
    testParseUriIgnoresInvalidVmessPort();
    testParseModernUdpUriLinks();
    testUnknownFormatHintFallsBackToAuto();
    testHintParseFailedFallsBackToAuto();
    testParseMihomoHttpOpts();
    testParseMihomoH2Opts();
    testParseMihomoLocalProxyProvider();
    testParseMihomoRemoteProxyProvider();
    testParseMihomoRemoteProxyProviderWithHttpFields();
    testParseMihomoHighFrequencyOptionalFields();
    testParseSingBoxTlsDisabledObject();
    testParseSingBoxHighFrequencyOptionalFields();
    testParseXrayHighFrequencyOptionalFields();
    testParseWireGuardNativeShapes();
    testParseModernUdpNativeShapes();
    testExportSingBox();
    testParseSingBoxExtendedProtocols();
    testExportFilteringByTarget();
    testExportXrayUsesStableRandomBalancer();
    testExportXraySupportsLeastLoadStrategyFromTemplate();
    testExportSingBoxDnsRuleUsesRouteActionWithPort53();
    testMihomoExportIncludesBypassCnProfileRules();
    testMihomoExportSupportsGlobalAndDirectProfiles();
    testSingBoxExportIncludesBypassCnProfileRules();
    testSingBoxAndXrayExportSupportDirectProfile();
    testXrayExportIncludesBypassCnProfileRules();
    testStructuredFieldsSurviveExportNormalization();
    testNodeManagementPreprocess();
    testExpandProfileMembersExpandsRegionWildcardInOrder();
    testExpandProfileMembersExpandsSingleRegionOnlyWhenPresent();
    testExpandProfileMembersExpandsNodeWildcard();
    testExpandProfileMembersKeepsLiteralMembersAndDedupes();
    testInvalidRegexIsIgnored();
    testWriteFileCreatesParentDirectories();
    testLoadConfigMalformedYamlThrows();
    testCoreRuntimeLifecycle();
    testStorePersistsOverrideFlags();
    testStorePersistsFetchMaxBytes();
    testStorePersistsProfileAndAssets();
    testStorePersistsProfilePath();
    testStoreDefaultsMissingProfilePathToEmpty();
    testStorePersistsRoutingRules();
    testStorePersistsStrategyGroups();
    testCustomRoutingRulesMapToAllTargets();
    testMatchRoutingRuleMapsToAllTargets();
    testPrivateRoutingRulesMapToAllTargets();
    testCustomStrategyGroupsRenderForMihomoAndSingBox();
    testFallbackAndLoadBalanceGroupsRenderForMihomoAndSingBox();
    testMihomoProfileGroupsRenderAndExpandMembers();
    testMihomoWithEmptyProfileGroupsKeepsLegacyGroupBehavior();
    testMihomoProfileGroupsSynthesizeMissingProxyAndAuto();
    testMihomoProfileDnsAndRulesRenderFromProfile();
    testAssetRecordsExposeConfiguredFiles();
    testMissingAssetsReturnsOnlyMissingRecords();
    testUpdateAssetPersistsMetadata();
    testUpdateAssetFailureKeepsPreviousFile();
    testSystemdUserServiceExampleExists();
    testReadmeDeclaresConfigGenerationAsPrimaryGoal();
    testFetchRejectsUnsupportedScheme();
    testFetchFileHonorsMaxBytes();
    testFetchFileUrlDecodesPercentEscapes();
    testDecodeFileUrlPathPreservesWindowsStyleDrivePath();
    testDecodeFileUrlPathPreservesUncStylePath();
    testResolveAgainstConfigDirUsesConfigBase();
    testResolveAgainstCliCwdUsesCwdBase();
    testResolveAgainstBaseKeepsAbsolutePath();
    return 0;
}
