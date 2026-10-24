#ifndef ZBSCOREEMAILRESPONSE_H
#define ZBSCOREEMAILRESPONSE_H

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * Response for GET/POST /scoring (real-time AI scoring).
 */
class ZBScoreEmailResponse {
    public:
        std::optional<std::string> email;
        std::optional<int> score;

        std::string toString();

        static ZBScoreEmailResponse from_json(const json& j);

        bool operator==(const ZBScoreEmailResponse& other) const;
};

#endif
