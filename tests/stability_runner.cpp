#include "stability_http_server.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "subcli/platform.hpp"

namespace fs = std::filesystem;

namespace {

struct Options {
    std::string mode;
    fs::path subcliBin;
    fs::path sourceDir;
    fs::path testRoot;
    fs::path configPath;
};

void fail(const std::string& message) {
    throw std::runtime_error(message);
}

std::string optionValue(int argc, char* argv[], const std::string& key) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] == key) {
            return argv[i + 1];
        }
    }
    return "";
}

Options parseOptions(int argc, char* argv[]) {
    Options options;
    options.mode = optionValue(argc, argv, "--mode");
    options.subcliBin = optionValue(argc, argv, "--subcli-bin");
    options.sourceDir = optionValue(argc, argv, "--source-dir");
    options.testRoot = optionValue(argc, argv, "--test-root");
    if (options.mode.empty() || options.subcliBin.empty() || options.sourceDir.empty() || options.testRoot.empty()) {
        fail("usage: subcli_stability_runner --mode user|package --subcli-bin PATH --source-dir DIR --test-root DIR");
    }
    options.configPath = options.testRoot / "config.yaml";
    return options;
}

void ensureConfig(const Options& options) {
    const auto configPath = options.configPath;
    if (fs::exists(configPath)) {
        return;
    }
    fs::create_directories(configPath.parent_path());
    std::ofstream out(configPath);
    if (!out) {
        fail("failed to create config: " + configPath.string());
    }
    out << "version: 1\n"
        << "data_dir: " << (options.testRoot / "data").string() << "\n"
        << "cache_dir: " << (options.testRoot / "cache").string() << "\n"
        << "asset_dir: " << (options.testRoot / "data/assets").string() << "\n"
        << "template_dir: " << (options.sourceDir / "templates").string() << "\n"
        << "profile_dir: " << (options.sourceDir / "profiles").string() << "\n"
        << "output_dir: " << (options.testRoot / "outputs").string() << "\n"
        << "state_dir: " << (options.testRoot / "data/state").string() << "\n"
        << "log_dir: " << (options.testRoot / "logs").string() << "\n"
        << "sub_file: " << (options.testRoot / "data/sub.yaml").string() << "\n"
        << "profile: bypass-cn\n"
        << "tun: false\n"
        << "log_level: info\n"
        << "parallelism: 4\n"
        << "timeout: 15\n"
        << "retry: 2\n"
        << "fetch_max_bytes: 10485760\n"
        << "templates:\n"
        << "  mihomo:\n"
        << "    normal: " << (options.sourceDir / "templates/mihomo_base.yaml").string() << "\n"
        << "    tun: " << (options.sourceDir / "templates/mihomo_tun.yaml").string() << "\n"
        << "  sing-box:\n"
        << "    normal: " << (options.sourceDir / "templates/singbox_base.json").string() << "\n"
        << "    tun: " << (options.sourceDir / "templates/singbox_tun.json").string() << "\n"
        << "  xray:\n"
        << "    normal: " << (options.sourceDir / "templates/xray_base.json").string() << "\n"
        << "    tun: " << (options.sourceDir / "templates/xray_tun.json").string() << "\n"
        << "grouping:\n"
        << "  region_rules:\n"
        << "    HK: \"(?i)(hong kong|hongkong|hk|香港)\"\n"
        << "    JP: \"(?i)(japan|jp|tokyo|osaka|日本)\"\n"
        << "node_management:\n"
        << "  dedupe: true\n"
        << "  rename_template: \"{name}\"\n"
        << "  sort_by: region,name\n";
    out.close();
}

std::vector<std::string> withConfig(const Options& options, const std::vector<std::string>& args) {
    std::vector<std::string> out;
    out.push_back("--config");
    out.push_back(options.configPath.string());
    out.insert(out.end(), args.begin(), args.end());
    return out;
}

subcli::ProcessRunResult runSubcli(const Options& options, const std::vector<std::string>& args, int timeoutSec = 20) {
    auto result = subcli::runProcessCapture(options.subcliBin.string(), args, timeoutSec);
    if (!result.started) {
        fail("failed to start subcli: " + result.error);
    }
    return result;
}

std::string runOk(const Options& options, const std::string& label, const std::vector<std::string>& args, int timeoutSec = 20) {
    const auto result = runSubcli(options, args, timeoutSec);
    if (result.exitCode != 0 || result.timedOut) {
        fail(label + " failed\noutput:\n" + result.output + "\nerror:\n" + result.error);
    }
    return result.output;
}

std::string runFail(const Options& options, const std::string& label, const std::vector<std::string>& args, int timeoutSec = 20) {
    const auto result = runSubcli(options, args, timeoutSec);
    if (result.exitCode == 0 && !result.timedOut) {
        fail(label + " unexpectedly succeeded\noutput:\n" + result.output);
    }
    return result.output + result.error;
}

void requireContains(const std::string& haystack, const std::string& needle, const std::string& label) {
    if (haystack.find(needle) == std::string::npos) {
        fail(label + " missing expected text: " + needle + "\nactual:\n" + haystack);
    }
}

std::string normalizedPathForCompare(const fs::path& path) {
    std::error_code ec;
    fs::path normalized = fs::weakly_canonical(path, ec);
    if (ec) {
        ec.clear();
        normalized = fs::absolute(path, ec);
        if (ec) {
            normalized = path;
        }
    }
    std::string value = normalized.lexically_normal().generic_string();
#ifdef _WIN32
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
#endif
    return value;
}

