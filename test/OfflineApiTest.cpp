#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include <cpr/cpr.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "ZeroBounce/ZeroBounce.h"

using json = nlohmann::json;

namespace {

struct RecordedRequest {
    std::string method;
    std::string host;
    std::string path;
    std::string apiKey;
    /// The Authorization header, for the Inbox Placement Tester's Bearer key.
    std::string authorization;
    std::map<std::string, std::string> fields;
    /// The full request URL, for assertions on path encoding and query order.
    std::string url;
    /// The raw request body, for assertions on JSON shape.
    std::string body;
};

std::string urlDecode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(value[i + 1]);
            int lo = hex(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i] == '+' ? ' ' : value[i]);
    }
    return out;
}

void parseQuery(const std::string& query, std::map<std::string, std::string>& fields) {
    size_t start = 0;
    while (start < query.size()) {
        size_t amp = query.find('&', start);
        if (amp == std::string::npos) {
            amp = query.size();
        }
        std::string pair = query.substr(start, amp - start);
        size_t eq = pair.find('=');
        if (eq == std::string::npos) {
            fields[urlDecode(pair)] = "";
        } else {
            fields[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
        }
        start = amp + 1;
    }
}

struct ParsedUrl {
    std::string host;
    std::string path;
    std::map<std::string, std::string> query;
};

ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl parsed;
    size_t hostStart = 0;
    auto scheme = url.find("://");
    if (scheme != std::string::npos) {
        hostStart = scheme + 3;
    }
    auto pathStart = url.find('/', hostStart);
    auto queryStart = url.find('?', hostStart);
    if (pathStart == std::string::npos) {
        parsed.host = url.substr(hostStart);
        parsed.path = "/";
    } else {
        parsed.host = url.substr(hostStart, pathStart - hostStart);
        if (queryStart == std::string::npos) {
            parsed.path = url.substr(pathStart);
        } else {
            parsed.path = url.substr(pathStart, queryStart - pathStart);
        }
    }
    if (queryStart != std::string::npos) {
        parseQuery(url.substr(queryStart + 1), parsed.query);
    }
    return parsed;
}

void flattenJson(const json& body, std::map<std::string, std::string>& fields) {
    if (!body.is_object()) {
        return;
    }
    for (auto it = body.begin(); it != body.end(); ++it) {
        if (it.value().is_string()) {
            fields[it.key()] = it.value().get<std::string>();
        } else if (it.value().is_boolean()) {
            fields[it.key()] = it.value().get<bool>() ? "true" : "false";
        } else if (it.value().is_number_integer()) {
            fields[it.key()] = std::to_string(it.value().get<int>());
        } else if (it.value().is_null()) {
            fields[it.key()] = "null";
        } else {
            fields[it.key()] = it.value().dump();
        }
    }
}

std::string htmlAccessDeniedPage() {
    std::string page =
        "<!DOCTYPE html><html lang=\"en-US\"><head><title>Access denied | zerobounce.net | Zero Bounce</title></head><body>";
    page.reserve(page.size() + 20000 * 16);
    for (int i = 0; i < 20000; ++i) {
        page += "<p>blocked</p>";
    }
    page += "</body></html>";
    return page;
}

const std::string& accessDeniedPage() {
    static const std::string page = htmlAccessDeniedPage();
    return page;
}

std::string jsonEscape(const std::string& value) {
    return json(value).dump();
}

class FakeApi : public BaseRequestHandler {
public:
    std::vector<RecordedRequest> take() {
        std::lock_guard<std::mutex> lock(mu_);
        return records_;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mu_);
        records_.clear();
    }

    size_t size() {
        std::lock_guard<std::mutex> lock(mu_);
        return records_.size();
    }

protected:
    cpr::Response doGet(const cpr::Url& url) override { return respond("GET", url.str(), {}, "", {}); }

    cpr::Response doGet(const cpr::Url& url, const cpr::Header& header) override {
        return respond("GET", url.str(), {}, "", header);
    }

    cpr::Response doGetWithParameters(const cpr::Url& url, const cpr::Parameters& parameters) override {
        cpr::CurlHolder holder;
        std::string query = parameters.GetContent(holder);
        return respond("GET", url.str(), {}, query, {});
    }

    cpr::Response doPost(const cpr::Url& url, const cpr::Header& header, const cpr::Body& body) override {
        return respond("POST", url.str(), {}, body.str(), header);
    }

    cpr::Response doPut(const cpr::Url& url, const cpr::Header& header, const cpr::Body& body) override {
        return respond("PUT", url.str(), {}, std::string(body), header);
    }

    cpr::Response doDelete(const cpr::Url& url, const cpr::Header& header) override {
        return respond("DELETE", url.str(), {}, "", header);
    }

    cpr::Response doPost(const cpr::Url& url, const cpr::Header& header, const cpr::Multipart& multipart) override {
        std::map<std::string, std::string> fields;
        for (const auto& part : multipart.parts) {
            fields[part.name] = part.is_file ? "<file>" : part.value;
        }
        return respond("POST", url.str(), fields, "", header);
    }

