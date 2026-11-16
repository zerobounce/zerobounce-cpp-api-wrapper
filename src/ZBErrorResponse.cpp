#include <algorithm>
#include <cctype>
#include <exception>
#include <sstream>
#include <nlohmann/json.hpp>

#include "ZeroBounce/ZBErrorResponse.h"
#include "ZeroBounce/utils.h"

using json = nlohmann::json;

ZBErrorResponse::ZBErrorResponse() {}

ZBErrorResponse::ZBErrorResponse(bool success, std::vector<std::string> errors) :
        success(success), errors(errors) {}

ZBErrorResponse::ZBErrorResponse(bool success, std::vector<std::string> errors, long statusCode) :
        success(success), errors(errors), statusCode(statusCode) {}

std::string ZBErrorResponse::toString()
{
    std::stringstream stringStream;
    stringStream << "ZBErrorResponse{" <<
                "success=" << success <<
                ", errors=[";

    for (auto it = errors.begin(); it != errors.end(); ++it) {
        stringStream << '\'' << *it << '\'';
        if (std::next(it) != errors.end()) {
            stringStream << ", ";
        }
    }

    stringStream << "], statusCode=" << statusCode << "}";

    return stringStream.str();
}

ZBErrorResponse ZBErrorResponse::parseError(std::string error) {
    ZBErrorResponse response;

    if (error.empty()) {
        return response;
    }

    std::vector<std::string> errors;

    try {
        json hashMap = json::parse(error);
        for (auto& entry : hashMap.items()) {
            std::string key = entry.key();
            std::transform(key.begin(), key.end(), key.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (key.find("error") != std::string::npos || key.find("message") != std::string::npos) {
                if (entry.value().is_array()) {
                    std::vector<std::string> values = entry.value();
                    errors.insert(errors.end(), values.begin(), values.end());
                } else if (!entry.value().is_null()) {
                    errors.push_back(entry.value().get<std::string>());
                }
            } else {
                if (key == "success" && zbSuccessFlag(entry.value()) >= 0) {
                    response.success = zbSuccessFlag(entry.value()) == 1;
                } else if (key == "success") {
                    // not a flag and not a result field
                    continue;
                }
            }
        }

        // Only a message or error key gives the text. A body without one (an
        // empty object, an unreadable "success", other fields) has no
        // message: other fields' values are not an error text.
        errors.erase(std::remove(errors.begin(), errors.end(), std::string()), errors.end());
        if (errors.empty()) {
            errors.push_back("ZeroBounce returned an error with no message text.");
        }

    } catch (const std::exception& e) {
        errors.push_back(error);
    }

    response.errors = errors;
    return response;
}

ZBErrorResponse ZBErrorResponse::parseError(std::string error, long statusCode) {
    ZBErrorResponse response = parseError(std::move(error));
    response.statusCode = statusCode;
    return response;
}

bool ZBErrorResponse::operator==(const ZBErrorResponse& other) const {
    return success == other.success &&
        errors == other.errors;
}