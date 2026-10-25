// Offline tests for the Warmup API client. A fake request handler records
// every request and answers without the network.

#include <algorithm>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <cpr/cpr.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "ZeroBounce/ZeroBounceWarmup.h"

using json = nlohmann::json;

namespace {

struct WarmupRequest {
    std::string method;
    std::string host;
    std::string path;
    std::string apiKey;
    std::map<std::string, std::string> query;
    std::string body;
};

std::string urlDecode(const std::string& value) {
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            out.push_back(static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else if (value[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

class FakeWarmupApi : public BaseRequestHandler {
public:
    std::vector<WarmupRequest> take() {
        std::lock_guard<std::mutex> lock(mu_);
        auto records = records_;
        records_.clear();
        return records;
    }

    size_t size() {
        std::lock_guard<std::mutex> lock(mu_);
        return records_.size();
    }

    void replyWith(long status, std::string contentType, std::string body) {
        std::lock_guard<std::mutex> lock(mu_);
        next_ = cpr::Response{};
        next_->status_code = status;
        next_->text = std::move(body);
        next_->header["Content-Type"] = std::move(contentType);
    }

protected:
    cpr::Response doGet(const cpr::Url& url) override { return respond("GET", url.str(), "", {}); }
    cpr::Response doGet(const cpr::Url& url, const cpr::Header& header) override {
        return respond("GET", url.str(), "", header);
    }
    cpr::Response doGetWithParameters(const cpr::Url& url, const cpr::Parameters&) override {
        return respond("GET", url.str(), "", {});
    }
    cpr::Response doPost(const cpr::Url& url, const cpr::Header& header, const cpr::Body& body) override {
        return respond("POST", url.str(), body.str(), header);
    }
    cpr::Response doPost(const cpr::Url& url, const cpr::Header& header, const cpr::Multipart&) override {
        return respond("POST", url.str(), "", header);
    }
    cpr::Response doPut(const cpr::Url& url, const cpr::Header& header, const cpr::Body& body) override {
        return respond("PUT", url.str(), std::string(body), header);
    }
    cpr::Response doDelete(const cpr::Url& url, const cpr::Header& header) override {
        return respond("DELETE", url.str(), "", header);
    }
    cpr::Response doDelete(const cpr::Url& url, const cpr::Header& header, const cpr::Body& body) override {
        return respond("DELETE", url.str(), std::string(body), header);
    }

private:
    cpr::Response respond(
        const std::string& method,
        const std::string& url,
        const std::string& body,
        const cpr::Header& header
    ) {
        WarmupRequest recorded;
        recorded.method = method;
        recorded.body = body;

        auto schemeEnd = url.find("://");
        auto hostStart = schemeEnd == std::string::npos ? 0 : schemeEnd + 3;
        auto pathStart = url.find('/', hostStart);
        recorded.host = url.substr(hostStart, pathStart - hostStart);
        auto queryStart = url.find('?', pathStart);
        recorded.path = url.substr(pathStart, queryStart - pathStart);
        if (queryStart != std::string::npos) {
            std::string query = url.substr(queryStart + 1);
            size_t at = 0;
            while (at < query.size()) {
                auto amp = query.find('&', at);
                std::string pair = query.substr(at, amp - at);
                auto eq = pair.find('=');
                if (eq != std::string::npos) {
                    recorded.query[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
                }
                if (amp == std::string::npos) break;
                at = amp + 1;
            }
        }
        auto keyIt = header.find("API-Key");
        recorded.apiKey = keyIt == header.end() ? "" : keyIt->second;

        cpr::Response response;
        {
            std::lock_guard<std::mutex> lock(mu_);
            records_.push_back(recorded);
            if (next_.has_value()) {
                response = *next_;
                next_.reset();
                return response;
            }
        }
        response.status_code = 200;
        response.text = R"({"ok":true,"echo_api_key":")" + recorded.apiKey + R"("})";
        response.header["Content-Type"] = "application/json";
        return response;
    }

    std::mutex mu_;
    std::vector<WarmupRequest> records_;
    std::optional<cpr::Response> next_;
};

class OfflineWarmupClient : public ZeroBounceWarmup {
public:
    OfflineWarmupClient(FakeWarmupApi& api, const std::string& key = "warmup-key",
                        const std::string& baseUrl = "https://warmup-api.zerobounce.net") {
        initialize(key, baseUrl);
        setRequestHandler(&api);
    }
};

WarmupRequest onlyRequest(FakeWarmupApi& api) {
    auto records = api.take();
    EXPECT_EQ(records.size(), 1u);
    if (records.empty()) {
        return {};
    }
    return records.front();
}

/// Run a call that must fail, and hand back the error's text.
std::string captureError(const std::function<void(OnRawJsonSuccessCallback, OnErrorCallback)>& call) {
    std::string message;
    bool rejected = false;
    call([](std::string) { ADD_FAILURE() << "unexpected success"; },
         [&](ZBErrorResponse error) {
             rejected = true;
             message = error.toString();
             EXPECT_LT(message.size(), 500u) << message.substr(0, 120);
         });
    EXPECT_TRUE(rejected);
    return message;
}

auto succeed = [](std::string) {};
auto failOnError = [](ZBErrorResponse error) { FAIL() << error.toString(); };

std::string accessDeniedPage() {
    std::string page =
        "<!DOCTYPE html><html lang=\"en-US\"><head><title>Access denied | zerobounce.net</title>"
        "</head><body>";
    for (int i = 0; i < 2000; ++i) {
        page += "<p>blocked</p>";
    }
    return page + "</body></html>";
}

}  // namespace

// -- construction ------------------------------------------------------------

TEST(OfflineWarmup, TheKeyTravelsInTheApiKeyHeader) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);

    client.getSeedCount(succeed, failOnError);
    auto request = onlyRequest(api);
    EXPECT_EQ(request.host, "warmup-api.zerobounce.net");
    EXPECT_EQ(request.path, "/api/v1/warmup/seed-count");
    EXPECT_EQ(request.apiKey, "warmup-key");
    // The key must never reach the query string, where it would be logged.
    EXPECT_TRUE(request.query.empty());
}

TEST(OfflineWarmup, AKeyThatIsEmptyOrPaddedIsRejected) {
    FakeWarmupApi api;
    for (const std::string& bad : {std::string(""), std::string(" key "), std::string("key\n"),
                                   std::string("key\tx")}) {
        OfflineWarmupClient client(api, bad);
        std::string message = captureError([&](auto ok, auto err) { client.getSeeds(ok, err); });
        EXPECT_NE(message.find("api_key"), std::string::npos) << message;
    }
    EXPECT_EQ(api.size(), 0u);
}

TEST(OfflineWarmup, APlainHttpBaseUrlIsRejected) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api, "warmup-key", "http://warmup-api.zerobounce.net");
    std::string message = captureError([&](auto ok, auto err) { client.getSeeds(ok, err); });
    EXPECT_NE(message.find("https"), std::string::npos) << message;
    EXPECT_EQ(api.size(), 0u);
}

// initialize() used to reject a base URL by quietly leaving the client
// un-ready, with nothing to read back at the point of the mistake.
TEST(OfflineWarmup, InitializeReportsWhyItRejectedTheArguments) {
    FakeWarmupApi api;

    OfflineWarmupClient good(api, "warmup-key", "https://warmup.example.com");
    EXPECT_TRUE(good.isReady());
    EXPECT_EQ(good.getInitializeError(), "");

    OfflineWarmupClient plainHttp(api, "warmup-key", "http://warmup.example.com");
    EXPECT_FALSE(plainHttp.isReady());
    EXPECT_EQ(plainHttp.getInitializeError(), "base_url must be an https:// URL");
    // And that same reason, not a guess, reaches the error callback.
    std::string message = captureError([&](auto ok, auto err) { plainHttp.getSeeds(ok, err); });
    EXPECT_NE(message.find("base_url must be an https:// URL"), std::string::npos) << message;

    OfflineWarmupClient noKey(api, "", "https://warmup.example.com");
    EXPECT_FALSE(noKey.isReady());
    EXPECT_EQ(noKey.getInitializeError(), "api_key is required");

    OfflineWarmupClient paddedKey(api, " warmup-key ", "https://warmup.example.com");
    EXPECT_FALSE(paddedKey.isReady());
    EXPECT_EQ(paddedKey.getInitializeError(),
              "api_key has surrounding whitespace or control characters");
    // The reason never repeats the key back.
    EXPECT_EQ(paddedKey.getInitializeError().find("warmup-key"), std::string::npos);

    // A later good initialize clears the earlier reason.
    plainHttp.initialize("warmup-key", "https://warmup.example.com");
    EXPECT_TRUE(plainHttp.isReady());
    EXPECT_EQ(plainHttp.getInitializeError(), "");

    EXPECT_EQ(api.size(), 0u);
}

TEST(OfflineWarmup, ACustomHttpsBaseUrlIsUsed) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api, "warmup-key", "https://warmup.example.com/");
    client.getSeeds(succeed, failOnError);
    auto request = onlyRequest(api);
    EXPECT_EQ(request.host, "warmup.example.com");
    EXPECT_EQ(request.path, "/api/v1/warmup/seeds");
}

