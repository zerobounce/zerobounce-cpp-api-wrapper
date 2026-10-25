#include <curl/curl.h>
#include <fstream>
#include <nlohmann/json.hpp>

#include "ZeroBounce/ZeroBounce.h"
#include "ZeroBounce/ZBS3ListFileEntry.h"
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
    // The upload needs a name to work from: one of first_name_column and
    // full_name_column is required, so neither being set is rejected here.
    if (options == nullptr || (options->firstNameColumn < 1 && options->fullNameColumn < 1)) {
        errorCallback(ZBErrorResponse::parseError("Empty parameter: first_name_column or full_name_column"));
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
    std::string fileId,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (!requireApiKey(this, errorCallback)) {
        return;
    }
    if (fileId.find_first_not_of(" \t\r\n") == std::string::npos) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("Empty parameter: file_id"));
        }
        return;
    }
    // LIVE: this takes the list evaluator's file_id as a form field, not the
    // file upload the docs describe - an upload is rejected with
    // "file_id: This field is required".
    postSendFileForm(
        this,
        bulkApiBaseUrl + "/send-evaluated-file/",
        {{"api_key", apiKey}, {"file_id", fileId}},
        successCallback,
        errorCallback
    );
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


// The documented fields shared by the six S3 / SFTP file jobs. Booleans go on
// the wire as the strings "true"/"false", and an option the caller left unset
// is not sent at all.
// LIVE: file_name and return_url are required on every one of these jobs,
// validateSFTPfile included, although the docs list file_name as optional for
// that one. Without either the API answers "This field is required".
static bool bulkFileJobFields(
    const BulkFileJobOptions& options,
    const std::string& apiKey,
    std::vector<std::pair<std::string, std::string>>& fields,
    OnErrorCallback errorCallback
) {
    const bool blankName = !options.fileName.has_value() ||
        options.fileName->find_first_not_of(" \t\r\n") == std::string::npos;
    if (blankName) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("Empty parameter: file_name"));
        }
        return false;
    }
    const bool blankReturnUrl = !options.returnUrl.has_value() ||
        options.returnUrl->find_first_not_of(" \t\r\n") == std::string::npos;
    if (blankReturnUrl) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("Empty parameter: return_url"));
        }
        return false;
    }
    fields.emplace_back("has_header_row", options.hasHeaderRow ? "true" : "false");
    fields.emplace_back("file_name", *options.fileName);
    fields.emplace_back("return_url", *options.returnUrl);
    if (options.removeDuplicates.has_value()) {
        fields.emplace_back("remove_duplicates", *options.removeDuplicates ? "true" : "false");
    }
    fields.emplace_back("api_key", apiKey);
    return true;
}

void ZeroBounce::validateS3File(
    const ValidateS3FileOptions& options,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    // The shared fields go through the same helper as the other five jobs, so
    // that what the API requires is stated once. Only the extra fields that
    // validateS3file alone accepts are added here.
    BulkFileJobOptions shared;
    shared.fileName = options.fileName;
    shared.hasHeaderRow = options.hasHeaderRow;
    shared.removeDuplicates = options.removeDuplicates;
    shared.returnUrl = options.returnUrl;
    std::vector<std::pair<std::string, std::string>> fields;
    if (!bulkFileJobFields(shared, apiKey, fields, errorCallback)) {
        return;
    }
    if (options.emailAddressColumn.has_value()) {
        fields.emplace_back("email_address_column", std::to_string(*options.emailAddressColumn));
    }
    if (options.firstNameColumn > 0) {
        fields.emplace_back("first_name_column", std::to_string(options.firstNameColumn));
    }
    if (options.lastNameColumn > 0) {
        fields.emplace_back("last_name_column", std::to_string(options.lastNameColumn));
    }
    if (options.genderColumn > 0) {
        fields.emplace_back("gender_column", std::to_string(options.genderColumn));
    }
    if (options.ipAddressColumn > 0) {
        fields.emplace_back("ip_address_column", std::to_string(options.ipAddressColumn));
    }
    if (options.allowPhase2.has_value()) {
        fields.emplace_back("allow_phase_2", *options.allowPhase2 ? "true" : "false");
    }
    postSendFileForm(this, bulkApiRootUrl + "/validateS3file", fields, successCallback, errorCallback);
}

void ZeroBounce::scoreS3File(
    const ScoreS3FileOptions& options,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    BulkFileJobOptions shared;
    shared.fileName = options.fileName;
    shared.hasHeaderRow = options.hasHeaderRow;
    shared.removeDuplicates = options.removeDuplicates;
    shared.returnUrl = options.returnUrl;
    std::vector<std::pair<std::string, std::string>> fields;
    if (!bulkFileJobFields(shared, apiKey, fields, errorCallback)) {
        return;
    }
    if (options.emailAddressColumn.has_value()) {
        fields.emplace_back("email_address_column", std::to_string(*options.emailAddressColumn));
    }
    postSendFileForm(this, bulkApiRootUrl + "/scoreS3file", fields, successCallback, errorCallback);
}

