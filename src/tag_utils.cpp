#include "subcli/tag_utils.hpp"

#include <unordered_set>

namespace subcli {

std::vector<std::string> normalizeTags(const std::vector<std::string>& tags) {
    std::vector<std::string> normalized;
    std::unordered_set<std::string> seen;
    for (const auto& tag : tags) {
        if (tag.empty()) {
            continue;
        }
        if (!seen.insert(tag).second) {
            continue;
        }
        normalized.push_back(tag);
    }
    return normalized;
}

} // namespace subcli
