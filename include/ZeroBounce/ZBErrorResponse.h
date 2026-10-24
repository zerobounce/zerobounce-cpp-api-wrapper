#ifndef ZBERRORRESPONSE_H
#define ZBERRORRESPONSE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

/**
 * The model used for when a request throws an error. This model was introduced in order to provide
 * a standardized way of handling the error responses that a request can return.
 *
 * If the error JSON received from the server includes the words "error" or "message", then the
 * values of those keys will be added to the [errors] array. If the error is not a JSON dictionary,
 * then JSON String will be added to the [errors] array.
 *
 * If any type of messages are received, then they will be added according to the same rule above
 * after the errors found above.
 */
class ZBErrorResponse {
    public:
        bool success = false;
        std::vector<std::string> errors;

        /**
         * The HTTP status of the failed response, or 0 when the failure never
         * reached the API. It lets callers tell a 403 from Cloudflare apart
         * from a 400/404 from the API and a 429 from the Warmup rate limit.
         */
        long statusCode = 0;

        ZBErrorResponse();
        ZBErrorResponse(bool success, std::vector<std::string> errors);
        ZBErrorResponse(bool success, std::vector<std::string> errors, long statusCode);

        std::string toString();

        static ZBErrorResponse parseError(std::string error);
        static ZBErrorResponse parseError(std::string error, long statusCode);

        bool operator==(const ZBErrorResponse& other) const;
};

#endif