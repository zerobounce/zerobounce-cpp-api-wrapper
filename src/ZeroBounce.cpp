#include <cctype>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>
#include <curl/curl.h>

#include "ZeroBounce/ZeroBounce.h"
#include "HttpError.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

static std::string urlEncode(const std::string& value) {
    char* escaped = curl_easy_escape(nullptr, value.c_str(), static_cast<int>(value.size()));
    if (!escaped) {
        throw std::bad_alloc();
    }
    std::string result(escaped);
    curl_free(escaped);
    return result;
}

ZeroBounce::ZeroBounce() {
    defaultRequestHandler = std::make_unique<RequestHandler>();
    requestHandler = defaultRequestHandler.get();
}

ZeroBounce* ZeroBounce::getInstance() {
    static ZeroBounce instance;
    return &instance;
}

void ZeroBounce::initialize(std::string apiKey) {
    this->apiKey = apiKey;
}

void ZeroBounce::initialize(
    std::string apiKey,
    ZBApiURL apiBaseUrl
) {
    this->apiKey = apiKey;
    this->apiBaseUrl = baseURLStringFromZBApiURL(apiBaseUrl);
}

static bool contentTypeIncludesApplicationJson(const std::string& contentType) {
    std::string lower;
    lower.reserve(contentType.size());
    for (unsigned char c : contentType) {
        lower += static_cast<char>(std::tolower(c));
    }
    return lower.find("application/json") != std::string::npos;
}

static bool shouldTreatGetFileBodyAsError(const std::string& body, const std::string& contentType) {
    if (contentTypeIncludesApplicationJson(contentType)) {
        return true;
    }
    return ZeroBounce::getFileJsonIndicatesError(body);
}

bool ZeroBounce::getFileJsonIndicatesError(const std::string& body) {
    std::size_t i = 0;
    while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) {
        i++;
    }
    if (i >= body.size() || body[i] != '{') {
        return false;
    }
    try {
        json o = json::parse(body);
        if (!o.is_object()) {
            return false;
        }
        if (o.contains("success")) {
            const auto& s = o["success"];
            if (s == false) {
                return true;
            }
            if (s.is_string()) {
                std::string sv = s.get<std::string>();
                if (sv == "false" || sv == "False") {
                    return true;
                }
            }
        }
        for (const char* k : {"message", "error", "error_message"}) {
            if (!o.contains(k)) {
                continue;
            }
            const auto& v = o[k];
            if (v.is_string() && !v.get<std::string>().empty()) {
                return true;
            }
            if (v.is_array() && !v.empty()) {
                return true;
            }
        }
        return o.contains("success");
    } catch (...) {
        return false;
    }
}

std::string ZeroBounce::formatGetFileErrorMessage(const std::string& body) {
    try {
        json o = json::parse(body);
        if (!o.is_object()) {
            return body.empty() ? "Invalid getfile response" : body;
        }
        for (const char* k : {"message", "error", "error_message"}) {
            if (!o.contains(k)) {
                continue;
            }
            const auto& v = o[k];
            if (v.is_string()) {
                std::string t = v.get<std::string>();
                if (!t.empty()) {
                    return t;
                }
            }
            if (v.is_array() && !v.empty() && v[0].is_string()) {
                return v[0].get<std::string>();
            }
        }
        return body;
    } catch (...) {
        return body.empty() ? "Invalid getfile response" : body;
    }
}

bool ZeroBounce::invalidApiKey(OnErrorCallback errorCallback) {
    if (apiKey.empty()) {
        ZBErrorResponse errorResponse = ZBErrorResponse::parseError(
            "ZeroBounce is not initialized. Please call ZeroBounce::getInstance()->initialize(apiKey) first"
        );
        errorCallback(errorResponse);
        return true;
    }
    return false;
}

void ZeroBounce::getCredits(
    OnSuccessCallback<ZBCreditsResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) return;

    sendRequest(
        apiBaseUrl + "/getcredits?api_key=" + urlEncode(apiKey),
        successCallback,
        errorCallback
    );

}

