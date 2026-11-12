#ifndef ZB_HTTP_ERROR_H
#define ZB_HTTP_ERROR_H

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

#include "ZeroBounce/ZBErrorResponse.h"

using json = nlohmann::json;

bool zbBodyLooksLikeHtml(const std::string& body);
bool zbContentTypeIsHtml(const std::string& contentType);
std::string zbMessageFromBody(const std::string& body, long statusCode);
void zbReportHttpError(
    long statusCode,
    const std::string& body,
    const std::function<void(ZBErrorResponse)>& errorCallback
);

/**
 * Whether an API key can be sent: true unless it has surrounding whitespace or
 * control characters. Such a key would be quoted, unredacted, in the HTTP
 * library's own error.
 */
bool zbApiKeyIsClean(const std::string& apiKey);

/** The error every call reports for a key zbApiKeyIsClean rejects. */
extern const char* const kZbDirtyApiKeyMessage;

/**
 * Strips trailing slashes from a base URL, so a path appended to it never
 * becomes `//validate`.
 */
std::string zbNormalizeBaseUrl(std::string url);

/**
 * HTTPS only: reports "url must be an https:// URL" and returns false for any
 * other URL, before anything is sent. Every request path calls this.
 */
bool zbCheckRequestUrl(
    const std::string& url,
    const std::function<void(ZBErrorResponse)>& errorCallback
);

/**
 * Whether a request failed in transport: no response at all (status 0), or a
 * transport error such as a timeout or a connection closed part-way through
 * the body, or a body shorter than its Content-Length. The status can be 2xx
 * when the body was cut short.
 */
bool zbTransportFailed(const cpr::Response& response);

/** A short message for a response zbTransportFailed accepts. Not redacted. */
std::string zbTransportErrorMessage(const cpr::Response& response);

/**
 * Reports a response zbTransportFailed accepts -- no response, or one cut
 * short, whatever its status -- with status 0 and the API key masked out of
 * the message, and returns true. Returns false for a complete response.
 */
bool zbReportTransportFailure(
    const cpr::Response& response,
    const std::string& apiKey,
    const std::function<void(ZBErrorResponse)>& errorCallback
);

/**
 * How every JSON call reads its response. A transport failure
 * (zbReportTransportFailure) is an error before the body is looked at, so the
 * part of a body that arrived is never taken as a result. Then, on HTTP
 * failure, HTML, an empty body, a body that is not JSON, "success": false or
 * an error-only body, reports a short error and returns false. On a JSON body
 * with status < 300, writes it to `out`.
 */
bool zbReadJsonResponse(
    const cpr::Response& response,
    const std::string& apiKey,
    json& out,
    const std::function<void(ZBErrorResponse)>& errorCallback
);

/**
 * zbReadJsonResponse for a call whose result is never a bare message: the email
 * finder and domain search. A body made only of message fields is reported as
 * an error. Not for validate, which answers a repeated greylist_processing
 * request with {"Message": "Greylist validation still in progress..."}.
 */
bool zbReadNonMessageJsonResponse(
    const cpr::Response& response,
    const std::string& apiKey,
    json& out,
    const std::function<void(ZBErrorResponse)>& errorCallback
);

/**
 * For the file downloads: reports a response that is not a complete 2xx -- a
 * transport failure, a body cut short (both with status 0), a redirect or an
 * HTTP error -- and returns true. The transport error text has the API key
 * masked out.
 */
bool zbReportFailedDownload(
    const cpr::Response& response,
    const std::string& apiKey,
    const std::function<void(ZBErrorResponse)>& errorCallback
);

/**
 * Whether a download's body is the API saying why there is no file: a JSON
 * content type, or a body that is a JSON object (a result file never starts
 * with "{"), whatever its keys.
 */
bool zbDownloadBodyIsJson(const std::string& body, const std::string& contentType);

/**
 * Writes a downloaded file, creating its parent directories. Reports a path
 * that is a directory, or a file that cannot be created or written, and
 * returns false. A file that could only be written in part is removed.
 */
bool zbSaveDownload(
    const std::string& localPath,
    const std::string& body,
    const std::function<void(ZBErrorResponse)>& errorCallback
);

/**
 * The forms a secret can be echoed in: JSON-escaped ('/' escaped and not),
 * URL-encoded and raw, without repeats. Empty for an empty secret.
 */
std::vector<std::string> zbSecretForms(const std::string& secret);

/**
 * Whether text is exactly one strict RFC 8259 JSON value, with only space,
 * tab, CR and LF around it (no byte order mark, no NUL byte).
 */
bool zbJsonIsStrict(const std::string& text);

/** json::parse(body) after zbJsonIsStrict; throws for anything else. */
json zbParseStrictJson(const std::string& body);

/** What an error says in place of a body that still held a secret. */
extern const char* const kZbSecretOmittedMessage;

/**
 * A copy of text with every form of secret (zbSecretForms) replaced by ***.
 * When a view of the result decoded up to three times (URL-decoding, '+' as a
 * space, JSON-unescaping, in any order) still holds the secret, the whole
 * text is replaced by kZbSecretOmittedMessage.
 */
std::string zbMaskSecret(std::string text, const std::string& secret);

/**
 * A copy of text with the API key, raw, URL-encoded and JSON-escaped,
 * replaced by ***. Nothing is replaced for an empty key.
 */
std::string zbRedactApiKey(std::string text, const std::string& apiKey);

/**
 * The outcome of one request, handed to the caller's callbacks only after the
 * SDK's own try/catch has finished. That catch is for transport, parse and file
 * errors: an exception thrown by the caller's success or error callback reaches
 * the caller unchanged instead of being reported as an API error, and an error
 * callback never runs after the success callback.
 */
template <typename T>
class ZBOutcome {
public:
    /**
     * apiKey is the key the request was sent with. Every error deliver hands
     * over has it taken out, whatever built the message: a body quoting the
     * key or the request URL, a transport error or an exception.
     */
    explicit ZBOutcome(std::string apiKey) : apiKey_(std::move(apiKey)) {}

    /** An error callback for the SDK's helpers that records the error instead. */
    std::function<void(ZBErrorResponse)> errorSink() {
        return [this](ZBErrorResponse error) { fail(std::move(error)); };
    }

    /** Records an error. The first one is kept. */
    void fail(ZBErrorResponse error) {
        if (!error_) {
            error_ = std::move(error);
        }
    }

    void succeed(T value) { value_ = std::move(value); }

    /** Calls the error callback if an error was recorded, else the success one if a value was. */
    void deliver(
        const std::function<void(T)>& successCallback,
        const std::function<void(ZBErrorResponse)>& errorCallback
    ) {
        if (error_) {
            if (errorCallback) {
                for (auto& message : error_->errors) {
                    message = zbRedactApiKey(std::move(message), apiKey_);
                }
                errorCallback(std::move(*error_));
            }
        } else if (value_ && successCallback) {
            successCallback(std::move(*value_));
        }
    }

private:
    std::string apiKey_;
    std::optional<T> value_;
    std::optional<ZBErrorResponse> error_;
};

#endif
