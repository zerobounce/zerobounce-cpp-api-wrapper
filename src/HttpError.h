#ifndef ZB_HTTP_ERROR_H
#define ZB_HTTP_ERROR_H

#include <functional>
#include <optional>
#include <string>
#include <utility>

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
 * On HTTP failure, HTML, an empty body, or a body that is not JSON, reports a short
 * error and returns false. On a JSON body with status < 300, writes it to `out`.
 */
bool zbReadJsonResponse(
    long statusCode,
    const std::string& body,
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
    long statusCode,
    const std::string& body,
    json& out,
    const std::function<void(ZBErrorResponse)>& errorCallback
);

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
                errorCallback(std::move(*error_));
            }
        } else if (value_ && successCallback) {
            successCallback(std::move(*value_));
        }
    }

private:
    std::optional<T> value_;
    std::optional<ZBErrorResponse> error_;
};

#endif