void ZeroBounce::getApiUsage(
    std::tm startDate,
    std::tm endDate,
    OnSuccessCallback<ZBGetApiUsageResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) return;

    std::string dateFormat = "%Y-%m-%d";
    std::ostringstream url;

    std::ostringstream startStr;
    std::ostringstream endStr;
    startStr << std::put_time(&startDate, dateFormat.c_str());
    endStr << std::put_time(&endDate, dateFormat.c_str());
    url << apiBaseUrl << "/getapiusage?api_key=" << urlEncode(apiKey)
        << "&start_date=" << urlEncode(startStr.str())
        << "&end_date=" << urlEncode(endStr.str());

    sendRequest(
        url.str(),
        successCallback,
        errorCallback
    );
}

static bool checkValidateTimeout(const std::optional<int>& timeout, OnErrorCallback errorCallback) {
    if (!timeout.has_value()) {
        return true;
    }
    int t = timeout.value();
    if (t >= 3 && t <= 60) {
        return true;
    }
    if (errorCallback) {
        errorCallback(ZBErrorResponse::parseError(
            "Invalid parameter: timeout (must be between 3 and 60 seconds)"
        ));
    }
    return false;
}

static bool checkValidateBatchTimeout(const std::optional<int>& timeout, OnErrorCallback errorCallback) {
    if (!timeout.has_value()) {
        return true;
    }
    int t = timeout.value();
    if (t >= 10 && t <= 120) {
        return true;
    }
    if (errorCallback) {
        errorCallback(ZBErrorResponse::parseError(
            "Invalid parameter: timeout (must be between 10 and 120 seconds)"
        ));
    }
    return false;
}

void ZeroBounce::validate(
    std::string email,
    std::string ipAddress,
    OnSuccessCallback<ZBValidateResponse> successCallback,
    OnErrorCallback errorCallback
) {
    validate(email, ipAddress, ZBValidateOptions{}, successCallback, errorCallback);
}

void ZeroBounce::validate(
    std::string email,
    std::string ipAddress,
    const ZBValidateOptions& options,
    OnSuccessCallback<ZBValidateResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) return;
    if (!checkValidateTimeout(options.timeout, errorCallback)) return;

    if (options.usePost) {
        std::string body =
            "api_key=" + urlEncode(apiKey) +
            "&email=" + urlEncode(email) +
            "&ip_address=" + (ipAddress.empty() ? "" : urlEncode(ipAddress));
        if (options.timeout.has_value()) {
            body += "&timeout=" + urlEncode(std::to_string(options.timeout.value()));
        }
        if (options.activityData.has_value()) {
            body += "&activity_data=" + urlEncode(options.activityData.value() ? "true" : "false");
        }
        if (options.verifyPlus.has_value()) {
            body += "&verify_plus=" + urlEncode(options.verifyPlus.value() ? "true" : "false");
        }
        try {
            cpr::Response reqResponse = requestHandler->Post(
                cpr::Url{apiBaseUrl + "/validate"},
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
                successCallback(ZBValidateResponse::from_json(parsed));
            }
        } catch (const std::exception&) {
            if (errorCallback) {
                errorCallback(ZBErrorResponse::parseError("ZeroBounce returned an unexpected JSON response."));
            }
        }
        return;
    }

    std::string url = apiBaseUrl + "/validate?api_key=" + urlEncode(apiKey) +
        "&email=" + urlEncode(email) +
        "&ip_address=" + (ipAddress.empty() ? "" : urlEncode(ipAddress));
    if (options.timeout.has_value()) {
        url += "&timeout=" + urlEncode(std::to_string(options.timeout.value()));
    }
    if (options.activityData.has_value()) {
        url += "&activity_data=" + urlEncode(options.activityData.value() ? "true" : "false");
    }
    if (options.verifyPlus.has_value()) {
        url += "&verify_plus=" + urlEncode(options.verifyPlus.value() ? "true" : "false");
    }

    sendRequest(url, successCallback, errorCallback);
}

void ZeroBounce::validateBatch(
    std::vector<ZBEmailToValidate> emailBatch,
    OnSuccessCallback<ZBValidateBatchResponse> successCallback,
    OnErrorCallback errorCallback
) {
    validateBatch(emailBatch, ZBValidateOptions{}, successCallback, errorCallback);
}

