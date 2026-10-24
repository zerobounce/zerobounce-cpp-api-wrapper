// Opt-in live API suite: calls every SDK method against the real API to check
// payloads and response shapes.
//
// It is its own executable, built only when ZEROBOUNCE_BUILD_LIVE_TESTS=ON, so
// `cmake --build build` and CI never compile it and `ctest` never runs it:
//
//   cmake -S . -B build-live -DZEROBOUNCE_BUILD_LIVE_TESTS=ON
//   cmake --build build-live --target ZeroBounceLiveApi
//   export ZEROBOUNCE_API_KEY=...          # validation API (skipped without it)
//   export ZEROBOUNCE_WARMUP_API_KEY=...   # Warmup API (skipped without it)
//   ./build-live/ZeroBounceLiveApi
//
// By default only free calls run: sandbox addresses (no credits), reads, and
// requests expected to fail (unknown file IDs, S3/SFTP without an integration)
// that still prove the API accepts the payload. The free tier checks that the
// credit balance did not change. Two flags opt in to more:
//
//   ZEROBOUNCE_LIVE_SPEND=1     spends credits: scoring, activity, email
//                               finder, and a bulk round trip whose job is
//                               deleted again
//   ZEROBOUNCE_LIVE_CHANGES=1   reversible account changes, each undone in
//                               cleanup even when a test fails: a test filter,
//                               and a self-managed Warmup sending account
//
// Never called, because the API cannot undo them: creating an Inbox Placement
// test (it cannot be deleted while running), setting the Inbox Placement
// webhook (it cannot be cleared), and ZB-managed Warmup onboarding (it starts
// mailbox provisioning).
//
// No key is ever written to a file or printed. Keys come from the environment.

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "ZeroBounce/ZeroBounce.h"
#include "ZeroBounce/ZeroBounceWarmup.h"

using json = nlohmann::json;

