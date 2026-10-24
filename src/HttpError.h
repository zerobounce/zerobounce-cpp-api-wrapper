#ifndef ZB_HTTP_ERROR_H
#define ZB_HTTP_ERROR_H

#include <functional>
#include <string>

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

#endif
