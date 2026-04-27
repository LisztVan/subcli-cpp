#pragma once

#include <string>

#include "subcli/models.hpp"

namespace subcli {

struct FetchResult {
    bool ok = false;
    std::string content;
    std::string error;
    long statusCode = 0;
    bool notModified = false;
    bool usedCache = false;
    std::string cacheReason;
    std::string etag;
    std::string lastModified;
};

FetchResult fetchSubscription(const Subscription& sub, bool useCacheFallback = true);
FetchResult fetchSubscriptionWithRetry(const Subscription& sub, bool useCacheFallback = true);
std::string decodeFileUrlPath(const std::string& encoded);

} // namespace subcli
