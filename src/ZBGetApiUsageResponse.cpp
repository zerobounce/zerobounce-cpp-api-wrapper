#include <sstream>

#include "ZeroBounce/utils.h"
#include "ZeroBounce/ZBGetApiUsageResponse.h"

std::string ZBGetApiUsageResponse::toString()
{
    std::stringstream stringStream;
    stringStream << "ZBGetApiUsageResponse{" <<
                "total=" << total <<
                ", statusValid=" << statusValid <<
                ", statusInvalid=" << statusInvalid <<
                ", statusCatchAll=" << statusCatchAll <<
                ", statusDoNotMail=" << statusDoNotMail <<
                ", statusSpamtrap=" << statusSpamtrap <<
                ", statusUnknown=" << statusUnknown <<
                ", subStatusToxic=" << subStatusToxic <<
                ", subStatusDisposable=" << subStatusDisposable <<
                ", subStatusRoleBased=" << subStatusRoleBased <<
                ", subStatusPossibleTrap=" << subStatusPossibleTrap <<
                ", subStatusGlobalSuppression=" << subStatusGlobalSuppression <<
                ", subStatusTimeoutExceeded=" << subStatusTimeoutExceeded <<
                ", subStatusMailServerTemporaryError=" << subStatusMailServerTemporaryError <<
                ", subStatusMailServerDidNotRespond=" << subStatusMailServerDidNotRespond <<
                ", subStatusGreyListed=" << subStatusGreyListed <<
                ", subStatusAntiSpamSystem=" << subStatusAntiSpamSystem <<
                ", subStatusDoesNotAcceptMail=" << subStatusDoesNotAcceptMail <<
                ", subStatusExceptionOccurred=" << subStatusExceptionOccurred <<
                ", subStatusFailedSyntaxCheck=" << subStatusFailedSyntaxCheck <<
                ", subStatusMailboxNotFound=" << subStatusMailboxNotFound <<
                ", subStatusUnRoutableIpAddress=" << subStatusUnroutableIpAddress <<
                ", subStatusPossibleTypo=" << subStatusPossibleTypo <<
                ", subStatusNoDnsEntries=" << subStatusNoDnsEntries <<
                ", subStatusRoleBasedCatchAll=" << subStatusRoleBasedCatchAll <<
                ", subStatusMailboxQuotaExceeded=" << subStatusMailboxQuotaExceeded <<
                ", subStatusForcibleDisconnect=" << subStatusForcibleDisconnect <<
                ", subStatusFailedSmtpConnection=" << subStatusFailedSmtpConnection <<
                ", subStatusAcceptAll=" << subStatusAcceptAll <<
                ", subStatusMxForward=" << subStatusMxForward <<
                ", subStatusAlternate=" << subStatusAlternate <<
                ", subStatusBlocked=" << subStatusBlocked <<
                ", subStatusAllowed=" << subStatusAllowed <<
                ", subStatusGold=" << subStatusGold <<
                ", subStatusRoleBasedAcceptAll=" << subStatusRoleBasedAcceptAll <<
                ", startDate='" << startDate << '\'' <<
                ", endDate='" << endDate << '\'' <<
                ", error='" << error << '\'' <<
                '}';

    return stringStream.str();
}

