#include "subcli/parser.hpp"

#include <cctype>

#include "parser_internal.hpp"

namespace subcli {

namespace {

using ParserFn = ParseResult (*)(const std::string&, const std::string&, const AppConfig&);

ParseResult safeParse(
    ParserFn parser,
    const std::string& parserName,
    const std::string& content,
    const std::string& sourceId,
    const AppConfig& config
) {
    ParseResult result;
    try {
        result = parser(content, sourceId, config);
    } catch (...) {
        result.warnings.push_back({"parser_exception", parserName + " parser failed; skipped"});
    }
    return result;
}

std::string normalizeHint(std::string hint) {
    for (auto& ch : hint) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return hint.empty() ? "auto" : hint;
}

ParseResult parseAuto(const std::string& content, const std::string& sourceId, const AppConfig& config) {
    std::vector<DiagnosticMessage> warnings;

    auto mihomo = safeParse(parseMihomoSubscription, "mihomo", content, sourceId, config);
    warnings.insert(warnings.end(), mihomo.warnings.begin(), mihomo.warnings.end());
    if (!mihomo.nodes.empty()) {
        mihomo.warnings = warnings;
        return mihomo;
    }

    auto singbox = safeParse(parseSingBoxSubscription, "sing-box", content, sourceId, config);
    warnings.insert(warnings.end(), singbox.warnings.begin(), singbox.warnings.end());
    if (!singbox.nodes.empty()) {
        singbox.warnings = warnings;
        return singbox;
    }

    auto xray = safeParse(parseXraySubscription, "xray", content, sourceId, config);
    warnings.insert(warnings.end(), xray.warnings.begin(), xray.warnings.end());
    if (!xray.nodes.empty()) {
        xray.warnings = warnings;
        return xray;
    }

    auto uri = safeParse(parseUriSubscription, "uri", content, sourceId, config);
    warnings.insert(warnings.end(), uri.warnings.begin(), uri.warnings.end());
    uri.warnings = warnings;
    return uri;
}

} // namespace

ParseResult parseSubscription(const std::string& content, const std::string& sourceId, const AppConfig& config) {
    return parseSubscription(content, sourceId, "auto", config);
}

ParseResult parseSubscription(
    const std::string& content,
    const std::string& sourceId,
    const std::string& formatHint,
    const AppConfig& config
) {
    const auto hint = normalizeHint(formatHint);
    if (hint == "auto") {
        return parseAuto(content, sourceId, config);
    }

    ParseResult hinted;
    if (hint == "mihomo") {
        hinted = safeParse(parseMihomoSubscription, "mihomo", content, sourceId, config);
    } else if (hint == "sing-box" || hint == "singbox") {
        hinted = safeParse(parseSingBoxSubscription, "sing-box", content, sourceId, config);
    } else if (hint == "xray") {
        hinted = safeParse(parseXraySubscription, "xray", content, sourceId, config);
    } else if (hint == "uri") {
        hinted = safeParse(parseUriSubscription, "uri", content, sourceId, config);
    } else {
        hinted.warnings.push_back({"unknown_format_hint", "unknown format_hint '" + formatHint + "', fallback to auto"});
        auto fallback = parseAuto(content, sourceId, config);
        fallback.warnings.insert(fallback.warnings.begin(), hinted.warnings.begin(), hinted.warnings.end());
        return fallback;
    }

    if (!hinted.nodes.empty()) {
        return hinted;
    }

    hinted.warnings.push_back({"hint_parse_failed_fallback_to_auto", "format_hint '" + hint + "' parsed no nodes; fallback to auto"});
    auto fallback = parseAuto(content, sourceId, config);
    fallback.warnings.insert(fallback.warnings.begin(), hinted.warnings.begin(), hinted.warnings.end());
    return fallback;
}

std::vector<ProxyNode> parseSubscriptionContent(
    const std::string& content,
    const std::string& sourceId,
    const AppConfig& config
) {
    return parseSubscription(content, sourceId, config).nodes;
}

} // namespace subcli