private:
    cpr::Response respond(
        const std::string& method,
        const std::string& url,
        std::map<std::string, std::string> fields,
        const std::string& extra,
        const cpr::Header& header
    ) {
        ParsedUrl parsed = parseUrl(url);
        fields.insert(parsed.query.begin(), parsed.query.end());
        if (!extra.empty()) {
            if (!extra.empty() && extra.front() == '{') {
                try {
                    flattenJson(json::parse(extra), fields);
                } catch (...) {
                    fields["__raw"] = extra;
                }
            } else if (extra.find('=') != std::string::npos && extra.front() != '<') {
                parseQuery(extra, fields);
            }
            fields["__raw"] = extra;
        }

        RecordedRequest recorded;
        recorded.method = method;
        recorded.host = parsed.host;
        recorded.path = parsed.path;
        recorded.fields = fields;
        recorded.url = url;
        recorded.body = extra;
        auto authIt = header.find("Authorization");
        recorded.authorization = authIt == header.end() ? "" : authIt->second;
        auto keyIt = fields.find("api_key");
        recorded.apiKey = keyIt == fields.end() ? "" : keyIt->second;
        // The Inbox Placement Tester authenticates with a Bearer header rather
        // than an api_key parameter, so the key can arrive either way.
        if (recorded.apiKey.empty() && recorded.authorization.rfind("Bearer ", 0) == 0) {
            recorded.apiKey = recorded.authorization.substr(7);
        }

        {
            std::lock_guard<std::mutex> lock(mu_);
            records_.push_back(recorded);
        }

        std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<int> delay(0, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay(rng)));

        return answer(recorded);
    }

    static cpr::Response answer(const RecordedRequest& request) {
        const std::string& key = request.apiKey;
        cpr::Response response;
        if (key.rfind("html-403-", 0) == 0 || key.rfind("html-200-", 0) == 0) {
            response.status_code = key.rfind("html-403-", 0) == 0 ? 403 : 200;
            response.text = accessDeniedPage();
            response.header["Content-Type"] = "text/html; charset=UTF-8";
            return response;
        }
        if (key.rfind("success-false-", 0) == 0) {
            // LIVE: an unknown file id comes back as an HTTP 200 whose body
            // says the call failed.
            response.status_code = 200;
            response.text = R"({"success":false,"message":"File cannot be found."})";
            response.header["Content-Type"] = "application/json";
            return response;
        }
        if (key.rfind("empty-", 0) == 0) {
            response.status_code = 200;
            response.text = "";
            response.header["Content-Type"] = "application/json";
            return response;
        }
        if (key.rfind("malformed-", 0) == 0) {
            response.status_code = 200;
            response.text = "<<<not-json>>>" + std::string(5000, 'x');
            response.header["Content-Type"] = "application/json";
            return response;
        }
        if (key.rfind("cf-json-", 0) == 0) {
            response.status_code = 403;
            response.text =
                R"({"type":"https://developers.cloudflare.com/support/troubleshooting/http-status-codes/cloudflare-1xxx-errors/error-1020/","title":"Error 1020: Access denied","status":403,"detail":"The request was blocked by a Cloudflare firewall rule configured by the site owner.","error_code":1020,"cloudflare_error":true,"what_you_should_do":"Do not retry. This block is intentional."})";
            response.header["Content-Type"] = "application/json";
            return response;
        }
        if (key.rfind("double-error-", 0) == 0) {
            response.status_code = 400;
            response.text = "Error: Error: api_key is invalid";
            response.header["Content-Type"] = "text/plain";
            return response;
        }
        if (request.path.find("validatebatch") != std::string::npos && key.rfind("batch-account-", 0) == 0) {
            response.status_code = 200;
            response.text =
                R"({"email_batch":[],"errors":[{"email_address":"all","error":"Invalid API Key or your account ran out of credits"}]})";
            response.header["Content-Type"] = "application/json";
            return response;
        }
        if (request.path.find("validatebatch") != std::string::npos && key.rfind("batch-partial-", 0) == 0) {
            response.status_code = 200;
            response.text =
                R"({"email_batch":[{"address":"good@example.com","status":"valid"}],"errors":[{"email_address":"bad@","error":"Invalid email address"}]})";
            response.header["Content-Type"] = "application/json";
            return response;
        }

        response.status_code = 200;
        response.header["Content-Type"] = "application/json";
        const std::string quoted = jsonEscape(key);
        auto endsWith = [](const std::string& value, const std::string& suffix) {
            return value.size() >= suffix.size() &&
                value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        if (request.path.find("/api/ipt") != std::string::npos) {
            if (key.rfind("ipt-detail", 0) == 0) {
                // The Inbox Placement Tester (Django REST framework) reports "detail".
                response.status_code = 404;
                response.text = "{\"detail\":\"Inbox placement test not found.\"}";
            } else {
                response.text = "{\"ok\":true,\"echo_api_key\":" + quoted + "}";
            }
        } else if (request.path.find("listS3files") != std::string::npos) {
            response.text = "[{\"name\":" + quoted + ",\"size\":1,\"last_modified\":\"\"}]";
} else if (request.path.find("getfile") != std::string::npos) {
            response.header["Content-Type"] = "application/octet-stream";
            response.text = "email\n" + key + "@example.com\n";
        } else if (request.path.find("validatebatch") != std::string::npos) {
            response.text = "{\"email_batch\":[{\"address\":" + quoted + ",\"status\":\"valid\"}],\"errors\":[]}";
        } else if (endsWith(request.path, "/validate")) {
            response.text = "{\"address\":" + quoted + ",\"status\":\"valid\"}";
        } else if (endsWith(request.path, "/scoring")) {
            response.text = "{\"email\":" + quoted + ",\"score\":10}";
        } else if (request.path.find("getcredits") != std::string::npos) {
            response.text = "{\"Credits\":\"7\"}";
        } else if (request.path.find("getapiusage") != std::string::npos) {
            response.text = "{\"total\":1}";
        } else if (endsWith(request.path, "/activity")) {
            response.text = "{\"found\":true,\"active_in_days\":\"3\"}";
        } else if (request.path.find("filters") != std::string::npos ||
            (request.method == "GET" && request.path.find("listevaluator") != std::string::npos &&
                request.path.find("sendfile") == std::string::npos)) {
            response.text = "{\"echo_api_key\":" + quoted + "}";
        } else if (request.path.find("guessformat") != std::string::npos) {
            response.text = "{\"email\":" + quoted + ",\"domain\":" + quoted + ",\"other_domain_formats\":[]}";
        } else {
            response.text = "{\"success\":true,\"message\":\"ok\",\"file_id\":" + quoted +
                ",\"file_name\":\"f.csv\",\"email\":" + quoted + ",\"domain\":" + quoted +
                ",\"other_domain_formats\":[]}";
        }
        return response;
    }

    std::mutex mu_;
    std::vector<RecordedRequest> records_;
};

class OfflineClient : public ZeroBounce {
public:
    OfflineClient(FakeApi& api, std::string key) {
        initialize(std::move(key));
        requestHandler = &api;
    }
};

struct ErrorCapture {
    bool called = false;
    std::string text;
};

void expectShort(const std::string& message) {
    EXPECT_LT(message.size(), 500u) << message.substr(0, 120);
    EXPECT_EQ(message.find("<html"), std::string::npos);
    EXPECT_EQ(message.find("<!DOCTYPE"), std::string::npos);
    EXPECT_EQ(message.find("<p>blocked</p>"), std::string::npos);
    EXPECT_EQ(message.find("Error: Error:"), std::string::npos);
    EXPECT_EQ(message.find("[json.exception"), std::string::npos);
}

ErrorCapture captureError(const ZBErrorResponse& error) {
    ErrorCapture captured;
    captured.called = true;
    captured.text = error.errors.empty() ? "" : error.errors.front();
    for (const auto& message : error.errors) {
        expectShort(message);
    }
    return captured;
}

RecordedRequest onlyRequest(FakeApi& api) {
    auto records = api.take();
    EXPECT_EQ(records.size(), 1u);
    if (records.empty()) {
        return {};
    }
    return records.front();
}

const char* kCsv = "../email_file.csv";

}  // namespace

TEST(OfflineApi, GetCreditsReachesEndpoint) {
    FakeApi api;
    OfflineClient client(api, "credits-key");
    bool ok = false;
    client.getCredits(
        [&](ZBCreditsResponse response) {
            ok = true;
            EXPECT_EQ(response.credits, 7);
        },
        [&](ZBErrorResponse error) { FAIL() << error.toString(); }
    );
    EXPECT_TRUE(ok);
    auto request = onlyRequest(api);
    EXPECT_EQ(request.method, "GET");
    EXPECT_EQ(request.host, "api.zerobounce.net");
    EXPECT_EQ(request.path, "/v2/getcredits");
    EXPECT_EQ(request.apiKey, "credits-key");
}

#if ZB_OFFLINE_CURRENT
TEST(OfflineApi, ValidateOmitsUnsetOptionsAndEncodesFalse) {
    FakeApi api;
    OfflineClient client(api, "validate-key");
    ZBValidateOptions options;
    options.activityData = false;
    options.verifyPlus = true;
    std::string echoed;
    client.validate(
        "person@example.com",
        "",
        options,
        [&](ZBValidateResponse response) { echoed = response.address; },
        [&](ZBErrorResponse error) { FAIL() << error.toString(); }
    );
    EXPECT_EQ(echoed, "validate-key");
    auto request = onlyRequest(api);
    EXPECT_EQ(request.method, "GET");
    EXPECT_EQ(request.path, "/v2/validate");
    EXPECT_EQ(request.fields["email"], "person@example.com");
    EXPECT_EQ(request.fields["ip_address"], "");
    EXPECT_EQ(request.fields["activity_data"], "false");
    EXPECT_EQ(request.fields["verify_plus"], "true");
    EXPECT_EQ(request.fields.count("timeout"), 0u);
}

