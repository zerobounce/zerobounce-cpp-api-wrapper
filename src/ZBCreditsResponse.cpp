#include <cstdint>
#include <limits>
#include <sstream>

#include "ZeroBounce/utils.h"
#include "ZeroBounce/ZBCreditsResponse.h"

std::string ZBCreditsResponse::toString()
{
    std::stringstream stringStream;
    stringStream << "ZBCreditsResponse{" <<
                "credits='" << credits << '\'' <<
                '}';

    return stringStream.str();
}

ZBCreditsResponse ZBCreditsResponse::from_json(const json& j) {
    ZBCreditsResponse r;

    // Credits is an integer, sent as a JSON string ("-1" for a dead key,
    // "12345") or as a JSON integer ({"Credits":2375323}, zerobounce.net's own
    // example). Missing, null, another JSON type (a fraction, a boolean), text
    // that is not an integer ("abc", "5.0", " 5": std::stoi would read "5.0"
    // as 5) or a value outside int is not an answer.
    if (j.is_object() && j.contains("Credits") && j["Credits"].is_number_integer()) {
        const json& value = j["Credits"];
        if (value.is_number_unsigned()) {
            const auto number = value.get<std::uint64_t>();
            if (number > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                throw ZBJsonFieldTypeError("Credits");
            }
            r.credits = static_cast<int>(number);
        } else {
            const auto number = value.get<std::int64_t>();
            if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
                throw ZBJsonFieldTypeError("Credits");
            }
            r.credits = static_cast<int>(number);
        }
        return r;
    }
    const std::string text = zbRequireString(j, "Credits");
    size_t digits = (!text.empty() && text[0] == '-') ? 1 : 0;
    if (digits == text.size() ||
        text.find_first_not_of("0123456789", digits) != std::string::npos) {
        throw ZBJsonFieldTypeError("Credits");
    }
    try {
        r.credits = std::stoi(text);
    } catch (const std::out_of_range&) {
        throw ZBJsonFieldTypeError("Credits");
    }

    return r;
}

bool ZBCreditsResponse::operator==(const ZBCreditsResponse& other) const {
    return credits == other.credits;
}