#ifndef ZBVALIDATESUBSTATUS_H
#define ZBVALIDATESUBSTATUS_H

#include "nlohmann/json.hpp"

/**
 * Enum that lists all the possible sub-statuses of the email validation result.
 */
enum ZBValidateSubStatus {
    None,
    AntispamSystem,
    Greylisted,
    MailServerTemporaryError,
    ForcibleDisconnect,
    MailServerDidNotRespond,
    TimeoutExceeded,
    FailedSmtpConnection,
    MailboxQuotaExceeded,
    ExceptionOccurred,
    PossibleTrap,
    RoleBased,
    GlobalSuppression,
    MailboxNotFound,
    NoDnsEntries,
    FailedSyntaxCheck,
    PossibleTypo,
    UnroutableIpAddress,
    LeadingPeriodRemoved,
    DoesNotAcceptMail,
    AliasAddress,
    RoleBasedCatchAll,
    Disposable,
    Toxic,
    AcceptAll
};

NLOHMANN_JSON_SERIALIZE_ENUM(
    ZBValidateSubStatus,
    {
        {ZBValidateSubStatus::None, ""},
        {ZBValidateSubStatus::AntispamSystem, "antispam_system"},
        {ZBValidateSubStatus::Greylisted, "greylisted"},
        {ZBValidateSubStatus::MailServerTemporaryError, "mail_server_temporary_error"},
        {ZBValidateSubStatus::ForcibleDisconnect, "forcible_disconnect"},
        {ZBValidateSubStatus::MailServerDidNotRespond, "mail_server_did_not_respond"},
        {ZBValidateSubStatus::TimeoutExceeded, "timeout_exceeded"},
        {ZBValidateSubStatus::FailedSmtpConnection, "failed_smtp_connection"},
        {ZBValidateSubStatus::MailboxQuotaExceeded, "mailbox_quota_exceeded"},
        {ZBValidateSubStatus::ExceptionOccurred, "exception_occurred"},
        {ZBValidateSubStatus::PossibleTrap, "possible_trap"},
        {ZBValidateSubStatus::RoleBased, "role_based"},
        {ZBValidateSubStatus::GlobalSuppression, "global_suppression"},
        {ZBValidateSubStatus::MailboxNotFound, "mailbox_not_found"},
        {ZBValidateSubStatus::NoDnsEntries, "no_dns_entries"},
        {ZBValidateSubStatus::FailedSyntaxCheck, "failed_syntax_check"},
        {ZBValidateSubStatus::PossibleTypo, "possible_typo"},
        {ZBValidateSubStatus::UnroutableIpAddress, "unroutable_ip_address"},
        {ZBValidateSubStatus::LeadingPeriodRemoved, "leading_period_removed"},
        {ZBValidateSubStatus::DoesNotAcceptMail, "does_not_accept_mail"},
        {ZBValidateSubStatus::AliasAddress, "alias_address"},
        {ZBValidateSubStatus::RoleBasedCatchAll, "role_based_catch_all"},
        {ZBValidateSubStatus::Disposable, "disposable"},
        {ZBValidateSubStatus::Toxic, "toxic"},
        {ZBValidateSubStatus::AcceptAll, "accept_all"}
    }
)

#endif