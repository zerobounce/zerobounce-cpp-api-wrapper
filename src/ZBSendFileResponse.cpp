#include <sstream>

#include "ZeroBounce/utils.h"
#include "ZeroBounce/ZBSendFileResponse.h"

std::string ZBSendFileResponse::toString()
{
    std::stringstream stringStream;
    stringStream << "ZBSendFileResponse{" <<
                "success=" << success <<
                ", message='";

    for (auto it = message.begin(); it != message.end(); ++it) {
        stringStream << *it;
        if (std::next(it) != message.end()) {
            stringStream << ", ";
        }
    }

    stringStream << '\'' <<
                ", fileName='" << fileName << '\'' <<
                ", fileId='" << fileId << '\'' << '}';

    return stringStream.str();
}

ZBSendFileResponse ZBSendFileResponse::from_json(const json& j) {
    ZBSendFileResponse r;

    r.success = getOrDefault<bool>(j, "success", false);

    if (j.contains("message") && j["message"].is_array()) {
        std::vector<std::string> messageArray = getOrDefault<std::vector<std::string>>(j, "message", {});
        r.message.insert(r.message.end(), messageArray.begin(), messageArray.end());
    } else {
        r.message.push_back(getOrDefault<std::string>(j, "message", ""));
    }

    r.fileName = getOrDefault<std::string>(j, "file_name", "");
    r.fileId = getOrDefault<std::string>(j, "file_id", "");

    return r;
}

bool ZBSendFileResponse::operator==(const ZBSendFileResponse& other) const {
    return success == other.success &&
        message == other.message &&
        fileName == other.fileName &&
        fileId == other.fileId;
}