void ZeroBounce::listS3files(
    std::optional<std::string> bucket,
    std::optional<std::string> search,
    OnSuccessCallback<std::vector<ZBS3ListFileEntry>> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) {
        return;
    }
    std::string url = bulkApiRootUrl + "/listS3files?api_key=" + p3UrlEncode(apiKey);
    if (bucket.has_value() && !bucket->empty()) {
        url += "&bucket=" + p3UrlEncode(*bucket);
    }
    if (search.has_value() && !search->empty()) {
        url += "&search=" + p3UrlEncode(*search);
    }
    try {
        cpr::Response reqResponse = getRequestHandler()->Get(
            cpr::Url{url},
            cpr::Header{{"Accept", "application/json"}}
        );
        json parsed;
        if (!zbReadJsonResponse(reqResponse.status_code, reqResponse.text, parsed, errorCallback)) {
            return;
        }
        if (successCallback) {
            successCallback(ZBS3ListFileEntry::listFromJson(parsed));
        }
    } catch (const std::exception&) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("ZeroBounce returned an unexpected JSON response."));
        }
    }
}

void ZeroBounce::exportS3File(
    std::string fileId,
    std::optional<std::string> exportBucket,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (fileId.find_first_not_of(" \t\r\n") == std::string::npos) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("Empty parameter: file_id"));
        }
        return;
    }
    std::vector<std::pair<std::string, std::string>> fields{{"file_id", fileId}};
    // export_bucket is optional: without it the export bucket from your
    // ZeroBounce settings is used, exactly as exportSFTPFile treats export_path.
    if (exportBucket.has_value()) {
        if (exportBucket->find_first_not_of(" \t\r\n") == std::string::npos) {
            if (errorCallback) {
                errorCallback(ZBErrorResponse::parseError("Empty parameter: export_bucket"));
            }
            return;
        }
        fields.emplace_back("export_bucket", *exportBucket);
    }
    fields.emplace_back("api_key", apiKey);
    postSendFileForm(this, bulkApiRootUrl + "/exportS3file", fields, successCallback, errorCallback);
}

void ZeroBounce::emailFinderS3File(
    const BulkFileJobOptions& options,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    std::vector<std::pair<std::string, std::string>> fields;
    if (!bulkFileJobFields(options, apiKey, fields, errorCallback)) {
        return;
    }
    postSendFileForm(this, bulkApiRootUrl + "/emailFinderS3file", fields, successCallback, errorCallback);
}

void ZeroBounce::domainSearchS3File(
    const BulkFileJobOptions& options,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    std::vector<std::pair<std::string, std::string>> fields;
    if (!bulkFileJobFields(options, apiKey, fields, errorCallback)) {
        return;
    }
    postSendFileForm(this, bulkApiRootUrl + "/domainSearchS3file", fields, successCallback, errorCallback);
}

// SFTP/FTP bulk. The server, path and protocol come from the account's settings.

void ZeroBounce::validateSFTPFile(
    const BulkFileJobOptions& options,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    std::vector<std::pair<std::string, std::string>> fields;
    if (!bulkFileJobFields(options, apiKey, fields, errorCallback)) {
        return;
    }
    postSendFileForm(this, bulkApiRootUrl + "/validateSFTPfile", fields, successCallback, errorCallback);
}

void ZeroBounce::scoreSFTPFile(
    const BulkFileJobOptions& options,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    std::vector<std::pair<std::string, std::string>> fields;
    if (!bulkFileJobFields(options, apiKey, fields, errorCallback)) {
        return;
    }
    postSendFileForm(this, bulkApiRootUrl + "/scoreSFTPfile", fields, successCallback, errorCallback);
}

void ZeroBounce::emailFinderSFTPFile(
    const BulkFileJobOptions& options,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    std::vector<std::pair<std::string, std::string>> fields;
    if (!bulkFileJobFields(options, apiKey, fields, errorCallback)) {
        return;
    }
    postSendFileForm(this, bulkApiRootUrl + "/emailFinderSFTPfile", fields, successCallback, errorCallback);
}

void ZeroBounce::domainSearchSFTPFile(
    const BulkFileJobOptions& options,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    std::vector<std::pair<std::string, std::string>> fields;
    if (!bulkFileJobFields(options, apiKey, fields, errorCallback)) {
        return;
    }
    postSendFileForm(this, bulkApiRootUrl + "/domainSearchSFTPfile", fields, successCallback, errorCallback);
}

