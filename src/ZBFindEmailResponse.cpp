#include "ZeroBounce/utils.h"
#include "ZeroBounce/ZBFindEmailResponse.h"
#include <sstream>

std::string ZBFindEmailResponse::toString() {
    std::stringstream stringStream;
    stringStream << "ZBFindEmailResponse{ " <<
        "email=" << '"' << this->email << '"' << ", "
        "emailConfidence=" << '"' << this->emailConfidence << '"' << ", "
        "domain=" << '"' << this->domain << '"' << ", "
        "companyName=" << '"' << this->companyName << '"' << ", "
        "didYouMean=" << '"' << this->didYouMean << '"' << ", "
        "failureReason=" << '"' << this->failureReason << '" }';
    return stringStream.str();
}

ZBFindEmailResponse ZBFindEmailResponse::from_json(const json& json_obj) {
    ZBFindEmailResponse response;
    response.email = getOrDefault<std::string>(json_obj, "email", "");
    response.emailConfidence = getOrDefault<std::string>(json_obj, "email_confidence", "");
    response.domain = getOrDefault<std::string>(json_obj, "domain", "");
    response.companyName = getOrDefault<std::string>(json_obj, "company_name", "");
    response.didYouMean = getOrDefault<std::string>(json_obj, "did_you_mean", "");
    response.failureReason = getOrDefault<std::string>(json_obj, "failure_reason", "");

    return response;
}

bool ZBFindEmailResponse::operator==(const ZBFindEmailResponse& other) const {
    bool fieldsAreEqual = this->email == other.email
        && this->emailConfidence == other.emailConfidence
        && this->domain == other.domain
        && this->companyName == other.companyName
        && this->didYouMean == other.didYouMean
        && this->failureReason == other.failureReason;
    if (!fieldsAreEqual) {
        return false;
    }
    return true;
}
