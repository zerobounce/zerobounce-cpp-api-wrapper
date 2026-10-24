#include "ZeroBounce/ZeroBounceWarmup.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include <nlohmann/json.hpp>

#include "HttpError.h"

using json = nlohmann::json;

namespace {

const std::vector<std::string> kOnboardingModels{"self_managed", "zb_managed"};
const std::vector<std::string> kEmailTypes{"transactional", "newsletter", "marketing"};

std::string trimmed(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool hasControlCharacters(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char c) {
        return std::iscntrl(c) != 0;
    });
}

std::string urlEncode(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

bool isIsoDate(const std::string& value) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
        return false;
    }
    for (size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u}) {
        if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }
    return true;
}

void reportError(const OnErrorCallback& errorCallback, const std::string& message, long statusCode = 0) {
    if (errorCallback) {
        errorCallback(ZBErrorResponse::parseError(message, statusCode));
    }
}

}  // namespace

ZeroBounceWarmup::ZeroBounceWarmup()
    : defaultRequestHandler(std::make_unique<RequestHandler>()) {
    requestHandler = defaultRequestHandler.get();
    root = std::string(DefaultBaseUrl) + BasePath;
}

void ZeroBounceWarmup::initialize(const std::string& key, const std::string& baseUrl) {
    ready = false;
    apiKey = key;
    if (trimmed(key).empty()) {
        return;
    }
    // A key with surrounding whitespace or control characters would be quoted,
    // unredacted, in the HTTP library's own error.
    if (key != trimmed(key) || hasControlCharacters(key)) {
        return;
    }
    std::string base = baseUrl.empty() ? std::string(DefaultBaseUrl) : baseUrl;
    std::string lower = base;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lower.rfind("https://", 0) != 0) {
        return;
    }
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    root = base + BasePath;
    ready = true;
}

// -- internals ---------------------------------------------------------------

namespace {

/** Replace every form of a secret in a message with ***. */
std::string redact(const std::string& text, const std::vector<std::string>& secrets) {
    std::string value = text;
    for (const auto& secret : secrets) {
        if (secret.empty()) {
            continue;
        }
        for (const auto& form : {secret, urlEncode(secret)}) {
            size_t at = 0;
            while ((at = value.find(form, at)) != std::string::npos) {
                value.replace(at, form.size(), "***");
                at += 3;
            }
        }
    }
    return value;
}

struct WarmupResult {
    long statusCode = 0;
    std::string contentType;
    std::string body;
};

}  // namespace

/**
 * Why these live in one place: every endpoint needs the same key check, the
 * same header, the same redaction and the same short-error handling.
 */
