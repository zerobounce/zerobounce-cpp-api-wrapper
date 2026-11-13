#include "HttpError.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <random>
#include <stdexcept>
#include <vector>

#include <curl/curl.h>

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

std::string jsonErrorTextFrom(const json& object, std::initializer_list<const char*> keys) {
    if (!object.is_object()) {
        return "";
    }
    for (const char* key : keys) {
        if (object.contains(key) && object[key].is_string()) {
            std::string value = trimCopy(object[key].get<std::string>());
            if (!value.empty()) {
                return value;
            }
        }
    }
    return "";
}

// Pulls the readable message out of an API error body, once the response is
// already known to be a failure. "detail" is how the Inbox Placement Tester
// (Django REST framework) reports errors.
std::string jsonErrorText(const json& object) {
    return jsonErrorTextFrom(object, {"error", "message", "error_message", "Message", "Error", "detail"});
}

// The API's own message when an HTTP 200 body is nothing but an error: an
// "error", "error_message" or "Error" field, and no "success" field to gate on.
// An invalid or drained key answers validate, activity and getapiusage with
// {"error": ...}, and the filters with {"Error": ...}. "message" and "detail"
// stay out of this list because successful responses use them too -- a bulk
// submit answers {"success": true, "message": "File Accepted"}.
std::string jsonErrorOnlyText(const json& object) {
    if (!object.is_object() || object.contains("success")) {
        return "";
    }
    return jsonErrorTextFrom(object, {"error", "error_message", "Error"});
}

// Whether a body is nothing but a message: a non-empty object with no "success"
// field whose keys are all message keys. The values do not matter: a string, a
// blank, null or a list is a message.
bool jsonMessageOnly(const json& object) {
    if (!object.is_object() || object.empty() || object.contains("success")) {
        return false;
    }
    static const char* const keys[] = {"error", "message", "error_message", "Message", "Error", "detail"};
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (std::find_if(std::begin(keys), std::end(keys),
                [&](const char* key) { return it.key() == key; }) == std::end(keys)) {
            return false;
        }
    }
    return true;
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

// The Content-Type header of a response ("" when there is none). cpr's header
// map compares names without regard to case.
static std::string zbResponseContentType(const cpr::Response& response) {
    auto it = response.header.find("Content-Type");
    return it == response.header.end() ? std::string() : it->second;
}

namespace {

// A strict RFC 8259 check over the whole body. nlohmann's parser is strict
// about the value itself, but it skips a leading UTF-8 byte order mark and
// stops at a NUL byte, so "\xEF\xBB\xBF{...}" and "{...}\0junk" passed as
// JSON. Only space, tab, CR and LF may surround the value.
class ZbStrictJsonScan {
public:
    explicit ZbStrictJsonScan(const std::string& text) : text_(text) {}

    bool whole() {
        ws();
        if (!value(0)) {
            return false;
        }
        ws();
        return at_ == text_.size();
    }

private:
    static constexpr int kMaxDepth = 512;

    bool more() const { return at_ < text_.size(); }
    char peek() const { return more() ? text_[at_] : '\0'; }

    void ws() {
        while (more() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')) {
            ++at_;
        }
    }

    bool digits() {
        if (!more() || !std::isdigit(static_cast<unsigned char>(peek()))) {
            return false;
        }
        while (more() && std::isdigit(static_cast<unsigned char>(peek()))) {
            ++at_;
        }
        return true;
    }

