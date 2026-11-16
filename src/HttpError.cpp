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

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

// Whether a body is nothing but a message: an object with no "success" field
// that is empty, or whose keys are message keys, or message keys beside fields
// that are all null (a message next to documented fields the API left null).
// The values of the message keys do not matter: a string, a blank, null or a
// list is a message. An object with a non-null field of its own is a result.
bool jsonMessageOnly(const json& object) {
    if (!object.is_object() || object.contains("success")) {
        return false;
    }
    if (object.empty()) {
        return true;
    }
    static const char* const keys[] = {"error", "message", "error_message", "Message", "Error", "detail"};
    bool hasMessage = false;
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (std::find_if(std::begin(keys), std::end(keys),
                [&](const char* key) { return it.key() == key; }) != std::end(keys)) {
            hasMessage = true;
        } else if (!it.value().is_null()) {
            return false;
        }
    }
    return hasMessage;
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

size_t zbSkipDownloadPadding(const std::string& body) {
    size_t at = 0;
    while (at < body.size()) {
        const auto c = static_cast<unsigned char>(body[at]);
        if (c == ' ' || (c >= '\t' && c <= '\r')) {
            ++at;
        } else if (c == 0xEF && body.compare(at, 3, "\xEF\xBB\xBF") == 0) {
            at += 3;
        } else if (c == 0xC2 && body.compare(at, 2, "\xC2\xA0") == 0) {
            at += 2;
        } else {
            break;
        }
    }
    return at;
}

const char* const kZbRedirectMessage = "ZeroBounce returned a redirect, which this client does not follow.";

bool zbFileStatusHasKnownField(const json& body) {
    static const char* const fields[] = {"success", "error_reason", "file_id", "file_name", "upload_date",
        "file_status", "complete_percentage", "file_phase_2_status", "return_url"};
    if (!body.is_object()) {
        return false;
    }
    return std::any_of(std::begin(fields), std::end(fields), [&body](const char* field) {
        return body.contains(field) && !body[field].is_null();
    });
}

bool zbBodyLooksLikeHtml(const std::string& body) {
    // After any amount of padding, so that a page cannot hide its first
    // markup behind more than the sniffed window of spaces.
    const size_t start = zbSkipDownloadPadding(body);
    std::string lower = lowerCopy(body.substr(start, std::min<size_t>(body.size() - start, 2000)));
    return lower.find("<!doctype html") != std::string::npos ||
        lower.find("<html") != std::string::npos ||
        lower.find("<head") != std::string::npos ||
        lower.find("<body") != std::string::npos;
}

bool zbContentTypeIsHtml(const std::string& contentType) {
    return lowerCopy(contentType).find("text/html") != std::string::npos;
}

