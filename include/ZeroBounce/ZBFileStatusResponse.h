#ifndef ZBFILESTATUSRESPONSE_H
#define ZBFILESTATUSRESPONSE_H

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * The model used for the GET /scoring/filestatus and GET /filestatus requests.
 */
class ZBFileStatusResponse {
    public:
        bool success = false;
        std::string message;
        std::string errorReason;
        std::string fileId;
        std::string fileName;
        std::string uploadDate;
        std::string fileStatus;
        std::string completePercentage;
        std::string filePhase2Status;
        std::string returnUrl;

        std::string toString();

        static ZBFileStatusResponse from_json(const json& j);

        bool operator==(const ZBFileStatusResponse& other) const;
};

#endif