    bool number() {
        if (peek() == '-') {
            ++at_;
        }
        if (peek() == '0') {
            ++at_;
        } else if (!digits()) {
            return false;
        }
        if (peek() == '.') {
            ++at_;
            if (!digits()) {
                return false;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            ++at_;
            if (peek() == '+' || peek() == '-') {
                ++at_;
            }
            if (!digits()) {
                return false;
            }
        }
        return true;
    }

    bool string() {
        if (peek() != '"') {
            return false;
        }
        ++at_;
        while (more()) {
            const auto c = static_cast<unsigned char>(text_[at_++]);
            if (c == '"') {
                return true;
            }
            if (c < 0x20) {
                return false;
            }
            if (c == '\\') {
                if (!more()) {
                    return false;
                }
                const char escape = text_[at_++];
                if (escape == 'u') {
                    for (int i = 0; i < 4; ++i) {
                        if (!more() || !std::isxdigit(static_cast<unsigned char>(text_[at_]))) {
                            return false;
                        }
                        ++at_;
                    }
                } else if (std::string("\"\\/bfnrt").find(escape) == std::string::npos || escape == '\0') {
                    return false;
                }
            }
        }
        return false;
    }

    bool literal(const char* word) {
        const std::string expected(word);
        if (text_.compare(at_, expected.size(), expected) != 0) {
            return false;
        }
        at_ += expected.size();
        return true;
    }

    bool value(int depth) {
        if (!more() || depth > kMaxDepth) {
            return false;
        }
        switch (peek()) {
        case '{':
            ++at_;
            ws();
            if (peek() == '}') {
                ++at_;
                return true;
            }
            for (;;) {
                if (!string()) {
                    return false;
                }
                ws();
                if (peek() != ':') {
                    return false;
                }
                ++at_;
                ws();
                if (!value(depth + 1)) {
                    return false;
                }
                ws();
                if (more() && peek() == ',') {
                    ++at_;
                    ws();
                    continue;
                }
                if (more() && peek() == '}') {
                    ++at_;
                    return true;
                }
                return false;
            }
        case '[':
            ++at_;
            ws();
            if (peek() == ']') {
                ++at_;
                return true;
            }
            for (;;) {
                ws();
                if (!value(depth + 1)) {
                    return false;
                }
                ws();
                if (more() && peek() == ',') {
                    ++at_;
                    continue;
                }
                if (more() && peek() == ']') {
                    ++at_;
                    return true;
                }
                return false;
            }
        case '"':
            return string();
        case 't':
            return literal("true");
        case 'f':
            return literal("false");
        case 'n':
            return literal("null");
        default:
            return number();
        }
    }

    const std::string& text_;
    size_t at_ = 0;
};

}  // namespace

bool zbJsonIsStrict(const std::string& text) {
    return ZbStrictJsonScan(text).whole();
}

json zbParseStrictJson(const std::string& body) {
    if (!zbJsonIsStrict(body)) {
        throw std::invalid_argument("not strict JSON");
    }
    return json::parse(body);
}

// The body checks of zbReadJsonResponse, once the transport check has passed.
// File-local, so that no call can read a body without that check.
static bool zbReadJsonBody(
    long statusCode,
    const std::string& body,
    const std::string& contentType,
    json& out,
    const std::function<void(ZBErrorResponse)>& errorCallback
) {
    // A text/html page is a proxy's or a portal's, not the API's answer, even
    // when its body happens to parse as JSON.
    if (statusCode > 299 || zbBodyLooksLikeHtml(body) || zbContentTypeIsHtml(contentType) ||
        trimCopy(body).empty()) {
        zbReportHttpError(statusCode, body, errorCallback);
        return false;
    }
    try {
        out = zbParseStrictJson(body);
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
    // LIVE: an invalid or drained key answers with an HTTP 200 and a body that
    // is only {"error": "Invalid API key"} -- no "success" field to gate on.
    // Without this the body was parsed into a zeroed result struct and handed
    // to the success callback, so a caller that never read .error silently got
    // empty data as though it were real.
    if (!jsonErrorOnlyText(out).empty()) {
        zbReportHttpError(statusCode, body, errorCallback);
        return false;
    }
    return true;
}

static bool zbReadNonMessageJsonBody(
    long statusCode,
    const std::string& body,
    const std::string& contentType,
    json& out,
    const std::function<void(ZBErrorResponse)>& errorCallback
) {
    if (!zbReadJsonBody(statusCode, body, contentType, out, errorCallback)) {
        return false;
    }
    // LIVE: guessformat answers an invalid or drained key with an HTTP 200 and
    // nothing but {"Message": "Invalid API key or your account ran out of
    // credits"}, which used to come back as an empty result.
    if (jsonMessageOnly(out)) {
        zbReportHttpError(statusCode, body, errorCallback);
        return false;
    }
    return true;
}

std::vector<std::string> zbSecretForms(const std::string& secret) {
    std::vector<std::string> forms;
    if (secret.empty()) {
        return forms;
    }
    auto add = [&forms](std::string form) {
        if (std::find(forms.begin(), forms.end(), form) == forms.end()) {
            forms.push_back(std::move(form));
        }
    };
    auto urlEncoded = [](const std::string& value) {
        std::string out;
        char* escaped = curl_easy_escape(nullptr, value.c_str(), static_cast<int>(value.size()));
        if (escaped) {
            out = escaped;
            curl_free(escaped);
        }
        return out;
    };
    // The same, with every %20 written as '+', as a form body has it.
    auto plusForSpaces = [](std::string value) {
        for (size_t at = 0; (at = value.find("%20", at)) != std::string::npos;) {
            value.replace(at, 3, "+");
            at += 1;
        }
        return value;
    };
    auto jsonEscaped = [](const std::string& value, bool escapeSlash) {
        std::string json;
        for (char c : value) {
            if (c == '"' || c == '\\' || (escapeSlash && c == '/')) {
                json += '\\';
            }
            json += c;
        }
        return json;
    };
    // A body may echo a request body (JSON-escaped, '/' escaped or not), an
    // echoed body quoted inside another JSON string (JSON-escaped twice), a
    // request URL or form (URL-encoded, spaces as %20 or '+'), or a JSON body
    // put into a URL or form (URL-encoded after JSON escaping). The longer,
    // escaped forms go first: they can contain the raw one.
    std::vector<std::string> jsonForms;
    for (bool escapeSlash : {true, false}) {
        jsonForms.push_back(jsonEscaped(secret, escapeSlash));
    }
    add(jsonEscaped(jsonForms[0], true));
    add(jsonEscaped(jsonForms[1], false));
    for (const auto& json : jsonForms) {
        std::string encoded = urlEncoded(json);
        if (!encoded.empty()) {
            add(encoded);
            add(plusForSpaces(encoded));
        }
    }
    for (const auto& json : jsonForms) {
        add(json);
    }
    std::string encoded = urlEncoded(secret);
    if (!encoded.empty()) {
        add(encoded);
        add(plusForSpaces(encoded));
    }
    add(secret);
    return forms;
}

const char* const kZbSecretOmittedMessage =
    "The response body was omitted because it contained a credential.";

namespace {

int zbHexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// text URL-decoded: each %XX as its byte, and '+' as a space when plusAsSpace
// is set (a form body).
std::string zbUrlDecodeView(const std::string& text, bool plusAsSpace) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        int hi = -1;
        int lo = -1;
        if (text[i] == '%' && i + 2 < text.size() && (hi = zbHexValue(text[i + 1])) >= 0 &&
            (lo = zbHexValue(text[i + 2])) >= 0) {
            out += static_cast<char>(hi * 16 + lo);
            i += 2;
        } else if (plusAsSpace && text[i] == '+') {
            out += ' ';
        } else {
            out += text[i];
        }
    }
    return out;
}

long zbHex4(const std::string& text, size_t at) {
    if (at + 4 > text.size()) {
        return -1;
    }
    long value = 0;
    for (size_t i = at; i < at + 4; ++i) {
        int digit = zbHexValue(text[i]);
        if (digit < 0) {
            return -1;
        }
        value = value * 16 + digit;
    }
    return value;
}

void zbAppendUtf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// text with JSON string escapes undone (\" \\ \/ \b \f \n \r \t \uXXXX,
// surrogate pairs included); anything else is kept.
std::string zbJsonUnescapeView(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
            out += text[i];
            continue;
        }
        static const char escapes[] = "\"\\/bfnrt";
        static const char decoded[] = "\"\\/\b\f\n\r\t";
        const char next = text[i + 1];
        const char* simple = next != '\0' ? std::strchr(escapes, next) : nullptr;
        if (simple) {
            out += decoded[simple - escapes];
            ++i;
            continue;
        }
        long cp = next == 'u' ? zbHex4(text, i + 2) : -1;
        if (cp < 0) {
            out += text[i];
            continue;
        }
        i += 5;
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 2 < text.size() && text[i + 1] == '\\' && text[i + 2] == 'u') {
            long low = zbHex4(text, i + 3);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                i += 6;
            }
        }
        zbAppendUtf8(out, static_cast<unsigned long>(cp));
    }
    return out;
}