std::string zbMessageFromBody(const std::string& body, long statusCode) {
    // Padding (a byte order mark, NBSP) in front of the text is not part of it.
    std::string trimmed = trimCopy(body.substr(zbSkipDownloadPadding(body)));
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
    ZbBodyRule rule,
    const std::function<void(ZBErrorResponse)>& errorCallback
) {
    // A redirect is reported as such and not followed, as on the downloads.
    if (statusCode >= 300 && statusCode < 400) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError(kZbRedirectMessage, statusCode));
        }
        return false;
    }
    // A text/html page is a proxy's or a portal's, not the API's answer, even
    // when its body happens to parse as JSON.
    if (statusCode > 299 || zbBodyLooksLikeHtml(body) || zbContentTypeIsHtml(contentType) ||
        zbSkipDownloadPadding(body) == body.size()) {
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
    // LIVE: a typed call's 200 that is only a message ("Message", "message",
    // "detail"), is empty ({}), or has a message beside fields that are all
    // null is the API refusing, not a result: it used to come back as a zeroed
    // struct. validate keeps a bare message (greylist processing answers
    // {"Message": "Greylist validation still in progress..."}), and the raw
    // JSON calls (filters, Inbox Placement, list evaluator) keep any object.
    if (rule == ZbBodyRule::Typed && jsonMessageOnly(out)) {
        zbReportHttpError(statusCode, body, errorCallback);
        return false;
    }
    if (rule == ZbBodyRule::TypedAllowMessage && out.is_object() && out.empty()) {
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

// The code point of a run of hex digits at text[at..], up to maxDigits (at
// least minDigits), or -1. Stops at the first character that is not hex.
long zbHexRun(const std::string& text, size_t at, size_t minDigits, size_t maxDigits, size_t& used) {
    long value = 0;
    size_t count = 0;
    while (count < maxDigits && at + count < text.size()) {
        const int digit = zbHexValue(text[at + count]);
        if (digit < 0) {
            break;
        }
        value = value * 16 + digit;
        ++count;
    }
    used = count;
    return count >= minDigits ? value : -1;
}

// text with string escapes undone: the JSON ones (\" \\ \/ \b \f \n \r \t
// \uXXXX, surrogate pairs included), and the spellings other languages use for
// the same character -- \u{27}, \U{27}, \U0027, \U00000027 and octal \047 --
// that a body echoing a secret may have chosen. Anything else is kept.
// wideU picks how a \U with no braces is read: eight hex digits (C and
// Python), or four (some others); both views are tried.
std::string zbJsonUnescapeView(const std::string& text, bool wideU = true) {
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
        if (next >= '0' && next <= '7') {
            // Octal: up to three digits, a byte value of at most \377.
            size_t digits = 0;
            unsigned value = 0;
            while (digits < 3 && i + 1 + digits < text.size() && text[i + 1 + digits] >= '0' &&
                   text[i + 1 + digits] <= '7' && value * 8 + (text[i + 1 + digits] - '0') <= 0377) {
                value = value * 8 + static_cast<unsigned>(text[i + 1 + digits] - '0');
                ++digits;
            }
            out += static_cast<char>(value);
            i += digits;
            continue;
        }
        if (next != 'u' && next != 'U') {
            out += text[i];
            continue;
        }
        long cp = -1;
        size_t consumed = 0;  // characters after the backslash
        if (i + 2 < text.size() && text[i + 2] == '{') {
            size_t used = 0;
            const long value = zbHexRun(text, i + 3, 1, 6, used);
            if (value >= 0 && value <= 0x10FFFF && i + 3 + used < text.size() && text[i + 3 + used] == '}') {
                cp = value;
                consumed = 3 + used;
            }
        } else {
            size_t used = 0;
            if (next == 'U' && wideU) {
                const long value = zbHexRun(text, i + 2, 8, 8, used);
                if (value >= 0 && value <= 0x10FFFF) {
                    cp = value;
                    consumed = 1 + 8;
                }
            }
            if (cp < 0) {
                const long value = zbHex4(text, i + 2);
                if (value >= 0) {
                    cp = value;
                    consumed = 1 + 4;
                }
            }
        }
        if (cp < 0) {
            out += text[i];
            continue;
        }
        i += consumed;
        if (next == 'u' && consumed == 5 && cp >= 0xD800 && cp <= 0xDBFF && i + 2 < text.size() &&
            text[i + 1] == '\\' && text[i + 2] == 'u') {
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
         {zbUrlDecodeView(text, false), zbUrlDecodeView(text, true), zbJsonUnescapeView(text, true),
          zbJsonUnescapeView(text, false)}) {
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

namespace {

// The next code point of a UTF-8 string, or -1 for a byte that does not start
// a well-formed sequence (such a byte is not looked at further).
long zbNextCodePoint(const std::string& text, size_t& at) {
    const auto lead = static_cast<unsigned char>(text[at]);
    size_t length = 1;
    unsigned long cp = lead;
    if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        cp = lead & 0x07;
    } else if (lead >= 0xE0 && lead < 0xF0) {
        length = 3;
        cp = lead & 0x0F;
    } else if (lead >= 0xC2 && lead < 0xE0) {
        length = 2;
        cp = lead & 0x1F;
    } else if (lead >= 0x80) {
        ++at;
        return -1;
    }
    if (at + length > text.size()) {
        ++at;
        return -1;
    }
    for (size_t i = 1; i < length; ++i) {
        const auto next = static_cast<unsigned char>(text[at + i]);
        if ((next & 0xC0) != 0x80) {
            ++at;
            return -1;
        }
        cp = (cp << 6) | (next & 0x3F);
    }
    at += length;
    return static_cast<long>(cp);
}

// Unicode whitespace (Zs, Zl, Zp), controls (Cc) and format characters (Cf):
// code points that are invisible or that change how text reads, and that no
// API key has.
bool zbIsInvisibleOrSpace(long cp) {
    struct Range {
        long low;
        long high;
    };
    static const Range ranges[] = {
        {0x007F, 0x009F}, {0x00A0, 0x00A0}, {0x00AD, 0x00AD}, {0x0600, 0x0605}, {0x061C, 0x061C},
        {0x06DD, 0x06DD}, {0x070F, 0x070F}, {0x0890, 0x0891}, {0x08E2, 0x08E2}, {0x1680, 0x1680},
        {0x180E, 0x180E}, {0x2000, 0x200F}, {0x2028, 0x202F}, {0x205F, 0x206F}, {0x3000, 0x3000},
        {0xFEFF, 0xFEFF}, {0xFFF9, 0xFFFB}, {0x110BD, 0x110BD}, {0x110CD, 0x110CD},
        {0x13430, 0x1343F}, {0x1BCA0, 0x1BCA3}, {0x1D173, 0x1D17A}, {0xE0001, 0xE0001},
        {0xE0020, 0xE007F},
    };
    return std::any_of(std::begin(ranges), std::end(ranges),
        [cp](const Range& range) { return cp >= range.low && cp <= range.high; });
}

}  // namespace

bool zbApiKeyIsClean(const std::string& apiKey) {
    if (apiKey != trimCopy(apiKey)) {
        return false;
    }
    for (size_t at = 0; at < apiKey.size();) {
        const auto byte = static_cast<unsigned char>(apiKey[at]);
        if (byte < 0x80) {
            if (std::iscntrl(byte) != 0) {
                return false;
            }
            ++at;
            continue;
        }
        const long cp = zbNextCodePoint(apiKey, at);
        if (cp >= 0 && zbIsInvisibleOrSpace(cp)) {
            return false;
        }
    }
    return true;
}

std::string zbNormalizeBaseUrl(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

bool zbUrlHasUserinfo(const std::string& url) {
    const auto scheme = url.find("://");
    if (scheme == std::string::npos) {
        return false;
    }
    const auto end = url.find_first_of("/?#", scheme + 3);
    const auto at = url.find('@', scheme + 3);
    return at != std::string::npos && (end == std::string::npos || at < end);
}

bool zbUrlHasEmptyHost(const std::string& url) {
    const auto scheme = url.find("://");
    if (scheme == std::string::npos) {
        return true;
    }
    const auto end = url.find_first_of("/?#", scheme + 3);
    const std::string authority = url.substr(scheme + 3, end == std::string::npos ? end : end - scheme - 3);
    // A host is what is in front of the port; an IPv6 host is in brackets.
    return authority.empty() || authority.front() == ':';
}

bool zbCheckRequestUrl(
    const std::string& url,
    const std::function<void(ZBErrorResponse)>& errorCallback
) {
    auto reject = [&errorCallback](const char* message) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError(message));
        }
        return false;
    };
    // The API key travels in the query string, a form field or a header, so a
    // plain-http URL would put it on the wire in the clear.
    if (lowerCopy(url.substr(0, 8)) != "https://") {
        return reject("url must be an https:// URL");
    }
    // The URL is not quoted: user:pass@ in it is a credential.
    if (zbUrlHasUserinfo(url)) {
        return reject("url must not contain a username or password");
    }
    if (zbUrlHasEmptyHost(url)) {
        return reject("url must be an https:// URL");
    }
    return true;
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
    message = zbRedactApiKey(std::move(message), apiKey);
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
    return zbReadRuledJsonResponse(response, apiKey, out, ZbBodyRule::Raw, errorCallback);
}

bool zbReadRuledJsonResponse(
    const cpr::Response& response,
    const std::string& apiKey,
    json& out,
    ZbBodyRule rule,
    const std::function<void(ZBErrorResponse)>& errorCallback
) {
    // Checked before the body: a 200 cut short before its Content-Length (or
    // with a read timeout part-way) used to be a result whenever the part that
    // arrived happened to parse as JSON.
    if (zbReportTransportFailure(response, apiKey, errorCallback)) {
        return false;
    }
    return zbReadJsonBody(
        response.status_code, response.text, zbResponseContentType(response), out, rule, errorCallback);
}

bool zbReadShapedJsonResponse(
    const cpr::Response& response,
    const std::string& apiKey,
    json& out,
    ZbJsonShape shape,
    ZbBodyRule rule,
    const std::function<void(ZBErrorResponse)>& errorCallback
) {
    if (!zbReadRuledJsonResponse(response, apiKey, out, rule, errorCallback)) {
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
            errorCallback(ZBErrorResponse::parseError(kZbRedirectMessage, response.status_code));
        }
        return true;
    }
    zbReportHttpError(response.status_code, response.text, errorCallback);
    return true;
}