ZBGetApiUsageResponse ZBGetApiUsageResponse::from_json(const json& j) {
    ZBGetApiUsageResponse r;

    r.total = getOrDefault<int>(j, "total", 0);
    r.statusValid = getOrDefault<int>(j, "status_valid", 0);
    r.statusInvalid = getOrDefault<int>(j, "status_invalid", 0);
    r.statusCatchAll = getOrDefault<int>(j, "status_catch_all", 0);
    r.statusDoNotMail = getOrDefault<int>(j, "status_do_not_mail", 0);
    r.statusSpamtrap = getOrDefault<int>(j, "status_spamtrap", 0);
    r.statusUnknown = getOrDefault<int>(j, "status_unknown", 0);
    r.subStatusToxic = getOrDefault<int>(j, "sub_status_toxic", 0);
    r.subStatusDisposable = getOrDefault<int>(j, "sub_status_disposable", 0);
    r.subStatusRoleBased = getOrDefault<int>(j, "sub_status_role_based", 0);
    r.subStatusPossibleTrap = getOrDefault<int>(j, "sub_status_possible_trap", 0);
    r.subStatusGlobalSuppression = getOrDefault<int>(j, "sub_status_global_suppression", 0);
    r.subStatusTimeoutExceeded = getOrDefault<int>(j, "sub_status_timeout_exceeded", 0);
    r.subStatusMailServerTemporaryError = getOrDefault<int>(j, "sub_status_mail_server_temporary_error", 0);
    r.subStatusMailServerDidNotRespond = getOrDefault<int>(j, "sub_status_mail_server_did_not_respond", 0);
    r.subStatusGreyListed = getOrDefault<int>(j, "sub_status_greylisted", 0);
    r.subStatusAntiSpamSystem = getOrDefault<int>(j, "sub_status_antispam_system", 0);
    r.subStatusDoesNotAcceptMail = getOrDefault<int>(j, "sub_status_does_not_accept_mail", 0);
    r.subStatusExceptionOccurred = getOrDefault<int>(j, "sub_status_exception_occurred", 0);
    r.subStatusFailedSyntaxCheck = getOrDefault<int>(j, "sub_status_failed_syntax_check", 0);
    r.subStatusMailboxNotFound = getOrDefault<int>(j, "sub_status_mailbox_not_found", 0);
    r.subStatusUnroutableIpAddress = getOrDefault<int>(j, "sub_status_unroutable_ip_address", 0);
    r.subStatusPossibleTypo = getOrDefault<int>(j, "sub_status_possible_typo", 0);
    r.subStatusNoDnsEntries = getOrDefault<int>(j, "sub_status_no_dns_entries", 0);
    r.subStatusRoleBasedCatchAll = getOrDefault<int>(j, "sub_status_role_based_catch_all", 0);
    r.subStatusMailboxQuotaExceeded = getOrDefault<int>(j, "sub_status_mailbox_quota_exceeded", 0);
    r.subStatusForcibleDisconnect = getOrDefault<int>(j, "sub_status_forcible_disconnect", 0);
    r.subStatusFailedSmtpConnection = getOrDefault<int>(j, "sub_status_failed_smtp_connection", 0);
    r.subStatusAcceptAll = getOrDefault<int>(j, "sub_status_accept_all", 0);
    r.subStatusMxForward = getOrDefault<int>(j, "sub_status_mx_forward", 0);
    r.subStatusAlternate = getOrDefault<int>(j, "sub_status_alternate", 0);
    r.subStatusBlocked = getOrDefault<int>(j, "sub_status_blocked", 0);
    r.subStatusAllowed = getOrDefault<int>(j, "sub_status_allowed", 0);
    r.subStatusGold = getOrDefault<int>(j, "sub_status_gold", 0);
    r.subStatusRoleBasedAcceptAll = getOrDefault<int>(j, "sub_status_role_based_accept_all", 0);
    r.startDate = getOrDefault<std::string>(j, "start_date", "");
    r.endDate = getOrDefault<std::string>(j, "end_date", "");
    r.error = getOrDefault<std::string>(j, "error", "");

    return r;
}

bool ZBGetApiUsageResponse::operator==(const ZBGetApiUsageResponse& other) const {
    return total == other.total &&
        statusValid == other.statusValid &&
        statusInvalid == other.statusInvalid &&
        statusCatchAll == other.statusCatchAll &&
        statusDoNotMail == other.statusDoNotMail &&
        statusSpamtrap == other.statusSpamtrap &&
        statusUnknown == other.statusUnknown &&
        subStatusToxic == other.subStatusToxic &&
        subStatusDisposable == other.subStatusDisposable &&
        subStatusRoleBased == other.subStatusRoleBased &&
        subStatusPossibleTrap == other.subStatusPossibleTrap &&
        subStatusGlobalSuppression == other.subStatusGlobalSuppression &&
        subStatusTimeoutExceeded == other.subStatusTimeoutExceeded &&
        subStatusMailServerTemporaryError == other.subStatusMailServerTemporaryError &&
        subStatusMailServerDidNotRespond == other.subStatusMailServerDidNotRespond &&
        subStatusGreyListed == other.subStatusGreyListed &&
        subStatusAntiSpamSystem == other.subStatusAntiSpamSystem &&
        subStatusDoesNotAcceptMail == other.subStatusDoesNotAcceptMail &&
        subStatusExceptionOccurred == other.subStatusExceptionOccurred &&
        subStatusFailedSyntaxCheck == other.subStatusFailedSyntaxCheck &&
        subStatusMailboxNotFound == other.subStatusMailboxNotFound &&
        subStatusUnroutableIpAddress == other.subStatusUnroutableIpAddress &&
        subStatusPossibleTypo == other.subStatusPossibleTypo &&
        subStatusNoDnsEntries == other.subStatusNoDnsEntries &&
        subStatusRoleBasedCatchAll == other.subStatusRoleBasedCatchAll &&
        subStatusMailboxQuotaExceeded == other.subStatusMailboxQuotaExceeded &&
        subStatusForcibleDisconnect == other.subStatusForcibleDisconnect &&
        subStatusFailedSmtpConnection == other.subStatusFailedSmtpConnection &&
        subStatusAcceptAll == other.subStatusAcceptAll &&
        subStatusMxForward == other.subStatusMxForward &&
        subStatusAlternate == other.subStatusAlternate &&
        subStatusBlocked == other.subStatusBlocked &&
        subStatusAllowed == other.subStatusAllowed &&
        subStatusGold == other.subStatusGold &&
        subStatusRoleBasedAcceptAll == other.subStatusRoleBasedAcceptAll &&
        startDate == other.startDate &&
        endDate == other.endDate &&
        error == other.error;
}