// -- paths and verbs ---------------------------------------------------------

TEST(OfflineWarmup, EveryEndpointUsesItsDocumentedPathAndVerb) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);

    client.listSendingAccounts(succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).path, "/api/v1/warmup/sending-accounts");
    client.getSendingAccount("a@b.com", succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).path, "/api/v1/warmup/sending-account");
    client.listFromAddresses({}, succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).path, "/api/v1/warmup/from-addresses");
    client.getEngagementRules(succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).path, "/api/v1/warmup/engagement-rules");
    client.getCustomEngagement("a@b.com", succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).path, "/api/v1/warmup/custom-engagement/a%40b.com");
    client.getSeeds(succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).path, "/api/v1/warmup/seeds");
    client.getProgressToday(succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).path, "/api/v1/warmup/progress-today");
    client.getSentToday(succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).path, "/api/v1/warmup/sent-today");
    client.getEmailsPerDay(succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).path, "/api/v1/warmup/emails-per-day");
    client.getStatistics("a@b.com", succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).path, "/api/v1/warmup/statistics");
    client.getStatisticsDetailed("a@b.com", succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).path, "/api/v1/warmup/statistics-detailed");
    client.getFromAddressStatistics("a@b.com", succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).path, "/api/v1/warmup/from-address-statistics");
    client.checkDomainDmarc("a@b.com", std::nullopt, succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).path, "/api/v1/warmup/check-domain-dmarc");

    WarmupAddSendingAccountOptions selfManaged;
    selfManaged.engagementRuleId = 0;
    client.addSendingAccount("a@b.com", "self_managed", selfManaged, succeed, failOnError);
    auto added = onlyRequest(api);
    EXPECT_EQ(added.method, "POST");
    EXPECT_EQ(added.path, "/api/v1/warmup/sending-accounts");

    client.updateSendingAccount("a@b.com", 1, 4, true, succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).method, "PUT");
    client.deleteSendingAccount("a@b.com", succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).method, "DELETE");
    client.changeFromAddress("a@b.com", "c@d.com", succeed, failOnError);
    auto changed = onlyRequest(api);
    EXPECT_EQ(changed.method, "PUT");
    EXPECT_EQ(changed.path, "/api/v1/warmup/smtp-credentials");
    client.setEngagementRule("a@b.com", 2, succeed, failOnError);
    auto rule = onlyRequest(api);
    EXPECT_EQ(rule.method, "PUT");
    EXPECT_EQ(rule.path, "/api/v1/warmup/engagement-rule");
    client.createCustomEngagement("a@b.com", 10, 10, 5, succeed, failOnError);
    auto created = onlyRequest(api);
    EXPECT_EQ(created.method, "POST");
    EXPECT_EQ(created.path, "/api/v1/warmup/custom-engagement");
    client.updateCustomEngagement("a@b.com", 10, 10, 5, succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).method, "PUT");
    client.deleteCustomEngagement("a@b.com", 1, succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).method, "DELETE");
    client.testSmtpConfiguration("a@b.com", "u", "p", "smtp.example.com", 587, succeed, failOnError);
    auto smtp = onlyRequest(api);
    EXPECT_EQ(smtp.method, "POST");
    EXPECT_EQ(smtp.path, "/api/v1/warmup/test-smtp-configuration");
}