void requireWorkspaceStatusPath(const std::string& statusJson, const std::string& key, const fs::path& expectedPath, const std::string& label) {
    const auto parsed = nlohmann::json::parse(statusJson, nullptr, false);
    if (parsed.is_discarded() || !parsed.contains(key) || !parsed[key].is_string()) {
        fail(label + " missing JSON string key: " + key + "\nactual:\n" + statusJson);
    }
    const std::string actual = normalizedPathForCompare(fs::path(parsed[key].get<std::string>()));
    const std::string expected = normalizedPathForCompare(expectedPath);
    if (actual != expected) {
        fail(label + " path mismatch for " + key + "\nexpected: " + expected + "\nactual: " + actual + "\njson:\n" + statusJson);
    }
}

void requireAnyFileNonEmpty(const fs::path& dir, const std::string& extension, const std::string& label) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        fail(label + " output directory does not exist: " + dir.string());
    }
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!ec && entry.is_regular_file(ec) && entry.path().extension() == extension && fs::file_size(entry.path(), ec) > 0) {
            return;
        }
    }
    fail(label + " expected a non-empty " + extension + " file under " + dir.string());
}

void runBoundaryChecks(const Options& options, subcli::StabilityHttpServer& server) {
    runOk(options, "sub add bad500", withConfig(options, {"sub", "add", "--name", "bad500", "--url", server.url("/sub/500"), "--force"}));
    const std::string bad500 = runFail(options, "sub update bad500", withConfig(options, {"sub", "update", "bad500", "--strict-network"}), 20);
    requireContains(bad500, "500", "HTTP 500 failure");

    runOk(options, "sub add empty", withConfig(options, {"sub", "add", "--name", "empty", "--url", server.url("/sub/empty"), "--force"}));
    (void)runFail(options, "sub update empty", withConfig(options, {"sub", "update", "empty", "--strict-network"}), 20);

    runOk(options, "sub add malformed", withConfig(options, {"sub", "add", "--name", "malformed", "--url", server.url("/sub/malformed"), "--force"}));
    (void)runSubcli(options, withConfig(options, {"sub", "update", "malformed"}), 20);

    runOk(options, "sub add unicode", withConfig(options, {"sub", "add", "--name", "unicode", "--url", server.url("/sub/unicode"), "--force"}));
    runOk(options, "sub update unicode", withConfig(options, {"sub", "update", "unicode", "--strict-network"}), 20);

    runOk(options, "sub add slow", withConfig(options, {"sub", "add", "--name", "slow", "--url", server.url("/sub/slow"), "--timeout", "1", "--force"}));
    (void)runFail(options, "sub update slow", withConfig(options, {"sub", "update", "slow", "--strict-network"}), 10);
}

void runJourney(const Options& options) {
    fs::remove_all(options.testRoot);
    fs::create_directories(options.testRoot);
    const fs::path outputDir = options.testRoot / "outputs";
    fs::create_directories(outputDir);

    ensureConfig(options);

    // Ensure appDir-relative paths that gPaths expect exist
    const auto appDir = options.subcliBin.parent_path();
    for (const auto& subdir : {"templates", "profiles"}) {
        const fs::path link = appDir / subdir;
        if (!fs::exists(link)) {
            std::error_code ec;
            fs::create_directory_symlink(options.sourceDir / subdir, link, ec);
            if (ec) {
                fs::copy(options.sourceDir / subdir, link, fs::copy_options::recursive, ec);
                if (ec) {
                    fail("failed to create " + std::string(subdir) + ": " + ec.message());
                }
            }
        }
    }
    // Clean global data between runs (sub.yaml from gPaths.subPath)
    std::error_code ec;
    fs::remove_all(appDir / "data", ec);

    const std::string help = runOk(options, "root help", {"--help"});
    requireContains(help, "First use:", "root help");
    requireContains(help, "does not replace proxy", "root help");

    const std::string subHelp = runOk(options, "sub help", withConfig(options, {"sub", "--help"}));
    requireContains(subHelp, "Subscriptions are URLs", "sub help");

    const std::string exportHelp = runOk(options, "export help", withConfig(options, {"export", "--help"}));
    requireContains(exportHelp, "Generate native client config files", "export help");

    runOk(options, "doctor", withConfig(options, {"doctor", "--json"}));
    runOk(options, "config list", withConfig(options, {"config", "list"}));
    runOk(options, "template list", withConfig(options, {"template", "list"}));
    runOk(options, "profile list", withConfig(options, {"profile", "list"}));
    runOk(options, "profile validate", withConfig(options, {"profile", "validate", (options.sourceDir / "profiles" / "bypass-cn.json").string()}));

    subcli::StabilityHttpServer server(options.sourceDir / "tests/stability_fixtures/subscriptions");
    server.start();
    runOk(options, "sub add", withConfig(options, {"sub", "add", "--name", "local-http", "--url", server.url("/sub/plain"), "--force"}));
    runOk(options, "sub update", withConfig(options, {"sub", "update", "local-http", "--strict-network"}));
    runOk(options, "sub list", withConfig(options, {"sub", "list"}));
    runOk(options, "export mihomo", withConfig(options, {"export", "mihomo", "--output-dir", outputDir.string(), "--strict-network"}));
    requireAnyFileNonEmpty(outputDir, ".yaml", "mihomo export");

    runBoundaryChecks(options, server);
    server.stop();
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const Options options = parseOptions(argc, argv);
        runJourney(options);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "stability runner failed: " << ex.what() << "\n";
        return 1;
    }
}