TEST(OfflineApi, ValidatePostFormAndTimeout) {
    FakeApi api;
    OfflineClient client(api, "post-key");
    ZBValidateOptions options;
    options.usePost = true;
    options.timeout = 15;
    client.validate(
        "person@example.com",
        "203.0.113.5",
        options,
        [&](ZBValidateResponse) {},
        [&](ZBErrorResponse error) { FAIL() << error.toString(); }
    );
    auto request = onlyRequest(api);
    EXPECT_EQ(request.method, "POST");
    EXPECT_EQ(request.host, "api.zerobounce.net");
    EXPECT_EQ(request.path, "/v2/validate");
    EXPECT_EQ(request.fields["api_key"], "post-key");
    EXPECT_EQ(request.fields["email"], "person@example.com");
    EXPECT_EQ(request.fields["ip_address"], "203.0.113.5");
    EXPECT_EQ(request.fields["timeout"], "15");
    EXPECT_EQ(request.fields.count("activity_data"), 0u);
}

#endif

TEST(OfflineApi, ValidateBatchUsesApiHostAndOmitsUnsetOptions) {
    FakeApi api;
    OfflineClient client(api, "batch-key");
    std::string echoed;
    client.validateBatch(
        {{"a@example.com", ""}, {"b@example.com", "203.0.113.9"}},
        [&](ZBValidateBatchResponse response) {
            ASSERT_EQ(response.emailBatch.size(), 1u);
            echoed = response.emailBatch.front().address;
            EXPECT_TRUE(response.errors.empty());
        },
        [&](ZBErrorResponse error) { FAIL() << error.toString(); }
    );
    EXPECT_EQ(echoed, "batch-key");
    auto request = onlyRequest(api);
    EXPECT_EQ(request.method, "POST");
    EXPECT_EQ(request.host, "api.zerobounce.net");
    EXPECT_EQ(request.path, "/v2/validatebatch");
    EXPECT_EQ(request.fields["api_key"], "batch-key");
    EXPECT_NE(request.fields["email_batch"].find("\"email_address\":\"a@example.com\""), std::string::npos);
    EXPECT_NE(request.fields["email_batch"].find("\"ip_address\":null"), std::string::npos);
    EXPECT_NE(request.fields["email_batch"].find("\"ip_address\":\"203.0.113.9\""), std::string::npos);
    EXPECT_EQ(request.fields.count("timeout"), 0u);
    EXPECT_EQ(request.fields.count("activity_data"), 0u);
    EXPECT_EQ(request.fields.count("verify_plus"), 0u);
}

#if ZB_OFFLINE_CURRENT
TEST(OfflineApi, ValidateBatchSendsExplicitOptions) {
    FakeApi api;
    OfflineClient client(api, "batch-opt");
    ZBValidateOptions options;
    options.timeout = 30;
    options.activityData = false;
    options.verifyPlus = true;
    client.validateBatch(
        {{"a@example.com"}},
        options,
        [&](ZBValidateBatchResponse) {},
        [&](ZBErrorResponse error) { FAIL() << error.toString(); }
    );
    auto request = onlyRequest(api);
    EXPECT_EQ(request.fields["timeout"], "30");
    EXPECT_EQ(request.fields["activity_data"], "false");
    EXPECT_EQ(request.fields["verify_plus"], "true");
}

#endif

TEST(OfflineApi, SendFileEncodesBooleansAndKeepsZeroColumn) {
    FakeApi api;
    OfflineClient client(api, "file-key");
    SendFileOptions options;
    options.returnUrl = "https://example.com/hook";
    options.allowPhase2IsSet = true;
    options.allowPhase2 = false;
    options.removeDuplicate = false;
    options.hasHeaderRow = true;
    client.sendFile(
        kCsv,
        0,
        options,
        [&](ZBSendFileResponse response) { EXPECT_EQ(response.fileId, "file-key"); },
        [&](ZBErrorResponse error) { FAIL() << error.toString(); }
    );
    auto request = onlyRequest(api);
    EXPECT_EQ(request.method, "POST");
    EXPECT_EQ(request.host, "bulkapi.zerobounce.net");
    EXPECT_EQ(request.path, "/v2/sendfile");
    EXPECT_EQ(request.fields["api_key"], "file-key");
    EXPECT_EQ(request.fields["email_address_column"], "0");
    EXPECT_EQ(request.fields["file"], "<file>");
    EXPECT_EQ(request.fields["has_header_row"], "true");
    EXPECT_EQ(request.fields["remove_duplicate"], "false");
    EXPECT_EQ(request.fields["allow_phase_2"], "false");
    EXPECT_EQ(request.fields["return_url"], "https://example.com/hook");
    EXPECT_EQ(request.fields.count("first_name_column"), 0u);
    EXPECT_EQ(request.fields.count("last_name_column"), 0u);
    EXPECT_EQ(request.fields.count("gender_column"), 0u);
    EXPECT_EQ(request.fields.count("ip_address_column"), 0u);
}

TEST(OfflineApi, SendFileSendsSetNameColumn) {
    FakeApi api;
    OfflineClient client(api, "cols");
    SendFileOptions options;
    options.firstNameColumn = 2;
    client.sendFile(kCsv, 1, options, [&](ZBSendFileResponse) {}, [&](ZBErrorResponse error) { FAIL() << error.toString(); });
    auto request = onlyRequest(api);
    EXPECT_EQ(request.fields["email_address_column"], "1");
    EXPECT_EQ(request.fields["first_name_column"], "2");
    EXPECT_EQ(request.fields.count("allow_phase_2"), 0u);
}

TEST(OfflineApi, ScoringSendFileOmitsValidationOnlyFields) {
    FakeApi api;
    OfflineClient client(api, "score-file");
    SendFileOptions options;
    options.firstNameColumn = 2;
    options.allowPhase2IsSet = true;
    options.allowPhase2 = true;
    client.scoringSendFile(kCsv, 1, options, [&](ZBSendFileResponse) {}, [&](ZBErrorResponse error) { FAIL() << error.toString(); });
    auto request = onlyRequest(api);
    EXPECT_EQ(request.host, "bulkapi.zerobounce.net");
    EXPECT_EQ(request.path, "/v2/scoring/sendfile");
    EXPECT_EQ(request.fields.count("first_name_column"), 0u);
    EXPECT_EQ(request.fields.count("allow_phase_2"), 0u);
    EXPECT_EQ(request.fields["has_header_row"], "true");
}

