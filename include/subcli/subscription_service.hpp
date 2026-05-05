#pragma once

#include <string>
#include <vector>

#include "subcli/models.hpp"

namespace subcli {

enum class SubscriptionImportMode {
    Merge,
    Replace,
};

struct SubscriptionImportResult {
    int created = 0;
    int updated = 0;
    int rejected = 0;
    std::vector<Subscription> subscriptions;
    std::vector<std::string> messages;
};

struct SubscriptionPrunePlan {
    std::vector<std::string> removeIds;
    std::vector<std::string> keepIds;
};

struct SubscriptionCheckResult {
    std::string id;
    std::string name;
    bool fetchable = false;
    bool parseable = false;
    int nodeCount = 0;
    std::string error;
};

std::string exportSubscriptionsToYaml(const std::vector<Subscription>& subscriptions);
SubscriptionImportResult importSubscriptionsFromYaml(
    const std::string& yamlContent,
    const std::vector<Subscription>& existing,
    SubscriptionImportMode mode
);
SubscriptionImportResult importSubscriptionsFromUriList(
    const std::string& uriListContent,
    const std::vector<Subscription>& existing,
    SubscriptionImportMode mode
);
SubscriptionImportResult mergeImportedSubscriptions(
    const std::vector<Subscription>& existing,
    const std::vector<Subscription>& imported,
    SubscriptionImportMode mode
);
SubscriptionPrunePlan planPruneSubscriptions(
    const std::vector<Subscription>& subscriptions,
    bool disabled,
    int failedDays
);
int batchSetGroupByTag(
    std::vector<Subscription>& subscriptions,
    const std::string& tag,
    const std::string& group
);

} // namespace subcli
