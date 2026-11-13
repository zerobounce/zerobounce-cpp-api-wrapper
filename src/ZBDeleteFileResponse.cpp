#include <sstream>

#include "ZeroBounce/utils.h"
#include "ZeroBounce/ZBDeleteFileResponse.h"

std::string ZBDeleteFileResponse::toString()
{
    std::stringstream stringStream;
    stringStream << "ZBDeleteFileResponse{" <<
                "success=" << success <<
                ", message='" << message << '\'' <<
                ", fileName='" << fileName << '\'' <<
                ", fileId='" << fileId << '\'' <<
                '}';

    return stringStream.str();
}

ZBDeleteFileResponse ZBDeleteFileResponse::from_json(const json& j) {
    ZBDeleteFileResponse r;

    r.success = getOrDefault<bool>(j, "success", false);
    r.message = zbFileJobMessage(j);
    r.fileName = getOrDefault<std::string>(j, "file_name", "");
    r.fileId = getOrDefault<std::string>(j, "file_id", "");

    return r;
}

bool ZBDeleteFileResponse::operator==(const ZBDeleteFileResponse& other) const {
    return success == other.success &&
        message == other.message &&
        fileName == other.fileName &&
        fileId == other.fileId;
}