namespace {

bool warmupReady(const std::string& apiKey, bool ready, const OnErrorCallback& errorCallback) {
    if (trimmed(apiKey).empty()) {
        reportError(errorCallback, "api_key is required");
        return false;
    }
    if (apiKey != trimmed(apiKey) || hasControlCharacters(apiKey)) {
        reportError(errorCallback, "api_key has surrounding whitespace or control characters");
        return false;
    }
    if (!ready) {
        reportError(errorCallback, "base_url must be an https:// URL");
        return false;
    }
    return true;
}

bool warmupSend(
    ZeroBounceWarmup* zb,
    const std::string& method,
    const std::string& url,
    const std::string* jsonBody,
    const std::vector<std::string>& extraSecrets,
    WarmupResult& out,
    const OnErrorCallback& errorCallback
) {
    // The key is masked out of every message, whatever else the caller adds.
    std::vector<std::string> secrets{zb->getApiKey()};
    secrets.insert(secrets.end(), extraSecrets.begin(), extraSecrets.end());

    const cpr::Header jsonHeader{
        {"Accept", "application/json"},
        {"Content-Type", "application/json"},
        {"API-Key", zb->getApiKey()}
    };
    const cpr::Header plainHeader{{"Accept", "application/json"}, {"API-Key", zb->getApiKey()}};

    cpr::Response response;
    try {
        if (method == "POST") {
            response = zb->getRequestHandler()->Post(
                cpr::Url{url}, jsonHeader, cpr::Body{jsonBody ? *jsonBody : std::string{}});
        } else if (method == "PUT") {
            response = zb->getRequestHandler()->Put(
                cpr::Url{url}, jsonHeader, cpr::Body{jsonBody ? *jsonBody : std::string{}});
        } else if (method == "DELETE") {
            // DELETE /custom-engagement is the one endpoint with a body.
            if (jsonBody != nullptr) {
                response = zb->getRequestHandler()->Delete(
                    cpr::Url{url}, jsonHeader, cpr::Body{*jsonBody});
            } else {
                response = zb->getRequestHandler()->Delete(cpr::Url{url}, plainHeader);
            }
        } else {
            response = zb->getRequestHandler()->Get(cpr::Url{url}, plainHeader);
        }
    } catch (const std::exception& error) {
        reportError(errorCallback, redact(error.what(), secrets));
        return false;
    }

    out.statusCode = response.status_code;
    out.body = response.text;
    auto typeIt = response.header.find("Content-Type");
    out.contentType = typeIt == response.header.end() ? "" : typeIt->second;

    if (out.statusCode >= 300 && out.statusCode < 400) {
        reportError(errorCallback,
            "ZeroBounce returned a redirect, which this client does not follow.", out.statusCode);
        return false;
    }
    if (out.statusCode > 299) {
        reportError(errorCallback,
            redact(zbMessageFromBody(out.body, out.statusCode), secrets), out.statusCode);
        return false;
    }
    return true;
}

/**
 * The JSON payload of a success. A documented empty success (204, or an empty
 * 200) is handed to the caller as "null", which is valid JSON.
 */
bool warmupJson(
    ZeroBounceWarmup* zb,
    const std::string& method,
    const std::string& url,
    const std::string* jsonBody,
    const std::vector<std::string>& secrets,
    OnRawJsonSuccessCallback successCallback,
    const OnErrorCallback& errorCallback
) {
    WarmupResult result;
    if (!warmupSend(zb, method, url, jsonBody, secrets, result, errorCallback)) {
        return false;
    }
    if (result.statusCode == 204 || trimmed(result.body).empty()) {
        if (successCallback) {
            successCallback("null");
        }
        return true;
    }
    try {
        json parsed = json::parse(result.body);
        if (successCallback) {
            successCallback(parsed.dump());
        }
        return true;
    } catch (const std::exception&) {
        reportError(errorCallback,
            redact(zbMessageFromBody(result.body, result.statusCode), secrets), result.statusCode);
        return false;
    }
}

bool warmupCsv(
    ZeroBounceWarmup* zb,
    const std::string& url,
    OnSuccessCallback<std::string> successCallback,
    const OnErrorCallback& errorCallback
) {
    WarmupResult result;
    if (!warmupSend(zb, "GET", url, nullptr, {}, result, errorCallback)) {
        return false;
    }
    std::string lowerType = result.contentType;
    std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (trimmed(result.body).empty() || lowerType.find("application/json") != std::string::npos) {
        reportError(errorCallback,
            redact(zbMessageFromBody(result.body, result.statusCode), {zb->getApiKey()}),
            result.statusCode);
        return false;
    }
    if (successCallback) {
        successCallback(result.body);
    }
    return true;
}

bool requireValue(const std::string& value, const char* name, const OnErrorCallback& errorCallback) {
    if (trimmed(value).empty()) {
        reportError(errorCallback, std::string(name) + " is required");
        return false;
    }
    return true;
}

bool requireRange(int value, int low, int high, const char* name, const OnErrorCallback& errorCallback) {
    if (value < low || value > high) {
        std::ostringstream message;
        message << name << " must be " << low << " to " << high;
        reportError(errorCallback, message.str());
        return false;
    }
    return true;
}

bool engagementBody(
    const std::string& cFrom, int replyPct, int importantPct, int clickPct,
    std::string& out, const OnErrorCallback& errorCallback
) {
    if (!requireValue(cFrom, "c_from", errorCallback)) return false;
    if (!requireRange(replyPct, 5, 45, "reply_pct", errorCallback)) return false;
    if (!requireRange(importantPct, 5, 40, "important_pct", errorCallback)) return false;
    if (!requireRange(clickPct, 0, 70, "click_pct", errorCallback)) return false;
    if (clickPct > static_cast<int>(importantPct * 1.75)) {
        reportError(errorCallback, "click_pct must be at most important_pct x 1.75");
        return false;
    }
    json body;
    body["c_from"] = cFrom;
    body["reply_pct"] = replyPct;
    body["important_pct"] = importantPct;
    body["click_pct"] = clickPct;
    out = body.dump();
    return true;
}

}  // namespace