void ZeroBounce::validateBatch(
    std::vector<ZBEmailToValidate> emailBatch,
    const ZBValidateOptions& options,
    OnSuccessCallback<ZBValidateBatchResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) return;
    if (!checkValidateBatchTimeout(options.timeout, errorCallback)) return;

    try {
        json payload;
        payload["api_key"] = apiKey;

        for (auto& email : emailBatch) {
            json emailObj;
            emailObj["email_address"] = email.emailAddress;

            if (email.ipAddress.empty()) {
                emailObj["ip_address"] = json::value_t::null;
            } else {
                emailObj["ip_address"] = email.ipAddress;
            }

            payload["email_batch"].push_back(emailObj);
        }

        if (options.timeout.has_value()) {
            payload["timeout"] = options.timeout.value();
        }
        if (options.activityData.has_value()) {
            payload["activity_data"] = options.activityData.value();
        }
        if (options.verifyPlus.has_value()) {
            payload["verify_plus"] = options.verifyPlus.value();
        }

        cpr::Response reqResponse = requestHandler->Post(
            cpr::Url{apiBaseUrl + "/validatebatch"},
            cpr::Header{
                {"Accept", "application/json"},
                {"Content-Type", "application/json"}
            },
            cpr::Body{payload.dump()}
        );

        json parsed;
        if (!zbReadJsonResponse(reqResponse.status_code, reqResponse.text, parsed, errorCallback)) {
            return;
        }
        ZBValidateBatchResponse response = ZBValidateBatchResponse::from_json(parsed);
        for (const auto& item : response.errors) {
            if (item.emailAddress == "all") {
                std::string message = item.error.empty()
                    ? "Invalid API Key or your account ran out of credits"
                    : item.error;
                if (errorCallback) {
                    errorCallback(ZBErrorResponse::parseError(message));
                }
                return;
            }
        }
        if (successCallback) {
            successCallback(response);
        }
    } catch (const std::exception&) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("ZeroBounce returned an unexpected JSON response."));
        }
    }
}

void ZeroBounce::sendFile(
    std::string filePath,
    int emailAddressColumnIndex,
    SendFileOptions options,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    sendFileInternal(false, filePath, emailAddressColumnIndex, options, successCallback, errorCallback);
}

void ZeroBounce::fileStatus(
    std::string fileId,
    OnSuccessCallback<ZBFileStatusResponse> successCallback,
    OnErrorCallback errorCallback
) {
    fileStatusInternal(false, fileId, successCallback, errorCallback);
}

void ZeroBounce::getFile(
    std::string fileId,
    std::string localDownloadPath,
    OnSuccessCallback<ZBGetFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    getFileInternal(false, fileId, localDownloadPath, nullptr, successCallback, errorCallback);
}

void ZeroBounce::getFileWithOptions(
    std::string fileId,
    std::string localDownloadPath,
    const GetFileOptions& options,
    OnSuccessCallback<ZBGetFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    getFileInternal(false, fileId, localDownloadPath, &options, successCallback, errorCallback);
}

void ZeroBounce::deleteFile(
    std::string fileId,
    OnSuccessCallback<ZBDeleteFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    deleteFileInternal(false, fileId, successCallback, errorCallback);
}

void ZeroBounce::scoringSendFile(
    std::string filePath,
    int emailAddressColumnIndex,
    SendFileOptions options,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    sendFileInternal(true, filePath, emailAddressColumnIndex, options, successCallback, errorCallback);
}

void ZeroBounce::scoringFileStatus(
    std::string fileId,
    OnSuccessCallback<ZBFileStatusResponse> successCallback,
    OnErrorCallback errorCallback
) {
    fileStatusInternal(true, fileId, successCallback, errorCallback);
}

void ZeroBounce::scoringGetFile(
    std::string fileId,
    std::string localDownloadPath,
    OnSuccessCallback<ZBGetFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    getFileInternal(true, fileId, localDownloadPath, nullptr, successCallback, errorCallback);
}

void ZeroBounce::scoringGetFileWithOptions(
    std::string fileId,
    std::string localDownloadPath,
    const GetFileOptions& options,
    OnSuccessCallback<ZBGetFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    getFileInternal(true, fileId, localDownloadPath, &options, successCallback, errorCallback);
}

void ZeroBounce::scoringDeleteFile(
    std::string fileId,
    OnSuccessCallback<ZBDeleteFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    deleteFileInternal(true, fileId, successCallback, errorCallback);
}

