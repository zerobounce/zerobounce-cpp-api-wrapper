#include <sstream>

#include "ZeroBounce/utils.h"
#include "ZeroBounce/ZBGetFileResponse.h"

std::string ZBGetFileResponse::toString()
{
    std::stringstream stringStream;
    stringStream << "ZBGetFileResponse{" <<
                "success=" << success <<
                ", message='" << message << '\'' <<
                ", localFilePath='" << localFilePath << '\'' <<
                '}';

    return stringStream.str();
}

ZBGetFileResponse ZBGetFileResponse::from_json(const json& j) {
    ZBGetFileResponse r;

    r.success = getOrDefault<bool>(j, "success", false);
    r.message = zbFileJobMessage(j);

    return r;
}

bool ZBGetFileResponse::operator==(const ZBGetFileResponse& other) const {
    return success == other.success &&
        message == other.message &&
        localFilePath == other.localFilePath;
}