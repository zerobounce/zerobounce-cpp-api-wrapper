#include <curl/curl.h>
#include <fstream>
#include <nlohmann/json.hpp>

#include "ZeroBounce/ZeroBounce.h"
#include "HttpError.h"

using json = nlohmann::json;

namespace {

std::string p3UrlEncode(const std::string& value) {
    char* escaped = curl_easy_escape(nullptr, value.c_str(), static_cast<int>(value.size()));
    if (!escaped) {
        throw std::bad_alloc();
    }
    std::string result(escaped);
    curl_free(escaped);
    return result;
}

bool requireApiKey(ZeroBounce* zb, OnErrorCallback errorCallback) {
    if (!zb->getApiKey().empty()) {
        return true;
    }
    if (errorCallback) {
        errorCallback(ZBErrorResponse::parseError(
            "ZeroBounce is not initialized. Please call ZeroBounce::getInstance()->initialize(apiKey) first"
        ));
    }
    return false;
}

void postSendFileForm(
    ZeroBounce* zb,
    const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& fields,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (!requireApiKey(zb, errorCallback)) {
        return;
    }
    try {
        std::string body;
        for (size_t i = 0; i < fields.size(); ++i) {
            if (i > 0) {
                body += "&";
            }
            body += p3UrlEncode(fields[i].first) + "=" + p3UrlEncode(fields[i].second);
        }
        cpr::Response reqResponse = zb->getRequestHandler()->Post(
            cpr::Url{url},
            cpr::Header{
                {"Accept", "application/json"},
                {"Content-Type", "application/x-www-form-urlencoded"}
            },
            cpr::Body{body}
        );
        json parsed;
        if (!zbReadJsonResponse(reqResponse.status_code, reqResponse.text, parsed, errorCallback)) {
            return;
        }
        if (successCallback) {
            successCallback(ZBSendFileResponse::from_json(parsed));
        }
    } catch (const std::exception&) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("ZeroBounce returned an unexpected JSON response."));
        }
    }
}

void sendRawJsonGet(
    ZeroBounce* zb,
    const std::string& url,
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    if (!requireApiKey(zb, errorCallback)) {
        return;
    }
    try {
        cpr::Response reqResponse = zb->getRequestHandler()->Get(
            cpr::Url{url},
            cpr::Header{{"Accept", "application/json"}}
        );
        json parsed;
        if (!zbReadJsonResponse(reqResponse.status_code, reqResponse.text, parsed, errorCallback)) {
            return;
        }
        if (successCallback) {
            successCallback(parsed.dump());
        }
    } catch (const std::exception&) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("ZeroBounce returned an unexpected JSON response."));
        }
    }
}

void sendRawJsonFormPost(
    ZeroBounce* zb,
    const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& fields,
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    if (!requireApiKey(zb, errorCallback)) {
        return;
    }
    try {
        std::string body;
        for (size_t i = 0; i < fields.size(); ++i) {
            if (i > 0) {
                body += "&";
            }
            body += p3UrlEncode(fields[i].first) + "=" + p3UrlEncode(fields[i].second);
        }
        cpr::Response reqResponse = zb->getRequestHandler()->Post(
            cpr::Url{url},
            cpr::Header{
                {"Accept", "application/json"},
                {"Content-Type", "application/x-www-form-urlencoded"}
            },
            cpr::Body{body}
        );
        json parsed;
        if (!zbReadJsonResponse(reqResponse.status_code, reqResponse.text, parsed, errorCallback)) {
            return;
        }
        if (successCallback) {
            successCallback(parsed.dump());
        }
    } catch (const std::exception&) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("ZeroBounce returned an unexpected JSON response."));
        }
    }
}

void bulkRootMultipart(
    ZeroBounce* zb,
    const std::string& url,
    const std::string& filePath,
    const std::vector<std::pair<std::string, std::string>>& fields,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (!requireApiKey(zb, errorCallback)) {
        return;
    }
    try {
        cpr::Multipart multipart{{"api_key", zb->getApiKey()}, {"file", cpr::File{filePath}}};
        for (const auto& field : fields) {
            multipart.parts.emplace_back(cpr::Part{field.first, field.second});
        }
        cpr::Response reqResponse = zb->getRequestHandler()->Post(
            cpr::Url{url},
            cpr::Header{{"Content-Type", "multipart/form-data"}},
            multipart
        );
        json parsed;
        if (!zbReadJsonResponse(reqResponse.status_code, reqResponse.text, parsed, errorCallback)) {
            return;
        }
        if (successCallback) {
            successCallback(ZBSendFileResponse::from_json(parsed));
        }
    } catch (const std::exception&) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("ZeroBounce returned an unexpected JSON response."));
        }
    }
}

