#ifndef ZEROBOUNCEWARMUP_H
#define ZEROBOUNCEWARMUP_H

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ZeroBounce/ZeroBounce.h"

/** What ZeroBounce should send on your behalf, for zb_managed onboarding. */
struct WarmupEmailContent {
    /** Any of "transactional", "newsletter", "marketing". */
    std::vector<std::string> typesOfEmails;
    std::optional<std::string> industry;
    std::optional<std::string> companyName;
    std::optional<std::string> website;
};

/** When ZeroBounce should send, for zb_managed onboarding. */
struct WarmupManagedSending {
    /** -11 to 14. */
    std::optional<int> timezoneOffset;
    /** YYYY-MM-DD. */
    std::optional<std::string> warmupStartDate;
    std::optional<int> dailyLimit;
};

/** Options for addSendingAccount. */
struct WarmupAddSendingAccountOptions {
    /** Required for "self_managed": 0 Default, 1 Slow Pace, 2 Repair, 3 Boost, 4 Protect. */
    std::optional<int> engagementRuleId;
    std::optional<bool> ignoreConflict;
    /** zb_managed only. */
    std::optional<WarmupEmailContent> emailContent;
    /** zb_managed only. */
    std::optional<WarmupManagedSending> managedSending;
};

/** Paging and filtering for listFromAddresses. */
struct WarmupListFromAddressesOptions {
    /** Letters, digits and . - _ @ + only. */
    std::optional<std::string> searchFrom;
    std::optional<int> offset;
    std::optional<int> limit;
};

/**
 * Client for the ZeroBounce Warmup API (https://www.zerobounce.net/docs/warmup-api).
 *
 * The Warmup API is separate from the email validation API: its own host, its
 * own team-scoped key, and an "API-Key" header instead of an api_key
 * parameter. Use ZeroBounceWarmup with a Warmup API key; ZeroBounce stays for
 * the validation API key.
 *
 * Every method answers through a callback, like ZeroBounce. Failures carry the
 * HTTP status on ZBErrorResponse::statusCode.
 */
class ZeroBounceWarmup {
    protected:
        std::unique_ptr<BaseRequestHandler> defaultRequestHandler;
        BaseRequestHandler* requestHandler = nullptr;

    private:
        std::string apiKey;
        std::string root;
        bool ready = false;
        std::string initializeError;

    public:
        static constexpr const char* DefaultBaseUrl = "https://warmup-api.zerobounce.net";
        static constexpr const char* BasePath = "/api/v1/warmup";
        /** Required by the schema but always replaced with the key's own team. */
        static constexpr int AnyTeamId = 1;

        ZeroBounceWarmup();
        virtual ~ZeroBounceWarmup() = default;

        /**
         * Store the Warmup API key, and optionally a custom https base URL.
         * A key with surrounding whitespace or control characters, or a base
         * URL that is not https, is rejected here: every later call then fails
         * with that message rather than sending the key anywhere.
         *
         * The rejection is not silent. `getInitializeError()` returns the
         * reason straight away, so a bad base URL can be caught at the point
         * of the mistake instead of on the first request.
         */
        void initialize(const std::string& apiKey, const std::string& baseUrl = DefaultBaseUrl);

        /** True when `initialize` accepted the key and the base URL. */
        bool isReady() const { return ready; }

        /**
         * Why the last `initialize` was rejected, or empty when it was accepted.
         * Never contains the API key.
         */
        const std::string& getInitializeError() const { return initializeError; }

        void setRequestHandler(BaseRequestHandler* handler) { requestHandler = handler; }
        BaseRequestHandler* getRequestHandler() { return requestHandler; }
        const std::string& getApiKey() const { return apiKey; }

        // -- sending accounts ------------------------------------------------

        /** Every sending account of the key's team. GET /sending-accounts */
        void listSendingAccounts(OnRawJsonSuccessCallback, OnErrorCallback);

        /** One sending account. GET /sending-account */
        void getSendingAccount(const std::string& fromAddress, OnRawJsonSuccessCallback, OnErrorCallback);

        /**
         * Add a from-address to warmup. POST /sending-accounts
         *
         * "self_managed" runs your own mailbox and needs engagementRuleId 0-4.
         * "zb_managed" has ZeroBounce provision it and takes emailContent and
         * managedSending; a 200 means accepted, and the mailbox is provisioned
         * afterwards.
         */
        void addSendingAccount(
            const std::string& fromAddress,
            const std::string& onboardingModel,
            const WarmupAddSendingAccountOptions& options,
            OnRawJsonSuccessCallback,
            OnErrorCallback
        );

        /**
         * Change an account's engagement rule, status and auto-enable flag.
         * PUT /sending-accounts. statusId is 0 to 8 (4 Paused, 5 Disabled).
         */
        void updateSendingAccount(
            const std::string& fromAddress,
            int engagementRuleId,
            int statusId,
            bool autoEnable,
            OnRawJsonSuccessCallback,
            OnErrorCallback
        );