// -- sending accounts --------------------------------------------------------

void ZeroBounceWarmup::listSendingAccounts(OnRawJsonSuccessCallback ok, OnErrorCallback err) {
    if (!warmupReady(apiKey, ready, err)) return;
    warmupJson(this, "GET", root + "/sending-accounts", nullptr, {}, ok, err);
}

void ZeroBounceWarmup::getSendingAccount(
    const std::string& fromAddress, OnRawJsonSuccessCallback ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    if (!requireValue(fromAddress, "from_address", err)) return;
    warmupJson(this, "GET", root + "/sending-account?from_address=" + urlEncode(fromAddress),
               nullptr, {}, ok, err);
}

void ZeroBounceWarmup::addSendingAccount(
    const std::string& fromAddress,
    const std::string& onboardingModel,
    const WarmupAddSendingAccountOptions& options,
    OnRawJsonSuccessCallback ok,
    OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    if (!requireValue(fromAddress, "from_address", err)) return;
    if (std::find(kOnboardingModels.begin(), kOnboardingModels.end(), onboardingModel)
            == kOnboardingModels.end()) {
        reportError(err, "onboarding_model must be self_managed or zb_managed");
        return;
    }

    json body;
    body["from_address"] = fromAddress;
    body["onboarding_model"] = onboardingModel;

    if (onboardingModel == "self_managed") {
        if (!options.engagementRuleId.has_value()) {
            reportError(err, "engagement_rule_id is required for self_managed");
            return;
        }
        if (!requireRange(*options.engagementRuleId, 0, 4, "engagement_rule_id", err)) return;
        body["engagement_rule_id"] = *options.engagementRuleId;
        if (options.emailContent.has_value() || options.managedSending.has_value()) {
            reportError(err, "email_content and managed_sending are for zb_managed");
            return;
        }
    }
    if (options.ignoreConflict.has_value()) {
        body["ignore_conflict"] = *options.ignoreConflict;
    }
    if (options.emailContent.has_value()) {
        const auto& content = *options.emailContent;
        json object = json::object();
        if (!content.typesOfEmails.empty()) {
            for (const auto& kind : content.typesOfEmails) {
                if (std::find(kEmailTypes.begin(), kEmailTypes.end(), kind) == kEmailTypes.end()) {
                    reportError(err,
                        "types_of_emails must be transactional, newsletter and/or marketing");
                    return;
                }
            }
            object["types_of_emails"] = content.typesOfEmails;
        }
        if (content.industry.has_value()) object["industry"] = *content.industry;
        if (content.companyName.has_value()) object["company_name"] = *content.companyName;
        if (content.website.has_value()) object["website"] = *content.website;
        body["email_content"] = object;
    }
    if (options.managedSending.has_value()) {
        const auto& sending = *options.managedSending;
        json object = json::object();
        if (sending.timezoneOffset.has_value()) {
            if (!requireRange(*sending.timezoneOffset, -11, 14, "timezone_offset", err)) return;
            object["timezone_offset"] = *sending.timezoneOffset;
        }
        if (sending.warmupStartDate.has_value()) {
            if (!isIsoDate(*sending.warmupStartDate)) {
                reportError(err, "warmup_start_date must be YYYY-MM-DD");
                return;
            }
            object["warmup_start_date"] = *sending.warmupStartDate;
        }
        if (sending.dailyLimit.has_value()) object["daily_limit"] = *sending.dailyLimit;
        body["managed_sending"] = object;
    }

    const std::string payload = body.dump();
    warmupJson(this, "POST", root + "/sending-accounts", &payload, {}, ok, err);
}