std::vector<std::pair<std::string, std::string>> emailFinderFields(
    int domainColumn,
    const EmailFinderSendFileOptions* options
) {
    std::vector<std::pair<std::string, std::string>> fields{{"domain_column", std::to_string(domainColumn)}};
    if (options != nullptr) {
        if (options->hasHeaderRow.has_value()) {
            fields.emplace_back("has_header_row", *options->hasHeaderRow ? "true" : "false");
        }
        if (options->firstNameColumn > 0) {
            fields.emplace_back("first_name_column", std::to_string(options->firstNameColumn));
        }
        if (options->lastNameColumn > 0) {
            fields.emplace_back("last_name_column", std::to_string(options->lastNameColumn));
        }
        if (options->middleNameColumn > 0) {
            fields.emplace_back("middle_name_column", std::to_string(options->middleNameColumn));
        }
        if (options->fullNameColumn > 0) {
            fields.emplace_back("full_name_column", std::to_string(options->fullNameColumn));
        }
    }
    return fields;
}

}  // namespace

void ZeroBounce::sendEmailFinderFile(
    std::string filePath,
    int domainColumn,
    const EmailFinderSendFileOptions* options,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) {
        return;
    }
    if (domainColumn < 1) {
        errorCallback(ZBErrorResponse::parseError("Empty parameter: domain_column"));
        return;
    }
    bulkRootMultipart(
        this,
        bulkApiRootUrl + "/email-finder/sendfile",
        filePath,
        emailFinderFields(domainColumn, options),
        successCallback,
        errorCallback
    );
}

void ZeroBounce::emailFinderFileStatus(
    std::string fileId,
    OnSuccessCallback<ZBFileStatusResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) {
        return;
    }
    sendRequest(
        bulkApiRootUrl + "/email-finder/filestatus?api_key=" + p3UrlEncode(apiKey) + "&file_id=" + p3UrlEncode(fileId),
        successCallback,
        errorCallback
    );
}

void ZeroBounce::emailFinderGetFile(
    std::string fileId,
    std::string localDownloadPath,
    OnSuccessCallback<ZBGetFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) {
        return;
    }
    try {
        cpr::Response reqResponse = requestHandler->Get(
            cpr::Url{bulkApiRootUrl + "/email-finder/getfile?api_key=" + p3UrlEncode(apiKey) + "&file_id=" + p3UrlEncode(fileId)}
        );
        std::string rsp = reqResponse.text;
        std::string contentType = reqResponse.header["Content-Type"];
        if (reqResponse.status_code > 299 || zbContentTypeIsHtml(contentType) || zbBodyLooksLikeHtml(rsp)) {
            zbReportHttpError(reqResponse.status_code, rsp, errorCallback);
            return;
        }
        if (ZeroBounce::getFileJsonIndicatesError(rsp)) {
            zbReportHttpError(reqResponse.status_code, rsp, errorCallback);
            return;
        }
        std::ofstream out(localDownloadPath, std::ios::binary);
        out.write(rsp.data(), static_cast<std::streamsize>(rsp.size()));
        ZBGetFileResponse response;
        response.localFilePath = localDownloadPath;
        successCallback(response);
    } catch (const std::exception&) {
        errorCallback(ZBErrorResponse::parseError("ZeroBounce returned an unexpected response."));
    }
}

void ZeroBounce::emailFinderDeleteFile(
    std::string fileId,
    OnSuccessCallback<ZBDeleteFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    sendRequest(
        bulkApiRootUrl + "/email-finder/deletefile?api_key=" + p3UrlEncode(apiKey) + "&file_id=" + p3UrlEncode(fileId),
        successCallback,
        errorCallback
    );
}

void ZeroBounce::sendDomainSearchFile(
    std::string filePath,
    int domainColumn,
    const DomainSearchSendFileOptions* options,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (domainColumn < 1) {
        errorCallback(ZBErrorResponse::parseError("Empty parameter: domain_column"));
        return;
    }
    std::vector<std::pair<std::string, std::string>> fields{{"domain_column", std::to_string(domainColumn)}};
    if (options != nullptr && options->hasHeaderRow.has_value()) {
        fields.emplace_back("has_header_row", *options->hasHeaderRow ? "true" : "false");
    }
    bulkRootMultipart(
        this,
        bulkApiRootUrl + "/domain-search/sendfile",
        filePath,
        fields,
        successCallback,
        errorCallback
    );
}