        /** Remove a sending account. DELETE /sending-accounts. Destructive and final. */
        void deleteSendingAccount(const std::string& fromAddress, OnRawJsonSuccessCallback, OnErrorCallback);

        /**
         * Move an account and its history to a new from-address.
         * PUT /smtp-credentials - despite the path, no SMTP credentials are stored.
         */
        void changeFromAddress(
            const std::string& oldFromAddress,
            const std::string& newFromAddress,
            OnRawJsonSuccessCallback,
            OnErrorCallback
        );

        /** Registered from-addresses. GET /from-addresses */
        void listFromAddresses(
            const WarmupListFromAddressesOptions& options,
            OnRawJsonSuccessCallback,
            OnErrorCallback
        );

        // -- engagement rules ------------------------------------------------

        /** -1 Custom, 0 Default, 1 Slow Pace, 2 Repair, 3 Boost, 4 Protect. */
        void getEngagementRules(OnRawJsonSuccessCallback, OnErrorCallback);

        /** Set a standard rule (0 to 4) on an address. PUT /engagement-rule */
        void setEngagementRule(const std::string& cFrom, int ruleId, OnRawJsonSuccessCallback, OnErrorCallback);

        /** An address's custom rule, with can_update and remaining_seconds. */
        void getCustomEngagement(const std::string& cFrom, OnRawJsonSuccessCallback, OnErrorCallback);

        /** Create a custom rule. Starts a 7 day cooldown on updates. */
        void createCustomEngagement(
            const std::string& cFrom, int replyPct, int importantPct, int clickPct,
            OnRawJsonSuccessCallback, OnErrorCallback
        );

        /** Update a custom rule. During the 7 day cooldown the API answers 429. */
        void updateCustomEngagement(
            const std::string& cFrom, int replyPct, int importantPct, int clickPct,
            OnRawJsonSuccessCallback, OnErrorCallback
        );

        /** Delete a custom rule and fall back to standard rule 1-4. */
        void deleteCustomEngagement(
            const std::string& cFrom, int engagementRuleId,
            OnRawJsonSuccessCallback, OnErrorCallback
        );

        // -- statistics, seeds and utilities ---------------------------------

        /** The seed list. GET /seeds */
        void getSeeds(OnRawJsonSuccessCallback, OnErrorCallback);

        /** The seed list as CSV (columns seed,provider). GET /seeds/download */
        void downloadSeeds(OnSuccessCallback<std::string>, OnErrorCallback);

        /** Number of seeds. GET /seed-count */
        void getSeedCount(OnRawJsonSuccessCallback, OnErrorCallback);

        /** Today's warmup progress. GET /progress-today */
        void getProgressToday(OnRawJsonSuccessCallback, OnErrorCallback);

        /** Messages sent today. GET /sent-today */
        void getSentToday(OnRawJsonSuccessCallback, OnErrorCallback);

        /** Current daily volume. GET /emails-per-day */
        void getEmailsPerDay(OnRawJsonSuccessCallback, OnErrorCallback);

        /** Statistics for an address. An address with no history returns zeros. */
        void getStatistics(const std::string& fromAddress, OnRawJsonSuccessCallback, OnErrorCallback);

        /** Daily statistics between two dates, inclusive. GET /statistics-per-day */
        void getStatisticsPerDay(
            const std::string& fromAddress,
            const std::string& startDate,
            const std::string& endDate,
            OnRawJsonSuccessCallback,
            OnErrorCallback
        );

        /** Detailed statistics for an address. GET /statistics-detailed */
        void getStatisticsDetailed(const std::string& cFrom, OnRawJsonSuccessCallback, OnErrorCallback);

        /** An address's statistics as CSV. GET /statistics/download */
        void downloadStatistics(const std::string& cFrom, OnSuccessCallback<std::string>, OnErrorCallback);

        /** Statistics for one from-address. GET /from-address-statistics */
        void getFromAddressStatistics(const std::string& fromAddress, OnRawJsonSuccessCallback, OnErrorCallback);

        /**
         * SPF, DKIM and DMARC status of a from-address's domain.
         * GET /check-domain-dmarc
         *
         * LIVE: pass an EMAIL ADDRESS. The live API answers "Please provide a
         * valid email address" for a bare domain, although the docs describe
         * a domain.
         */
        void checkDomainDmarc(
            const std::string& fromAddress,
            const std::optional<std::string>& dkimSelector,
            OnRawJsonSuccessCallback,
            OnErrorCallback
        );

        /**
         * Check that SMTP credentials connect and hand off a message.
         * POST /test-smtp-configuration
         *
         * Nothing is stored, and the test message goes to a ZeroBounce test
         * recipient, not fromAddress.
         *
         * LIVE: the API answers any failed SMTP login with HTTP 500. That is an
         * API bug. The username and password are masked in every error here.
         */
        void testSmtpConfiguration(
            const std::string& fromAddress,
            const std::string& username,
            const std::string& password,
            const std::string& server,
            int port,
            OnRawJsonSuccessCallback,
            OnErrorCallback
        );
};

#endif