TEST(OfflineWarmup, StatisticsPerDaySendsBothDates) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    client.getStatisticsPerDay("a@b.com", "2026-01-02", "2026-01-09", succeed, failOnError);
    auto request = onlyRequest(api);
    EXPECT_EQ(request.path, "/api/v1/warmup/statistics-per-day");
    EXPECT_EQ(request.query["start_date"], "2026-01-02");
    EXPECT_EQ(request.query["end_date"], "2026-01-09");
}

// -- argument validation -----------------------------------------------------

TEST(OfflineWarmup, RequiredArgumentsAreCheckedBeforeAnyRequest) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);

    captureError([&](auto ok, auto err) { client.getSendingAccount("", ok, err); });
    captureError([&](auto ok, auto err) { client.deleteSendingAccount(" ", ok, err); });
    captureError([&](auto ok, auto err) { client.changeFromAddress("", "b@c.com", ok, err); });
    captureError([&](auto ok, auto err) { client.changeFromAddress("a@b.com", "", ok, err); });
    captureError([&](auto ok, auto err) { client.setEngagementRule("", 1, ok, err); });
    captureError([&](auto ok, auto err) { client.getStatistics("", ok, err); });
    captureError([&](auto ok, auto err) { client.checkDomainDmarc("", std::nullopt, ok, err); });
    EXPECT_EQ(api.size(), 0u) << "nothing should reach the network";
}

