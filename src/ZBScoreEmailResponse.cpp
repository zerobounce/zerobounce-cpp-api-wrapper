#include <cstdint>
#include <limits>
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
    // A score is an integer, or the API's string form of one: a fraction
    // (5.5, "5.5") or other text ("abc", "9 ") is not read as a score.
    if (j["score"].is_number_integer()) {
        const json& value = j["score"];
        if (value.is_number_unsigned()
                ? value.get<std::uint64_t>() <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())
                : (value.get<std::int64_t>() >= std::numeric_limits<int>::min() &&
                   value.get<std::int64_t>() <= std::numeric_limits<int>::max())) {
            return static_cast<int>(value.get<std::int64_t>());
        }
    } else if (j["score"].is_string()) {
        const std::string text = j["score"].get<std::string>();
        const size_t digits = (!text.empty() && text[0] == '-') ? 1 : 0;
        if (digits < text.size() && text.find_first_not_of("0123456789", digits) == std::string::npos) {
            try {
                return std::stoi(text);
            } catch (const std::out_of_range&) {
            }
        }
    }
    throw ZBJsonFieldTypeError("score");
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
