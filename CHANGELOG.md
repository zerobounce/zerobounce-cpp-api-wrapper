# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- Every JSON call (`validate`, `getCredits`, `validateBatch`, `sendFile`, the file status and delete calls, the email finder and domain search, the S3 and SFTP file jobs, the filters, the Inbox Placement Tester and the rest) ignored the transport error. A response cut short after its HTTP 200 header (the connection closed before `Content-Length` bytes arrived, or the read timed out part-way) was returned as a result whenever the part that arrived parsed as JSON. They now call the error callback with `The response from ZeroBounce was cut short: <transport error>` and `statusCode` 0, as the downloads do, with the API key masked out of the message. A request that got no response at all now reports `Could not reach ZeroBounce: <transport error>` instead of `Empty response from ZeroBounce (HTTP 0)`; the status is 0 either way. The Warmup client is not affected.

## [3.0.1] - 2026-11-11

### Changed

- The bundled libcurl is built without zlib (`CURL_ZLIB` defaults to `OFF`, as in the C SDK; `-DCURL_ZLIB=ON` still turns it on). Left at curl's `AUTO`, the macOS build linked the system `/usr/lib/libz` while a Linux machine without zlib headers built without it, and cpr fetched zlib-ng for it either way. The SDK never asks for compressed responses. Build-only: no API change.

### Fixed

- A file download cut short after its HTTP 200 header was saved and reported as a success. When the connection closed part-way through the body (for example after 26 of 1000 bytes), or the body stalled past the read timeout, `getFile`, `getFileWithOptions`, `scoringGetFile`, `scoringGetFileWithOptions`, `emailFinderGetFile` and `domainSearchGetFile` wrote the partial file and called the success callback. They now call the error callback with `The response from ZeroBounce was cut short: <transport error>` and `statusCode` 0, and write nothing. A body shorter than its `Content-Length` is treated the same way even when the request handler reports no transport error.
- `ZeroBounceWarmup` reported a request that got no response at all (connection refused, timed out, failed TLS handshake: HTTP status 0) as a success: the JSON calls passed the payload `null` to the success callback. Every Warmup call now reports `Could not reach ZeroBounce: <transport error>` with `statusCode` 0 to the error callback, and a response cut short is reported as above. The CSV downloads (`downloadSeeds`, `downloadStatistics`) already failed, with `Empty response from ZeroBounce (HTTP 0)`; they now give the same message.
- A download answered with a JSON object instead of a file was saved as the file when its keys were `Message` or `Error` (as an invalid key answers) or other keys, unless the content type was `application/json`; `emailFinderGetFile` and `domainSearchGetFile` saved it even with that content type. Every download now reports any JSON object body, whatever its keys or content type, to the error callback with the HTTP status, and writes nothing, as the Pascal and iOS SDKs do. `ZeroBounce::getFileJsonIndicatesError` and `formatGetFileErrorMessage` also recognise the `Message` and `Error` keys.
- When a downloaded file could be written only in part (a full disk, a file size limit), the error was reported but the truncated file was left behind. It is now removed.

## [3.0.0] - 2026-11-12

### Breaking

- Every request is HTTPS only. A base URL set to anything but `https://` with `setBulkApiRootUrl`, `setBulkApiBaseUrl`, `setBulkApiScoringBaseUrl`, `setListEvaluatorBaseUrl` or `setInboxPlacementBaseUrl` makes each call that uses it report `url must be an https:// URL` to its error callback and send nothing. 2.2.0 sent those requests, API key included, over plain `http://`. The `ZBApiURL` hosts are all `https://` and are not affected.
- `initialize` rejects an API key with surrounding whitespace or control characters, as `ZeroBounceWarmup::initialize` already did: every call then reports `api_key has surrounding whitespace or control characters` to its error callback and sends nothing. 2.2.0 sent such a key. Re-initializing with a rejected key does not fall back to the key set before it.
- `RequestHandler` gained two data members (its timeouts) and overrides `doPut` and `doDelete`, so code built against 2.2.0 that subclasses or embeds it must be rebuilt.

### Added

- `RequestHandler(timeoutMs, connectTimeoutMs)` builds a handler with other timeouts, and `BaseRequestHandler::DefaultTimeoutMs` (120000) and `DefaultConnectTimeoutMs` (30000) name the defaults.

### Changed

- Every request has a 30-second connect timeout, the TLS handshake included, next to the 120-second total timeout it already had.
- The five base URL setters strip trailing slashes, so `setBulkApiBaseUrl("https://host/v2/")` no longer produces `https://host/v2//sendfile`.
- `emailFinderGetFile` and `domainSearchGetFile` create the download path's missing parent directories, as `getFile` already did.

