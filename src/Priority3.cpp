#include <curl/curl.h>
#include <fstream>
#include <nlohmann/json.hpp>

#include "ZeroBounce/ZeroBounce.h"

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

void postSendFileForm(
    ZeroBounce* zb,
    const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& fields,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
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
        std::string rsp = reqResponse.text;
        if (reqResponse.status_code > 299) {
            if (errorCallback) {
                errorCallback(ZBErrorResponse::parseError(rsp));
            }
        } else if (successCallback) {
            successCallback(ZBSendFileResponse::from_json(json::parse(rsp)));
        }
    } catch (const std::exception& e) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError(e.what()));
        }
    }
}

void sendRawJsonGet(
    ZeroBounce* zb,
    const std::string& url,
    OnRawJsonSuccessCallback successCallback,
    OnErrorCallback errorCallback
) {
    try {
        cpr::Response reqResponse = zb->getRequestHandler()->Get(
            cpr::Url{url},
            cpr::Header{{"Accept", "application/json"}}
        );
        std::string rsp = reqResponse.text;
        if (reqResponse.status_code > 299) {
            if (errorCallback) {
                errorCallback(ZBErrorResponse::parseError(rsp));
            }
        } else if (successCallback) {
            successCallback(rsp);
        }
    } catch (const std::exception& e) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError(e.what()));
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
        std::string rsp = reqResponse.text;
        if (reqResponse.status_code > 299) {
            if (errorCallback) {
                errorCallback(ZBErrorResponse::parseError(rsp));
            }
        } else if (successCallback) {
            successCallback(rsp);
        }
    } catch (const std::exception& e) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError(e.what()));
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
        std::string rsp = reqResponse.text;
        if (reqResponse.status_code > 299) {
            if (errorCallback) {
                errorCallback(ZBErrorResponse::parseError(rsp));
            }
        } else if (successCallback) {
            successCallback(ZBSendFileResponse::from_json(json::parse(rsp)));
        }
    } catch (const std::exception& e) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError(e.what()));
        }
    }
}

std::vector<std::pair<std::string, std::string>> emailFinderFields(
    int domainColumn,
    const EmailFinderSendFileOptions* options
) {
    std::vector<std::pair<std::string, std::string>> fields{{"domain_column", std::to_string(domainColumn)}};
    if (options != nullptr) {
        if (options->hasHeaderRow.has_value() && *options->hasHeaderRow) {
            fields.emplace_back("has_header_row", "true");
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
        if (reqResponse.status_code > 299) {
            errorCallback(ZBErrorResponse::parseError(rsp));
            return;
        }
        if (ZeroBounce::getFileJsonIndicatesError(rsp)) {
            errorCallback(ZBErrorResponse::parseError(rsp));
            return;
        }
        std::ofstream out(localDownloadPath, std::ios::binary);
        out.write(rsp.data(), static_cast<std::streamsize>(rsp.size()));
        ZBGetFileResponse response;
        response.localFilePath = localDownloadPath;
        successCallback(response);
    } catch (const std::exception& e) {
        errorCallback(ZBErrorResponse::parseError(e.what()));
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
    if (options != nullptr && options->hasHeaderRow.has_value() && *options->hasHeaderRow) {
        fields.emplace_back("has_header_row", "true");
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
        if (reqResponse.status_code > 299) {
            errorCallback(ZBErrorResponse::parseError(rsp));
            return;
        }
        if (ZeroBounce::getFileJsonIndicatesError(rsp)) {
            errorCallback(ZBErrorResponse::parseError(rsp));
            return;
        }
        std::ofstream out(localDownloadPath, std::ios::binary);
        out.write(rsp.data(), static_cast<std::streamsize>(rsp.size()));
        ZBGetFileResponse response;
        response.localFilePath = localDownloadPath;
        successCallback(response);
    } catch (const std::exception& e) {
        errorCallback(ZBErrorResponse::parseError(e.what()));
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
        bulkApiBaseUrl + "/listevaluator/",
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
        bulkApiBaseUrl + "/listevaluator/" + fileId + "/?api_key=" + p3UrlEncode(apiKey),
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

void ZeroBounce::validateS3File(
    const ValidateS3FileOptions& options,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (options.fileName.empty()) {
        errorCallback(ZBErrorResponse::parseError("Empty parameter: file_name"));
        return;
    }
    std::vector<std::pair<std::string, std::string>> fields{{"file_name", options.fileName}};
    if (options.hasHeaderRow.has_value()) {
        fields.emplace_back("has_header_row", *options.hasHeaderRow ? "true" : "false");
    }
    if (options.removeDuplicates.has_value()) {
        fields.emplace_back("remove_duplicates", *options.removeDuplicates ? "true" : "false");
    }
    if (options.removeDuplicate.has_value()) {
        fields.emplace_back("remove_duplicate", *options.removeDuplicate ? "true" : "false");
    }
    if (options.emailAddressColumn.has_value()) {
        fields.emplace_back("email_address_column", std::to_string(*options.emailAddressColumn));
    }
    if (!options.returnUrl.empty()) {
        fields.emplace_back("return_url", options.returnUrl);
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
    fields.emplace_back("api_key", apiKey);
    postSendFileForm(this, bulkApiRootUrl + "/validateS3file", fields, successCallback, errorCallback);
}

void ZeroBounce::scoreS3File(
    const ScoreS3FileOptions& options,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (options.fileName.empty()) {
        errorCallback(ZBErrorResponse::parseError("Empty parameter: file_name"));
        return;
    }
    std::vector<std::pair<std::string, std::string>> fields{{"file_name", options.fileName}, {"api_key", apiKey}};
    if (options.hasHeaderRow.has_value()) {
        fields.emplace_back("has_header_row", *options.hasHeaderRow ? "true" : "false");
    }
    if (options.removeDuplicates.has_value()) {
        fields.emplace_back("remove_duplicates", *options.removeDuplicates ? "true" : "false");
    }
    if (options.removeDuplicate.has_value()) {
        fields.emplace_back("remove_duplicate", *options.removeDuplicate ? "true" : "false");
    }
    if (options.emailAddressColumn.has_value()) {
        fields.emplace_back("email_address_column", std::to_string(*options.emailAddressColumn));
    }
    postSendFileForm(this, bulkApiRootUrl + "/scoreS3file", fields, successCallback, errorCallback);
}

void ZeroBounce::exportS3File(
    std::string fileId,
    std::string exportBucket,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (fileId.empty()) {
        errorCallback(ZBErrorResponse::parseError("Empty parameter: file_id"));
        return;
    }
    if (exportBucket.empty()) {
        errorCallback(ZBErrorResponse::parseError("Empty parameter: export_bucket"));
        return;
    }
    postSendFileForm(
        this,
        bulkApiRootUrl + "/exportS3file",
        {{"file_id", fileId}, {"export_bucket", exportBucket}, {"api_key", apiKey}},
        successCallback,
        errorCallback
    );
}