TEST(OfflineWarmup, EngagementRuleIdsAreRangeChecked) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    for (int bad : {-1, 5}) {
        std::string message =
            captureError([&](auto ok, auto err) { client.setEngagementRule("a@b.com", bad, ok, err); });
        EXPECT_NE(message.find("rule_id"), std::string::npos) << message;
    }
    EXPECT_EQ(api.size(), 0u);

    for (int good = 0; good <= 4; ++good) {
        client.setEngagementRule("a@b.com", good, succeed, failOnError);
        EXPECT_EQ(json::parse(onlyRequest(api).body)["rule_id"], good);
    }
}

TEST(OfflineWarmup, CustomEngagementPercentagesAreRangeChecked) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    const std::vector<std::array<int, 3>> bad{
        {4, 10, 5},    // reply_pct below 5
        {46, 10, 5},   // reply_pct above 45
        {10, 4, 5},    // important_pct below 5
        {10, 41, 5},   // important_pct above 40
        {10, 10, 71},  // click_pct above 70
        {10, 10, 18},  // click_pct above important_pct x 1.75
    };
    for (const auto& values : bad) {
        captureError([&](auto ok, auto err) {
            client.createCustomEngagement("a@b.com", values[0], values[1], values[2], ok, err);
        });
    }
    EXPECT_EQ(api.size(), 0u);

    client.createCustomEngagement("a@b.com", 20, 20, 35, succeed, failOnError);
    auto body = json::parse(onlyRequest(api).body);
    EXPECT_EQ(body["reply_pct"], 20);
    EXPECT_EQ(body["click_pct"], 35);
}

TEST(OfflineWarmup, DeleteCustomEngagementOnlyFallsBackToAStandardRule) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    std::string message =
        captureError([&](auto ok, auto err) { client.deleteCustomEngagement("a@b.com", 0, ok, err); });
    EXPECT_NE(message.find("engagement_rule_id"), std::string::npos) << message;
    EXPECT_EQ(api.size(), 0u);

    client.deleteCustomEngagement("a@b.com", 3, succeed, failOnError);
    EXPECT_EQ(json::parse(onlyRequest(api).body)["engagement_rule_id"], 3);
}