void ZeroBounce::domainSearchFileStatus(
    std::string fileId,
    OnSuccessCallback<ZBFileStatusResponse> successCallback,
    OnErrorCallback errorCallback
) {
    sendRequest(
        bulkApiRootUrl + "/domain-search/filestatus?api_key=" + p3UrlEncode(apiKey) + "&file_id=" + p3UrlEncode(fileId),
        successCallback,
        errorCallback
    );
}

void ZeroBounce::domainSearchGetFile(
    std::string fileId,
    std::string localDownloadPath,
    OnSuccessCallback<ZBGetFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) {
        return;
    }
    try {
        cpr::Response reqResponse = requestHandler->Get(
            cpr::Url{bulkApiRootUrl + "/domain-search/getfile?api_key=" + p3UrlEncode(apiKey) + "&file_id=" + p3UrlEncode(fileId)}
        );
        std::string rsp = reqResponse.text;
        std::string contentType = reqResponse.header["Content-Type"];
        if (reqResponse.status_code > 299 || zbContentTypeIsHtml(contentType) || zbBodyLooksLikeHtml(rsp)) {
            zbReportHttpError(reqResponse.status_code, rsp, errorCallback);
            return;
        }
        if (ZeroBounce::getFileJsonIndicatesError(rsp)) {
            zbReportHttpError(reqResponse.status_code, rsp, errorCallback);
            return;
        }
        std::ofstream out(localDownloadPath, std::ios::binary);
        out.write(rsp.data(), static_cast<std::streamsize>(rsp.size()));
        ZBGetFileResponse response;
        response.localFilePath = localDownloadPath;
        successCallback(response);
    } catch (const std::exception&) {
        errorCallback(ZBErrorResponse::parseError("ZeroBounce returned an unexpected response."));
    }
}

void ZeroBounce::domainSearchDeleteFile(
    std::string fileId,
    OnSuccessCallback<ZBDeleteFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    sendRequest(
        bulkApiRootUrl + "/domain-search/deletefile?api_key=" + p3UrlEncode(apiKey) + "&file_id=" + p3UrlEncode(fileId),
        successCallback,
        errorCallback
    );
}

void ZeroBounce::sendListEvaluatorFile(
    std::string filePath,
    int emailAddressColumn,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (emailAddressColumn < 1) {
        errorCallback(ZBErrorResponse::parseError("Empty parameter: email_address_column"));
        return;
    }
    bulkRootMultipart(
        this,
        listEvaluatorBaseUrl + "/",
        filePath,
        {{"email_address_column", std::to_string(emailAddressColumn)}},
        successCallback,
        errorCallback
    );
}

void ZeroBounce::getListEvaluatorResult(
    std::string fileId,
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    sendRawJsonGet(
        this,
        listEvaluatorBaseUrl + "/" + p3UrlEncode(fileId) + "/?api_key=" + p3UrlEncode(apiKey),
        successCallback,
        errorCallback
    );
}

void ZeroBounce::sendEvaluatedFile(
    std::string filePath,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    bulkRootMultipart(this, bulkApiBaseUrl + "/send-evaluated-file/", filePath, {}, successCallback, errorCallback);
}

void ZeroBounce::addFilter(
    std::string rule,
    std::string target,
    std::string value,
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    sendRawJsonFormPost(
        this,
        apiBaseUrl + "/filters/add",
        {{"api_key", apiKey}, {"rule", rule}, {"target", target}, {"value", value}},
        successCallback,
        errorCallback
    );
}

void ZeroBounce::listFilters(bool usePost, OnRawJsonSuccessCallback successCallback, OnErrorCallback errorCallback) {
    if (usePost) {
        sendRawJsonFormPost(this, apiBaseUrl + "/filters/list", {{"api_key", apiKey}}, successCallback, errorCallback);
    } else {
        sendRawJsonGet(this, apiBaseUrl + "/filters/list?api_key=" + p3UrlEncode(apiKey), successCallback, errorCallback);
    }
}

