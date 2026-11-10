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
 * The documented fields shared by the eight S3 and SFTP/FTP file jobs.
 * `emailFinderS3File`, `domainSearchS3File` and the four SFTP/FTP jobs take this
 * struct; `validateS3File` and `scoreS3File` take their own options, which carry
 * the same fields.
 *
 * `fileName` and `returnUrl` are required by every one of these jobs,
 * `validateSFTPFile` included, although the docs list `file_name` as optional
 * there. Leaving either unset or blank is rejected with
 * "Empty parameter: file_name" or "Empty parameter: return_url" before a request
 * is sent.
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
