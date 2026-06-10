#ifndef ZBVALIDATERESPONSE_H
#define ZBVALIDATERESPONSE_H

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "ZeroBounce/ZBValidateStatus.h"
#include "ZeroBounce/ZBValidateSubStatus.h"

using json = nlohmann::json;

/**
 * The model used for the GET /validate request.
 */
class ZBValidateResponse {
    public:
        std::string address;
        ZBValidateStatus status;
        ZBValidateSubStatus subStatus;
        std::string account;
        std::string domain;
        std::string didYouMean;
        std::string domainAgeDays;
        bool freeEmail = false;
        bool mxFound = false;
        std::string mxRecord;
        std::string smtpProvider;
        std::optional<bool> catchallDomain;
        std::string firstName;
        std::string lastName;
        std::string gender;
        std::string city;
        std::string region;
        std::string zipCode;
        std::string country;
        std::string processedAt;
        std::string error;

        std::string toString(bool isBatch = false);

        static ZBValidateResponse from_json(const json& j);

        bool operator==(const ZBValidateResponse& other) const;
};

#endif