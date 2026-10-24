# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `validate` and `validateBatch` take `ZBValidateOptions`: `timeout`, `activityData`, `verifyPlus`, and `usePost` (form-encoded `POST /validate`).
- `ZBValidateResponse` reads `active_in_days` and `active_first_seen`.
- `scoreEmail` (`GET /v2/scoring`) and `scoreEmailPost` (form-encoded `POST /v2/scoring`).
- Bulk email finder (`sendEmailFinderFile`, `emailFinderFileStatus`, `emailFinderGetFile`, `emailFinderDeleteFile`).
- Bulk domain search (`sendDomainSearchFile`, `domainSearchFileStatus`, `domainSearchGetFile`, `domainSearchDeleteFile`).
- List evaluator (`sendListEvaluatorFile`, `getListEvaluatorResult`, `sendEvaluatedFile`).
- Filters (`addFilter`, `listFilters`, `deleteFilter`).
- S3 bulk (`validateS3File`, `scoreS3File`, `exportS3File`, `listS3files`, `emailFinderS3File`, `domainSearchS3File`).
- SFTP/FTP bulk: `validateSFTPFile`, `scoreSFTPFile`, `emailFinderSFTPFile`, `domainSearchSFTPFile`, and `exportSFTPFile`, all taking `BulkFileJobOptions`. The server, path and protocol come from your ZeroBounce settings. `validateSFTPFile` is the one job whose `fileName` may be left unset; `exportSFTPFile` takes `export_path` rather than the `export_bucket` `exportS3File` sends.
- `ZBValidateOptions` accepts `domainInfo`, `greylistProcessing`, and `callbackUrl`. `callbackUrl` needs `greylistProcessing = true` and is rejected without it, and when blank, before a request is sent.
- `ZBValidateResponse` carries `domainWebsiteExists`, `domainRegistrantCompanyName`, and `message`. `message` is read from the API's `Message` key, returned while greylist processing is still running.
- Inbox Placement Tester on `https://bulkapi.zerobounce.net/api/ipt`: `getInboxPlacementProviders`, `getInboxPlacementTestTypes`, `createInboxPlacementTest`, `getInboxPlacementTest`, `listInboxPlacementTests`, `deleteInboxPlacementTest`, `getInboxPlacementWebhook`, `setInboxPlacementWebhook`.
- `setListEvaluatorBaseUrl` and `setInboxPlacementBaseUrl` override those base URLs.
- `BaseRequestHandler` gains `Put` and `Delete`, used by the Inbox Placement Tester and the Warmup API, plus a `Delete` overload that carries a body, which the Warmup `DELETE /custom-engagement` needs. All have default implementations, so existing subclasses keep compiling.
- `ZeroBounceWarmup`, a client for the Warmup API. It is a separate product with its own host (`https://warmup-api.zerobounce.net`), its own team-scoped key sent in an `API-Key` header, and all 26 documented endpoints, including the seed-list and statistics CSV downloads.
- `ZBErrorResponse::statusCode` carries the HTTP status of a failed response, so callers can tell a Cloudflare 403 from an API 400/404 and a Warmup 429.
- An opt-in live API suite, built only with `-DZEROBOUNCE_BUILD_LIVE_TESTS=ON`. A normal build and CI never compile it, and `ctest` never runs it; it needs keys in the environment, and only spends credits or changes the account behind `ZEROBOUNCE_LIVE_SPEND=1` and `ZEROBOUNCE_LIVE_CHANGES=1`.

### Changed

- List evaluator requests use their own base URL (`https://bulkapi.zerobounce.net/v2/listevaluator`) instead of building the path from the bulk base, and the file id is URL-encoded as one path segment. `sendEvaluatedFile` stays on the bulk base.

- API requests time out after 120 seconds and do not follow redirects.
- `validateBatch` sends `timeout` as a JSON string. Sent as a number, the live API rejects the whole batch with an account-level error.
- The Inbox Placement Tester authenticates with an `Authorization: Bearer` header instead of an `api_key` parameter, so the key no longer reaches a query string that gets logged, and `listInboxPlacementTests` calls `GET /tests`. `GET /test` answers `Method "GET" not allowed`.
- `sendEvaluatedFile(fileId)` posts the list evaluator's `file_id` as a form field. The endpoint rejects a file upload with "file_id: This field is required", so it no longer takes a file path.
- The S3 and SFTP/FTP jobs require `file_name` and `return_url`, `validateSFTPFile` included, although the docs list `file_name` as optional for that one. The API answers "This field is required" otherwise.
- Query values, including the API key, email, and file id, are percent-encoded.

### Fixed

- The api-usage response now reads `sub_status_leading_period_removed` and `sub_status_alias_address`, two counters the API returns that were silently discarded.

- `{"success": false}` on an HTTP 200 reaches the error callback instead of the success one. The API answers an unknown bulk file id that way.
- An error body whose JSON carries `error`, `message`, `error_message`, or `detail` reports that text instead of the raw JSON. `detail` is what the Inbox Placement Tester (Django REST framework) returns.

- A 403 HTML "Access denied" page (about 1.4 MB from the live API on `getFile`) is returned as a short error. An HTML page with status 200 is an error as well, including `getFile`, `emailFinderGetFile`, and `domainSearchGetFile`, instead of a saved CSV or a JSON parse.
- Empty and non-JSON bodies are a short error. The message is no longer the nlohmann `[json.exception.parse_error…]` text, and a repeated `Error:` / `[Error]:` prefix is removed.
- `validateBatch` reports an account-level failure (HTTP 200, error for address `all`, such as an invalid API key or no credits) through the error callback. An error for one address stays on the successful result.
- `sendFile` / `scoringSendFile` send `has_header_row`, `remove_duplicate`, and `allow_phase_2` as `true` or `false`. Bulk email finder and domain search send `has_header_row` when it is set to false.
- `findEmail`, `searchDomain`, and the bulk finder, domain search, list evaluator, filter, and S3 calls reject an empty API key before a request is sent.
- `ZBValidateResponse::operator==` compares every field. It previously stopped after `address` and `status`.

### Security

- Query parameters are percent-encoded, and redirects are disabled, so a value cannot rewrite the request URL or send the client to another host.
- Error callbacks no longer include the raw HTML access-denied page.
- A Warmup API key with surrounding whitespace or control characters is rejected, because the transport would otherwise quote it unredacted in its own error, and a Warmup base URL must be `https://`.
- The Warmup SMTP test masks the username and password out of every error it raises.

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

[Unreleased]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v2.1.4...HEAD
[2.1.3]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v2.1.2...v2.1.3
[2.1.2]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v2.1.0...v2.1.2
[2.1.0]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v2.0.10...v2.1.0
[2.0.10]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v2.0.0...v2.0.10
[2.0.0]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v1.1.1...v2.0.0
[1.1.1]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/zerobounce/zerobounce-cpp-api-wrapper/releases/tag/v1.1.0
