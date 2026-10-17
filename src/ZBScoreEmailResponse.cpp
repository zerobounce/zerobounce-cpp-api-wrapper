#include <sstream>

#include "ZeroBounce/utils.h"
#include "ZeroBounce/ZBScoreEmailResponse.h"

std::string ZBScoreEmailResponse::toString() {
    std::stringstream stringStream;
    stringStream << "ZBScoreEmailResponse{"
        << "email='";
    if (email.has_value()) {
        stringStream << email.value();
    }
    stringStream << '\''
        << ", score=";
    if (score.has_value()) {
        stringStream << score.value();
    } else {
        stringStream << "null";
    }
    stringStream << '}';
    return stringStream.str();
}

static std::optional<int> parseScoreField(const json& j) {
    if (!j.contains("score") || j["score"].is_null()) {
        return std::nullopt;
    }
    if (j["score"].is_number_integer()) {
        return j["score"].get<int>();
    }
    if (j["score"].is_number()) {
        return static_cast<int>(j["score"].get<double>());
    }
    if (j["score"].is_string()) {
        return std::stoi(j["score"].get<std::string>());
    }
    return std::nullopt;
}

ZBScoreEmailResponse ZBScoreEmailResponse::from_json(const json& j) {
    ZBScoreEmailResponse r;

    if (j.contains("email") && !j["email"].is_null()) {
        r.email = j["email"].get<std::string>();
    }
    r.score = parseScoreField(j);

    return r;
}

bool ZBScoreEmailResponse::operator==(const ZBScoreEmailResponse& other) const {
    return email == other.email && score == other.score;
}
