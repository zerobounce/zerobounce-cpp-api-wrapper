#ifndef ZB_PRIORITY3_OPTIONS_H
#define ZB_PRIORITY3_OPTIONS_H

#include <optional>
#include <string>

struct EmailFinderSendFileOptions {
    int firstNameColumn = 0;
    int lastNameColumn = 0;
    int middleNameColumn = 0;
    int fullNameColumn = 0;
    std::optional<bool> hasHeaderRow;
};

struct DomainSearchSendFileOptions {
    std::optional<bool> hasHeaderRow;
};

#endif