void ZeroBounce::deleteFilter(
    std::string rule,
    std::string target,
    std::string value,
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    sendRawJsonFormPost(
        this,
        apiBaseUrl + "/filters/delete",
        {{"api_key", apiKey}, {"rule", rule}, {"target", target}, {"value", value}},
        successCallback,
        errorCallback
    );
}


// Inbox Placement Tester (bulkapi.zerobounce.net/api/ipt). The API key goes on
// the query string rather than in the body, and bodies are JSON with camelCase
// fields. Only fields the caller set are sent.

namespace {

bool iptTestPath(const std::string& testId, std::string& out, OnErrorCallback errorCallback) {
    if (testId.find_first_not_of(" \t\r\n") == std::string::npos) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("Empty parameter: test_id"));
        }
        return false;
    }
    auto first = testId.find_first_not_of(" \t\r\n");
    auto last = testId.find_last_not_of(" \t\r\n");
    out = p3UrlEncode(testId.substr(first, last - first + 1));
    return true;
}

void iptRequest(
    ZeroBounce* zb,
    const std::string& method,
    const std::string& url,
    const std::string* jsonBody,
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    if (!requireApiKey(zb, errorCallback)) {
        return;
    }
    try {
        cpr::Response reqResponse;
        if (method == "POST") {
            reqResponse = zb->getRequestHandler()->Post(
                cpr::Url{url},
                cpr::Header{{"Accept", "application/json"}, {"Content-Type", "application/json"}},
                cpr::Body{jsonBody ? *jsonBody : std::string{}}
            );
        } else if (method == "PUT") {
            reqResponse = zb->getRequestHandler()->Put(
                cpr::Url{url},
                cpr::Header{{"Accept", "application/json"}, {"Content-Type", "application/json"}},
                cpr::Body{jsonBody ? *jsonBody : std::string{}}
            );
        } else if (method == "DELETE") {
            reqResponse = zb->getRequestHandler()->Delete(
                cpr::Url{url},
                cpr::Header{{"Accept", "application/json"}}
            );
        } else {
            reqResponse = zb->getRequestHandler()->Get(
                cpr::Url{url},
                cpr::Header{{"Accept", "application/json"}}
            );
        }
        json parsed;
        if (!zbReadJsonResponse(reqResponse.status_code, reqResponse.text, parsed, errorCallback)) {
            return;
        }
        if (successCallback) {
            successCallback(parsed.dump());
        }
    } catch (const std::exception&) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("ZeroBounce returned an unexpected JSON response."));
        }
    }
}

}  // namespace

void ZeroBounce::setListEvaluatorBaseUrl(const std::string& url) {
    listEvaluatorBaseUrl = url;
}

void ZeroBounce::setInboxPlacementBaseUrl(const std::string& url) {
    inboxPlacementBaseUrl = url;
}

void ZeroBounce::getInboxPlacementProviders(
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    iptRequest(this, "GET",
        inboxPlacementBaseUrl + "/providers?api_key=" + p3UrlEncode(apiKey),
        nullptr, successCallback, errorCallback);
}

void ZeroBounce::getInboxPlacementTestTypes(
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    iptRequest(this, "GET",
        inboxPlacementBaseUrl + "/test-types?api_key=" + p3UrlEncode(apiKey),
        nullptr, successCallback, errorCallback);
}