### Fixed

- `getFile`, `getFileWithOptions`, `scoringGetFile`, `scoringGetFileWithOptions`, `emailFinderGetFile` and `domainSearchGetFile` reported success and wrote an empty file when no response arrived at all (connection refused, timed out, failed TLS handshake: HTTP status 0). They now call the error callback with `Could not reach ZeroBounce: <transport error>` and `statusCode` 0, and write nothing. A redirect is reported as `ZeroBounce returned a redirect, which this client does not follow.` with its status.
- `emailFinderGetFile` and `domainSearchGetFile` reported success when the downloaded file could not be written (for example a path under a regular file), and `getFile` and its siblings did so when the file could not be opened (for example a file name longer than the file system allows). Each now reports the failure to the error callback.
- `getFile("fid", "results.csv")`, a bare file name, failed with `filesystem error: in create_directories: No such file or directory [""]`. The file is now saved in the current directory.

## [2.2.0] - 2026-11-11

### Breaking

- Several public classes changed layout, so code built against 2.1.4 must be rebuilt against 2.2.0; mixing them is undefined behaviour. `ZBValidateResponse` has new members in the middle (`activeInDays` and `activeFirstSeen` after `domainAgeDays`; `domainWebsiteExists`, `domainRegistrantCompanyName` and `message` before `error`). `ZBGetApiUsageResponse` has `subStatusLeadingPeriodRemoved` and `subStatusAliasAddress` before `startDate`. `ZBErrorResponse` gained `statusCode`. `ZeroBounce` gained private members (the bulk, list evaluator and Inbox Placement Tester base URLs). `BaseRequestHandler` gained three virtual functions (`doPut` and two `doDelete`), which changes its vtable.

#### Upgrading from 2.1.4

- Rebuild everything that includes the headers, including your own `BaseRequestHandler` subclasses.
- Behaviour changes to check: an invalid or drained key, `{"success": false}` on an HTTP 200, and an account-level `validateBatch` error now reach the error callback (see Fixed); an exception thrown by your own callback now propagates (see Changed); `ZBErrorResponse::toString()` ends with `, statusCode=N`.
- macOS builds of the bundled libcurl need OpenSSL (see Changed).

### Added