void ZeroBounceWarmup::updateSendingAccount(
    const std::string& fromAddress, int engagementRuleId, int statusId, bool autoEnable,
    OnRawJsonSuccessCallback ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    if (!requireValue(fromAddress, "from_address", err)) return;
    if (!requireRange(engagementRuleId, 0, 4, "engagement_rule_id", err)) return;
    if (!requireRange(statusId, 0, 8, "status_id", err)) return;

    json body;
    body["team_id"] = AnyTeamId;
    body["from_address"] = fromAddress;
    body["engagement_rule_id"] = engagementRuleId;
    body["status_id"] = statusId;
    body["auto_enable"] = autoEnable;
    const std::string payload = body.dump();
    warmupJson(this, "PUT", root + "/sending-accounts", &payload, {}, ok, err);
}

void ZeroBounceWarmup::deleteSendingAccount(
    const std::string& fromAddress, OnRawJsonSuccessCallback ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    if (!requireValue(fromAddress, "from_address", err)) return;
    warmupJson(this, "DELETE", root + "/sending-accounts?from_address=" + urlEncode(fromAddress),
               nullptr, {}, ok, err);
}

void ZeroBounceWarmup::changeFromAddress(
    const std::string& oldFromAddress, const std::string& newFromAddress,
    OnRawJsonSuccessCallback ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    if (!requireValue(oldFromAddress, "old_from_address", err)) return;
    if (!requireValue(newFromAddress, "new_from_address", err)) return;

    json body;
    body["team_id"] = AnyTeamId;
    body["old_from_address"] = oldFromAddress;
    body["new_from_address"] = newFromAddress;
    const std::string payload = body.dump();
    warmupJson(this, "PUT", root + "/smtp-credentials", &payload, {}, ok, err);
}

void ZeroBounceWarmup::listFromAddresses(
    const WarmupListFromAddressesOptions& options, OnRawJsonSuccessCallback ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;

    std::string url = root + "/from-addresses";
    std::string separator = "?";
    if (options.searchFrom.has_value()) {
        const std::string& search = *options.searchFrom;
        const bool allowed = std::all_of(search.begin(), search.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == '@' || c == '+';
        });
        if (!allowed) {
            reportError(err, "search_from takes letters, digits and . - _ @ + only");
            return;
        }
        url += separator + "search_from=" + urlEncode(search);
        separator = "&";
    }
    // The API takes the paging values as strings.
    if (options.offset.has_value()) {
        if (*options.offset < 0) {
            reportError(err, "offset must be a non-negative integer");
            return;
        }
        url += separator + "offset=" + std::to_string(*options.offset);
        separator = "&";
    }
    if (options.limit.has_value()) {
        if (*options.limit < 0) {
            reportError(err, "limit must be a non-negative integer");
            return;
        }
        url += separator + "limit=" + std::to_string(*options.limit);
    }
    warmupJson(this, "GET", url, nullptr, {}, ok, err);
}

// -- engagement rules --------------------------------------------------------

void ZeroBounceWarmup::getEngagementRules(OnRawJsonSuccessCallback ok, OnErrorCallback err) {
    if (!warmupReady(apiKey, ready, err)) return;
    warmupJson(this, "GET", root + "/engagement-rules", nullptr, {}, ok, err);
}

