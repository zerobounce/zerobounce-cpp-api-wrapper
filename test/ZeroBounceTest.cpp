#include <ctime>
#include <string>

#include <cpr/cpr.h>
#include <gtest/gtest.h>

#include "ZeroBounce/ZeroBounce.h"

/**
 * Class used to handle http requests made with libcpr during tests.
 */
class MockRequestHandler : public BaseRequestHandler {
    private:
        cpr::Response response;

    protected:
        cpr::Response doGet(const cpr::Url& url) {
            return response;
        }
        cpr::Response doGet(const cpr::Url& url, const cpr::Header& header) {
            return response;
        }
        cpr::Response doPost(const cpr::Url& url, const cpr::Header& header, const cpr::Body& body) {
            return response;
        }
        cpr::Response doPost(const cpr::Url& url, const cpr::Header& header, const cpr::Multipart& multipart) {
            return response;
        }

    public:
        void setResponse(cpr::Response response) {
            this->response = response;
        }
};

/**
 * The ZeroBounce test class.
 */
class ZeroBounceTest : public ZeroBounce {
    private:
        static ZeroBounceTest* instance;

    public:
        void setRequestHandler(BaseRequestHandler* requestHandler) {
            this->requestHandler = requestHandler;
        }

        static ZeroBounceTest* getInstance() {
            if (ZeroBounceTest::instance == nullptr) {
                ZeroBounceTest::instance = new ZeroBounceTest();
            }
            return ZeroBounceTest::instance;
        }
};

ZeroBounceTest* ZeroBounceTest::instance = nullptr;

/**
 * Function that creates a mock response to be returned by http requests.
 *
 * @param content           content of the response
 * @param statusCode        status code of the response
 * @param contentType       content type of the response
 * @return cpr::Response    generated response
 */
cpr::Response mockResponse(std::string content, long statusCode, std::string contentType = "") {
    cpr::Response reqResponse;

    reqResponse.text = content;
    reqResponse.status_code = statusCode;

    if (!contentType.empty()) {
        reqResponse.header["Content-Type"] = contentType;
    }

    return reqResponse;
}

/**
 * Class that handle tests suite.
 */
class Tests : public ::testing::Test {
    protected:
        const std::string API_KEY = "api-key";
        MockRequestHandler* mockRequestHandler;

        void SetUp() override {
            mockRequestHandler = new MockRequestHandler();
            ZeroBounceTest::getInstance()->initialize(API_KEY);
            ZeroBounceTest::getInstance()->setRequestHandler(mockRequestHandler);
        }

        void TearDown() override {
            delete mockRequestHandler;
        }
};

TEST_F(Tests, testGetCreditsInvalid) {
    std::string responseJson = "{\"Credits\":\"-1\"}";

    cpr::Response reqResponse = mockResponse(responseJson, 400);
    mockRequestHandler->setResponse(reqResponse);

    ZBErrorResponse expectedResponse = ZBErrorResponse::parseError(responseJson);

    ZeroBounceTest::getInstance()->getCredits(
        [&](ZBCreditsResponse response) {
            FAIL() << response.toString();
        },
        [&](ZBErrorResponse errorResponse) {
            ASSERT_EQ(errorResponse, expectedResponse);
        }
    );
}