void ZeroBounce::getActivityData(
    std::string email,
    OnSuccessCallback<ZBActivityDataResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) return;

    sendRequest(
        apiBaseUrl + "/activity?api_key=" + urlEncode(apiKey) + "&email=" + urlEncode(email),
        successCallback,
        errorCallback
    );
}

void ZeroBounce::scoreEmail(
    std::string email,
    OnSuccessCallback<ZBScoreEmailResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) return;

    sendRequest(
        apiBaseUrl + "/scoring?api_key=" + urlEncode(apiKey) + "&email=" + urlEncode(email),
        successCallback,
        errorCallback
    );
}

void ZeroBounce::scoreEmailPost(
    std::string email,
    OnSuccessCallback<ZBScoreEmailResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) return;

    try {
        std::string body =
            "api_key=" + urlEncode(apiKey) + "&email=" + urlEncode(email);

        cpr::Response reqResponse = requestHandler->Post(
            cpr::Url{apiBaseUrl + "/scoring"},
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
            successCallback(ZBScoreEmailResponse::from_json(parsed));
        }
    } catch (const std::exception&) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("ZeroBounce returned an unexpected JSON response."));
        }
    }
}

template <typename T>
void ZeroBounce::sendRequest(
    std::string urlPath,
    OnSuccessCallback<T> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) return;

    try {
        cpr::Response reqResponse = requestHandler->Get(
            cpr::Url{urlPath},
            cpr::Header{{"Accept", "application/json"}}
        );

        json parsed;
        if (!zbReadJsonResponse(reqResponse.status_code, reqResponse.text, parsed, errorCallback)) {
            return;
        }
        if (successCallback) {
            successCallback(T::from_json(parsed));
        }
    } catch (const std::exception&) {
        if (errorCallback) {
            errorCallback(ZBErrorResponse::parseError("ZeroBounce returned an unexpected JSON response."));
        }
    }
}

void ZeroBounce::sendFileInternal(
    bool scoring,
    std::string filePath,
    int emailAddressColumnIndex,
    SendFileOptions options,
    OnSuccessCallback<ZBSendFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) return;

    try {
        std::string urlPath = (scoring ? bulkApiScoringBaseUrl : bulkApiBaseUrl) + "/sendfile";

        cpr::Multipart multipart{
            {"api_key", apiKey},
            {"file", cpr::File{filePath}},
            {"email_address_column", emailAddressColumnIndex}
        };

        if (!scoring) {
            if (options.firstNameColumn > 0) {
                multipart.parts.emplace_back(cpr::Part{"first_name_column", options.firstNameColumn});
            }
            if (options.lastNameColumn > 0) {
                multipart.parts.emplace_back(cpr::Part{"last_name_column", options.lastNameColumn});
            }
            if (options.genderColumn > 0) {
                multipart.parts.emplace_back(cpr::Part{"gender_column", options.genderColumn});
            }
            if (options.ipAddressColumn > 0) {
                multipart.parts.emplace_back(cpr::Part{"ip_address_column", options.ipAddressColumn});
            }
        }

        if (!options.returnUrl.empty()) {
                multipart.parts.emplace_back(cpr::Part{"return_url", options.returnUrl});
        }

        multipart.parts.emplace_back(cpr::Part{"has_header_row", std::string(options.hasHeaderRow ? "true" : "false")});
        multipart.parts.emplace_back(cpr::Part{"remove_duplicate", std::string(options.removeDuplicate ? "true" : "false")});

        if (!scoring && options.allowPhase2IsSet) {
            multipart.parts.emplace_back(cpr::Part{"allow_phase_2", std::string(options.allowPhase2 ? "true" : "false")});
        }

        cpr::Response reqResponse = requestHandler->Post(
            cpr::Url{urlPath},
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

void ZeroBounce::fileStatusInternal(
    bool scoring,
    std::string fileId,
    OnSuccessCallback<ZBFileStatusResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) return;

    sendRequest(
        (scoring ? bulkApiScoringBaseUrl : bulkApiBaseUrl) + "/filestatus?api_key=" + urlEncode(apiKey)
            + "&file_id=" + urlEncode(fileId),
        successCallback,
        errorCallback
    );
}

void ZeroBounce::getFileInternal(
    bool scoring,
    std::string fileId,
    std::string localDownloadPath,
    const GetFileOptions* getFileOptionsPtr,
    OnSuccessCallback<ZBGetFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) return;

    GetFileOptions emptyOptions;
    const GetFileOptions& options = getFileOptionsPtr ? *getFileOptionsPtr : emptyOptions;

    try {
        const std::string base = (scoring ? bulkApiScoringBaseUrl : bulkApiBaseUrl) + "/getfile";
        cpr::Parameters params;
        params.Add({{"api_key", apiKey}, {"file_id", fileId}});
        if (!options.downloadType.empty()) {
            params.Add({{"download_type", options.downloadType}});
        }
        if (!scoring && options.activityData.has_value()) {
            params.Add({{"activity_data", *options.activityData ? std::string("true") : std::string("false")}});
        }

        cpr::Response reqResponse = requestHandler->getWithParameters(cpr::Url{base}, params);

        std::string contentType = reqResponse.header["Content-Type"];

        std::string rsp = reqResponse.text;

        if (reqResponse.status_code > 299 || zbContentTypeIsHtml(contentType) || zbBodyLooksLikeHtml(rsp)) {
            zbReportHttpError(reqResponse.status_code, rsp, errorCallback);
        } else {
            if (successCallback) {
                if (shouldTreatGetFileBodyAsError(rsp, contentType)) {
                    if (errorCallback) {
                        errorCallback(ZBErrorResponse::parseError(formatGetFileErrorMessage(rsp)));
                    }
                } else {
                    fs::path filePath(localDownloadPath);

                    if (fs::is_directory(filePath)) {
                        ZBErrorResponse errorResponse = ZBErrorResponse::parseError("Invalid file path");
                        errorCallback(errorResponse);
                        return;
                    }

                    fs::create_directories(filePath.parent_path());

                    std::ofstream fileStream(filePath, std::ofstream::out | std::ofstream::binary);

                    fileStream.write(rsp.c_str(), static_cast<std::streamsize>(rsp.size()));
                    fileStream.close();

                    ZBGetFileResponse response;
                    response.success = true;
                    response.localFilePath = localDownloadPath;
                    successCallback(response);
                }
            }
        }
    } catch (const std::exception& e) {
        ZBErrorResponse errorResponse = ZBErrorResponse::parseError(e.what());
        errorCallback(errorResponse);
    }
}