TEST(OfflineWarmup, UpdateSendingAccountChecksItsRanges) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    captureError([&](auto ok, auto err) { client.updateSendingAccount("a@b.com", 5, 4, true, ok, err); });
    captureError([&](auto ok, auto err) { client.updateSendingAccount("a@b.com", 1, 9, true, ok, err); });
    EXPECT_EQ(api.size(), 0u);

    client.updateSendingAccount("a@b.com", 1, 5, false, succeed, failOnError);
    auto body = json::parse(onlyRequest(api).body);
    EXPECT_EQ(body["team_id"], 1);
    EXPECT_EQ(body["status_id"], 5);
    EXPECT_EQ(body["auto_enable"], false);
}

TEST(OfflineWarmup, AddSendingAccountChecksTheOnboardingModel) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);

    std::string message = captureError([&](auto ok, auto err) {
        client.addSendingAccount("a@b.com", "managed", {}, ok, err);
    });
    EXPECT_NE(message.find("self_managed"), std::string::npos) << message;

    // self_managed needs its engagement rule and takes no ZB-managed fields.
    captureError([&](auto ok, auto err) {
        client.addSendingAccount("a@b.com", "self_managed", {}, ok, err);
    });
    WarmupAddSendingAccountOptions mixed;
    mixed.engagementRuleId = 0;
    mixed.emailContent = WarmupEmailContent{};
    captureError([&](auto ok, auto err) {
        client.addSendingAccount("a@b.com", "self_managed", mixed, ok, err);
    });
    EXPECT_EQ(api.size(), 0u);

    WarmupAddSendingAccountOptions good;
    good.engagementRuleId = 2;
    good.ignoreConflict = true;
    client.addSendingAccount("a@b.com", "self_managed", good, succeed, failOnError);
    auto body = json::parse(onlyRequest(api).body);
    EXPECT_EQ(body["onboarding_model"], "self_managed");
    EXPECT_EQ(body["engagement_rule_id"], 2);
    EXPECT_EQ(body["ignore_conflict"], true);
}

TEST(OfflineWarmup, AddSendingAccountChecksTheZbManagedPayload) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);

    WarmupAddSendingAccountOptions badTypes;
    badTypes.emailContent = WarmupEmailContent{{"promotional"}, std::nullopt, std::nullopt, std::nullopt};
    captureError([&](auto ok, auto err) {
        client.addSendingAccount("a@b.com", "zb_managed", badTypes, ok, err);
    });

    WarmupAddSendingAccountOptions badOffset;
    WarmupManagedSending offset;
    offset.timezoneOffset = 15;
    badOffset.managedSending = offset;
    captureError([&](auto ok, auto err) {
        client.addSendingAccount("a@b.com", "zb_managed", badOffset, ok, err);
    });

    WarmupAddSendingAccountOptions badDate;
    WarmupManagedSending date;
    date.warmupStartDate = "02/01/2026";
    badDate.managedSending = date;
    captureError([&](auto ok, auto err) {
        client.addSendingAccount("a@b.com", "zb_managed", badDate, ok, err);
    });
    EXPECT_EQ(api.size(), 0u);

    WarmupAddSendingAccountOptions good;
    good.emailContent = WarmupEmailContent{
        {"newsletter", "marketing"}, std::nullopt, std::nullopt, std::nullopt};
    WarmupManagedSending sending;
    sending.timezoneOffset = -5;
    sending.warmupStartDate = "2026-03-04";
    good.managedSending = sending;
    client.addSendingAccount("a@b.com", "zb_managed", good, succeed, failOnError);
    auto body = json::parse(onlyRequest(api).body);
    EXPECT_EQ(body["onboarding_model"], "zb_managed");
    EXPECT_EQ(body["email_content"]["types_of_emails"][1], "marketing");
    EXPECT_EQ(body["managed_sending"]["warmup_start_date"], "2026-03-04");
}

