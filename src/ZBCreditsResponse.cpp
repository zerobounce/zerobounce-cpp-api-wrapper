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

    // Credits is a JSON string holding an integer ("-1" for a dead key).
    // Missing, null, another JSON type, or text that is not an integer
    // ("abc", "5.0", " 5") is not an answer: std::stoi would read "5.0" as 5.
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