void ZeroBounceWarmup::setEngagementRule(
    const std::string& cFrom, int ruleId, OnRawJsonSuccessCallback ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    if (!requireValue(cFrom, "c_from", err)) return;
    if (!requireRange(ruleId, 0, 4, "rule_id", err)) return;

    json body;
    body["rule_id"] = ruleId;
    const std::string payload = body.dump();
    warmupJson(this, "PUT", root + "/engagement-rule?c_from=" + urlEncode(cFrom),
               &payload, {}, ok, err);
}

void ZeroBounceWarmup::getCustomEngagement(
    const std::string& cFrom, OnRawJsonSuccessCallback ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    if (!requireValue(cFrom, "c_from", err)) return;
    const std::string segment = trimmed(cFrom);
    if (segment == "." || segment == "..") {
        reportError(err, "c_from is invalid");
        return;
    }
    warmupJson(this, "GET", root + "/custom-engagement/" + urlEncode(segment), nullptr, {}, ok, err);
}

void ZeroBounceWarmup::createCustomEngagement(
    const std::string& cFrom, int replyPct, int importantPct, int clickPct,
    OnRawJsonSuccessCallback ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    std::string payload;
    if (!engagementBody(cFrom, replyPct, importantPct, clickPct, payload, err)) return;
    warmupJson(this, "POST", root + "/custom-engagement", &payload, {}, ok, err);
}

void ZeroBounceWarmup::updateCustomEngagement(
    const std::string& cFrom, int replyPct, int importantPct, int clickPct,
    OnRawJsonSuccessCallback ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    std::string payload;
    if (!engagementBody(cFrom, replyPct, importantPct, clickPct, payload, err)) return;
    warmupJson(this, "PUT", root + "/custom-engagement", &payload, {}, ok, err);
}

void ZeroBounceWarmup::deleteCustomEngagement(
    const std::string& cFrom, int engagementRuleId, OnRawJsonSuccessCallback ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    if (!requireValue(cFrom, "c_from", err)) return;
    if (!requireRange(engagementRuleId, 1, 4, "engagement_rule_id", err)) return;

    json body;
    body["c_from"] = cFrom;
    body["engagement_rule_id"] = engagementRuleId;
    const std::string payload = body.dump();
    warmupJson(this, "DELETE", root + "/custom-engagement", &payload, {}, ok, err);
}

// -- statistics, seeds and utilities -----------------------------------------

void ZeroBounceWarmup::getSeeds(OnRawJsonSuccessCallback ok, OnErrorCallback err) {
    if (!warmupReady(apiKey, ready, err)) return;
    warmupJson(this, "GET", root + "/seeds", nullptr, {}, ok, err);
}

void ZeroBounceWarmup::downloadSeeds(OnSuccessCallback<std::string> ok, OnErrorCallback err) {
    if (!warmupReady(apiKey, ready, err)) return;
    warmupCsv(this, root + "/seeds/download", ok, err);
}

void ZeroBounceWarmup::getSeedCount(OnRawJsonSuccessCallback ok, OnErrorCallback err) {
    if (!warmupReady(apiKey, ready, err)) return;
    warmupJson(this, "GET", root + "/seed-count", nullptr, {}, ok, err);
}

void ZeroBounceWarmup::getProgressToday(OnRawJsonSuccessCallback ok, OnErrorCallback err) {
    if (!warmupReady(apiKey, ready, err)) return;
    warmupJson(this, "GET", root + "/progress-today", nullptr, {}, ok, err);
}

void ZeroBounceWarmup::getSentToday(OnRawJsonSuccessCallback ok, OnErrorCallback err) {
    if (!warmupReady(apiKey, ready, err)) return;
    warmupJson(this, "GET", root + "/sent-today", nullptr, {}, ok, err);
}

void ZeroBounceWarmup::getEmailsPerDay(OnRawJsonSuccessCallback ok, OnErrorCallback err) {
    if (!warmupReady(apiKey, ready, err)) return;
    warmupJson(this, "GET", root + "/emails-per-day", nullptr, {}, ok, err);
}