// The SDK's own request encoding plus three layers a server or proxy may add
// when it echoes the request back.
constexpr int kZbSecretDecodeLayers = 4;

// Whether secret can be read in text or in any view of it decoded up to
// layers more times: URL-decoded, form-decoded ('+' as a space too) and
// JSON-unescaped, in any order.
bool zbSecretInDecodedViews(const std::string& text, const std::string& secret, int layers) {
    if (text.find(secret) != std::string::npos) {
        return true;
    }
    if (layers == 0) {
        return false;
    }
    for (const std::string& view :
         {zbUrlDecodeView(text, false), zbUrlDecodeView(text, true), zbJsonUnescapeView(text)}) {
        if (view != text && zbSecretInDecodedViews(view, secret, layers - 1)) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::string zbMaskSecret(std::string text, const std::string& secret) {
    if (secret.empty()) {
        return text;
    }
    for (const auto& form : zbSecretForms(secret)) {
        for (size_t at = 0; (at = text.find(form, at)) != std::string::npos; at += 3) {
            text.replace(at, form.size(), "***");
        }
    }
    // Redaction is closed under encoding: a spelling the forms above miss
    // (three URL layers, URL of double JSON, ...) still decodes to the
    // secret, and then none of the text is kept.
    if (zbSecretInDecodedViews(text, secret, kZbSecretDecodeLayers)) {
        return kZbSecretOmittedMessage;
    }
    return text;
}

std::string zbRedactApiKey(std::string text, const std::string& apiKey) {
    return zbMaskSecret(std::move(text), apiKey);
}

const char* const kZbDirtyApiKeyMessage = "api_key has surrounding whitespace or control characters";

bool zbApiKeyIsClean(const std::string& apiKey) {
    if (apiKey != trimCopy(apiKey)) {
        return false;
    }
    return std::none_of(apiKey.begin(), apiKey.end(), [](unsigned char c) {
        return std::iscntrl(c) != 0;
    });
}

std::string zbNormalizeBaseUrl(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

bool zbCheckRequestUrl(
    const std::string& url,
    const std::function<void(ZBErrorResponse)>& errorCallback
) {
    // The API key travels in the query string, a form field or a header, so a
    // plain-http URL would put it on the wire in the clear.
    if (lowerCopy(url.substr(0, 8)) == "https://") {
        return true;
    }
    if (errorCallback) {
        errorCallback(ZBErrorResponse::parseError("url must be an https:// URL"));
    }
    return false;
}

bool zbTransportFailed(const cpr::Response& response) {
    if (response.status_code == 0 || response.error) {
        return true;
    }
    // A body shorter than its Content-Length was cut off. libcurl reports
    // that itself (CURLE_PARTIAL_FILE); this also covers a request handler
    // that does not pass the transport error on. A compressed body is
    // measured after decoding, so it is not compared.
    auto length = response.header.find("Content-Length");
    if (length == response.header.end() || response.header.count("Content-Encoding") != 0) {
        return false;
    }
    try {
        size_t used = 0;
        unsigned long long expected = std::stoull(length->second, &used);
        return used > 0 && expected > response.text.size();
    } catch (const std::exception&) {
        return false;
    }
}

std::string zbTransportErrorMessage(const cpr::Response& response) {
    std::string detail = response.error.message;
    if (response.status_code == 0) {
        return "Could not reach ZeroBounce: " + (detail.empty() ? std::string("no response") : detail);
    }
    return "The response from ZeroBounce was cut short: " +
        (detail.empty() ? std::string("the body is shorter than its Content-Length") : detail);
}

bool zbReportTransportFailure(
    const cpr::Response& response,
    const std::string& apiKey,
    const std::function<void(ZBErrorResponse)>& errorCallback
) {
    if (!zbTransportFailed(response)) {
        return false;
    }
    // No response, or only part of one: the connection was refused,
    // closed early, timed out or failed its TLS handshake.
    std::string message = zbTransportErrorMessage(response);
    if (!apiKey.empty()) {
        for (size_t at = 0; (at = message.find(apiKey, at)) != std::string::npos; at += 3) {
            message.replace(at, apiKey.size(), "***");
        }
    }
    if (errorCallback) {
        // Status 0: no complete HTTP response arrived.
        errorCallback(ZBErrorResponse::parseError(message, 0));
    }
    return true;
}

bool zbReadJsonResponse(
    const cpr::Response& response,
    const std::string& apiKey,
    json& out,
    const std::function<void(ZBErrorResponse)>& errorCallback
) {
    // Checked before the body: a 200 cut short before its Content-Length (or
    // with a read timeout part-way) used to be a result whenever the part that
    // arrived happened to parse as JSON.
    if (zbReportTransportFailure(response, apiKey, errorCallback)) {
        return false;
    }
    return zbReadJsonBody(response.status_code, response.text, zbResponseContentType(response), out, errorCallback);
}

bool zbReadNonMessageJsonResponse(
    const cpr::Response& response,
    const std::string& apiKey,
    json& out,
    const std::function<void(ZBErrorResponse)>& errorCallback
) {
    if (zbReportTransportFailure(response, apiKey, errorCallback)) {
        return false;
    }
    return zbReadNonMessageJsonBody(
        response.status_code, response.text, zbResponseContentType(response), out, errorCallback);
}

bool zbReadShapedJsonResponse(
    const cpr::Response& response,
    const std::string& apiKey,
    json& out,
    ZbJsonShape shape,
    bool nonMessage,
    const std::function<void(ZBErrorResponse)>& errorCallback
) {
    const bool read = nonMessage ? zbReadNonMessageJsonResponse(response, apiKey, out, errorCallback)
                                 : zbReadJsonResponse(response, apiKey, out, errorCallback);
    if (!read) {
        return false;
    }
    if (shape == ZbJsonShape::Array ? !out.is_array() : !out.is_object()) {
        zbReportHttpError(response.status_code, response.text, errorCallback);
        return false;
    }
    return true;
}

bool zbReportFailedDownload(
    const cpr::Response& response,
    const std::string& apiKey,
    const std::function<void(ZBErrorResponse)>& errorCallback
) {
    // Checked before the status: a body cut short after a 200 header (the
    // connection closed, or the read timed out) has a 2xx status and a
    // transport error, and used to be saved as though it were the whole file.
    if (zbReportTransportFailure(response, apiKey, errorCallback)) {
        return true;
    }
    if (response.status_code >= 200 && response.status_code <= 299) {
        return false;
    }
    if (response.status_code >= 300 && response.status_code < 400) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError(
                "ZeroBounce returned a redirect, which this client does not follow.", response.status_code));
        }
        return true;
    }
    zbReportHttpError(response.status_code, response.text, errorCallback);
    return true;
}