bool zbDownloadBodyIsJson(const std::string& body, const std::string& contentType) {
    std::string type = lowerCopy(contentType);
    type = trimCopy(type.substr(0, type.find(';')));
    if (type.find("application/json") != std::string::npos ||
        (type.size() >= 5 && type.compare(type.size() - 5, 5, "+json") == 0)) {
        return true;
    }
    // A result file never starts with "{" or "[" (a list of lists is not a
    // CSV either), whatever padding or byte order mark is in front of it.
    const size_t first = zbSkipDownloadPadding(body);
    return first < body.size() && (body[first] == '{' || body[first] == '[');
}

std::string zbDownloadJsonMessage(const std::string& body, long statusCode) {
    const size_t first = zbSkipDownloadPadding(body);
    if (first < body.size() && body[first] == '[') {
        try {
            json parsed = json::parse(trimCopy(body.substr(first)));
            std::string joined;
            if (parsed.is_array()) {
                for (const auto& item : parsed) {
                    if (item.is_string() && !trimCopy(item.get<std::string>()).empty()) {
                        if (!joined.empty()) {
                            joined += "\n";
                        }
                        joined += trimCopy(item.get<std::string>());
                    }
                }
            }
            if (!joined.empty()) {
                return joined;
            }
        } catch (const std::exception&) {
        }
    }
    return zbMessageFromBody(body, statusCode);
}