- `validate` and `validateBatch` take `ZBValidateOptions`: `timeout`, `activityData`, `verifyPlus`, and `usePost` (form-encoded `POST /validate`). `validateBatch` sends `timeout` as a JSON string; sent as a number, the live API rejects the whole batch with an account-level error.
- `ZBValidateResponse` reads `active_in_days` and `active_first_seen`.
- `ZBValidateSubStatus::AiAgentMailbox`, read from `ai_agent_mailbox`. 2.1.4 read that sub-status as `ZBValidateSubStatus::None`.
- `scoreEmail` (`GET /v2/scoring`) and `scoreEmailPost` (form-encoded `POST /v2/scoring`).
- Bulk email finder (`sendEmailFinderFile`, `emailFinderFileStatus`, `emailFinderGetFile`, `emailFinderDeleteFile`). The upload requires `domain_column` and one of `first_name_column` and `full_name_column`; neither being set is rejected before a request is sent.
- Bulk domain search (`sendDomainSearchFile`, `domainSearchFileStatus`, `domainSearchGetFile`, `domainSearchDeleteFile`). This upload and the bulk email finder one send `has_header_row` only when `hasHeaderRow` is set, `false` included.
- List evaluator (`sendListEvaluatorFile`, `getListEvaluatorResult`, `sendEvaluatedFile`). Its requests use their own base URL, `https://bulkapi.zerobounce.net/v2/listevaluator`, with the file id URL-encoded as one path segment; `sendEvaluatedFile` posts to the bulk base. `sendEvaluatedFile(fileId)` sends the list evaluator's `file_id` as a form field, because the endpoint rejects a file upload with "file_id: This field is required".
- Filters (`addFilter`, `listFilters`, `deleteFilter`). An HTTP 200 `{"Error": ...}`, which is how they answer an invalid key, goes to the error callback.
- S3 bulk (`validateS3File`, `scoreS3File`, `exportS3File`, `listS3files`, `emailFinderS3File`, `domainSearchS3File`). `ValidateS3FileOptions` and `ScoreS3FileOptions` carry `returnUrl` and a plain `bool hasHeaderRow`. `validateS3File` and `scoreS3File` send `remove_duplicates`, not the singular `remove_duplicate` that belongs to the bulk `/v2/sendfile` upload, where `SendFileOptions` still carries it. `exportS3File` takes an `std::optional<std::string>` bucket: leave it unset to use the export bucket from your ZeroBounce settings.
- SFTP/FTP bulk: `validateSFTPFile`, `scoreSFTPFile`, `emailFinderSFTPFile`, `domainSearchSFTPFile`, and `exportSFTPFile`, all taking `BulkFileJobOptions`. The server, path and protocol come from your ZeroBounce settings. `exportSFTPFile` takes an optional `export_path` rather than the `export_bucket` `exportS3File` sends.
- All eight S3 and SFTP/FTP jobs (validate, score, email finder and domain search, on S3 and on SFTP/FTP) build their form from the same fields and require `fileName` and `returnUrl`; either one unset or blank is rejected before a request is sent. The docs list them as optional, `validateSFTPFile`'s `file_name` included, but without a return URL the API answers `Provided return URL is not permitted: None`, and without a file name, "This field is required".
- `ZBValidateOptions` accepts `domainInfo`, `greylistProcessing`, and `callbackUrl`. `callbackUrl` needs `greylistProcessing = true` and is rejected without it, and when blank, before a request is sent.
- `ZBValidateResponse` carries `domainWebsiteExists`, `domainRegistrantCompanyName`, and `message`. `message` is read from the API's `Message` key, returned while greylist processing is still running.
- Inbox Placement Tester on `https://bulkapi.zerobounce.net/api/ipt`: `getInboxPlacementProviders`, `getInboxPlacementTestTypes`, `createInboxPlacementTest`, `getInboxPlacementTest`, `listInboxPlacementTests`, `deleteInboxPlacementTest`, `getInboxPlacementWebhook`, `setInboxPlacementWebhook`. It authenticates with an `Authorization: Bearer` header, so the key never reaches a query string that gets logged. `listInboxPlacementTests` calls `GET /tests`; `GET /test` answers `Method "GET" not allowed`.
- `setListEvaluatorBaseUrl`, `setInboxPlacementBaseUrl`, `setBulkApiRootUrl`, `setBulkApiBaseUrl`, and `setBulkApiScoringBaseUrl` override those base URLs. The bulk hosts were fixed to production and had no setter, so no bulk endpoint could be pointed at a test host.
- `BaseRequestHandler` gains `Put` and `Delete`, used by the Inbox Placement Tester and the Warmup API, plus a `Delete` overload that carries a body, which the Warmup `DELETE /custom-engagement` needs. All have default implementations, so existing subclasses keep compiling.
- `ZeroBounceWarmup`, a client for the Warmup API. It is a separate product with its own host (`https://warmup-api.zerobounce.net`), its own team-scoped key sent in an `API-Key` header, and all 26 documented endpoints, including the seed-list and statistics CSV downloads. `isReady()` and `getInitializeError()` report whether `initialize` accepted the key and base URL, and why it did not, instead of leaving the client quietly unusable until the first call. `initialize` rejects a key with surrounding whitespace or control characters, because the transport would otherwise quote it unredacted in its own error, and a base URL that is not `https://`. `testSmtpConfiguration` masks the username and password out of every error it raises.
- `ZBErrorResponse::statusCode` carries the HTTP status of a failed response, including an HTTP 200 whose body reports an error (such as the `validateBatch` account-level error or a `getFile` JSON error body), so callers can tell a Cloudflare 403 from an API 400/404 and a Warmup 429. It is 0 when the SDK raised the error itself, for example for an argument rejected before a request was sent.
- An opt-in live API suite, built only with `-DZEROBOUNCE_BUILD_LIVE_TESTS=ON`. A normal build and CI never compile it, and `ctest` never runs it; it needs keys in the environment, and only spends credits or changes the account behind `ZEROBOUNCE_LIVE_SPEND=1` and `ZEROBOUNCE_LIVE_CHANGES=1`. Its `getCredits` check requires a positive balance, because the API answers `-1` for an invalid key.

### Changed

