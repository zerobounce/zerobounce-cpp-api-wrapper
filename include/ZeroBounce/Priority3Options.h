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

struct ValidateS3FileOptions {
    std::string fileName;
    std::optional<bool> hasHeaderRow;
    std::optional<bool> removeDuplicates;
    std::optional<bool> removeDuplicate;
    std::optional<int> emailAddressColumn;
    std::string returnUrl;
    int firstNameColumn = 0;
    int lastNameColumn = 0;
    int genderColumn = 0;
    int ipAddressColumn = 0;
    std::optional<bool> allowPhase2;
};

struct ScoreS3FileOptions {
    std::string fileName;
    std::optional<bool> hasHeaderRow;
    std::optional<bool> removeDuplicates;
    std::optional<bool> removeDuplicate;
    std::optional<int> emailAddressColumn;
};

#endif