bool zbReportBadDownloadBody(
    const cpr::Response& response,
    const std::function<void(ZBErrorResponse)>& errorCallback,
    const std::function<std::string(const std::string&)>& jsonMessage
) {
    const std::string& body = response.text;
    const std::string contentType = zbResponseContentType(response);
    if (zbSkipDownloadPadding(body) == body.size() || zbContentTypeIsHtml(contentType) ||
        zbBodyLooksLikeHtml(body)) {
        zbReportHttpError(response.status_code, body, errorCallback);
        return true;
    }
    if (zbDownloadBodyIsJson(body, contentType)) {
        // The API saying why there is no file, whatever its keys or content
        // type: its message, not the raw body.
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError(
                jsonMessage ? jsonMessage(body) : zbDownloadJsonMessage(body, response.status_code),
                response.status_code));
        }
        return true;
    }
    return false;
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
    // Moved over the target in one step, so that the old file stays until the
    // new one is ready. On Windows a plain rename does not replace an existing
    // file, so the replacing move is asked for.
#ifdef _WIN32
    if (!MoveFileExW(tempPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    }
#else
    fs::rename(tempPath, filePath, ec);
#endif
    if (ec) {
        std::error_code ignored;
        fs::remove(tempPath, ignored);
        return fail("Could not write " + localPath + ": " + ec.message());
    }
    return true;
}
