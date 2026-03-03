#include <exception>
#include <sstream>
#include <nlohmann/json.hpp>

#include "ZeroBounce/ZBErrorResponse.h"

using json = nlohmann::json;

ZBErrorResponse::ZBErrorResponse() {}

ZBErrorResponse::ZBErrorResponse(bool success, std::vector<std::string> errors) :
        success(success), errors(errors) {}

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

    stringStream << "]}";

    return stringStream.str();
}

ZBErrorResponse ZBErrorResponse::parseError(std::string error) {
    ZBErrorResponse response;

    if (error.empty()) {
        return response;
    }

    std::vector<std::string> errors;
    std::vector<std::string> otherMessages;

    try {
        json hashMap = json::parse(error);
        for (auto& entry : hashMap.items()) {
            if (entry.key().find("error") != std::string::npos || entry.key().find("message") != std::string::npos) {
                if (entry.value().is_array()) {
                    std::vector<std::string> values = entry.value();
                    errors.insert(errors.end(), values.begin(), values.end());
                } else if (!entry.value().is_null()) {
                    errors.push_back(entry.value().get<std::string>());
                }
            } else {
                if (entry.key() == "success" && entry.value().is_boolean()) {
                    response.success = entry.value().get<bool>();
                } else if (entry.value().is_array()) {
                    std::vector<std::string> values = entry.value();
                    otherMessages.insert(otherMessages.end(), values.begin(), values.end());
                } else if (!entry.value().is_null()) {
                    otherMessages.push_back(entry.value().get<std::string>());
                }
            }
        }

        errors.insert(errors.end(), otherMessages.begin(), otherMessages.end());

    } catch (const std::exception& e) {
        errors.push_back(error);
    }

    response.errors = errors;
    return response;
}

bool ZBErrorResponse::operator==(const ZBErrorResponse& other) const {
    return success == other.success &&
        errors == other.errors;
}