namespace {

/// Well-formed but matching no file: a malformed id can be Cloudflare-blocked
/// instead of answered.
const std::string kUnknownId = "0d1b7a4e-1111-2222-3333-444455556666";
const std::string kCallback = "https://example.com/zerobounce-sdk-live-test";
const std::string kSandbox = "valid@example.com";

int passed = 0;
int failed = 0;
int skipped = 0;
std::vector<std::string> failures;

std::string env(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return "";
    }
    std::string text = value;
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string apiKey() { return env("ZEROBOUNCE_API_KEY"); }
std::string warmupKey() { return env("ZEROBOUNCE_WARMUP_API_KEY"); }
bool flag(const char* name) { return env(name) == "1"; }

std::string redact(const std::string& text) {
    std::string value = text;
    for (const auto& secret : {apiKey(), warmupKey()}) {
        if (secret.empty()) {
            continue;
        }
        size_t at = 0;
        while ((at = value.find(secret, at)) != std::string::npos) {
            value.replace(at, secret.size(), "***");
            at += 3;
        }
    }
    return value;
}

/// A check either passes, or records why it did not.
void check(const std::string& name, const std::function<std::string()>& body) {
    std::string problem;
    try {
        problem = body();
    } catch (const std::exception& error) {
        problem = redact(error.what());
    }
    if (problem.empty()) {
        passed++;
        std::cout << "  + " << name << std::endl;
    } else {
        failed++;
        failures.push_back(name + ": " + redact(problem));
        std::cout << "  X " << name << "\n      " << redact(problem) << std::endl;
    }
}

void skip(const std::string& name, const std::string& reason) {
    skipped++;
    std::cout << "  - SKIP " << name << " (" << reason << ")" << std::endl;
}

/// One client per key, built once. Neither client is copyable.
ZeroBounce& validationClient() {
    static ZeroBounce zb;
    static bool ready = false;
    if (!ready) {
        zb.initialize(apiKey());
        ready = true;
    }
    return zb;
}

std::tm daysAgo(int days) {
    std::time_t now = std::time(nullptr);
    now -= static_cast<std::time_t>(days) * 24 * 3600;
    std::tm value{};
#if defined(_WIN32)
    localtime_s(&value, &now);
#else
    localtime_r(&now, &value);
#endif
    return value;
}

/// Run a call that must fail, and hand back the error's text.
std::string expectFailure(
    const std::string& what,
    const std::function<void(std::function<void()>, OnErrorCallback)>& call
) {
    std::string message;
    bool rejected = false;
    call([&]() { rejected = false; },
         [&](ZBErrorResponse error) {
             rejected = true;
             message = redact(error.toString());
         });
    if (!rejected) {
        return "";
    }
    return message.empty() ? what + " failed without a message" : message;
}

// -- validation API, free ----------------------------------------------------

void validationFree() {
    std::cout << "Validation API (free calls)" << std::endl;
    ZeroBounce& zb = validationClient();

    long long before = -2;
    check("getCredits returns a balance", [&]() -> std::string {
        std::string problem = "no response";
        zb.getCredits([&](ZBCreditsResponse response) {
            before = response.credits;
            problem.clear();
        }, [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    check("getApiUsage parses the documented date formats", [&]() -> std::string {
        std::string problem = "no response";
        zb.getApiUsage(daysAgo(7), daysAgo(0),
            [&](ZBGetApiUsageResponse) { problem.clear(); },
            [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    check("validate on a sandbox address spends nothing", [&]() -> std::string {
        std::string problem = "no response";
        zb.validate(kSandbox, "", [&](ZBValidateResponse response) {
            problem = response.address == kSandbox ? "" : "address did not echo";
        }, [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    check("validate works over POST as well as GET", [&]() -> std::string {
        // No billable add-on here: activity_data, verify_plus and domain_info
        // are charged even on a sandbox address, so they live in the SPEND tier.
        ZBValidateOptions options;
        options.usePost = true;
        options.timeout = 30;
        std::string problem = "no response";
        zb.validate(kSandbox, "", options, [&](ZBValidateResponse) { problem.clear(); },
            [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    check("LIVE: validateBatch accepts timeout as a string", [&]() -> std::string {
        // Sent as a JSON number the API rejects the whole batch with the
        // account-level "Invalid API Key or your account ran out of credits".
        ZBValidateOptions options;
        options.timeout = 30;
        std::string problem = "no response";
        zb.validateBatch({ZBEmailToValidate(kSandbox, "")}, options,
            [&](ZBValidateBatchResponse response) {
                problem.clear();
                for (const auto& error : response.errors) {
                    std::string address = error.emailAddress;
                    for (auto& c : address) c = static_cast<char>(std::tolower(c));
                    if (address == "all") {
                        problem = "account-level error: " + error.error;
                    }
                }
            },
            [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    check("an unknown file id is a short, clean error", [&]() -> std::string {
        std::string message = expectFailure("fileStatus", [&](auto ok, auto err) {
            zb.fileStatus(kUnknownId, [&](ZBFileStatusResponse) { ok(); }, err);
        });
        if (message.empty()) {
            return "fileStatus unexpectedly succeeded";
        }
        if (message.size() >= 500 || message.find("<html") != std::string::npos) {
            return "message not short and clean";
        }
        return "";
    });

    check("listS3files answers (empty without an S3 integration)", [&]() -> std::string {
        std::string problem;
        zb.listS3files(std::nullopt, std::nullopt,
            [&](std::vector<ZBS3ListFileEntry>) {},
            [&](ZBErrorResponse error) {
                // No S3 integration configured is an expected, documented failure.
                if (error.toString().size() >= 500) {
                    problem = "message too long";
                }
            });
        return problem;
    });

    check("LIVE: S3/SFTP jobs reject a payload without return_url before sending",
        [&]() -> std::string {
            BulkFileJobOptions options;
            options.fileName = "f.csv";
            options.hasHeaderRow = true;
            std::string message = expectFailure("emailFinderS3File", [&](auto ok, auto err) {
                zb.emailFinderS3File(options, [&](ZBSendFileResponse) { ok(); }, err);
            });
            if (message.find("return_url") == std::string::npos) {
                return "unexpected message: " + message;
            }
            return "";
        });

    check("LIVE: a complete S3 job payload is accepted by the API", [&]() -> std::string {
        // Without an S3 integration this fails on the integration, not the payload.
        BulkFileJobOptions options;
        options.fileName = "f.csv";
        options.hasHeaderRow = true;
        options.returnUrl = kCallback;
        std::string problem;
        zb.emailFinderS3File(options, [&](ZBSendFileResponse) {}, [&](ZBErrorResponse error) {
            const std::string message = redact(error.toString());
            if (message.find("return URL is not permitted") != std::string::npos ||
                message.find("This field is required") != std::string::npos) {
                problem = "payload rejected: " + message;
            }
        });
        return problem;
    });

    check("filters list", [&]() -> std::string {
        std::string problem = "no response";
        zb.listFilters(false, [&](std::string) { problem.clear(); },
            [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    check("LIVE: the Inbox Placement Tester accepts a Bearer key", [&]() -> std::string {
        std::string problem = "no response";
        zb.getInboxPlacementProviders([&](std::string) { problem.clear(); },
            [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    check("inbox placement test types", [&]() -> std::string {
        std::string problem = "no response";
        zb.getInboxPlacementTestTypes([&](std::string) { problem.clear(); },
            [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    check("LIVE: inbox placement tests are listed from /tests, and the webhook reads back",
        [&]() -> std::string {
            // The listing is GET /tests (plural); /test is the single-test path
            // and answers "Method \"GET\" not allowed".
            ListInboxPlacementTestsOptions options;
            options.page = 1;
            options.pageSize = 10;
            std::string problem = "no response";
            zb.listInboxPlacementTests(options, [&](std::string) { problem.clear(); },
                [&](ZBErrorResponse error) { problem = error.toString(); });
            if (!problem.empty()) {
                return problem;
            }
            problem = "no webhook response";
            zb.getInboxPlacementWebhook([&](std::string) { problem.clear(); },
                [&](ZBErrorResponse error) { problem = error.toString(); });
            return problem;
        });

    check("list evaluator result for an unknown id fails cleanly", [&]() -> std::string {
        std::string message = expectFailure("getListEvaluatorResult", [&](auto ok, auto err) {
            zb.getListEvaluatorResult(kUnknownId, [&](std::string) { ok(); }, err);
        });
        if (message.empty()) {
            return "getListEvaluatorResult unexpectedly succeeded";
        }
        return message.size() < 500 ? "" : "message too long";
    });

    check("LIVE: sendEvaluatedFile posts the file id as a form field", [&]() -> std::string {
        std::string message = expectFailure("sendEvaluatedFile", [&](auto ok, auto err) {
            zb.sendEvaluatedFile(kUnknownId, [&](ZBSendFileResponse) { ok(); }, err);
        });
        if (message.empty()) {
            return "sendEvaluatedFile unexpectedly succeeded";
        }
        if (message.find("not allowed") != std::string::npos) {
            return "wrong method or path: " + message;
        }
        return "";
    });

    check("the free calls did not change the credit balance", [&]() -> std::string {
        long long after = -2;
        std::string problem = "no response";
        validationClient().getCredits([&](ZBCreditsResponse response) {
            after = response.credits;
            problem.clear();
        }, [&](ZBErrorResponse error) { problem = error.toString(); });
        if (!problem.empty()) {
            return problem;
        }
        return after == before
            ? ""
            : "credits went from " + std::to_string(before) + " to " + std::to_string(after);
    });
}

// -- validation API, spends credits ------------------------------------------

void validationSpend() {
    std::cout << "\nValidation API (ZEROBOUNCE_LIVE_SPEND=1)\n";
    ZeroBounce& zb = validationClient();

    check("validate accepts every documented option", [&]() -> std::string {
        // LIVE: activity_data, verify_plus and domain_info are billable add-ons
        // and are charged even for a sandbox address, so this is not free.
        ZBValidateOptions options;
        options.timeout = 30;
        options.activityData = true;
        options.verifyPlus = true;
        options.domainInfo = true;
        options.greylistProcessing = true;
        options.callbackUrl = kCallback;
        std::string problem = "no response";
        zb.validate(kSandbox, "", options, [&](ZBValidateResponse) { problem.clear(); },
            [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    check("scoreEmail", [&]() -> std::string {
        std::string problem = "no response";
        zb.scoreEmail(kSandbox, [&](ZBScoreEmailResponse) { problem.clear(); },
            [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    check("scoreEmail over POST", [&]() -> std::string {
        std::string problem = "no response";
        zb.scoreEmailPost(kSandbox, [&](ZBScoreEmailResponse) { problem.clear(); },
            [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    check("getActivityData", [&]() -> std::string {
        std::string problem = "no response";
        zb.getActivityData(kSandbox, [&](ZBActivityDataResponse) { problem.clear(); },
            [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    check("email finder", [&]() -> std::string {
        std::string problem = "no response";
        zb.findEmailByDomain("example.com", "John", "Doe",
            [&](ZBFindEmailResponse) { problem.clear(); },
            [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    check("domain search", [&]() -> std::string {
        std::string problem = "no response";
        zb.searchDomainByDomain("example.com", [&](ZBDomainSearchResponse) { problem.clear(); },
            [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    std::string fileId;
    check("bulk validation round trip: upload, status, delete", [&]() -> std::string {
        const std::string path = "zb-sdk-live.csv";
        {
            std::ofstream out(path);
            out << "email\nvalid@example.com\ninvalid@example.com\n";
        }
        SendFileOptions options;
        options.hasHeaderRow = true;
        std::string problem = "no response";
        zb.sendFile(path, 1, options, [&](ZBSendFileResponse response) {
            if (response.fileId.empty()) {
                problem = "no file_id in the upload response";
                return;
            }
            fileId = response.fileId;
            problem.clear();
        }, [&](ZBErrorResponse error) { problem = error.toString(); });
        std::remove(path.c_str());
        if (!problem.empty()) {
            return problem;
        }
        problem = "no status response";
        zb.fileStatus(fileId, [&](ZBFileStatusResponse status) {
            problem = status.fileId == fileId ? "" : "status did not echo the file id";
        }, [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    if (fileId.empty()) {
        skip("the uploaded validation job is deleted again", "upload did not produce a file id");
    } else {
        check("the uploaded validation job is deleted again", [&]() -> std::string {
            // LIVE: a just-accepted job answers "File cannot be found" for a
            // short while, so the delete is retried rather than leaving it behind.
            std::string last;
            for (int attempt = 0; attempt < 12; ++attempt) {
                bool done = false;
                zb.deleteFile(fileId, [&](ZBDeleteFileResponse) { done = true; },
                    [&](ZBErrorResponse error) { last = redact(error.toString()); });
                if (done) {
                    return "";
                }
                std::this_thread::sleep_for(std::chrono::seconds(10));
            }
            return "could not delete " + fileId + " after retrying: " + last;
        });
    }
}

// -- validation API, reversible account changes ------------------------------

void validationChanges() {
    std::cout << "\nValidation API (ZEROBOUNCE_LIVE_CHANGES=1)\n";
    ZeroBounce& zb = validationClient();
    const std::string value =
        "zb-sdk-live-" + std::to_string(std::time(nullptr)) + ".example.com";

    bool added = false;
    check("addFilter creates a reversible allow rule", [&]() -> std::string {
        std::string problem = "no response";
        zb.addFilter("allow", "domain", value, [&](std::string) {
            added = true;
            problem.clear();
        }, [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    if (!added) {
        skip("the test filter is removed again", "filter was not created");
        return;
    }
    check("the test filter is removed again", [&]() -> std::string {
        std::string problem = "no response";
        zb.deleteFilter("allow", "domain", value, [&](std::string) { problem.clear(); },
            [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });
}

// -- Warmup API --------------------------------------------------------------

ZeroBounceWarmup& warmupClient() {
    static ZeroBounceWarmup zb;
    static bool ready = false;
    if (!ready) {
        zb.initialize(warmupKey());
        ready = true;
    }
    return zb;
}

void warmupFree() {
    std::cout << "\nWarmup API (reads)\n";
    ZeroBounceWarmup& zb = warmupClient();

    auto read = [&](const std::string& name,
                    const std::function<void(OnRawJsonSuccessCallback, OnErrorCallback)>& call) {
        check(name, [&]() -> std::string {
            std::string problem = "no response";
            call([&](std::string) { problem.clear(); },
                 [&](ZBErrorResponse error) { problem = error.toString(); });
            return problem;
        });
    };

    read("listSendingAccounts", [&](auto ok, auto err) { zb.listSendingAccounts(ok, err); });
    read("listFromAddresses", [&](auto ok, auto err) { zb.listFromAddresses({}, ok, err); });
    read("getEngagementRules", [&](auto ok, auto err) { zb.getEngagementRules(ok, err); });
    read("getSeeds", [&](auto ok, auto err) { zb.getSeeds(ok, err); });
    read("getSeedCount", [&](auto ok, auto err) { zb.getSeedCount(ok, err); });
    read("getProgressToday", [&](auto ok, auto err) { zb.getProgressToday(ok, err); });
    read("getSentToday", [&](auto ok, auto err) { zb.getSentToday(ok, err); });
    read("getEmailsPerDay", [&](auto ok, auto err) { zb.getEmailsPerDay(ok, err); });

    check("downloadSeeds returns CSV text", [&]() -> std::string {
        std::string problem = "no response";
        zb.downloadSeeds([&](std::string csv) {
            problem = csv.empty() ? "empty CSV body" : "";
            if (!csv.empty() && csv.front() == '{') {
                problem = "got JSON instead of CSV";
            }
        }, [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    check("LIVE: checkDomainDmarc rejects a bare domain before sending", [&]() -> std::string {
        std::string message = expectFailure("checkDomainDmarc", [&](auto ok, auto err) {
            zb.checkDomainDmarc("example.com", std::nullopt, [&](std::string) { ok(); }, err);
        });
        if (message.empty()) {
            return "checkDomainDmarc with a domain unexpectedly succeeded";
        }
        return message.find("email address") != std::string::npos
            ? ""
            : "unexpected message: " + message;
    });

    check("LIVE: checkDomainDmarc accepts an email address", [&]() -> std::string {
        std::string problem = "no response";
        zb.checkDomainDmarc("sender@example.com", std::nullopt,
            [&](std::string) { problem.clear(); },
            [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    // The statistics endpoints need a from-address this team actually has; the
    // API answers "Sending account ... was not found" for anything else.
    std::string known;
    zb.listSendingAccounts([&](std::string body) {
        try {
            json parsed = json::parse(body);
            const json* list = &parsed;
            if (parsed.is_object() && parsed.contains("data")) {
                list = &parsed["data"];
            }
            if (list->is_array() && !list->empty()) {
                for (const char* key : {"from_address", "c_from", "fromAddress"}) {
                    if ((*list)[0].contains(key) && (*list)[0][key].is_string()) {
                        known = (*list)[0][key].get<std::string>();
                        break;
                    }
                }
            }
        } catch (const std::exception&) {
        }
    }, [](ZBErrorResponse) {});

    if (known.empty()) {
        for (const char* name : {"statistics for a registered from-address",
                                 "statistics per day over a short range",
                                 "detailed statistics and the CSV download for that address",
                                 "from-address statistics"}) {
            skip(name, "this team has no sending account");
        }
    } else {
        read("statistics for a registered from-address",
             [&](auto ok, auto err) { zb.getStatistics(known, ok, err); });
        check("statistics per day over a short range", [&]() -> std::string {
            char start[11];
            char end[11];
            std::tm from = daysAgo(6);
            std::tm to = daysAgo(0);
            std::strftime(start, sizeof(start), "%Y-%m-%d", &from);
            std::strftime(end, sizeof(end), "%Y-%m-%d", &to);
            std::string problem = "no response";
            zb.getStatisticsPerDay(known, start, end, [&](std::string) { problem.clear(); },
                [&](ZBErrorResponse error) { problem = error.toString(); });
            return problem;
        });
        check("detailed statistics and the CSV download for that address", [&]() -> std::string {
            std::string problem = "no response";
            zb.getStatisticsDetailed(known, [&](std::string) { problem.clear(); },
                [&](ZBErrorResponse error) { problem = error.toString(); });
            if (!problem.empty()) {
                return problem;
            }
            problem = "no CSV body";
            zb.downloadStatistics(known, [&](std::string) { problem.clear(); },
                [&](ZBErrorResponse error) { problem = error.toString(); });
            return problem;
        });
        read("from-address statistics",
             [&](auto ok, auto err) { zb.getFromAddressStatistics(known, ok, err); });
    }

    check("an unknown Warmup resource fails with a status code", [&]() -> std::string {
        std::string problem;
        zb.getCustomEngagement("no-such-address@example.com", [](std::string) {},
            [&](ZBErrorResponse error) {
                if (error.statusCode == 0) {
                    problem = "no status code on the error";
                } else if (error.toString().find(warmupKey()) != std::string::npos) {
                    problem = "the key leaked into the message";
                }
            });
        return problem;
    });
}

void warmupChanges() {
    std::cout << "\nWarmup API (ZEROBOUNCE_LIVE_CHANGES=1)\n";
    ZeroBounceWarmup& zb = warmupClient();
    const std::string address =
        "zb-sdk-live-" + std::to_string(std::time(nullptr)) + "@example.com";

    bool added = false;
    check("a self-managed sending account can be added", [&]() -> std::string {
        WarmupAddSendingAccountOptions options;
        options.engagementRuleId = 0;
        options.ignoreConflict = true;
        std::string problem = "no response";
        zb.addSendingAccount(address, "self_managed", options, [&](std::string) {
            added = true;
            problem.clear();
        }, [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });

    if (!added) {
        skip("the test sending account is removed again", "account was not created");
        return;
    }
    check("the test sending account is removed again", [&]() -> std::string {
        std::string problem = "no response";
        zb.deleteSendingAccount(address, [&](std::string) { problem.clear(); },
            [&](ZBErrorResponse error) { problem = error.toString(); });
        return problem;
    });
}

}  // namespace

int main() {
    const std::string key = apiKey();
    const std::string warmup = warmupKey();
    const bool spend = flag("ZEROBOUNCE_LIVE_SPEND");
    const bool changes = flag("ZEROBOUNCE_LIVE_CHANGES");

    std::cout << "ZeroBounce live API suite\n";
    std::cout << "  validation key: " << (key.empty() ? "MISSING" : "set") << "\n";
    std::cout << "  warmup key:     " << (warmup.empty() ? "MISSING" : "set") << "\n";
    std::cout << "  flags:          SPEND=" << (spend ? 1 : 0)
              << " CHANGES=" << (changes ? 1 : 0) << "\n\n";

    if (!key.empty()) {
        validationFree();
        if (spend) {
            validationSpend();
        }
        if (changes) {
            validationChanges();
        }
    }
    if (!warmup.empty()) {
        warmupFree();
        if (changes) {
            warmupChanges();
        }
    }

    std::cout << "\n" << passed << " passed, " << failed << " failed, " << skipped
              << " skipped\n";
    if (!failures.empty()) {
        std::cout << "\nFailures:\n";
        for (const auto& line : failures) {
            std::cout << "  - " << line << "\n";
        }
    }
    return failed == 0 ? 0 : 1;
}