- libcpr goes from 1.10.2 to 1.11.2, and the libcurl it builds by default from 7.80.0 to 8.23.0. cpr 1.11.2 on its own fetches curl 8.10.1; `CMakeLists.txt` declares curl 8.23.0 (the GitHub release tarball, pinned by SHA-256) before cpr, so cpr builds that release with its usual settings. With `-DCPR_USE_SYSTEM_CURL=ON` the system libcurl is used instead, and cpr 1.11.2 requires 7.64.0 or newer.
- On macOS, the bundled libcurl uses OpenSSL instead of Secure Transport, which curl removed in 8.15, so a macOS build needs OpenSSL installed (for example Homebrew's `openssl@3`; pass `-DOPENSSL_ROOT_DIR=...` if CMake does not find it), or `-DCPR_USE_SYSTEM_CURL=ON` to use the system libcurl. Certificates are still checked against the macOS trust store (keychain), through curl's Apple SecTrust support, so a missing or stale OpenSSL CA bundle does not break HTTPS. Linux still uses OpenSSL and Windows still uses Schannel.
- The bundled libcurl is built without libpsl, as 2.1.4's curl 7.80.0 was. curl 8.23.0 otherwise fails to configure when libpsl is not installed; `-DCURL_USE_LIBPSL=ON` builds it with libpsl.
- The bundled libcurl is built for HTTP and HTTPS only, and without the optional libraries it would otherwise link when they are installed on the build machine: libssh2, libssh, GSSAPI, nghttp2 (HTTP/2), libidn2, brotli and zstd. The build no longer depends on what else is installed. curl 8.23.0 dropped the `HTTP_ONLY` option that cpr 1.11.2 sets, so `CMakeLists.txt` turns the other protocols off itself. 2.1.4's curl 7.80.0 honoured `HTTP_ONLY`, but linked libssh2 and libidn2 when they were found; it had nghttp2, brotli and zstd off by default. Each setting is a cache variable, so `-D` overrides it.
- `CMakeLists.txt` no longer rewrites the fetched curl's `lib/parsedate.c`. The rewrite was a CodeQL workaround for the `sscanf` calls in curl 7.80.0, which curl 8.10.1 and 8.23.0 no longer contain there.
- The `ZeroBounce` target passes C++17 on to the targets that link it (`target_compile_features(ZeroBounce PUBLIC cxx_std_17)`), so a project set to C++14 compiles its code that includes the headers as C++17 instead of failing inside them. The library itself still builds as C++20.
- An exception thrown by your success or error callback now propagates out of the call that ran it. 2.1.4 caught a `std::exception` thrown by either callback and passed its `what()` text to the error callback, so a bug in your success callback looked like an API error and the error callback ran after the success callback, and an error callback that threw was called a second time. Only the SDK's own failures (transport, response parsing, writing a downloaded file) reach the error callback.
- `CMakeLists.txt` sets `CMAKE_POLICY_VERSION_MINIMUM` to 3.5, so that CMake 4, which no longer configures a project declaring a minimum below 3.5, can configure the fetched nlohmann_json 3.11.2 (it declares 3.1), and sets policy CMP0026 to `NEW`. It also sets CMP0135 to `NEW` on CMake 3.24 and newer, so files extracted from a URL download get the time of extraction, and CMake no longer warns that the policy is unset. The minimum CMake version stays 3.20.
- `ZBErrorResponse::toString()` ends with `, statusCode=N`. Code that matches its exact output needs updating.
- API requests time out after 120 seconds and do not follow redirects.
- Query values, including the API key, email, and file id, are percent-encoded.

### Fixed

- An invalid or drained API key was a silent success. The API answers one with an HTTP 200 and no `success` field: `{"error": "Invalid API key or your account ran out of credits"}` from `validate` and `getActivityData`, `{"error": "Invalid API key"}` from `getApiUsage`, and `{"Message": ...}` from the email finder and domain search. Each was parsed into an empty result and passed to the success callback. Each now goes to the error callback with the API's own message. `message`, `Message` and `detail` decide failure only for the email finder and domain search, because successful responses use them too: a bulk submit answers `{"success": true, "message": "File Accepted"}`, a filter change answers with a bare `Message`, and a repeated `greylist_processing` validate answers `{"Message": "Greylist validation still in progress. Please try again later."}`, which is a result. `getCredits` still returns `-1` for an invalid key, as documented.
- The api-usage response now reads `sub_status_leading_period_removed` and `sub_status_alias_address`, two counters the API returns that were silently discarded.
- `{"success": false}` on an HTTP 200 reaches the error callback instead of the success one on the JSON calls, such as `fileStatus` and `deleteFile`. The API answers an unknown bulk file id that way. `getFile` and `scoringGetFile` already treated such a body as an error in 2.1.4.
- An error body reports one message: the first non-empty `error`, `message`, `error_message`, `Message`, `Error`, or `detail` field, with a leading `Error:` removed; `detail` is how the Inbox Placement Tester (Django REST framework) reports errors. 2.1.4 already reported the text of those fields, but added the value of every other string field (such as a `file_id`) after it, and included the whole raw JSON body whenever another field held a number or a boolean.
- A 403 HTML "Access denied" page (about 1.4 MB from the live API on `getFile`) is returned as a short error. An HTML page with status 200 is an error as well; `getFile` and `scoringGetFile` no longer save it as the results file.
- Empty and non-JSON bodies are a short error. The message is no longer the nlohmann `[json.exception.parse_error…]` text, and a repeated `Error:` / `[Error]:` prefix is removed.
- `validateBatch` reports an account-level failure (HTTP 200, error for address `all`, such as an invalid API key or no credits) through the error callback. An error for one address stays on the successful result.
- `sendFile` / `scoringSendFile` send `has_header_row`, `remove_duplicate`, and `allow_phase_2` as `true` or `false`.
- `findEmail` and the email finder and domain search calls (`findEmailByDomain`, `findEmailByCompanyName`, `searchDomainByDomain`, `searchDomainByCompanyName`) reject an empty API key before a request is sent.
- `ZBValidateResponse::operator==` compares every field. It previously stopped after `address` and `status`.
- `getFile`, `getFileWithOptions`, `scoringGetFile` and `scoringGetFileWithOptions` save the file when no success callback is passed. Without one, 2.1.4 handled a response with an HTTP status below 300 not at all: it saved no file and did not report a JSON error body, or a directory given as the download path, to the error callback.
- `ZBDomainFormat::toString()`, which `ZBDomainSearchResponse::toString()` also uses for each `otherDomainFormats` entry, starts with `ZBDomainFormat{` instead of `ZBDomainSearchResponse{`.
- `ZBFindEmailResponse::toString()` ends with `" }`. It streamed the multi-character constant `'" }'`, which printed as a number.

### Security

- Query parameters are percent-encoded, and redirects are disabled, so a value cannot rewrite the request URL or send the client to another host.
- Error callbacks no longer include the raw HTML access-denied page.

## [2.1.4] - 2026-08-14

### Changed

- No library changes: the release sets the version to 2.1.4 and moves the CI workflows to `actions/checkout` v7 and `github/codeql-action` 4.36.3.

## [2.1.3] - 2026-06-18

### Fixed

- `validateBatch` posts to `api.zerobounce.net/v2/validatebatch` instead of `bulkapi.zerobounce.net`.

## [2.1.2] - 2026-06-10

### Added

- `ZBValidateResponse.catchallDomain`, read from `catchall_domain`.

## [2.1.0] - 2026-04-05

### Added

- `getFileWithOptions` and `scoringGetFileWithOptions`, with `GetFileOptions.downloadType` (`phase_1`, `phase_2`, `combined`) and `activityData` (validation `getFile` only).
- `SendFileOptions.allowPhase2` / `allowPhase2IsSet`, sent as `allow_phase_2` on validation `sendFile` only.
- `ZBFileStatusResponse.filePhase2Status`.

### Fixed

- `getFile` calls the error callback when the body is a JSON error, including HTTP 200 with `"success": false`, instead of saving that body as the results file.

## [2.0.10] - 2026-03-03

### Changed

- License the project under MIT.

## [2.0.0] - 2026-03-03

### Added

- `findEmailByDomain`, `findEmailByCompanyName`, `searchDomainByDomain`, and `searchDomainByCompanyName`.
- `initialize(apiKey, ZBApiURL)` to select the API base URL.
- `ZBValidateSubStatus::AcceptAll`.

### Changed

- The library builds as C++20 and uses libcpr for HTTP.

### Deprecated

- `findEmail`. Use the email-finder or domain-search methods instead.

## [1.1.1] - 2023-09-18

### Added

- `ZBFindEmailResponse::toString`.

## [1.1.0] - 2023-09-12

### Added

- Initial release: `getCredits`, `getApiUsage`, `validate`, `validateBatch`, bulk and AI-scoring file methods, `getActivityData`, and `findEmail`.

[Unreleased]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v3.0.1...HEAD
[3.0.1]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v3.0.0...v3.0.1
[3.0.0]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v2.2.0...v3.0.0
[2.2.0]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v2.1.4...v2.2.0
[2.1.4]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v2.1.3...v2.1.4
[2.1.3]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v2.1.2...v2.1.3
[2.1.2]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v2.1.0...v2.1.2
[2.1.0]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v2.0.10...v2.1.0
[2.0.10]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v2.0.0...v2.0.10
[2.0.0]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v1.1.1...v2.0.0
[1.1.1]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/releases/tag/v1.1.0
