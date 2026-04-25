#include "subcli/fetch.hpp"

#include <algorithm>
#include <curl/curl.h>

#include "subcli/util.hpp"

namespace subcli {

namespace {

struct BodyWriteState {
    std::string* out = nullptr;
    long maxBytes = 10 * 1024 * 1024;
    bool exceeded = false;
};

size_t writeBody(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* state = static_cast<BodyWriteState*>(userdata);
    const size_t bytes = size * nmemb;
    if (!state || !state->out) {
        return 0;
    }
    if (state->maxBytes > 0 && state->out->size() + bytes > static_cast<size_t>(state->maxBytes)) {
        state->exceeded = true;
        return 0;
    }
    state->out->append(ptr, bytes);
    return size * nmemb;
}

bool hasAllowedScheme(const std::string& url) {
    return url.rfind("file://", 0) == 0 || url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

std::string trimHeaderValue(std::string v) {
    while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ' || v.back() == '\t')) {
        v.pop_back();
    }
    size_t i = 0;
    while (i < v.size() && (v[i] == ' ' || v[i] == '\t')) {
        ++i;
    }
    return v.substr(i);
}

size_t readHeader(char* buffer, size_t size, size_t nitems, void* userdata) {
    const size_t bytes = size * nitems;
    auto* result = static_cast<FetchResult*>(userdata);
    std::string line(buffer, bytes);
    std::string low = toLower(line);

    if (low.rfind("etag:", 0) == 0) {
        result->etag = trimHeaderValue(line.substr(5));
    } else if (low.rfind("last-modified:", 0) == 0) {
        result->lastModified = trimHeaderValue(line.substr(14));
    }
    return bytes;
}

FetchResult fetchOnce(const Subscription& sub, bool useCacheFallback) {
    FetchResult result;

    if (!hasAllowedScheme(sub.url)) {
        result.error = "unsupported subscription url scheme: " + sub.url;
        return result;
    }

    if (sub.url.rfind("file://", 0) == 0) {
        const std::string path = sub.url.substr(7);
        if (!fileExists(path)) {
            result.error = "local file not found: " + path;
            return result;
        }
        result.content = readFile(path);
        if (sub.fetchMaxBytes > 0 && result.content.size() > static_cast<size_t>(sub.fetchMaxBytes)) {
            result.content.clear();
            result.error = "subscription content exceeds fetch_max_bytes";
            return result;
        }
        result.ok = true;
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "failed to init curl";
        if (useCacheFallback && !sub.cachePath.empty() && fileExists(sub.cachePath)) {
            result.content = readFile(sub.cachePath);
            result.ok = true;
            result.usedCache = true;
            result.cacheReason = "curl_init_failed_fallback";
            result.error.clear();
        }
        return result;
    }

    std::string body;
    BodyWriteState bodyState{&body, std::max<long>(1024, sub.fetchMaxBytes), false};
    curl_easy_setopt(curl, CURLOPT_URL, sub.url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(std::max(1, sub.timeout)));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(std::max(1, sub.timeout)));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bodyState);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, readHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &result);

    struct curl_slist* headers = nullptr;
    if (!sub.userAgent.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, sub.userAgent.c_str());
    }
    for (const auto& kv : sub.headers) {
        std::string h = kv.first + ": " + kv.second;
        headers = curl_slist_append(headers, h.c_str());
    }
    if (!sub.etag.empty()) {
        headers = curl_slist_append(headers, ("If-None-Match: " + sub.etag).c_str());
    }
    if (!sub.lastModified.empty()) {
        headers = curl_slist_append(headers, ("If-Modified-Since: " + sub.lastModified).c_str());
    }
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    result.statusCode = code;

    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);

    if (bodyState.exceeded) {
        result.error = "subscription content exceeds fetch_max_bytes";
        return result;
    }

    if (rc != CURLE_OK) {
        result.error = std::string("network fetch failed: ") + curl_easy_strerror(rc);
        if (useCacheFallback && !sub.cachePath.empty() && fileExists(sub.cachePath)) {
            result.content = readFile(sub.cachePath);
            result.ok = true;
            result.usedCache = true;
            result.cacheReason = "network_error_fallback";
            result.error.clear();
        }
        return result;
    }

    if (code == 304) {
        result.notModified = true;
        if (!sub.cachePath.empty() && fileExists(sub.cachePath)) {
            result.content = readFile(sub.cachePath);
            result.ok = true;
            result.usedCache = true;
            result.cacheReason = "not_modified_cache";
            return result;
        }
        result.error = "subscription returned 304 but no cache exists";
        return result;
    }

    if (code >= 200 && code < 300) {
        result.content = body;
        result.ok = true;
        return result;
    }

    result.error = "http status " + std::to_string(code);
    if (useCacheFallback && !sub.cachePath.empty() && fileExists(sub.cachePath)) {
        result.content = readFile(sub.cachePath);
        result.ok = true;
        result.usedCache = true;
        result.cacheReason = "http_error_fallback";
        result.error.clear();
    }
    return result;
}

} // namespace

FetchResult fetchSubscription(const Subscription& sub, bool useCacheFallback) {
    return fetchOnce(sub, useCacheFallback);
}

FetchResult fetchSubscriptionWithRetry(const Subscription& sub, bool useCacheFallback) {
    const int attempts = std::max(1, sub.retry + 1);
    FetchResult last;
    for (int i = 0; i < attempts; ++i) {
        auto result = fetchOnce(sub, false);
        if (result.ok) {
            return result;
        }

        last = result;
        if (result.notModified || result.statusCode == 304) {
            break;
        }

        const bool retryableHttp = result.statusCode == 0 || result.statusCode == 429 || result.statusCode >= 500;
        if (!retryableHttp || i + 1 >= attempts) {
            break;
        }
    }

    if (useCacheFallback && !sub.cachePath.empty() && fileExists(sub.cachePath)) {
        last.content = readFile(sub.cachePath);
        last.ok = true;
        last.usedCache = true;
        last.cacheReason = "retry_failed_fallback";
        last.error.clear();
    }
    return last;
}

} // namespace subcli