void ZeroBounce::deleteFileInternal(
    bool scoring,
    std::string fileId,
    OnSuccessCallback<ZBDeleteFileResponse> successCallback,
    OnErrorCallback errorCallback
) {
    if (invalidApiKey(errorCallback)) return;

    sendRequest(
        (scoring ? bulkApiScoringBaseUrl : bulkApiBaseUrl) + "/deletefile?api_key=" + urlEncode(apiKey)
            + "&file_id=" + urlEncode(fileId),
        successCallback,
        errorCallback
    );
}

void ZeroBounce::findEmailInternal(
    std::string domain,
    std::string company_name,
    std::string first_name,
    std::string middle_name,
    std::string last_name,
    OnSuccessCallback<ZBFindEmailResponse> successCallback,
    OnErrorCallback errorCallback
) {
    std::stringstream urlStream;
    urlStream << apiBaseUrl << "/guessformat?api_key=" << urlEncode(apiKey);
    if (domain.size() > 0) {
        urlStream << "&domain=" << urlEncode(domain);
    }
    if (company_name.size() > 0) {
        urlStream << "&company_name=" << urlEncode(company_name);
    }
    if (first_name.size() > 0) {
        urlStream << "&first_name=" << urlEncode(first_name);
    }
    if (middle_name.size() > 0) {
        urlStream << "&middle_name=" << urlEncode(middle_name);
    }
    if (last_name.size() > 0) {
        urlStream << "&last_name=" << urlEncode(last_name);
    }
    sendRequest(urlStream.str(), successCallback, errorCallback);
}

void ZeroBounce::findEmailByDomain(
    std::string domain,
    std::string first_name,
    std::string middle_name,
    std::string last_name,
    OnSuccessCallback<ZBFindEmailResponse> successCallback,
    OnErrorCallback errorCallback
) {
    findEmailInternal(domain, "", first_name, middle_name, last_name, successCallback, errorCallback);
}