void ZeroBounce::exportSFTPFile(
    std::string fileId,
    std::optional<std::string> exportPath,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (fileId.find_first_not_of(" \t\r\n") == std::string::npos) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("Empty parameter: file_id"));
        }
        return;
    }
    std::vector<std::pair<std::string, std::string>> fields{{"file_id", fileId}};
    if (exportPath.has_value()) {
        if (exportPath->find_first_not_of(" \t\r\n") == std::string::npos) {
            if (errorCallback) {
                errorCallback(ZBErrorResponse::parseError("Empty parameter: export_path"));
            }
            return;
        }
        // export_path, not the export_bucket exportS3File sends.
        fields.emplace_back("export_path", *exportPath);
    }
    fields.emplace_back("api_key", apiKey);
    postSendFileForm(this, bulkApiRootUrl + "/exportSFTPfile", fields, successCallback, errorCallback);
}

// Inbox Placement Tester (bulkapi.zerobounce.net/api/ipt). LIVE: the API key
// goes in an `Authorization: Bearer` header, never on the query string or in
// the body, and bodies are JSON with camelCase fields. Only fields the caller
// set are sent.

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
    // LIVE: the Inbox Placement Tester authenticates with a Bearer header.
    // Sent as an api_key parameter the request is rejected as unauthorized.
    const std::string bearer = "Bearer " + zb->getApiKey();
    try {
        cpr::Response reqResponse;
        if (method == "POST") {
            reqResponse = zb->getRequestHandler()->Post(
                cpr::Url{url},
                cpr::Header{
                    {"Accept", "application/json"},
                    {"Content-Type", "application/json"},
                    {"Authorization", bearer}
                },
                cpr::Body{jsonBody ? *jsonBody : std::string{}}
            );
        } else if (method == "PUT") {
            reqResponse = zb->getRequestHandler()->Put(
                cpr::Url{url},
                cpr::Header{
                    {"Accept", "application/json"},
                    {"Content-Type", "application/json"},
                    {"Authorization", bearer}
                },
                cpr::Body{jsonBody ? *jsonBody : std::string{}}
            );
        } else if (method == "DELETE") {
            reqResponse = zb->getRequestHandler()->Delete(
                cpr::Url{url},
                cpr::Header{{"Accept", "application/json"}, {"Authorization", bearer}}
            );
        } else {
            reqResponse = zb->getRequestHandler()->Get(
                cpr::Url{url},
                cpr::Header{{"Accept", "application/json"}, {"Authorization", bearer}}
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

void ZeroBounce::setBulkApiRootUrl(const std::string& url) {
    bulkApiRootUrl = url;
}

void ZeroBounce::setBulkApiBaseUrl(const std::string& url) {
    bulkApiBaseUrl = url;
}

void ZeroBounce::setBulkApiScoringBaseUrl(const std::string& url) {
    bulkApiScoringBaseUrl = url;
}

void ZeroBounce::getInboxPlacementProviders(
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    iptRequest(this, "GET",
        inboxPlacementBaseUrl + "/providers",
        nullptr, successCallback, errorCallback);
}

void ZeroBounce::getInboxPlacementTestTypes(
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    iptRequest(this, "GET",
        inboxPlacementBaseUrl + "/test-types",
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
        inboxPlacementBaseUrl + "/test",
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
        inboxPlacementBaseUrl + "/test/" + path,
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

    // The listing is GET /tests (plural); /test is the single-test path and
    // answers "Method \"GET\" not allowed".
    std::string url = inboxPlacementBaseUrl + "/tests";
    std::string separator = "?";
    auto addParam = [&url, &separator](const std::string& name, const std::string& value) {
        url += separator + name + "=" + value;
        separator = "&";
    };
    if (options.status.has_value()) addParam("status", p3UrlEncode(options.status.value()));
    if (options.createdAfter.has_value()) addParam("createdAfter", p3UrlEncode(options.createdAfter.value()));
    if (options.createdBefore.has_value()) addParam("createdBefore", p3UrlEncode(options.createdBefore.value()));
    if (options.page.has_value()) addParam("page", std::to_string(options.page.value()));
    if (options.pageSize.has_value()) addParam("page_size", std::to_string(options.pageSize.value()));
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
        inboxPlacementBaseUrl + "/test/" + path,
        nullptr, successCallback, errorCallback);
}

void ZeroBounce::getInboxPlacementWebhook(
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    iptRequest(this, "GET",
        inboxPlacementBaseUrl + "/webhook",
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
        inboxPlacementBaseUrl + "/webhook",
        &encoded, successCallback, errorCallback);
}
