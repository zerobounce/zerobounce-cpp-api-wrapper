#ifndef ZBGETAPIUSAGERESPONSE_H
#define ZBGETAPIUSAGERESPONSE_H

#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * The model used for the GET /getapiusage request.
 */
class ZBGetApiUsageResponse {
    public:
        int total = 0;
        int statusValid = 0;
        int statusInvalid = 0;
        int statusCatchAll = 0;
        int statusDoNotMail = 0;
        int statusSpamtrap = 0;
        int statusUnknown = 0;
        int subStatusToxic = 0;
        int subStatusDisposable = 0;
        int subStatusRoleBased = 0;
        int subStatusPossibleTrap = 0;
        int subStatusGlobalSuppression = 0;
        int subStatusTimeoutExceeded = 0;
        int subStatusMailServerTemporaryError = 0;
        int subStatusMailServerDidNotRespond = 0;
        int subStatusGreyListed = 0;
        int subStatusAntiSpamSystem = 0;
        int subStatusDoesNotAcceptMail = 0;
        int subStatusExceptionOccurred = 0;
        int subStatusFailedSyntaxCheck = 0;
        int subStatusMailboxNotFound = 0;
        int subStatusUnroutableIpAddress = 0;
        int subStatusPossibleTypo = 0;
        int subStatusNoDnsEntries = 0;
        int subStatusRoleBasedCatchAll = 0;
        int subStatusMailboxQuotaExceeded = 0;
        int subStatusForcibleDisconnect = 0;
        int subStatusFailedSmtpConnection = 0;
        int subStatusAcceptAll = 0;
        int subStatusMxForward = 0;
        int subStatusAlternate = 0;
        int subStatusBlocked = 0;
        int subStatusAllowed = 0;
        int subStatusGold = 0;
        int subStatusRoleBasedAcceptAll = 0;
        std::string startDate;
        std::string endDate;
        std::string error;

        std::string toString();

        static ZBGetApiUsageResponse from_json(const json& j);

        bool operator==(const ZBGetApiUsageResponse& other) const;
};

#endif