TEST(OfflineWarmup, ListFromAddressesChecksItsSearchAndPaging) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);

    WarmupListFromAddressesOptions bad;
    bad.searchFrom = "bad search!";
    captureError([&](auto ok, auto err) { client.listFromAddresses(bad, ok, err); });
    WarmupListFromAddressesOptions negative;
    negative.offset = -1;
    captureError([&](auto ok, auto err) { client.listFromAddresses(negative, ok, err); });
    EXPECT_EQ(api.size(), 0u);

    WarmupListFromAddressesOptions good;
    good.searchFrom = "a.b-c_d@e+f";
    good.offset = 0;
    good.limit = 50;
    client.listFromAddresses(good, succeed, failOnError);
    auto request = onlyRequest(api);
    EXPECT_EQ(request.query["search_from"], "a.b-c_d@e+f");
    EXPECT_EQ(request.query["offset"], "0");
    EXPECT_EQ(request.query["limit"], "50");
}

TEST(OfflineWarmup, StatisticsPerDayRejectsABackwardsRangeAndBadDates) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    captureError([&](auto ok, auto err) {
        client.getStatisticsPerDay("a@b.com", "2026-02-02", "2026-01-01", ok, err);
    });
    captureError([&](auto ok, auto err) {
        client.getStatisticsPerDay("a@b.com", "01/02/2026", "2026-01-01", ok, err);
    });
    EXPECT_EQ(api.size(), 0u);
}

TEST(OfflineWarmup, CheckDomainDmarcRequiresAnEmailAddressNotADomain) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    // LIVE: the API answers a bare domain with "Please provide a valid email
    // address", although the docs describe a domain.
    std::string message = captureError([&](auto ok, auto err) {
        client.checkDomainDmarc("example.com", std::nullopt, ok, err);
    });
    EXPECT_NE(message.find("email address"), std::string::npos) << message;
    EXPECT_EQ(api.size(), 0u);

    client.checkDomainDmarc("sender@example.com", std::string("zb"), succeed, failOnError);
    EXPECT_EQ(onlyRequest(api).query["dkim_selector"], "zb");
}

TEST(OfflineWarmup, CustomEngagementPathIdsCannotTraverse) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    for (const std::string& bad : {std::string("."), std::string(".."), std::string(" .. ")}) {
        captureError([&](auto ok, auto err) { client.getCustomEngagement(bad, ok, err); });
    }
    EXPECT_EQ(api.size(), 0u);
}

TEST(OfflineWarmup, TheSmtpTestChecksItsPort) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    for (int bad : {0, 65536}) {
        std::string message = captureError([&](auto ok, auto err) {
            client.testSmtpConfiguration("a@b.com", "u", "p", "s", bad, ok, err);
        });
        EXPECT_NE(message.find("port"), std::string::npos) << message;
    }
    EXPECT_EQ(api.size(), 0u);
}

// -- responses ---------------------------------------------------------------

TEST(OfflineWarmup, AnEmptySuccessBecomesNull) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);

    api.replyWith(204, "application/json", "");
    std::string body;
    client.deleteSendingAccount("a@b.com", [&](std::string text) { body = text; }, failOnError);
    EXPECT_EQ(body, "null");
    api.take();

    api.replyWith(200, "application/json", "");
    body.clear();
    client.getSeedCount([&](std::string text) { body = text; }, failOnError);
    EXPECT_EQ(body, "null");
}

