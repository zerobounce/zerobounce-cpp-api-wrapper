#include <sstream>

#include "ZeroBounce/utils.h"
#include "ZeroBounce/ZBValidateResponse.h"

std::string ZBValidateResponse::toString(bool isBatch)
{
    std::stringstream stringStream;

    if (!isBatch) {
        stringStream << "ZBValidateResponse";
    }

    stringStream << "{address='" << address << '\'' <<
                ", status=" << status <<
                ", subStatus=" << subStatus <<
                ", freeEmail=" << freeEmail <<
                ", didYouMean='" << didYouMean << '\'' <<
                ", account='" << account << '\'' <<
                ", domain='" << domain << '\'' <<
                ", domainAgeDays='" << domainAgeDays << '\'' <<
                ", smtpProvider='" << smtpProvider << '\'' <<
                ", catchallDomain=" << (catchallDomain.has_value() ? (catchallDomain.value() ? "true" : "false") : "null") <<
                ", mxFound=" << mxFound <<
                ", mxRecord='" << mxRecord << '\'' <<
                ", firstName='" << firstName << '\'' <<
                ", lastName='" << lastName << '\'' <<
                ", gender='" << gender << '\'' <<
                ", country='" << country << '\'' <<
                ", region='" << region << '\'' <<
                ", city='" << city << '\'' <<
                ", zipCode='" << zipCode << '\'' <<
                ", processedAt='" << processedAt << '\'';

    if (!isBatch) {
        stringStream << ", error='" << error << '\'';
    }

    stringStream << '}';

    return stringStream.str();
}

ZBValidateResponse ZBValidateResponse::from_json(const json& j) {
    ZBValidateResponse r;

    r.address = getOrDefault<std::string>(j, "address", "");
    r.status = getOrDefault<ZBValidateStatus>(j, "status", ZBValidateStatus::UnknownValue);
    r.subStatus = getOrDefault<ZBValidateSubStatus>(j, "sub_status", ZBValidateSubStatus::None);
    r.freeEmail = getOrDefault<bool>(j, "free_email", false);
    r.didYouMean = getOrDefault<std::string>(j, "did_you_mean", "");
    r.account = getOrDefault<std::string>(j, "account", "");
    r.domain = getOrDefault<std::string>(j, "domain", "");
    r.domainAgeDays = getOrDefault<std::string>(j, "domain_age_days", "");
    r.smtpProvider = getOrDefault<std::string>(j, "smtp_provider", "");

    if (j.contains("catchall_domain") && !j["catchall_domain"].is_null()) {
        r.catchallDomain = j["catchall_domain"].get<bool>();
    }

    bool mxFound;
    std::istringstream ss(getOrDefault<std::string>(j, "mx_found", "false"));
    ss >> std::boolalpha >> mxFound;
    r.mxFound = mxFound;

    r.mxRecord = getOrDefault<std::string>(j, "mx_record", "");
    r.firstName = getOrDefault<std::string>(j, "firstname", "");
    r.lastName = getOrDefault<std::string>(j, "lastname", "");
    r.gender = getOrDefault<std::string>(j, "gender", "");
    r.country = getOrDefault<std::string>(j, "country", "");
    r.region = getOrDefault<std::string>(j, "region", "");
    r.city = getOrDefault<std::string>(j, "city", "");
    r.zipCode = getOrDefault<std::string>(j, "zipcode", "");
    r.processedAt = getOrDefault<std::string>(j, "processed_at", "");
    r.error = getOrDefault<std::string>(j, "error", "");

    return r;
}

bool ZBValidateResponse::operator==(const ZBValidateResponse& other) const {
    return address == other.address &&
        status == other.status;
        subStatus == other.subStatus;
        account == other.account &&
        domain == other.domain &&
        didYouMean == other.didYouMean &&
        domainAgeDays == other.domainAgeDays &&
        freeEmail == other.freeEmail &&
        mxFound == other.mxFound &&
        mxRecord == other.mxRecord &&
        smtpProvider == other.smtpProvider &&
        catchallDomain == other.catchallDomain &&
        firstName == other.firstName &&
        lastName == other.lastName &&
        gender == other.gender &&
        city == other.city &&
        region == other.region &&
        zipCode == other.zipCode &&
        country == other.country &&
        processedAt == other.processedAt &&
        error == other.error;
}
