#pragma once

#include <string>
#include <vector>

namespace subcli {

struct WorkspaceInitResult {
    bool ok = false;
    std::string root;
    std::string error;
};

struct WorkspaceStatusResult {
    bool ok = false;
    std::string persistedPath;
    bool hasDefault = false;
    std::string defaultRoot;
    bool defaultRootExists = false;
    bool defaultRootHasMarker = false;
    std::string error;
};

struct WorkspaceMigrateOptions {
    std::string fromRoot;
    std::string toRoot;
    bool dryRun = false;
    bool overwrite = false;
};

struct WorkspaceMigrateResult {
    bool ok = false;
    std::vector<std::string> copied;
    std::vector<std::string> skipped;
    std::string error;
};

WorkspaceInitResult workspaceInit(const std::string& root);
bool workspaceUse(const std::string& root, std::string& error);
bool workspaceUnset(std::string& error);
WorkspaceStatusResult workspaceStatus();
WorkspaceMigrateResult workspaceMigrate(const WorkspaceMigrateOptions& options);

} // namespace subcli