void ZeroBounceWarmup::getStatistics(
    const std::string& fromAddress, OnRawJsonSuccessCallback ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    if (!requireValue(fromAddress, "from_address", err)) return;
    warmupJson(this, "GET", root + "/statistics?from_address=" + urlEncode(fromAddress),
               nullptr, {}, ok, err);
}

void ZeroBounceWarmup::getStatisticsPerDay(
    const std::string& fromAddress, const std::string& startDate, const std::string& endDate,
    OnRawJsonSuccessCallback ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    if (!requireValue(fromAddress, "from_address", err)) return;
    if (!isIsoDate(startDate)) {
        reportError(err, "start_date must be YYYY-MM-DD");
        return;
    }
    if (!isIsoDate(endDate)) {
        reportError(err, "end_date must be YYYY-MM-DD");
        return;
    }
    if (startDate > endDate) {
        reportError(err, "start_date is later than end_date");
        return;
    }
    warmupJson(this, "GET",
        root + "/statistics-per-day?from_address=" + urlEncode(fromAddress)
             + "&start_date=" + urlEncode(startDate) + "&end_date=" + urlEncode(endDate),
        nullptr, {}, ok, err);
}

void ZeroBounceWarmup::getStatisticsDetailed(
    const std::string& cFrom, OnRawJsonSuccessCallback ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    if (!requireValue(cFrom, "c_from", err)) return;
    warmupJson(this, "GET", root + "/statistics-detailed?c_from=" + urlEncode(cFrom),
               nullptr, {}, ok, err);
}

void ZeroBounceWarmup::downloadStatistics(
    const std::string& cFrom, OnSuccessCallback<std::string> ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    if (!requireValue(cFrom, "c_from", err)) return;
    warmupCsv(this, root + "/statistics/download?c_from=" + urlEncode(cFrom), ok, err);
}

void ZeroBounceWarmup::getFromAddressStatistics(
    const std::string& fromAddress, OnRawJsonSuccessCallback ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    if (!requireValue(fromAddress, "from_address", err)) return;
    warmupJson(this, "GET", root + "/from-address-statistics?from_address=" + urlEncode(fromAddress),
               nullptr, {}, ok, err);
}

void ZeroBounceWarmup::checkDomainDmarc(
    const std::string& fromAddress, const std::optional<std::string>& dkimSelector,
    OnRawJsonSuccessCallback ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    if (!requireValue(fromAddress, "from_address", err)) return;
    if (fromAddress.find('@') == std::string::npos) {
        reportError(err, "from_address must be an email address, e.g. sender@example.com");
        return;
    }
    std::string url = root + "/check-domain-dmarc?from_address=" + urlEncode(fromAddress);
    if (dkimSelector.has_value()) {
        if (!requireValue(*dkimSelector, "dkim_selector", err)) return;
        url += "&dkim_selector=" + urlEncode(*dkimSelector);
    }
    warmupJson(this, "GET", url, nullptr, {}, ok, err);
}

void ZeroBounceWarmup::testSmtpConfiguration(
    const std::string& fromAddress, const std::string& username, const std::string& password,
    const std::string& server, int port, OnRawJsonSuccessCallback ok, OnErrorCallback err
) {
    if (!warmupReady(apiKey, ready, err)) return;
    if (!requireValue(fromAddress, "from_address", err)) return;
    if (!requireValue(username, "username", err)) return;
    if (!requireValue(password, "password", err)) return;
    if (!requireValue(server, "server", err)) return;
    if (!requireRange(port, 1, 65535, "port", err)) return;

    json body;
    body["from_address"] = fromAddress;
    body["username"] = username;
    body["password"] = password;
    body["server"] = server;
    body["port"] = port;
    const std::string payload = body.dump();
    warmupJson(this, "POST", root + "/test-smtp-configuration", &payload,
               {password, username}, ok, err);
}