bool zbDownloadBodyIsJson(const std::string& body, const std::string& contentType) {
    if (lowerCopy(contentType).find("application/json") != std::string::npos) {
        return true;
    }
    auto first = std::find_if(body.begin(), body.end(), [](unsigned char c) { return !std::isspace(c); });
    return first != body.end() && *first == '{';
}

bool zbSaveDownload(
    const std::string& localPath,
    const std::string& body,
    const std::function<void(ZBErrorResponse)>& errorCallback
) {
    namespace fs = std::filesystem;
    auto fail = [&](const std::string& message) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError(message));
        }
        return false;
    };
    const fs::path filePath(localPath);
    std::error_code ec;
    if (localPath.empty() || fs::is_directory(filePath, ec)) {
        return fail("Invalid file path");
    }
    // A bare file name has no parent directory to create.
    if (!filePath.parent_path().empty()) {
        fs::create_directories(filePath.parent_path(), ec);
        if (ec) {
            return fail("Could not create the directory for " + localPath + ": " + ec.message());
        }
    }
    // Written beside the target and moved over it only when complete, so a
    // failed write (a full disk, a file size limit) neither leaves a partial
    // file nor destroys a file already at the path. The name is unique among
    // concurrent downloads to the same path.
    static std::atomic<unsigned long long> counter{0};
    std::random_device random;
    fs::path tempPath = filePath;
    tempPath += "." + std::to_string(random()) + "-" + std::to_string(++counter) + ".part";
    std::ofstream out(tempPath, std::ofstream::out | std::ofstream::binary | std::ofstream::trunc);
    if (!out) {
        return fail("Could not open " + localPath + " for writing");
    }
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    out.close();
    if (!out) {
        fs::remove(tempPath, ec);
        return fail("Could not write " + localPath);
    }
    // std::filesystem::rename replaces an existing file, on Windows too.
    fs::rename(tempPath, filePath, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(tempPath, ignored);
        return fail("Could not write " + localPath + ": " + ec.message());
    }
    return true;
}
