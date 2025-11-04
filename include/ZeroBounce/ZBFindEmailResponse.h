#ifndef ZB_FIND_EMAIL_RESPONSE_H
#define ZB_FIND_EMAIL_RESPONSE_H

#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * @brief The class associated with the GET /guessformat request.
 *
 */
class ZBFindEmailResponse {

public:
    std::string email;
    std::string emailConfidence;
    std::string domain;
    std::string companyName;
    std::string didYouMean;
    std::string failureReason;

    std::string toString();

    static ZBFindEmailResponse from_json(const json& json_obj);

    bool operator==(const ZBFindEmailResponse& other) const;
};



#endif