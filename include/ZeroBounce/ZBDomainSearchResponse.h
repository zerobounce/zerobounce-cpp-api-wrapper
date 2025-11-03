#ifndef ZB_DOMAIN_SEARCH_RESPONSE_H
#define ZB_DOMAIN_SEARCH_RESPONSE_H

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * @brief highlights how a domain can format its emails and how confident is
 * the API about it
 */
class ZBDomainFormat {

public:
    std::string format;
    std::string confidence;

    std::string toString();

    static ZBDomainFormat from_json(const json& json_obj);

    bool operator==(const ZBDomainFormat& other) const;
};

/**
 * @brief The class associated with the GET /guessformat request.
 *
 */
class ZBDomainSearchResponse {

public:
    std::string domain;
    std::string companyName;
    std::string format;
    std::string confidence;
    std::string didYouMean;
    std::string failureReason;
    std::vector<ZBDomainFormat> otherDomainFormats;

    std::string toString();

    static ZBDomainSearchResponse from_json(const json& json_obj);

    bool operator==(const ZBDomainSearchResponse& other) const;
};



#endif