void ZeroBounce::findEmailByDomain(
    std::string domain,
    std::string first_name,
    std::string last_name,
    OnSuccessCallback<ZBFindEmailResponse> successCallback,
    OnErrorCallback errorCallback
) {
    findEmailInternal(domain, "", first_name, "", last_name, successCallback, errorCallback);
}

void ZeroBounce::findEmailByDomain(
    std::string domain,
    std::string first_name,
    OnSuccessCallback<ZBFindEmailResponse> successCallback,
    OnErrorCallback errorCallback
) {
    findEmailInternal(domain, "", first_name, "", "", successCallback, errorCallback);
}

void ZeroBounce::findEmailByCompanyName(
    std::string company_name,
    std::string first_name,
    std::string middle_name,
    std::string last_name,
    OnSuccessCallback<ZBFindEmailResponse> successCallback,
    OnErrorCallback errorCallback
) {
    findEmailInternal("", company_name, first_name, middle_name, last_name, successCallback, errorCallback);
}

void ZeroBounce::findEmailByCompanyName(
    std::string company_name,
    std::string first_name,
    std::string last_name,
    OnSuccessCallback<ZBFindEmailResponse> successCallback,
    OnErrorCallback errorCallback
) {
    findEmailInternal("", company_name, first_name, "", last_name, successCallback, errorCallback);
}

void ZeroBounce::findEmailByCompanyName(
    std::string company_name,
    std::string first_name,
    OnSuccessCallback<ZBFindEmailResponse> successCallback,
    OnErrorCallback errorCallback
) {
    findEmailInternal("", company_name, first_name, "", "", successCallback, errorCallback);
}

void ZeroBounce::searchDomainInternal(
    std::string domain,
    std::string company_name,
    OnSuccessCallback<ZBDomainSearchResponse> successCallback,
    OnErrorCallback errorCallback
) {
    std::stringstream urlStream;
    urlStream << apiBaseUrl << "/guessformat?api_key=" << urlEncode(apiKey);
    if (domain.size() > 0) {
        urlStream << "&domain=" << urlEncode(domain);
    }
    if (company_name.size() > 0) {
        urlStream << "&company_name=" << urlEncode(company_name);
    }
    sendRequest(urlStream.str(), successCallback, errorCallback);
}

void ZeroBounce::searchDomainByDomain(
    std::string domain,
    OnSuccessCallback<ZBDomainSearchResponse> successCallback,
    OnErrorCallback errorCallback
) {
    searchDomainInternal(domain, "", successCallback, errorCallback);
}

void ZeroBounce::searchDomainByCompanyName(
    std::string company_name,
    OnSuccessCallback<ZBDomainSearchResponse> successCallback,
    OnErrorCallback errorCallback
) {
    searchDomainInternal("", company_name, successCallback, errorCallback);
}

void ZeroBounce::findEmail(
    std::string domain,
    std::string first_name,
    std::string middle_name,
    std::string last_name,
    OnSuccessCallback<ZBFindEmailResponse> successCallback,
    OnErrorCallback errorCallback
) {
    std::stringstream urlStream;
    urlStream << apiBaseUrl << "/guessformat?api_key=" << urlEncode(apiKey);
    if (domain.size() > 0) {
        urlStream << "&domain=" << urlEncode(domain);
    }
    if (first_name.size() > 0) {
        urlStream << "&first_name=" << urlEncode(first_name);
    }
    if (middle_name.size() > 0) {
        urlStream << "&middle_name=" << urlEncode(middle_name);
    }
    if (last_name.size() > 0) {
        urlStream << "&last_name=" << urlEncode(last_name);
    }
    sendRequest(urlStream.str(), successCallback, errorCallback);
}

void ZeroBounce::findEmail(
    std::string domain,
    std::string first_name,
    std::string last_name,
    OnSuccessCallback<ZBFindEmailResponse> successCallback,
    OnErrorCallback errorCallback
) {
    ZeroBounce::findEmail(
        domain, first_name, "", last_name, successCallback, errorCallback
    );
}

template void ZeroBounce::sendRequest<ZBFileStatusResponse>(
    std::string urlPath,
    OnSuccessCallback<ZBFileStatusResponse> successCallback,
    OnErrorCallback errorCallback
);

template void ZeroBounce::sendRequest<ZBDeleteFileResponse>(
    std::string urlPath,
    OnSuccessCallback<ZBDeleteFileResponse> successCallback,
    OnErrorCallback errorCallback
);
