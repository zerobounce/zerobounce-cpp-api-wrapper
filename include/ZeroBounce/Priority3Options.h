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

/**
 * The documented fields shared by the six S3 and SFTP/FTP file jobs.
 *
 * `fileName` is required by every job except `validateSFTPFile`, where the docs
 * list it as optional: leaving it unset there uses the input bucket configured
 * in your ZeroBounce settings.
 */
struct BulkFileJobOptions {
    std::optional<std::string> fileName;
    bool hasHeaderRow = true;
    std::optional<bool> removeDuplicates;
    std::optional<std::string> returnUrl;
};

/**
 * `validateS3File` takes the shared job fields plus the column indexes and
 * `allow_phase_2` that only this endpoint accepts.
 *
 * `fileName` and `returnUrl` are both required: the live API rejects a job
 * without a return URL ("Provided return URL is not permitted: None") even
 * though the docs mark it optional. `hasHeaderRow` is always sent, like the
 * other S3 and SFTP jobs.
 */
struct ValidateS3FileOptions {
    std::string fileName;
    bool hasHeaderRow = true;
    std::optional<bool> removeDuplicates;
    std::optional<int> emailAddressColumn;
    std::string returnUrl;
    int firstNameColumn = 0;
    int lastNameColumn = 0;
    int genderColumn = 0;
    int ipAddressColumn = 0;
    std::optional<bool> allowPhase2;
};

/**
 * `scoreS3File` takes the shared job fields plus `email_address_column`.
 * `allow_phase_2` and the name columns are not among them.
 *
 * `fileName` and `returnUrl` are both required, for the same reason as on
 * `ValidateS3FileOptions`.
 */
struct ScoreS3FileOptions {
    std::string fileName;
    bool hasHeaderRow = true;
    std::optional<bool> removeDuplicates;
    std::optional<int> emailAddressColumn;
    std::string returnUrl;
};

#endif
