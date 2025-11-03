#include "ZeroBounce/utils.h"
#include "ZeroBounce/ZBDomainSearchResponse.h"

std::string ZBDomainFormat::toString() {
    std::stringstream stringStream;
    stringStream << "ZBDomainSearchResponse{ " <<
        "confidence=" << '"' << this->confidence << '"' << ", "
        "format=" << '"' << this->format << '"' << "}";

    return stringStream.str();
}

ZBDomainFormat ZBDomainFormat::from_json(const json& json_obj) {
    ZBDomainFormat response;
    response.format = getOrDefault<std::string>(json_obj, "format", "");
    response.confidence = getOrDefault<std::string>(json_obj, "confidence", "");
    return response;
}

bool ZBDomainFormat::operator==(const ZBDomainFormat& other) const {
    return this->confidence == other.confidence && this->format == other.format;
}

std::string ZBDomainSearchResponse::toString() {
    std::stringstream stringStream;
    stringStream << "ZBDomainSearchResponse{ " <<
        "domain=" << '"' << this->domain << '"' << ", "
        "companyName=" << '"' << this->companyName << '"' << ", "
        "format=" << '"' << this->format << '"' << ", "
        "confidence=" << '"' << this->confidence << '"' << ", "
        "didYouMean=" << '"' << this->didYouMean << '"' << ", "
        "failureReason=" << '"' << this->failureReason << '"' << ", "
        "otherDomainFormats=" << '[';

    if (this->otherDomainFormats.size() > 0) {
        stringStream << this->otherDomainFormats[0].toString();
    }
    for (int index = 1; index < this->otherDomainFormats.size(); index ++) {
        stringStream << ", " << this->otherDomainFormats[index].toString();
    }
    stringStream << "] }";
    return stringStream.str();
}

ZBDomainSearchResponse ZBDomainSearchResponse::from_json(const json& json_obj) {
    ZBDomainSearchResponse response;
    response.domain = getOrDefault<std::string>(json_obj, "domain", "");
    response.companyName = getOrDefault<std::string>(json_obj, "company_name", "");
    response.format = getOrDefault<std::string>(json_obj, "format", "");
    response.confidence = getOrDefault<std::string>(json_obj, "confidence", "");
    response.didYouMean = getOrDefault<std::string>(json_obj, "did_you_mean", "");
    response.failureReason = getOrDefault<std::string>(json_obj, "failure_reason", "");

    response.otherDomainFormats = std::vector<ZBDomainFormat>();
    const json domainFormats = json_obj["other_domain_formats"];
    if (domainFormats.is_array()) {
        for (int index = 0; index < domainFormats.size(); index ++) {
            response.otherDomainFormats.push_back(
            ZBDomainFormat::from_json(domainFormats.at(index))
            );
        }
    }
    return response;
}

bool ZBDomainSearchResponse::operator==(const ZBDomainSearchResponse& other) const {
    bool fieldsAreEqual = this->domain == other.domain
        && this->companyName = other.companyName
        && this->format == other.format
        && this->confidence == other.confidence
        && this->didYouMean == other.didYouMean
        && this->failureReason == other.failureReason;
    if (!fieldsAreEqual) {
        return false;
    }
    if (this->otherDomainFormats.size() != other.otherDomainFormats.size()) {
        return false;
    }
    for (int index = 0; index < this->otherDomainFormats.size(); index ++) {
        if (this->otherDomainFormats[index] != other.otherDomainFormats[index]) {
            return false;
        }
    }
    return true;
}