TEST(OfflineApi, FileStatusAndDeleteUseBulkHost) {
    FakeApi api;
    OfflineClient client(api, "status-key");
    client.fileStatus("file-1", [&](ZBFileStatusResponse response) { EXPECT_EQ(response.fileId, "status-key"); }, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto status = onlyRequest(api);
    EXPECT_EQ(status.path, "/v2/filestatus");
    EXPECT_EQ(status.host, "bulkapi.zerobounce.net");
    EXPECT_EQ(status.fields["file_id"], "file-1");
    api.clear();
    client.scoringFileStatus("file-2", [&](ZBFileStatusResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(onlyRequest(api).path, "/v2/scoring/filestatus");
    api.clear();
    client.deleteFile("file-1", [&](ZBDeleteFileResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(onlyRequest(api).path, "/v2/deletefile");
    api.clear();
    client.scoringDeleteFile("file-2", [&](ZBDeleteFileResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(onlyRequest(api).path, "/v2/scoring/deletefile");
}

TEST(OfflineApi, GetFileWithOptionsParameterOrder) {
    FakeApi api;
    OfflineClient client(api, "getfile-key");
    GetFileOptions options;
    options.downloadType = ZBDownloadType::Phase2;
    options.activityData = true;
    bool ok = false;
    client.getFileWithOptions(
        "file-9",
        "../test/downloads/offline-get.csv",
        options,
        [&](ZBGetFileResponse response) {
            ok = true;
            std::ifstream in(response.localFilePath);
            std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            EXPECT_NE(body.find("getfile-key@example.com"), std::string::npos);
            EXPECT_EQ(body.find("<html"), std::string::npos);
        },
        [&](ZBErrorResponse error) { FAIL() << error.toString(); }
    );
    EXPECT_TRUE(ok);
    auto request = onlyRequest(api);
    EXPECT_EQ(request.path, "/v2/getfile");
    EXPECT_EQ(request.host, "bulkapi.zerobounce.net");
    EXPECT_EQ(request.fields["file_id"], "file-9");
    EXPECT_EQ(request.fields["download_type"], "phase_2");
    EXPECT_EQ(request.fields["activity_data"], "true");

    api.clear();
    GetFileOptions scoringOptions;
    scoringOptions.downloadType = ZBDownloadType::Phase1;
    scoringOptions.activityData = true;
    client.scoringGetFileWithOptions(
        "file-8",
        "../test/downloads/offline-score.csv",
        scoringOptions,
        [&](ZBGetFileResponse) {},
        [&](ZBErrorResponse error) { FAIL() << error.toString(); }
    );
    auto scoring = onlyRequest(api);
    EXPECT_EQ(scoring.path, "/v2/scoring/getfile");
    EXPECT_EQ(scoring.fields["download_type"], "phase_1");
    EXPECT_EQ(scoring.fields.count("activity_data"), 0u);
}

TEST(OfflineApi, ActivityUsageAndScoreEndpoints) {
    FakeApi api;
    OfflineClient client(api, "usage-key");
    std::tm start{};
    start.tm_year = 118;
    start.tm_mon = 0;
    start.tm_mday = 1;
    std::tm end{};
    end.tm_year = 118;
    end.tm_mon = 0;
    end.tm_mday = 31;
    client.getApiUsage(start, end, [&](ZBGetApiUsageResponse response) { EXPECT_EQ(response.total, 1); }, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto usage = onlyRequest(api);
    EXPECT_EQ(usage.path, "/v2/getapiusage");
    EXPECT_EQ(usage.fields["start_date"], "2018-01-01");
    EXPECT_EQ(usage.fields["end_date"], "2018-01-31");

    api.clear();
    client.getActivityData("person@example.com", [&](ZBActivityDataResponse response) { EXPECT_TRUE(response.found); }, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto activity = onlyRequest(api);
    EXPECT_EQ(activity.path, "/v2/activity");
    EXPECT_EQ(activity.fields["email"], "person@example.com");

#if ZB_OFFLINE_CURRENT
    api.clear();
    std::string echoed;
    client.scoreEmail("person@example.com", [&](ZBScoreEmailResponse response) { echoed = response.email.value_or(""); }, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(echoed, "usage-key");
    auto score = onlyRequest(api);
    EXPECT_EQ(score.method, "GET");
    EXPECT_EQ(score.path, "/v2/scoring");
    EXPECT_EQ(score.host, "api.zerobounce.net");

    api.clear();
    client.scoreEmailPost("person@example.com", [&](ZBScoreEmailResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto posted = onlyRequest(api);
    EXPECT_EQ(posted.method, "POST");
    EXPECT_EQ(posted.path, "/v2/scoring");
    EXPECT_EQ(posted.fields["email"], "person@example.com");
#endif
}

TEST(OfflineApi, FinderAndDomainSearch) {
    FakeApi api;
    OfflineClient client(api, "finder-key");
    client.findEmailByDomain("example.com", "Ada", "M", "Lovelace", [&](ZBFindEmailResponse response) { EXPECT_EQ(response.email, "finder-key"); }, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto byDomain = onlyRequest(api);
    EXPECT_EQ(byDomain.path, "/v2/guessformat");
    EXPECT_EQ(byDomain.fields["domain"], "example.com");
    EXPECT_EQ(byDomain.fields["first_name"], "Ada");
    EXPECT_EQ(byDomain.fields["middle_name"], "M");
    EXPECT_EQ(byDomain.fields["last_name"], "Lovelace");
    EXPECT_EQ(byDomain.fields.count("company_name"), 0u);

    api.clear();
    client.findEmailByCompanyName("ZeroBounce", "Ada", [&](ZBFindEmailResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto byCompany = onlyRequest(api);
    EXPECT_EQ(byCompany.fields["company_name"], "ZeroBounce");
    EXPECT_EQ(byCompany.fields["first_name"], "Ada");
    EXPECT_EQ(byCompany.fields.count("domain"), 0u);
    EXPECT_EQ(byCompany.fields.count("last_name"), 0u);

    api.clear();
    client.searchDomainByDomain("example.com", [&](ZBDomainSearchResponse response) { EXPECT_EQ(response.domain, "finder-key"); }, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(onlyRequest(api).fields.count("company_name"), 0u);

    api.clear();
    client.searchDomainByCompanyName("ZeroBounce", [&](ZBDomainSearchResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto company = onlyRequest(api);
    EXPECT_EQ(company.fields["company_name"], "ZeroBounce");
    EXPECT_EQ(company.fields.count("domain"), 0u);
}

#if ZB_OFFLINE_CURRENT
TEST(OfflineApi, Priority3EndpointsAndZeroColumn) {
    FakeApi api;
    OfflineClient client(api, "p3-key");
    EmailFinderSendFileOptions finder;
    finder.hasHeaderRow = false;
    finder.firstNameColumn = 0;
    client.sendEmailFinderFile(kCsv, 1, &finder, [&](ZBSendFileResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto sent = onlyRequest(api);
    EXPECT_EQ(sent.host, "bulkapi.zerobounce.net");
    EXPECT_EQ(sent.path, "/email-finder/sendfile");
    EXPECT_EQ(sent.fields["domain_column"], "1");
    EXPECT_EQ(sent.fields["has_header_row"], "false");
    EXPECT_EQ(sent.fields.count("first_name_column"), 0u);

    api.clear();
    client.emailFinderFileStatus("fid", [&](ZBFileStatusResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(onlyRequest(api).path, "/email-finder/filestatus");
    api.clear();
    client.emailFinderGetFile("fid", "../test/downloads/finder.csv", [&](ZBGetFileResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(onlyRequest(api).path, "/email-finder/getfile");
    api.clear();
    client.emailFinderDeleteFile("fid", [&](ZBDeleteFileResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(onlyRequest(api).path, "/email-finder/deletefile");

    api.clear();
    DomainSearchSendFileOptions domainOpts;
    domainOpts.hasHeaderRow = false;
    client.sendDomainSearchFile(kCsv, 2, &domainOpts, [&](ZBSendFileResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto domain = onlyRequest(api);
    EXPECT_EQ(domain.path, "/domain-search/sendfile");
    EXPECT_EQ(domain.fields["domain_column"], "2");
    EXPECT_EQ(domain.fields["has_header_row"], "false");

    api.clear();
    client.domainSearchFileStatus("fid", [&](ZBFileStatusResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(onlyRequest(api).path, "/domain-search/filestatus");
    api.clear();
    client.domainSearchGetFile("fid", "../test/downloads/domain.csv", [&](ZBGetFileResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(onlyRequest(api).path, "/domain-search/getfile");
    api.clear();
    client.domainSearchDeleteFile("fid", [&](ZBDeleteFileResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(onlyRequest(api).path, "/domain-search/deletefile");

    api.clear();
    client.sendListEvaluatorFile(kCsv, 1, [&](ZBSendFileResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto evaluator = onlyRequest(api);
    EXPECT_EQ(evaluator.path, "/v2/listevaluator/");
    EXPECT_EQ(evaluator.fields["email_address_column"], "1");

    api.clear();
    std::string raw;
    client.getListEvaluatorResult("fid", [&](std::string body) { raw = body; }, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_NE(raw.find("p3-key"), std::string::npos);
    EXPECT_EQ(onlyRequest(api).path, "/v2/listevaluator/fid/");

    api.clear();
    client.sendEvaluatedFile(kCsv, [&](ZBSendFileResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(onlyRequest(api).path, "/v2/send-evaluated-file/");

    api.clear();
    client.addFilter("exclude", "domain", "example.com", [&](std::string) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto added = onlyRequest(api);
    EXPECT_EQ(added.method, "POST");
    EXPECT_EQ(added.host, "api.zerobounce.net");
    EXPECT_EQ(added.path, "/v2/filters/add");
    EXPECT_EQ(added.fields["rule"], "exclude");
    EXPECT_EQ(added.fields["target"], "domain");
    EXPECT_EQ(added.fields["value"], "example.com");

    api.clear();
    client.listFilters(false, [&](std::string body) { EXPECT_NE(body.find("p3-key"), std::string::npos); }, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto listed = onlyRequest(api);
    EXPECT_EQ(listed.method, "GET");
    EXPECT_EQ(listed.path, "/v2/filters/list");

    api.clear();
    client.listFilters(true, [&](std::string) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(onlyRequest(api).method, "POST");

    api.clear();
    client.deleteFilter("exclude", "domain", "example.com", [&](std::string) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(onlyRequest(api).path, "/v2/filters/delete");

    api.clear();
    ValidateS3FileOptions s3;
    s3.fileName = "inbox.csv";
    s3.emailAddressColumn = 0;
    s3.hasHeaderRow = false;
    s3.removeDuplicate = true;
    client.validateS3File(s3, [&](ZBSendFileResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto validated = onlyRequest(api);
    EXPECT_EQ(validated.method, "POST");
    EXPECT_EQ(validated.path, "/validateS3file");
    EXPECT_EQ(validated.fields["file_name"], "inbox.csv");
    EXPECT_EQ(validated.fields["email_address_column"], "0");
    EXPECT_EQ(validated.fields["has_header_row"], "false");
    EXPECT_EQ(validated.fields["remove_duplicate"], "true");
    EXPECT_EQ(validated.fields.count("remove_duplicates"), 0u);
    EXPECT_EQ(validated.fields.count("first_name_column"), 0u);

    api.clear();
    ScoreS3FileOptions score;
    score.fileName = "inbox.csv";
    client.scoreS3File(score, [&](ZBSendFileResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto scored = onlyRequest(api);
    EXPECT_EQ(scored.path, "/scoreS3file");
    EXPECT_EQ(scored.fields.count("email_address_column"), 0u);

    api.clear();
    client.exportS3File("fid", "bucket-out", [&](ZBSendFileResponse) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto exported = onlyRequest(api);
    EXPECT_EQ(exported.path, "/exportS3file");
    EXPECT_EQ(exported.fields["file_id"], "fid");
    EXPECT_EQ(exported.fields["export_bucket"], "bucket-out");

    api.clear();
    client.listS3files(std::nullopt, std::nullopt, [&](std::vector<ZBS3ListFileEntry> files) {
        ASSERT_EQ(files.size(), 1u);
        EXPECT_EQ(files.front().name, "p3-key");
    }, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto listedFiles = onlyRequest(api);
    EXPECT_EQ(listedFiles.path, "/listS3files");
    EXPECT_EQ(listedFiles.fields.count("bucket"), 0u);
    EXPECT_EQ(listedFiles.fields.count("search"), 0u);

    api.clear();
    client.listS3files(std::string("inbox"), std::string("csv"), [&](std::vector<ZBS3ListFileEntry>) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto filtered = onlyRequest(api);
    EXPECT_EQ(filtered.fields["bucket"], "inbox");
    EXPECT_EQ(filtered.fields["search"], "csv");

}

TEST(OfflineApi, S3AndSftpFileJobsSendTheDocumentedFieldsOnly) {
    FakeApi api;
    OfflineClient client(api, "p3-key");
    auto fail = [&](ZBErrorResponse e) { FAIL() << e.toString(); };

    BulkFileJobOptions finder;
    finder.fileName = "lists/leads.csv";
    finder.hasHeaderRow = true;
    finder.returnUrl = "https://example.com/done";
    client.emailFinderS3File(finder, [&](ZBSendFileResponse) {}, fail);
    auto found = onlyRequest(api);
    EXPECT_EQ(found.method, "POST");
    EXPECT_EQ(found.path, "/emailFinderS3file");
    EXPECT_EQ(found.fields["file_name"], "lists/leads.csv");
    // Booleans go on the wire as the strings "true"/"false".
    EXPECT_EQ(found.fields["has_header_row"], "true");
    EXPECT_EQ(found.fields["return_url"], "https://example.com/done");
    // Unset options stay off the request entirely.
    EXPECT_EQ(found.fields.count("remove_duplicates"), 0u);

    api.clear();
    BulkFileJobOptions domains;
    domains.fileName = "lists/leads.csv";
    domains.hasHeaderRow = false;
    domains.removeDuplicates = true;
    domains.returnUrl = "https://example.com/done";
    client.domainSearchS3File(domains, [&](ZBSendFileResponse) {}, fail);
    auto searched = onlyRequest(api);
    EXPECT_EQ(searched.path, "/domainSearchS3file");
    EXPECT_EQ(searched.fields["has_header_row"], "false");
    EXPECT_EQ(searched.fields["remove_duplicates"], "true");
    EXPECT_EQ(searched.fields["return_url"], "https://example.com/done");

    // LIVE: file_name is required here too, although the docs list it as
    // optional for this one endpoint.
    api.clear();
    BulkFileJobOptions named;
    named.fileName = "my_file.txt";
    named.returnUrl = "https://example.com/done";
    client.validateSFTPFile(named, [&](ZBSendFileResponse) {}, fail);
    auto validated = onlyRequest(api);
    EXPECT_EQ(validated.path, "/validateSFTPfile");
    EXPECT_EQ(validated.fields["file_name"], "my_file.txt");
    EXPECT_EQ(validated.fields["has_header_row"], "true");

    api.clear();
    client.scoreSFTPFile(named, [&](ZBSendFileResponse) {}, fail);
    EXPECT_EQ(onlyRequest(api).path, "/scoreSFTPfile");

    api.clear();
    client.emailFinderSFTPFile(named, [&](ZBSendFileResponse) {}, fail);
    EXPECT_EQ(onlyRequest(api).path, "/emailFinderSFTPfile");

    api.clear();
    client.domainSearchSFTPFile(named, [&](ZBSendFileResponse) {}, fail);
    EXPECT_EQ(onlyRequest(api).path, "/domainSearchSFTPfile");

    // exportSFTPfile takes export_path, not the export_bucket exportS3file uses.
    api.clear();
    client.exportSFTPFile("fid", std::string("/exports"), [&](ZBSendFileResponse) {}, fail);
    auto exportedSftp = onlyRequest(api);
    EXPECT_EQ(exportedSftp.path, "/exportSFTPfile");
    EXPECT_EQ(exportedSftp.fields["file_id"], "fid");
    EXPECT_EQ(exportedSftp.fields["export_path"], "/exports");
    EXPECT_EQ(exportedSftp.fields.count("export_bucket"), 0u);

    api.clear();
    client.exportSFTPFile("fid", std::nullopt, [&](ZBSendFileResponse) {}, fail);
    EXPECT_EQ(onlyRequest(api).fields.count("export_path"), 0u);
}

TEST(OfflineApi, S3AndSftpFileJobsRejectInvalidArgumentsBeforeRequest) {
    FakeApi api;
    OfflineClient client(api, "p3-key");
    auto succeeded = [&](ZBSendFileResponse) { FAIL() << "success"; };
    auto expectFileName = [&](ZBErrorResponse error) {
        EXPECT_NE(error.toString().find("file_name"), std::string::npos);
    };

    BulkFileJobOptions missing;
    missing.returnUrl = "https://example.com/done";
    client.emailFinderS3File(missing, succeeded, expectFileName);
    client.domainSearchS3File(missing, succeeded, expectFileName);
    client.scoreSFTPFile(missing, succeeded, expectFileName);
    client.emailFinderSFTPFile(missing, succeeded, expectFileName);
    client.domainSearchSFTPFile(missing, succeeded, expectFileName);

    BulkFileJobOptions blank;
    blank.fileName = "   ";
    blank.returnUrl = "https://example.com/done";
    client.validateSFTPFile(blank, succeeded, expectFileName);

    // LIVE: return_url is required on every one of these jobs.
    auto expectReturnUrl = [&](ZBErrorResponse error) {
        EXPECT_NE(error.toString().find("return_url"), std::string::npos);
    };
    BulkFileJobOptions noReturnUrl;
    noReturnUrl.fileName = "f.csv";
    client.emailFinderS3File(noReturnUrl, succeeded, expectReturnUrl);
    client.domainSearchS3File(noReturnUrl, succeeded, expectReturnUrl);
    client.validateSFTPFile(noReturnUrl, succeeded, expectReturnUrl);
    client.scoreSFTPFile(noReturnUrl, succeeded, expectReturnUrl);
    client.emailFinderSFTPFile(noReturnUrl, succeeded, expectReturnUrl);
    client.domainSearchSFTPFile(noReturnUrl, succeeded, expectReturnUrl);

    client.exportSFTPFile("  ", std::nullopt, succeeded, [&](ZBErrorResponse error) {
        EXPECT_NE(error.toString().find("file_id"), std::string::npos);
    });
    client.exportSFTPFile("fid", std::string(" "), succeeded, [&](ZBErrorResponse error) {
        EXPECT_NE(error.toString().find("export_path"), std::string::npos);
    });

    EXPECT_EQ(api.size(), 0u);
}

#endif

TEST(OfflineApi, RejectsMissingKeyAndInvalidArgumentsBeforeRequest) {
    FakeApi api;
    OfflineClient client(api, "");
    auto reject = [&](const ZBErrorResponse& error) {
        auto captured = captureError(error);
        EXPECT_TRUE(captured.called);
        EXPECT_NE(captured.text.find("initialize"), std::string::npos);
    };
    client.getCredits([&](ZBCreditsResponse) { FAIL() << "success"; }, reject);
    client.findEmailByDomain("example.com", "Ada", [&](ZBFindEmailResponse) { FAIL() << "success"; }, reject);
    client.searchDomainByCompanyName("ZeroBounce", [&](ZBDomainSearchResponse) { FAIL() << "success"; }, reject);
#if ZB_OFFLINE_CURRENT
    client.listFilters(false, [&](std::string) { FAIL() << "success"; }, reject);
    client.emailFinderDeleteFile("fid", [&](ZBDeleteFileResponse) { FAIL() << "success"; }, reject);
    client.sendDomainSearchFile(kCsv, 1, nullptr, [&](ZBSendFileResponse) { FAIL() << "success"; }, reject);
#endif
    EXPECT_EQ(api.size(), 0u);

#if ZB_OFFLINE_CURRENT
    api.clear();
    OfflineClient keyed(api, "keyed");
    keyed.validate("a@example.com", "", ZBValidateOptions{2, std::nullopt, std::nullopt, false}, [&](ZBValidateResponse) { FAIL() << "success"; }, [&](ZBErrorResponse error) {
        EXPECT_NE(error.toString().find("timeout"), std::string::npos);
    });
    ZBValidateOptions batchTimeout;
    batchTimeout.timeout = 9;
    keyed.validateBatch({{"a@example.com"}}, batchTimeout, [&](ZBValidateBatchResponse) { FAIL() << "success"; }, [&](ZBErrorResponse error) {
        EXPECT_NE(error.toString().find("timeout"), std::string::npos);
    });
    keyed.sendEmailFinderFile(kCsv, 0, nullptr, [&](ZBSendFileResponse) { FAIL() << "success"; }, [&](ZBErrorResponse error) {
        EXPECT_NE(error.toString().find("domain_column"), std::string::npos);
    });
    ValidateS3FileOptions missingName;
    keyed.validateS3File(missingName, [&](ZBSendFileResponse) { FAIL() << "success"; }, [&](ZBErrorResponse error) {
        EXPECT_NE(error.toString().find("file_name"), std::string::npos);
    });
    EXPECT_EQ(api.size(), 0u);
#endif
}

TEST(OfflineApi, Html403IsAShortError) {
    FakeApi api;
    OfflineClient client(api, "html-403-user");
    bool errored = false;
    client.getCredits([&](ZBCreditsResponse) { FAIL() << "success"; }, [&](ZBErrorResponse error) {
        auto captured = captureError(error);
        errored = true;
        EXPECT_NE(captured.text.find("Access denied"), std::string::npos);
        EXPECT_GT(accessDeniedPage().size(), 100000u);
    });
    EXPECT_TRUE(errored);
    EXPECT_EQ(api.size(), 1u);
}

TEST(OfflineApi, Html200OnValidateIsAnError) {
    FakeApi api;
    OfflineClient client(api, "html-200-user");
    bool errored = false;
    client.validate("a@example.com", "", [&](ZBValidateResponse) { FAIL() << "HTML was treated as JSON"; }, [&](ZBErrorResponse error) {
        auto captured = captureError(error);
        errored = true;
        EXPECT_NE(captured.text.find("Access denied"), std::string::npos);
    });
    EXPECT_TRUE(errored);
}

TEST(OfflineApi, Html200GetFileIsNotSavedAsCsv) {
    FakeApi api;
    OfflineClient client(api, "html-200-file");
    const std::string path = "../test/downloads/offline-html.csv";
    std::filesystem::remove(path);
    bool errored = false;
    client.getFile("fid", path, [&](ZBGetFileResponse) { FAIL() << "HTML page was saved as a file"; }, [&](ZBErrorResponse error) {
        auto captured = captureError(error);
        errored = true;
        EXPECT_NE(captured.text.find("Access denied"), std::string::npos);
    });
    EXPECT_TRUE(errored);
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(OfflineApi, CloudflareJson403DoesNotDumpTheBody) {
    FakeApi api;
    OfflineClient client(api, "cf-json-user");
    bool errored = false;
    client.getCredits([&](ZBCreditsResponse) { FAIL() << "success"; }, [&](ZBErrorResponse error) {
        auto captured = captureError(error);
        errored = true;
        EXPECT_EQ(captured.text.find("developers.cloudflare.com"), std::string::npos);
        EXPECT_NE(captured.text.find("Access denied"), std::string::npos);
    });
    EXPECT_TRUE(errored);
}

TEST(OfflineApi, EmptyAndMalformedBodiesAreShortErrors) {
    FakeApi api;
    OfflineClient emptyClient(api, "empty-user");
    bool emptyError = false;
    emptyClient.validate("a@example.com", "", [&](ZBValidateResponse) { FAIL() << "empty success"; }, [&](ZBErrorResponse error) {
        auto captured = captureError(error);
        emptyError = true;
        EXPECT_NE(captured.text.find("Empty response"), std::string::npos);
    });
    EXPECT_TRUE(emptyError);

    OfflineClient malformedClient(api, "malformed-user");
    bool malformed = false;
    malformedClient.validate("a@example.com", "", [&](ZBValidateResponse) { FAIL() << "malformed success"; }, [&](ZBErrorResponse error) {
        auto captured = captureError(error);
        malformed = true;
        EXPECT_NE(captured.text.find("not valid JSON"), std::string::npos);
        EXPECT_EQ(captured.text.find("<<<not-json>>>"), std::string::npos);
    });
    EXPECT_TRUE(malformed);
}

TEST(OfflineApi, DoubledErrorPrefixIsCollapsed) {
    FakeApi api;
    OfflineClient client(api, "double-error-user");
    bool errored = false;
    client.getActivityData("a@example.com", [&](ZBActivityDataResponse) { FAIL() << "success"; }, [&](ZBErrorResponse error) {
        auto captured = captureError(error);
        errored = true;
        EXPECT_EQ(captured.text, "api_key is invalid");
    });
    EXPECT_TRUE(errored);
}

TEST(OfflineApi, BatchAccountErrorIsNotASuccess) {
    FakeApi api;
    OfflineClient client(api, "batch-account-user");
    bool errored = false;
    client.validateBatch(
        {{"a@example.com"}},
        [&](ZBValidateBatchResponse response) { FAIL() << response.toString(); },
        [&](ZBErrorResponse error) {
            auto captured = captureError(error);
            errored = true;
            EXPECT_NE(captured.text.find("Invalid API Key"), std::string::npos);
            EXPECT_NE(captured.text.find("ran out of credits"), std::string::npos);
        }
    );
    EXPECT_TRUE(errored);
}

TEST(OfflineApi, BatchPartialErrorsStayOnTheSuccessResult) {
    FakeApi api;
    OfflineClient client(api, "batch-partial-user");
    bool ok = false;
    client.validateBatch(
        {{"good@example.com"}, {"bad@"}},
        [&](ZBValidateBatchResponse response) {
            ok = true;
            ASSERT_EQ(response.emailBatch.size(), 1u);
            EXPECT_EQ(response.emailBatch.front().address, "good@example.com");
            ASSERT_EQ(response.errors.size(), 1u);
            EXPECT_EQ(response.errors.front().emailAddress, "bad@");
            EXPECT_EQ(response.errors.front().error, "Invalid email address");
        },
        [&](ZBErrorResponse error) { FAIL() << error.toString(); }
    );
    EXPECT_TRUE(ok);
}

TEST(OfflineApi, ConcurrentClientsDoNotShareKeys) {
    FakeApi api;
    constexpr int kCalls = 12;
    std::vector<std::string> echoes(kCalls);
    std::vector<std::thread> threads;
    for (int i = 0; i < kCalls; ++i) {
        threads.emplace_back([&, i] {
            const std::string key = "conc-" + std::to_string(i);
            OfflineClient client(api, key);
            client.validate(
                "person@example.com",
                "",
                [&](ZBValidateResponse response) { echoes[i] = response.address; },
                [&](ZBErrorResponse error) { echoes[i] = "ERROR " + error.toString(); }
            );
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    auto records = api.take();
    ASSERT_EQ(records.size(), static_cast<size_t>(kCalls));
    std::map<std::string, int> seen;
    for (const auto& request : records) {
        seen[request.apiKey]++;
        EXPECT_EQ(request.apiKey.rfind("conc-", 0), 0u);
        EXPECT_EQ(request.fields.at("api_key"), request.apiKey);
    }
    for (int i = 0; i < kCalls; ++i) {
        const std::string key = "conc-" + std::to_string(i);
        EXPECT_EQ(seen[key], 1);
        EXPECT_EQ(echoes[i], key);
    }
}

TEST(OfflineApi, ValidateResponseEqualityIncludesSubStatus) {
    ZBValidateResponse left;
    left.address = "a@example.com";
    left.status = ZBValidateStatus::Valid;
    left.subStatus = ZBValidateSubStatus::None;
    ZBValidateResponse right = left;
    right.subStatus = ZBValidateSubStatus::MailboxNotFound;
    EXPECT_NE(left, right);
    right.subStatus = left.subStatus;
    right.catchallDomain = true;
    EXPECT_NE(left, right);
}

TEST(OfflineApi, ValidateSendsTheNewOptionsOnlyWhenSet) {
    FakeApi api;
    OfflineClient client(api, "parity-key");

    client.validate("a@example.com", "", ZBValidateOptions{}, [&](ZBValidateResponse) {},
                    [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto plain = onlyRequest(api);
    EXPECT_EQ(plain.url.find("domain_info"), std::string::npos);
    EXPECT_EQ(plain.url.find("greylist_processing"), std::string::npos);
    EXPECT_EQ(plain.url.find("callback_url"), std::string::npos);

    api.clear();
    ZBValidateOptions set;
    set.domainInfo = true;
    set.greylistProcessing = false;
    client.validate("a@example.com", "", set, [&](ZBValidateResponse) {},
                    [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto withOptions = onlyRequest(api);
    EXPECT_NE(withOptions.url.find("domain_info=true"), std::string::npos);
    EXPECT_NE(withOptions.url.find("greylist_processing=false"), std::string::npos);

    api.clear();
    ZBValidateOptions posted;
    posted.greylistProcessing = true;
    posted.callbackUrl = std::string("https://example.com/cb");
    posted.usePost = true;
    client.validate("a@example.com", "", posted, [&](ZBValidateResponse) {},
                    [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto form = onlyRequest(api);
    EXPECT_EQ(form.method, "POST");
    EXPECT_NE(form.body.find("greylist_processing=true"), std::string::npos);
    EXPECT_NE(form.body.find("callback_url="), std::string::npos);
}

TEST(OfflineApi, CallbackUrlWithoutGreylistProcessingSendsNoRequest) {
    FakeApi api;
    OfflineClient client(api, "parity-key");

    ZBValidateOptions lonely;
    lonely.callbackUrl = std::string("https://example.com/cb");
    bool rejected = false;
    client.validate("a@example.com", "", lonely, [&](ZBValidateResponse) { FAIL() << "success"; },
                    [&](ZBErrorResponse error) {
                        rejected = true;
                        EXPECT_NE(error.toString().find("greylist_processing"), std::string::npos);
                    });
    EXPECT_TRUE(rejected);

    ZBValidateOptions blank;
    blank.greylistProcessing = true;
    blank.callbackUrl = std::string("   ");
    rejected = false;
    client.validate("a@example.com", "", blank, [&](ZBValidateResponse) { FAIL() << "success"; },
                    [&](ZBErrorResponse error) {
                        rejected = true;
                        EXPECT_NE(error.toString().find("callback_url"), std::string::npos);
                    });
    EXPECT_TRUE(rejected);
    EXPECT_EQ(api.size(), 0u);
}

TEST(OfflineApi, InboxPlacementUsesItsOwnHostAndCamelCaseJson) {
    FakeApi api;
    OfflineClient client(api, "ipt-key");

    client.getInboxPlacementProviders([&](std::string) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto providers = onlyRequest(api);
    EXPECT_EQ(providers.method, "GET");
    EXPECT_EQ(providers.path, "/api/ipt/providers");
    // LIVE: the Inbox Placement Tester authenticates with a Bearer header, and
    // the key must not reach the query string, where it would be logged.
    EXPECT_EQ(providers.authorization, "Bearer ipt-key");
    EXPECT_EQ(providers.url.find("api_key"), std::string::npos);

    api.clear();
    client.getInboxPlacementTestTypes([&](std::string) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(onlyRequest(api).path, "/api/ipt/test-types");

    api.clear();
    client.createInboxPlacementTest("basic", CreateInboxPlacementTestOptions{}, [&](std::string) {},
                                    [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto created = onlyRequest(api);
    EXPECT_EQ(created.method, "POST");
    EXPECT_EQ(created.path, "/api/ipt/test");
    EXPECT_EQ(created.body, "{\"name\":\"basic\"}");

    api.clear();
    CreateInboxPlacementTestOptions full;
    full.testType = 1;
    full.senderEmail = std::string("from@example.com");
    full.webhookUrl = std::string("https://example.com/hook");
    client.createInboxPlacementTest("full", full, [&](std::string) {},
                                    [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto body = onlyRequest(api).body;
    EXPECT_NE(body.find("\"testType\":1"), std::string::npos);
    EXPECT_NE(body.find("\"senderEmail\":\"from@example.com\""), std::string::npos);
    EXPECT_NE(body.find("\"webhookUrl\""), std::string::npos);

    api.clear();
    client.getInboxPlacementTest("a b/c", [&](std::string) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_NE(onlyRequest(api).url.find("/api/ipt/test/a%20b%2Fc"), std::string::npos);

    api.clear();
    client.deleteInboxPlacementTest("t1", [&](std::string) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_EQ(onlyRequest(api).method, "DELETE");

    api.clear();
    client.setInboxPlacementWebhook("https://example.com/hook", [&](std::string) {},
                                    [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto webhook = onlyRequest(api);
    EXPECT_EQ(webhook.method, "PUT");
    EXPECT_NE(webhook.body.find("\"webhookUrl\""), std::string::npos);
}

TEST(OfflineApi, InboxPlacementListSendsOnlyTheFiltersThatAreSet) {
    FakeApi api;
    OfflineClient client(api, "ipt-key");

    client.listInboxPlacementTests(ListInboxPlacementTestsOptions{}, [&](std::string) {},
                                   [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto bare = onlyRequest(api);
    EXPECT_EQ(bare.url.find("status="), std::string::npos);
    EXPECT_EQ(bare.url.find("page="), std::string::npos);

    api.clear();
    ListInboxPlacementTestsOptions filters;
    filters.status = std::string("finished");
    filters.createdAfter = std::string("2026-01-01");
    filters.page = 2;
    filters.pageSize = 50;
    client.listInboxPlacementTests(filters, [&](std::string) {},
                                   [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    auto url = onlyRequest(api).url;
    EXPECT_NE(url.find("status=finished"), std::string::npos);
    EXPECT_NE(url.find("createdAfter=2026-01-01"), std::string::npos);
    EXPECT_NE(url.find("page=2"), std::string::npos);
    EXPECT_NE(url.find("page_size=50"), std::string::npos);
}

TEST(OfflineApi, InboxPlacementRejectsInvalidArgumentsBeforeRequest) {
    FakeApi api;
    OfflineClient client(api, "ipt-key");
    auto expectRejected = [&](auto call) {
        bool rejected = false;
        call([&](std::string) { FAIL() << "success"; }, [&](ZBErrorResponse) { rejected = true; });
        EXPECT_TRUE(rejected);
    };

    expectRejected([&](auto ok, auto err) { client.createInboxPlacementTest("", {}, ok, err); });
    expectRejected([&](auto ok, auto err) { client.createInboxPlacementTest(std::string(65, 'x'), {}, ok, err); });
    expectRejected([&](auto ok, auto err) {
        CreateInboxPlacementTestOptions bad; bad.testType = 4;
        client.createInboxPlacementTest("t", bad, ok, err);
    });
    expectRejected([&](auto ok, auto err) {
        CreateInboxPlacementTestOptions bad; bad.testType = 1;
        client.createInboxPlacementTest("t", bad, ok, err);
    });
    expectRejected([&](auto ok, auto err) {
        CreateInboxPlacementTestOptions bad;
        bad.providers = std::vector<InboxPlacementProviderSelection>{{1, 0}};
        client.createInboxPlacementTest("t", bad, ok, err);
    });
    expectRejected([&](auto ok, auto err) {
        ListInboxPlacementTestsOptions bad; bad.status = std::string("queued");
        client.listInboxPlacementTests(bad, ok, err);
    });
    expectRejected([&](auto ok, auto err) {
        ListInboxPlacementTestsOptions bad; bad.pageSize = 101;
        client.listInboxPlacementTests(bad, ok, err);
    });
    expectRejected([&](auto ok, auto err) { client.getInboxPlacementTest("  ", ok, err); });
    expectRejected([&](auto ok, auto err) { client.deleteInboxPlacementTest("", ok, err); });
    expectRejected([&](auto ok, auto err) { client.setInboxPlacementWebhook("  ", ok, err); });
    EXPECT_EQ(api.size(), 0u);
}

TEST(OfflineApi, InboxPlacementDetailErrorBecomesTheMessage) {
    FakeApi api;
    OfflineClient client(api, "ipt-detail-key");

    bool rejected = false;
    client.getInboxPlacementProviders([&](std::string) { FAIL() << "success"; }, [&](ZBErrorResponse error) {
        rejected = true;
        EXPECT_NE(error.toString().find("Inbox placement test not found"), std::string::npos);
        EXPECT_LT(error.toString().size(), 500u);
    });
    EXPECT_TRUE(rejected);
}

TEST(OfflineApi, SuccessFalseOnAnHttp200IsAnError) {
    FakeApi api;
    OfflineClient client(api, "success-false-key");

    // LIVE: the API answers an unknown file id with an HTTP 200 whose body
    // says success: false.
    bool rejected = false;
    client.fileStatus("0d1b7a4e-1111-2222-3333-444455556666",
        [&](ZBFileStatusResponse) { FAIL() << "success"; },
        [&](ZBErrorResponse error) {
            rejected = true;
            EXPECT_EQ(error.statusCode, 200);
            EXPECT_NE(error.toString().find("File cannot be found."), std::string::npos);
            EXPECT_LT(error.toString().size(), 500u);
        });
    EXPECT_TRUE(rejected);
}

TEST(OfflineApi, ListEvaluatorUsesItsOwnBaseUrlAndEncodesFileId) {
    FakeApi api;
    OfflineClient client(api, "le-key");

    client.getListEvaluatorResult("a b/c", [&](std::string) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_NE(onlyRequest(api).url.find("/v2/listevaluator/a%20b%2Fc/"), std::string::npos);

    api.clear();
    client.setListEvaluatorBaseUrl("https://example.com/custom/listevaluator");
    client.getListEvaluatorResult("fid", [&](std::string) {}, [&](ZBErrorResponse e) { FAIL() << e.toString(); });
    EXPECT_NE(onlyRequest(api).url.find("/custom/listevaluator/fid/"), std::string::npos);
}
