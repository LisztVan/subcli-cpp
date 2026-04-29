#include "subcli/workspace.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "subcli/store.hpp"

namespace subcli {

namespace fs = std::filesystem;

namespace {

fs::path normalizeRoot(const std::string& root) {
    std::error_code ec;
    return fs::absolute(fs::path(root), ec).lexically_normal();
}

fs::path markerPath(const fs::path& root) {
    return root / ".subcli-workspace";
}

fs::path persistedDefaultFilePath() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    fs::path base;
    if (xdg != nullptr && *xdg != '\0') {
        base = fs::path(xdg);
    } else {
        const char* home = std::getenv("HOME");
        if (home != nullptr && *home != '\0') {
            base = fs::path(home) / ".config";
        } else {
            base = fs::temp_directory_path();
        }
    }
    return base / "subcli" / "workspace-default";
}

bool ensureDefaultFiles(const fs::path& root, std::string& error) {
    try {
        const fs::path configPath = root / "config.yaml";
        if (!fs::exists(configPath)) {
            saveConfig(configPath.string(), AppConfig{});
        }
        const fs::path subPath = root / "sub.yaml";
        if (!fs::exists(subPath)) {
            saveSubscriptions(subPath.string(), {});
        }
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

bool copyItem(const fs::path& from, const fs::path& to, bool overwrite, bool dryRun, std::string& error) {
    std::error_code ec;
    if (fs::is_directory(from, ec)) {
        if (ec) {
            error = "failed to inspect directory: " + from.string();
            return false;
        }
        if (!dryRun) {
            fs::create_directories(to, ec);
            if (ec) {
                error = "failed to create directory: " + to.string();
                return false;
            }
            fs::copy_options opts = fs::copy_options::recursive;
            if (overwrite) {
                opts |= fs::copy_options::overwrite_existing;
            } else {
                opts |= fs::copy_options::skip_existing;
            }
            fs::copy(from, to, opts, ec);
            if (ec) {
                error = "failed to copy directory: " + from.string();
                return false;
            }
        }
        return true;
    }

    if (!dryRun) {
        fs::create_directories(to.parent_path(), ec);
        if (ec) {
            error = "failed to create parent directory: " + to.parent_path().string();
            return false;
        }
        if (fs::exists(to, ec) && !overwrite) {
            return true;
        }
        fs::copy_file(from, to, overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none, ec);
        if (ec) {
            error = "failed to copy file: " + from.string();
            return false;
        }
    }
    return true;
}

} // namespace

WorkspaceInitResult workspaceInit(const std::string& root) {
    WorkspaceInitResult out;
    if (root.empty()) {
        out.error = "workspace root is empty";
        return out;
    }

    const fs::path workspaceRoot = normalizeRoot(root);
    out.root = workspaceRoot.string();

    std::error_code ec;
    fs::create_directories(workspaceRoot, ec);
    if (ec) {
        out.error = "failed to create workspace root: " + workspaceRoot.string();
        return out;
    }

    for (const char* dir : {"profiles", "templates", "assets", "cache", "outputs", "state"}) {
        fs::create_directories(workspaceRoot / dir, ec);
        if (ec) {
            out.error = "failed to create workspace directory: " + (workspaceRoot / dir).string();
            return out;
        }
    }

    {
        std::ofstream marker(markerPath(workspaceRoot));
        if (!marker) {
            out.error = "failed to create workspace marker";
            return out;
        }
        marker << "subcli-workspace\n";
    }

    std::string error;
    if (!ensureDefaultFiles(workspaceRoot, error)) {
        out.error = error;
        return out;
    }

    out.ok = true;
    return out;
}

bool workspaceUse(const std::string& root, std::string& error) {
    error.clear();
    if (root.empty()) {
        error = "workspace root is empty";
        return false;
    }

    const fs::path workspaceRoot = normalizeRoot(root);
    std::error_code ec;
    if (!fs::exists(workspaceRoot, ec) || !fs::is_directory(workspaceRoot, ec)) {
        error = "workspace root does not exist: " + workspaceRoot.string();
        return false;
    }

    const fs::path persisted = persistedDefaultFilePath();
    fs::create_directories(persisted.parent_path(), ec);
    if (ec) {
        error = "failed to create default workspace directory";
        return false;
    }
    std::ofstream out(persisted);
    if (!out) {
        error = "failed to persist default workspace";
        return false;
    }
    out << workspaceRoot.string() << "\n";
    return true;
}

bool workspaceUnset(std::string& error) {
    error.clear();
    std::error_code ec;
    const fs::path persisted = persistedDefaultFilePath();
    const bool removed = fs::remove(persisted, ec);
    if (ec) {
        error = "failed to remove persisted default workspace";
        return false;
    }
    (void)removed;
    return true;
}

WorkspaceStatusResult workspaceStatus() {
    WorkspaceStatusResult out;
    out.persistedPath = persistedDefaultFilePath().string();
    std::error_code ec;
    if (!fs::exists(out.persistedPath, ec)) {
        out.ok = true;
        return out;
    }
    if (ec) {
        out.error = "failed to inspect persisted default workspace";
        return out;
    }

    std::ifstream in(out.persistedPath);
    if (!in) {
        out.error = "failed to read persisted default workspace";
        return out;
    }

    std::string value;
    std::getline(in, value);
    if (value.empty()) {
        out.ok = true;
        return out;
    }

    out.hasDefault = true;
    out.defaultRoot = normalizeRoot(value).string();
    out.defaultRootExists = fs::exists(out.defaultRoot, ec) && fs::is_directory(out.defaultRoot, ec);
    out.defaultRootHasMarker = fs::exists(markerPath(out.defaultRoot), ec);
    out.ok = true;
    return out;
}

WorkspaceMigrateResult workspaceMigrate(const WorkspaceMigrateOptions& options) {
    WorkspaceMigrateResult out;
    if (options.fromRoot.empty() || options.toRoot.empty()) {
        out.error = "fromRoot and toRoot are required";
        return out;
    }

    const fs::path from = normalizeRoot(options.fromRoot);
    const fs::path to = normalizeRoot(options.toRoot);

    std::error_code ec;
    if (!fs::exists(from, ec) || !fs::is_directory(from, ec)) {
        out.error = "source workspace does not exist: " + from.string();
        return out;
    }

    if (!options.dryRun) {
        WorkspaceInitResult init = workspaceInit(to.string());
        if (!init.ok) {
            out.error = init.error;
            return out;
        }
    }

    const std::vector<std::string> durableItems = {
        "config.yaml", "sub.yaml", "profiles", "templates", "assets", ".subcli-workspace"};

    for (const std::string& item : durableItems) {
        const fs::path src = from / item;
        if (!fs::exists(src, ec)) {
            out.skipped.push_back(item + " (missing)");
            continue;
        }
        const fs::path dst = to / item;
        std::string error;
        if (!copyItem(src, dst, options.overwrite, options.dryRun, error)) {
            out.error = error;
            return out;
        }
        out.copied.push_back(item);
    }

    for (const std::string& item : {"cache", "state"}) {
        if (fs::exists(from / item, ec)) {
            out.skipped.push_back(item + " (ephemeral)");
        }
    }

    out.ok = true;
    return out;
}

} // namespace subcli
