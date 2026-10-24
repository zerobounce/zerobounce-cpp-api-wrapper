#include "HttpError.h"

#include <algorithm>
#include <cctype>

namespace {

std::string trimCopy(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        begin++;
    }
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        end--;
    }
    return value.substr(begin, end - begin);
}

std::string lowerCopy(const std::string& value) {
    std::string lower;
    lower.reserve(value.size());
    for (unsigned char c : value) {
        lower.push_back(static_cast<char>(std::tolower(c)));
    }
    return lower;
}

std::string htmlTitle(const std::string& body) {
    std::string lower = lowerCopy(body);
    auto start = lower.find("<title");
    if (start == std::string::npos) {
        return "";
    }
    auto openEnd = lower.find('>', start);
    if (openEnd == std::string::npos) {
        return "";
    }
    auto close = lower.find("</title>", openEnd);
    if (close == std::string::npos) {
        return "";
    }
    return trimCopy(body.substr(openEnd + 1, close - openEnd - 1));
}

std::string stripErrorPrefix(std::string message) {
    for (;;) {
        if (message.rfind("Error:", 0) == 0) {
            message = trimCopy(message.substr(6));
            continue;
        }
        if (message.rfind("[Error]:", 0) == 0) {
            message = trimCopy(message.substr(8));
            continue;
        }
        return message;
    }
}

// Pulls the readable message out of an API error body. "detail" is how the
// Inbox Placement Tester (Django REST framework) reports errors.
std::string jsonErrorText(const json& object) {
    if (!object.is_object()) {
        return "";
    }
    for (const char* key : {"error", "message", "error_message", "detail"}) {
        if (object.contains(key) && object[key].is_string()) {
            std::string value = trimCopy(object[key].get<std::string>());
            if (!value.empty()) {
                return value;
            }
        }
    }
    return "";
}

bool jsonHasErrorOrMessageKey(const json& object) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key().find("error") != std::string::npos || it.key().find("message") != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool zbBodyLooksLikeHtml(const std::string& body) {
    std::string lower = lowerCopy(body.substr(0, std::min<size_t>(body.size(), 2000)));
    return lower.find("<!doctype html") != std::string::npos ||
        lower.find("<html") != std::string::npos ||
        lower.find("<head") != std::string::npos ||
        lower.find("<body") != std::string::npos;
}

bool zbContentTypeIsHtml(const std::string& contentType) {
    return lowerCopy(contentType).find("text/html") != std::string::npos;
}

std::string zbMessageFromBody(const std::string& body, long statusCode) {
    std::string trimmed = trimCopy(body);
    if (trimmed.empty()) {
        return "Empty response from ZeroBounce (HTTP " + std::to_string(statusCode) + ")";
    }
    if (zbBodyLooksLikeHtml(body)) {
        std::string title = htmlTitle(body);
        if (title.empty()) {
            return "ZeroBounce returned an HTML error page instead of an API response.";
        }
        return "ZeroBounce returned an HTML error page (\"" + title + "\") instead of an API response.";
    }
    if (trimmed.front() == '{' || trimmed.front() == '[') {
        try {
            json parsed = json::parse(trimmed);
            if (parsed.is_object() && parsed.contains("title") && parsed["title"].is_string()) {
                std::string title = parsed["title"].get<std::string>();
                bool accessDenied = title.find("Access denied") != std::string::npos || title.find("1020") != std::string::npos;
                bool cloudflare = parsed.contains("cloudflare_error") && parsed["cloudflare_error"] == true;
                if (accessDenied && (cloudflare || !jsonHasErrorOrMessageKey(parsed))) {
                    return title;
                }
            }
            std::string fromField = jsonErrorText(parsed);
            if (!fromField.empty()) {
                return stripErrorPrefix(fromField);
            }
            return trimmed;
        } catch (const std::exception&) {
            return "ZeroBounce returned a response that is not valid JSON.";
        }
    }
    if (trimmed.size() > 1000) {
        return "ZeroBounce returned a response that is not valid JSON.";
    }
    std::string message = stripErrorPrefix(trimmed);
    if (message.empty()) {
        return "Empty response from ZeroBounce (HTTP " + std::to_string(statusCode) + ")";
    }
    return message;
}

// Whether a "success" field means failure. The API sends it as a JSON false,
// "false" or "False" depending on the endpoint.
static bool zbSuccessMeansFailure(const json& value) {
    if (value.is_boolean()) {
        return !value.get<bool>();
    }
    if (value.is_string()) {
        std::string text = value.get<std::string>();
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return trimCopy(text) == "false";
    }
    return false;
}

void zbReportHttpError(
    long statusCode,
    const std::string& body,
    const std::function<void(ZBErrorResponse)>& errorCallback
) {
    if (!errorCallback) {
        return;
    }
    errorCallback(ZBErrorResponse::parseError(zbMessageFromBody(body, statusCode), statusCode));
}

bool zbReadJsonResponse(
    long statusCode,
    const std::string& body,
    json& out,
    const std::function<void(ZBErrorResponse)>& errorCallback
) {
    if (statusCode > 299 || zbBodyLooksLikeHtml(body) || trimCopy(body).empty()) {
        zbReportHttpError(statusCode, body, errorCallback);
        return false;
    }
    try {
        out = json::parse(body);
    } catch (const std::exception&) {
        zbReportHttpError(statusCode, body, errorCallback);
        return false;
    }
    // LIVE: {"success": false} on an HTTP 200 is a failure, not a result. The
    // API answers an unknown bulk file id that way, with a 200.
    if (out.is_object() && out.contains("success") && zbSuccessMeansFailure(out["success"])) {
        zbReportHttpError(statusCode, body, errorCallback);
        return false;
    }
    return true;
}