void ZeroBounce::createInboxPlacementTest(
    const std::string& name,
    const CreateInboxPlacementTestOptions& options,
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    if (name.find_first_not_of(" \t\r\n") == std::string::npos) {
        if (errorCallback) errorCallback(ZBErrorResponse::parseError("Empty parameter: name"));
        return;
    }
    if (name.size() > 64) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("Invalid parameter: name (at most 64 characters)"));
        }
        return;
    }
    if (options.testType.has_value()) {
        int testType = options.testType.value();
        if (testType != 1 && testType != 2 && testType != 3) {
            if (errorCallback) {
                errorCallback(ZBErrorResponse::parseError("Invalid parameter: test_type (must be 1, 2 or 3)"));
            }
            return;
        }
        bool senderMissing = !options.senderEmail.has_value() ||
            options.senderEmail->find_first_not_of(" \t\r\n") == std::string::npos;
        if (testType == 1 && senderMissing) {
            if (errorCallback) {
                errorCallback(ZBErrorResponse::parseError(
                    "Empty parameter: sender_email (required when test_type is 1)"));
            }
            return;
        }
    }
    if (options.providers.has_value()) {
        for (const auto& provider : options.providers.value()) {
            if (provider.maxSeeds.has_value() && provider.maxSeeds.value() < 1) {
                if (errorCallback) {
                    errorCallback(ZBErrorResponse::parseError(
                        "Invalid parameter: providers (max_seeds must be at least 1)"));
                }
                return;
            }
        }
    }

    json body;
    body["name"] = name;
    if (options.testType.has_value()) body["testType"] = options.testType.value();
    if (options.senderEmail.has_value()) body["senderEmail"] = options.senderEmail.value();
    if (options.providerIds.has_value()) body["providerIds"] = options.providerIds.value();
    if (options.seedIds.has_value()) body["seedIds"] = options.seedIds.value();
    if (options.providers.has_value()) {
        json providers = json::array();
        for (const auto& provider : options.providers.value()) {
            json entry;
            entry["id"] = provider.id;
            if (provider.maxSeeds.has_value()) entry["maxSeeds"] = provider.maxSeeds.value();
            providers.push_back(entry);
        }
        body["providers"] = providers;
    }
    if (options.webhookUrl.has_value()) body["webhookUrl"] = options.webhookUrl.value();

    std::string encoded = body.dump();
    iptRequest(this, "POST",
        inboxPlacementBaseUrl + "/test?api_key=" + p3UrlEncode(apiKey),
        &encoded, successCallback, errorCallback);
}

void ZeroBounce::getInboxPlacementTest(
    const std::string& testId,
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    std::string path;
    if (!iptTestPath(testId, path, errorCallback)) return;
    iptRequest(this, "GET",
        inboxPlacementBaseUrl + "/test/" + path + "?api_key=" + p3UrlEncode(apiKey),
        nullptr, successCallback, errorCallback);
}

void ZeroBounce::listInboxPlacementTests(
    const ListInboxPlacementTestsOptions& options,
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    if (options.status.has_value() &&
        options.status.value() != "running" && options.status.value() != "finished") {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("Invalid parameter: status (must be running or finished)"));
        }
        return;
    }
    if (options.page.has_value() && options.page.value() < 1) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("Invalid parameter: page (must be at least 1)"));
        }
        return;
    }
    if (options.pageSize.has_value() &&
        (options.pageSize.value() < 1 || options.pageSize.value() > 100)) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("Invalid parameter: page_size (must be between 1 and 100)"));
        }
        return;
    }

    std::string url = inboxPlacementBaseUrl + "/tests?api_key=" + p3UrlEncode(apiKey);
    if (options.status.has_value()) url += "&status=" + p3UrlEncode(options.status.value());
    if (options.createdAfter.has_value()) url += "&createdAfter=" + p3UrlEncode(options.createdAfter.value());
    if (options.createdBefore.has_value()) url += "&createdBefore=" + p3UrlEncode(options.createdBefore.value());
    if (options.page.has_value()) url += "&page=" + std::to_string(options.page.value());
    if (options.pageSize.has_value()) url += "&page_size=" + std::to_string(options.pageSize.value());
    iptRequest(this, "GET", url, nullptr, successCallback, errorCallback);
}

void ZeroBounce::deleteInboxPlacementTest(
    const std::string& testId,
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    std::string path;
    if (!iptTestPath(testId, path, errorCallback)) return;
    iptRequest(this, "DELETE",
        inboxPlacementBaseUrl + "/test/" + path + "?api_key=" + p3UrlEncode(apiKey),
        nullptr, successCallback, errorCallback);
}

void ZeroBounce::getInboxPlacementWebhook(
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    iptRequest(this, "GET",
        inboxPlacementBaseUrl + "/webhook?api_key=" + p3UrlEncode(apiKey),
        nullptr, successCallback, errorCallback);
}

void ZeroBounce::setInboxPlacementWebhook(
    const std::string& webhookUrl,
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    if (webhookUrl.find_first_not_of(" \t\r\n") == std::string::npos) {
        if (errorCallback) errorCallback(ZBErrorResponse::parseError("Empty parameter: webhook_url"));
        return;
    }
    json body;
    body["webhookUrl"] = webhookUrl;
    std::string encoded = body.dump();
    iptRequest(this, "PUT",
        inboxPlacementBaseUrl + "/webhook?api_key=" + p3UrlEncode(apiKey),
        &encoded, successCallback, errorCallback);
}