TEST_F(Tests, testGetCreditsValid) {
    std::string responseJson = "{\"Credits\":\"100\"}";

    cpr::Response reqResponse = mockResponse(responseJson, 200);
    mockRequestHandler->setResponse(reqResponse);

    ZBCreditsResponse expectedResponse = ZBCreditsResponse::from_json(json::parse(responseJson));

    ZeroBounceTest::getInstance()->getCredits(
        [&](ZBCreditsResponse response) {
            ASSERT_EQ(response, expectedResponse);
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}

TEST_F(Tests, testGetApiUsageInvalid) {
    std::string responseJson = "{\"error\":\"Invalid API key\"}";

    cpr::Response reqResponse = mockResponse(responseJson, 400);
    mockRequestHandler->setResponse(reqResponse);

    ZBErrorResponse expectedResponse = ZBErrorResponse::parseError(responseJson);

    std::tm startDate = {0};
    startDate.tm_year = 118;
    startDate.tm_mon = 0;
    startDate.tm_mday = 1;

    std::tm endDate = {0};
    endDate.tm_year = 123;
    endDate.tm_mon = 11;
    endDate.tm_mday = 12;

    ZeroBounceTest::getInstance()->getApiUsage(
        startDate,
        endDate,
        [&](ZBGetApiUsageResponse response) {
            FAIL() << response.toString();
        },
        [&](ZBErrorResponse errorResponse) {
            ASSERT_EQ(errorResponse, expectedResponse);
        }
    );
}

TEST_F(Tests, testGetApiUsageValid) {
    std::string responseJson = R"({
        "total": 3,
        "status_valid": 1,
        "status_invalid": 2,
        "status_catch_all": 0,
        "status_do_not_mail": 0,
        "status_spamtrap": 0,
        "status_unknown": 0,
        "sub_status_toxic": 0,
        "sub_status_disposable": 0,
        "sub_status_role_based": 0,
        "sub_status_possible_trap": 0,
        "sub_status_global_suppression": 0,
        "sub_status_timeout_exceeded": 0,
        "sub_status_mail_server_temporary_error": 0,
        "sub_status_mail_server_did_not_respond": 0,
        "sub_status_greylisted": 0,
        "sub_status_antispam_system": 0,
        "sub_status_does_not_accept_mail": 0,
        "sub_status_exception_occurred": 0,
        "sub_status_failed_syntax_check": 0,
        "sub_status_mailbox_not_found": 2,
        "sub_status_unroutable_ip_address": 0,
        "sub_status_possible_typo": 0,
        "sub_status_no_dns_entries": 0,
        "sub_status_role_based_catch_all": 0,
        "sub_status_mailbox_quota_exceeded": 0,
        "sub_status_forcible_disconnect": 0,
        "sub_status_failed_smtp_connection": 0,
        "sub_status_accept_all": 0,
        "sub_status_mx_forward": 0,
        "sub_status_alternate": 0,
        "sub_status_blocked": 0,
        "sub_status_allowed": 0,
        "start_date": "1/1/2018",
        "end_date": "12/12/2019"
    })";

    cpr::Response reqResponse = mockResponse(responseJson, 200);
    mockRequestHandler->setResponse(reqResponse);

    ZBGetApiUsageResponse expectedResponse = ZBGetApiUsageResponse::from_json(json::parse(responseJson));

    std::tm startDate = {0};
    startDate.tm_year = 118;
    startDate.tm_mon = 0;
    startDate.tm_mday = 1;

    std::tm endDate = {0};
    endDate.tm_year = 123;
    endDate.tm_mon = 11;
    endDate.tm_mday = 12;

    ZeroBounceTest::getInstance()->getApiUsage(
        startDate,
        endDate,
        [&](ZBGetApiUsageResponse response) {
            ASSERT_EQ(response, expectedResponse);
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}

TEST_F(Tests, testSingleEmailValidateInvalid) {
    std::string responseJson = "{\"error\":\"Invalid API key or your account ran out of credits\"}";

    cpr::Response reqResponse = mockResponse(responseJson, 400);
    mockRequestHandler->setResponse(reqResponse);

    ZBErrorResponse expectedResponse = ZBErrorResponse::parseError(responseJson);

    ZeroBounceTest::getInstance()->validate(
        "valid@example.com",
        "127.0.0.1",
        [&](ZBValidateResponse response) {
            FAIL() << response.toString();
        },
        [&](ZBErrorResponse errorResponse) {
            ASSERT_EQ(errorResponse, expectedResponse);
        }
    );
}

TEST_F(Tests, testSingleEmailValidateValid) {
    std::string responseJson = R"({
        "address": "valid@example.com",
        "status": "valid",
        "sub_status": "",
        "free_email": false,
        "did_you_mean": null,
        "account": null,
        "domain": null,
        "domain_age_days": "9692",
        "smtp_provider": "example",
        "mx_found": "true",
        "mx_record": "mx.example.com",
        "firstname": "zero",
        "lastname": "bounce",
        "gender": "male",
        "country": null,
        "region": null,
        "city": null,
        "zipcode": null,
        "processed_at": "2023-04-05 13:29:47.553"
    })";

    cpr::Response reqResponse = mockResponse(responseJson, 200);
    mockRequestHandler->setResponse(reqResponse);

    ZBValidateResponse expectedResponse = ZBValidateResponse::from_json(json::parse(responseJson));

    ZeroBounceTest::getInstance()->validate(
        "valid@example.com",
        "127.0.0.1",
        [&](ZBValidateResponse response) {
            ASSERT_EQ(response, expectedResponse);
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}

TEST_F(Tests, testBatchEmailValidateInvalid) {
    std::string responseJson = "{\"Message\":\"Missing parameter: email_address.\"}";

    cpr::Response reqResponse = mockResponse(responseJson, 400);
    mockRequestHandler->setResponse(reqResponse);

    ZBErrorResponse expectedResponse = ZBErrorResponse::parseError(responseJson);

    std::vector<ZBEmailToValidate> emails;

    ZeroBounceTest::getInstance()->validateBatch(
        emails,
        [&](ZBValidateBatchResponse response) {
            FAIL() << response.toString();
        },
        [&](ZBErrorResponse errorResponse) {
            ASSERT_EQ(errorResponse, expectedResponse);
        }
    );
}

TEST_F(Tests, testBatchEmailValidateValid) {
    std::string responseJson = R"({
        "email_batch": [
            {
                "address": "valid@example.com",
                "status": "valid",
                "sub_status": "",
                "free_email": false,
                "did_you_mean": null,
                "account": null,
                "domain": null,
                "domain_age_days": "9692",
                "smtp_provider": "example",
                "mx_found": "true",
                "mx_record": "mx.example.com",
                "firstname": "zero",
                "lastname": "bounce",
                "gender": "male",
                "country": null,
                "region": null,
                "city": null,
                "zipcode": null,
                "processed_at": "2023-04-05 14:35:24.051"
            },
            {
                "address": "invalid@example.com",
                "status": "invalid",
                "sub_status": "mailbox_not_found",
                "free_email": false,
                "did_you_mean": null,
                "account": null,
                "domain": null,
                "domain_age_days": "9692",
                "smtp_provider": "example",
                "mx_found": "true",
                "mx_record": "mx.example.com",
                "firstname": "zero",
                "lastname": "bounce",
                "gender": "male",
                "country": null,
                "region": null,
                "city": null,
                "zipcode": null,
                "processed_at": "2023-04-05 14:35:24.051"
            }
        ],
        "errors": []
    })";

    cpr::Response reqResponse = mockResponse(responseJson, 200);
    mockRequestHandler->setResponse(reqResponse);

    ZBValidateBatchResponse expectedResponse = ZBValidateBatchResponse::from_json(json::parse(responseJson));

    std::vector<ZBEmailToValidate> emails = {
        {"valid@example.com", "127.0.0.1"},
		{"invalid@example.com"}
    };

    ZeroBounceTest::getInstance()->validateBatch(
        emails,
        [&](ZBValidateBatchResponse response) {
            ASSERT_EQ(response, expectedResponse);
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}

TEST_F(Tests, testSendFileInvalid) {
    std::string responseJson = "{\"success\":\"False\",\"message\":[\"api_key is invalid\"]}";

    cpr::Response reqResponse = mockResponse(responseJson, 401);
    mockRequestHandler->setResponse(reqResponse);

    ZBErrorResponse expectedResponse = ZBErrorResponse::parseError(responseJson);

    SendFileOptions options;

    ZeroBounceTest::getInstance()->sendFile(
        "../email_file.csv",
        1,
        options,
        [&](ZBSendFileResponse response) {
            FAIL() << response.toString();
        },
        [&](ZBErrorResponse errorResponse) {
            ASSERT_EQ(errorResponse, expectedResponse);
        }
    );
}

TEST_F(Tests, testSendFileValid) {
    std::string responseJson = R"({
        "success":true,
        "message":"File Accepted",
        "file_name":"email_file.csv",
        "file_id":"aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff"
    })";

    cpr::Response reqResponse = mockResponse(responseJson, 201);
    mockRequestHandler->setResponse(reqResponse);

    ZBSendFileResponse expectedResponse = ZBSendFileResponse::from_json(json::parse(responseJson));

    SendFileOptions options;

    ZeroBounceTest::getInstance()->sendFile(
        "../email_file.csv",
        1,
        options,
        [&](ZBSendFileResponse response) {
            ASSERT_EQ(response, expectedResponse);
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}

TEST_F(Tests, testFileStatusInvalid) {
    std::string responseJson = "{\"success\":\"False\",\"message\":[\"api_key is invalid\"]}";

    cpr::Response reqResponse = mockResponse(responseJson, 401);
    mockRequestHandler->setResponse(reqResponse);

    ZBErrorResponse expectedResponse = ZBErrorResponse::parseError(responseJson);

    ZeroBounceTest::getInstance()->fileStatus(
        "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff",
        [&](ZBFileStatusResponse response) {
            FAIL() << response.toString();
        },
        [&](ZBErrorResponse errorResponse) {
            ASSERT_EQ(errorResponse, expectedResponse);
        }
    );
}

TEST_F(Tests, testFileStatusValid) {
    std::string responseJson = R"({
        "success": true,
        "file_id": "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff",
        "file_name": "email_file.csv",
        "upload_date": "10/20/2018 4:35:58 PM",
        "file_status": "Complete",
        "complete_percentage": "100%",
        "error_reason": null,
        "return_url": "Your return URL if provided when calling sendfile API"
    })";

    cpr::Response reqResponse = mockResponse(responseJson, 200);
    mockRequestHandler->setResponse(reqResponse);

    ZBFileStatusResponse expectedResponse = ZBFileStatusResponse::from_json(json::parse(responseJson));

    ZeroBounceTest::getInstance()->fileStatus(
        "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff",
        [&](ZBFileStatusResponse response) {
            ASSERT_EQ(response, expectedResponse);
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}

TEST_F(Tests, testGetFileInvalid) {
    std::string responseJson = "{\"success\":false,\"message\":\"File deleted.\"}";

    cpr::Response reqResponse = mockResponse(responseJson, 400);
    mockRequestHandler->setResponse(reqResponse);

    ZBErrorResponse expectedResponse = ZBErrorResponse::parseError(responseJson);

    ZeroBounceTest::getInstance()->getFile(
        "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff",
        "test/downloads/file.csv",
        [&](ZBGetFileResponse response) {
            FAIL() << response.toString();
        },
        [&](ZBErrorResponse errorResponse) {
            ASSERT_EQ(errorResponse, expectedResponse);
        }
    );
}

TEST_F(Tests, testGetFileValid) {
    std::string responseJson =
        R"("Email Address","First Name","Last Name","Gender","ZB Status","ZB Sub Status","ZB Account","ZB Domain","ZB First Name","ZB Last Name","ZB Gender","ZB Free Email","ZB MX Found","ZB MX Record","ZB SMTP Provider","ZB Did You Mean"
        "valid@example.com","zero","bounce","","valid","","","","zero","bounce","male","False","true","mx.example.com","example",""
        )";

    cpr::Response reqResponse = mockResponse(responseJson, 200, "application/octet-stream");
    mockRequestHandler->setResponse(reqResponse);

    ZBGetFileResponse expectedResponse;
    expectedResponse.success = true;
    expectedResponse.localFilePath = "test/downloads/file.csv";

    ZeroBounceTest::getInstance()->getFile(
        "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff",
        "test/downloads/file.csv",
        [&](ZBGetFileResponse response) {
            ASSERT_EQ(response, expectedResponse);
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}

TEST_F(Tests, testDeleteFileInvalid) {
    std::string responseJson = "{\"success\":false,\"message\":\"File cannot be found.\"}";

    cpr::Response reqResponse = mockResponse(responseJson, 400);
    mockRequestHandler->setResponse(reqResponse);

    ZBErrorResponse expectedResponse = ZBErrorResponse::parseError(responseJson);

    ZeroBounceTest::getInstance()->deleteFile(
        "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff",
        [&](ZBDeleteFileResponse response) {
            FAIL() << response.toString();
        },
        [&](ZBErrorResponse errorResponse) {
            ASSERT_EQ(errorResponse, expectedResponse);
        }
    );
}

TEST_F(Tests, testDeleteFileValid) {
    std::string responseJson = R"({
        "success":true,
        "message":"File Deleted",
        "file_name":"test2",
        "file_id":"b222a0fd-90d5-416c-8f1a-9cc3851fc823"
    })";

    cpr::Response reqResponse = mockResponse(responseJson, 200);
    mockRequestHandler->setResponse(reqResponse);

    ZBDeleteFileResponse expectedResponse = ZBDeleteFileResponse::from_json(json::parse(responseJson));

    ZeroBounceTest::getInstance()->deleteFile(
        "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff",
        [&](ZBDeleteFileResponse response) {
            ASSERT_EQ(response, expectedResponse);
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}

TEST_F(Tests, testScoringSendFileInvalid) {
    std::string responseJson = "{\"success\":\"False\",\"message\":[\"api_key is invalid\"]}";

    cpr::Response reqResponse = mockResponse(responseJson, 401);
    mockRequestHandler->setResponse(reqResponse);

    ZBErrorResponse expectedResponse = ZBErrorResponse::parseError(responseJson);

    SendFileOptions options;

    ZeroBounceTest::getInstance()->scoringSendFile(
        "../email_file.csv",
        1,
        options,
        [&](ZBSendFileResponse response) {
            FAIL() << response.toString();
        },
        [&](ZBErrorResponse errorResponse) {
            ASSERT_EQ(errorResponse, expectedResponse);
        }
    );
}

TEST_F(Tests, testScoringSendFileValid) {
    std::string responseJson = R"({
        "success":true,
        "message":"File Accepted",
        "file_name":"email_file.csv",
        "file_id":"aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff"
    })";

    cpr::Response reqResponse = mockResponse(responseJson, 201);
    mockRequestHandler->setResponse(reqResponse);

    ZBSendFileResponse expectedResponse = ZBSendFileResponse::from_json(json::parse(responseJson));

    SendFileOptions options;

    ZeroBounceTest::getInstance()->scoringSendFile(
        "../email_file.csv",
        1,
        options,
        [&](ZBSendFileResponse response) {
            ASSERT_EQ(response, expectedResponse);
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}

TEST_F(Tests, testScoringFileStatusInvalid) {
    std::string responseJson = "{\"success\":\"False\",\"message\":[\"api_key is invalid\"]}";

    cpr::Response reqResponse = mockResponse(responseJson, 401);
    mockRequestHandler->setResponse(reqResponse);

    ZBErrorResponse expectedResponse = ZBErrorResponse::parseError(responseJson);

    ZeroBounceTest::getInstance()->scoringFileStatus(
        "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff",
        [&](ZBFileStatusResponse response) {
            FAIL() << response.toString();
        },
        [&](ZBErrorResponse errorResponse) {
            ASSERT_EQ(errorResponse, expectedResponse);
        }
    );
}

TEST_F(Tests, testScoringFileStatusValid) {
    std::string responseJson = R"({
        "success": true,
        "file_id": "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff",
        "file_name": "email_file.csv",
        "upload_date": "10/20/2018 4:35:58 PM",
        "file_status": "Complete",
        "complete_percentage": "100%",
        "error_reason": null,
        "return_url": "Your return URL if provided when calling sendfile API"
    })";

    cpr::Response reqResponse = mockResponse(responseJson, 200);
    mockRequestHandler->setResponse(reqResponse);

    ZBFileStatusResponse expectedResponse = ZBFileStatusResponse::from_json(json::parse(responseJson));

    ZeroBounceTest::getInstance()->scoringFileStatus(
        "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff",
        [&](ZBFileStatusResponse response) {
            ASSERT_EQ(response, expectedResponse);
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}

TEST_F(Tests, testScoringGetFileInvalid) {
    std::string responseJson = "{\"success\":false,\"message\":\"File deleted.\"}";

    cpr::Response reqResponse = mockResponse(responseJson, 400);
    mockRequestHandler->setResponse(reqResponse);

    ZBErrorResponse expectedResponse = ZBErrorResponse::parseError(responseJson);

    ZeroBounceTest::getInstance()->scoringGetFile(
        "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff",
        "test/downloads/ai_file.csv",
        [&](ZBGetFileResponse response) {
            FAIL() << response.toString();
        },
        [&](ZBErrorResponse errorResponse) {
            ASSERT_EQ(errorResponse, expectedResponse);
        }
    );
}

TEST_F(Tests, testScoringGetFileValid) {
    std::string responseJson =
        R"("email","firstname","lastname","ZeroBounceQualityScore"
        "valid@example.com","zero","bounce","10")";

    cpr::Response reqResponse = mockResponse(responseJson, 200, "application/octet-stream");
    mockRequestHandler->setResponse(reqResponse);

    ZBGetFileResponse expectedResponse;
    expectedResponse.success = true;
    expectedResponse.localFilePath = "test/downloads/ai_file.csv";

    ZeroBounceTest::getInstance()->scoringGetFile(
        "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff",
        "test/downloads/ai_file.csv",
        [&](ZBGetFileResponse response) {
            ASSERT_EQ(response, expectedResponse);
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}

TEST_F(Tests, testScoringDeleteFileInvalid) {
    std::string responseJson = "{\"success\":false,\"message\":\"File cannot be found.\"}";

    cpr::Response reqResponse = mockResponse(responseJson, 400);
    mockRequestHandler->setResponse(reqResponse);

    ZBErrorResponse expectedResponse = ZBErrorResponse::parseError(responseJson);

    ZeroBounceTest::getInstance()->scoringDeleteFile(
        "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff",
        [&](ZBDeleteFileResponse response) {
            FAIL() << response.toString();
        },
        [&](ZBErrorResponse errorResponse) {
            ASSERT_EQ(errorResponse, expectedResponse);
        }
    );
}

TEST_F(Tests, testScoringDeleteFileValid) {
    std::string responseJson = R"({
        "success":true,
        "message":"File Deleted",
        "file_name":"test2",
        "file_id":"b222a0fd-90d5-416c-8f1a-9cc3851fc823"
    })";

    cpr::Response reqResponse = mockResponse(responseJson, 200);
    mockRequestHandler->setResponse(reqResponse);

    ZBDeleteFileResponse expectedResponse = ZBDeleteFileResponse::from_json(json::parse(responseJson));

    ZeroBounceTest::getInstance()->scoringDeleteFile(
        "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff",
        [&](ZBDeleteFileResponse response) {
            ASSERT_EQ(response, expectedResponse);
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}

TEST_F(Tests, testActivityDataInvalid) {
    std::string responseJson = "{\"error\":\"Invalid API key or your account ran out of credits\"}";

    cpr::Response reqResponse = mockResponse(responseJson, 400);
    mockRequestHandler->setResponse(reqResponse);

    ZBErrorResponse expectedResponse = ZBErrorResponse::parseError(responseJson);

    ZeroBounceTest::getInstance()->getActivityData(
        "valid@example.com",
        [&](ZBActivityDataResponse response) {
            FAIL() << response.toString();
        },
        [&](ZBErrorResponse errorResponse) {
            ASSERT_EQ(errorResponse, expectedResponse);
        }
    );
}

TEST_F(Tests, testActivityDataValid) {
    std::string responseJson = "{\"found\":true,\"active_in_days\":\"180\"}";

    cpr::Response reqResponse = mockResponse(responseJson, 200);
    mockRequestHandler->setResponse(reqResponse);

    ZBActivityDataResponse expectedResponse = ZBActivityDataResponse::from_json(json::parse(responseJson));

    ZeroBounceTest::getInstance()->getActivityData(
        "valid@example.com",
        [&](ZBActivityDataResponse response) {
            ASSERT_EQ(response, expectedResponse);
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}

TEST_F(Tests, testFindMailStatusInvalid) {
    std::string responseJson = "{\n"
        "    \"email\": \"\",\n"
        "    \"email_confidence\": \"\",\n"
        "    \"domain\": \"example.com\",\n"
        "    \"company_name\": \"\",\n"
        "    \"did_you_mean\": \"\",\n"
        "    \"failure_reason\": \"\"\n"
        "}";

    cpr::Response reqResponse = mockResponse(responseJson, 200);
    mockRequestHandler->setResponse(reqResponse);

    ZBFindEmailResponse expectedResponse = ZBFindEmailResponse::from_json(json::parse(responseJson));

    ZeroBounceTest::getInstance()->findEmailByDomain(
        "example.com",
        "John",
        "Doe",
        [&](ZBFindEmailResponse response) {
            ASSERT_EQ(response, expectedResponse);
            ASSERT_EQ(response.toString(), expectedResponse.toString());
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}

TEST_F(Tests, testFindMailStatusValid) {
    std::string responseJson = "{\n"
        "    \"email\": \"john.doe@example.com\",\n"
        "    \"email_confidence\": \"high\",\n"
        "    \"domain\": \"example.com\",\n"
        "    \"company_name\": \"\",\n"
        "    \"did_you_mean\": \"\",\n"
        "    \"failure_reason\": \"\"\n"
        "}";

    cpr::Response reqResponse = mockResponse(responseJson, 200);
    mockRequestHandler->setResponse(reqResponse);

    ZBFindEmailResponse expectedResponse = ZBFindEmailResponse::from_json(json::parse(responseJson));

    ZeroBounceTest::getInstance()->findEmailByDomain(
        "example.com",
        "John",
        "Doe",
        [&](ZBFindEmailResponse response) {
            ASSERT_EQ(response, expectedResponse);
            ASSERT_EQ(response.toString(), expectedResponse.toString());
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}

TEST_F(Tests, testSearchDomainStatusInvalid) {
    std::string responseJson = "{\n"
        "    \"domain\": \"example.com\",\n"
        "    \"company_name\": \"\",\n"
        "    \"format\": \"unknown\",\n"
        "    \"confidence\": \"undetermined\",\n"
        "    \"did_you_mean\": \"\",\n"
        "    \"failure_reason\": \"\",\n"
        "    \"other_domain_formats\": []\n"
        "}";

    cpr::Response reqResponse = mockResponse(responseJson, 200);
    mockRequestHandler->setResponse(reqResponse);

    ZBDomainSearchResponse expectedResponse = ZBDomainSearchResponse::from_json(json::parse(responseJson));

    ZeroBounceTest::getInstance()->searchDomainByDomain(
        "example.com",
        [&](ZBDomainSearchResponse response) {
            ASSERT_EQ(response, expectedResponse);
            ASSERT_EQ(response.toString(), expectedResponse.toString());
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}

TEST_F(Tests, testSearchDomainStatusValid) {
    std::string responseJson = "{\n"
        "    \"domain\": \"example.com\",\n"
        "    \"company_name\": \"\",\n"
        "    \"format\": \"first.last\",\n"
        "    \"confidence\": \"high\",\n"
        "    \"did_you_mean\": \"\",\n"
        "    \"failure_reason\": \"\",\n"
        "    \"other_domain_formats\": [\n"
        "        {\n"
        "            \"format\": \"first_last\",\n"
        "            \"confidence\": \"high\"\n"
        "        },\n"
        "        {\n"
        "            \"format\": \"first\",\n"
        "            \"confidence\": \"medium\"\n"
        "        }\n"
        "    ]\n"
        "}";

    cpr::Response reqResponse = mockResponse(responseJson, 200);
    mockRequestHandler->setResponse(reqResponse);

    ZBDomainSearchResponse expectedResponse = ZBDomainSearchResponse::from_json(json::parse(responseJson));

    ZeroBounceTest::getInstance()->searchDomainByDomain(
        "example.com",
        [&](ZBDomainSearchResponse response) {
            ASSERT_EQ(response, expectedResponse);
            ASSERT_EQ(response.toString(), expectedResponse.toString());
        },
        [&](ZBErrorResponse errorResponse) {
            FAIL() << errorResponse.toString();
        }
    );
}