TEST(OfflineWarmup, CsvDownloadsComeBackAsText) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);

    api.replyWith(200, "text/csv", "seed,provider\na@b.com,gmail\n");
    std::string csv;
    client.downloadSeeds([&](std::string text) { csv = text; },
                         [](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(csv, "seed,provider\na@b.com,gmail\n");
    api.take();

    api.replyWith(200, "text/csv", "day,sent\n2026-01-01,10\n");
    client.downloadStatistics("a@b.com", [](std::string) {},
                              [](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(onlyRequest(api).query["c_from"], "a@b.com");
}

TEST(OfflineWarmup, AJsonErrorInPlaceOfACsvIsAnError) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    api.replyWith(200, "application/json", R"({"message":"No statistics for this address."})");
    bool rejected = false;
    client.downloadSeeds([](std::string) { FAIL() << "success"; }, [&](ZBErrorResponse error) {
        rejected = true;
        EXPECT_NE(error.toString().find("No statistics for this address."), std::string::npos);
    });
    EXPECT_TRUE(rejected);
}

TEST(OfflineWarmup, TheHttpStatusIsOnTheError) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    api.replyWith(429, "application/json",
                  R"({"message":"Custom rule was updated less than 7 days ago."})");
    bool rejected = false;
    client.updateCustomEngagement("a@b.com", 10, 10, 5, [](std::string) { FAIL() << "success"; },
        [&](ZBErrorResponse error) {
            rejected = true;
            EXPECT_EQ(error.statusCode, 429);
            EXPECT_NE(error.toString().find("Custom rule was updated"), std::string::npos);
        });
    EXPECT_TRUE(rejected);
}

TEST(OfflineWarmup, AValidationErrorListBecomesOneShortMessage) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    api.replyWith(422, "application/json",
                  R"({"detail":[{"loc":["body","reply_pct"],"msg":"ensure this value is <= 45"}]})");
    std::string message =
        captureError([&](auto ok, auto err) { client.createCustomEngagement("a@b.com", 10, 10, 5, ok, err); });
    EXPECT_LT(message.size(), 500u);
}

TEST(OfflineWarmup, AnHtmlErrorPageIsNotPastedIntoTheMessage) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    api.replyWith(403, "text/html", accessDeniedPage());
    bool rejected = false;
    client.getSeeds([](std::string) { FAIL() << "success"; }, [&](ZBErrorResponse error) {
        rejected = true;
        EXPECT_EQ(error.statusCode, 403);
        EXPECT_LT(error.toString().size(), 500u);
        EXPECT_EQ(error.toString().find("<html"), std::string::npos);
    });
    EXPECT_TRUE(rejected);
}

TEST(OfflineWarmup, ARedirectIsRefusedRatherThanFollowed) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    api.replyWith(302, "text/html", "");
    bool rejected = false;
    client.getSeeds([](std::string) { FAIL() << "success"; }, [&](ZBErrorResponse error) {
        rejected = true;
        EXPECT_EQ(error.statusCode, 302);
        EXPECT_NE(error.toString().find("redirect"), std::string::npos);
    });
    EXPECT_TRUE(rejected);
}

TEST(OfflineWarmup, TheKeyNeverAppearsInAnError) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    api.replyWith(401, "application/json", R"({"message":"Invalid key warmup-key"})");
    std::string message = captureError([&](auto ok, auto err) { client.getSeeds(ok, err); });
    EXPECT_EQ(message.find("warmup-key"), std::string::npos) << message;
    EXPECT_NE(message.find("***"), std::string::npos) << message;
}

TEST(OfflineWarmup, SmtpCredentialsNeverAppearInAnError) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    api.replyWith(500, "application/json",
                  R"({"message":"login failed for user bob with s3cret"})");
    bool rejected = false;
    client.testSmtpConfiguration("a@b.com", "bob", "s3cret", "smtp.example.com", 587,
        [](std::string) { FAIL() << "success"; },
        [&](ZBErrorResponse error) {
            rejected = true;
            const std::string message = error.toString();
            EXPECT_EQ(message.find("s3cret"), std::string::npos) << message;
            EXPECT_EQ(message.find("bob"), std::string::npos) << message;
            // LIVE: a failed SMTP login comes back as a 500, not a 4xx.
            EXPECT_EQ(error.statusCode, 500);
        });
    EXPECT_TRUE(rejected);
}

TEST(OfflineWarmup, AMalformedBodyDoesNotLeakAParserDump) {
    FakeWarmupApi api;
    OfflineWarmupClient client(api);
    api.replyWith(200, "application/json", "<<<not-json>>>" + std::string(5000, 'x'));
    std::string message = captureError([&](auto ok, auto err) { client.getSeeds(ok, err); });
    EXPECT_NE(message.find("not valid JSON"), std::string::npos) << message;
}
