#include "stability_http_server.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "subcli/platform.hpp"

namespace fs = std::filesystem;

namespace {

struct Options {
    std::string mode;
    fs::path subcliBin;
    fs::path sourceDir;
    fs::path testRoot;
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
    return options;
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
    runOk(options, "sub add bad500", {"sub", "add", "--name", "bad500", "--url", server.url("/sub/500"), "--force"});
    const std::string bad500 = runFail(options, "sub update bad500", {"sub", "update", "bad500", "--strict-network"}, 20);
    requireContains(bad500, "500", "HTTP 500 failure");

    runOk(options, "sub add empty", {"sub", "add", "--name", "empty", "--url", server.url("/sub/empty"), "--force"});
    (void)runFail(options, "sub update empty", {"sub", "update", "empty", "--strict-network"}, 20);

    runOk(options, "sub add malformed", {"sub", "add", "--name", "malformed", "--url", server.url("/sub/malformed"), "--force"});
    (void)runSubcli(options, {"sub", "update", "malformed"}, 20);

    runOk(options, "sub add unicode", {"sub", "add", "--name", "unicode", "--url", server.url("/sub/unicode"), "--force"});
    runOk(options, "sub update unicode", {"sub", "update", "unicode", "--strict-network"}, 20);

    runOk(options, "sub add slow", {"sub", "add", "--name", "slow", "--url", server.url("/sub/slow"), "--timeout", "1", "--force"});
    (void)runFail(options, "sub update slow", {"sub", "update", "slow", "--strict-network"}, 10);
}

void runJourney(const Options& options) {
    fs::remove_all(options.testRoot);
    fs::create_directories(options.testRoot);
    const fs::path workspace = options.testRoot / "subcli 稳定性 workspace with space";
    const fs::path outputDir = options.testRoot / "outputs";
    fs::create_directories(outputDir);

    const std::string help = runOk(options, "root help", {"--help"});
    requireContains(help, "First use:", "root help");
    requireContains(help, "subcli init", "root help");
    requireContains(help, "does not replace proxy", "root help");

    const std::string initHelp = runOk(options, "init help", {"init", "--help"});
    requireContains(initHelp, "remember", "init help");

    const std::string workspaceHelp = runOk(options, "workspace help", {"workspace", "--help"});
    requireContains(workspaceHelp, "workspace init initializes", "workspace help");

    const std::string subHelp = runOk(options, "sub help", {"sub", "--help"});
    requireContains(subHelp, "Subscriptions are URLs", "sub help");

    const std::string exportHelp = runOk(options, "export help", {"export", "--help"});
    requireContains(exportHelp, "Generate native client config files", "export help");

    runOk(options, "init", {"init", workspace.string()});
    const std::string status = runOk(options, "workspace status", {"workspace", "status", "--json"});
    requireContains(status, workspace.string(), "workspace status");

    const fs::path overrideWorkspace = options.testRoot / "override workspace";
    runOk(options, "workspace init override", {"workspace", "init", overrideWorkspace.string()});
    const std::string switched = runOk(options, "workspace status after switch", {"workspace", "status", "--json"});
    requireContains(switched, overrideWorkspace.string(), "workspace status after second init");

    runOk(options, "workspace use original", {"workspace", "use", workspace.string()});
    const std::string restored = runOk(options, "workspace status restored", {"workspace", "status", "--json"});
    requireContains(restored, workspace.string(), "workspace status after workspace use");

    runOk(options, "doctor", {"doctor", "--json"});
    runOk(options, "config list", {"config", "list"});
    runOk(options, "template list", {"template", "list"});
    runOk(options, "profile list", {"profile", "list"});
    runOk(options, "profile validate", {"profile", "validate", (workspace / "profiles" / "bypass-cn.json").string()});

    subcli::StabilityHttpServer server(options.sourceDir / "tests/stability_fixtures/subscriptions");
    server.start();
    runOk(options, "sub add", {"sub", "add", "--name", "local-http", "--url", server.url("/sub/plain"), "--force"});
    runOk(options, "sub update", {"sub", "update", "local-http", "--strict-network"});
    runOk(options, "sub list", {"sub", "list"});
    runOk(options, "export mihomo", {"export", "mihomo", "--output-dir", outputDir.string(), "--strict-network"});
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
