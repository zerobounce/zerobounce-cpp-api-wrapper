# AGENTS.md

Guidance for AI coding agents working in this repository.

## Project

`zerobounce-cpp-api-wrapper` is the ZeroBounce C++ SDK. Callers construct a `ZeroBounce`, call `initialize` with an API key, and receive results through success and error callbacks. HTTP goes through libcpr behind `BaseRequestHandler`. There is no package registry: releases are GitHub tags.

## Layout

- `include/ZeroBounce/`: public headers. `ZeroBounce.h` is the client.
- `src/ZeroBounce.cpp`: validation, bulk file, credits, activity, scoring.
- `src/Priority3.cpp`: bulk email finder, domain search, list evaluator, filters, the S3 / SFTP bulk jobs, and the Inbox Placement Tester. All six S3 / SFTP file jobs build their shared form fields through `bulkFileJobFields`; `validateS3File` and `scoreS3File` then append the extra fields only they take. Do not re-implement those shared fields per job: that duplication is how `return_url` came to be optional on two of them. `exportSFTPFile` sends `export_path`, not the `export_bucket` `exportS3File` sends, and both are optional.
- `src/HttpError.cpp`: turns HTTP failures, HTML pages, and empty or non-JSON bodies into a short `ZBErrorResponse`.
- `test/ZeroBounceTest.cpp`: existing GoogleTest cases with a scripted response.
- `test/OfflineApiTest.cpp`: offline suite. A fake `BaseRequestHandler` records every request and answers without the network.
- `CMakeLists.txt`: the only version field, `project(ZeroBounce VERSION x.y.z)`.

## Commands

```bash
cmake -S . -B build -G "Unix Makefiles"
cmake --build build          # this is the typecheck; there is no separate lint step
ctest --test-dir build --output-on-failure
```

CI (`.github/workflows/sdk_ci.yml`) configures, builds, and runs `ctest`. That build is the typecheck.

The live API suite is its own executable and off by default, so neither a
normal build nor CI compiles it:

```bash
cmake -S . -B build -DZEROBOUNCE_BUILD_LIVE_TESTS=ON
cmake --build build --target ZeroBounceLiveApi
export ZEROBOUNCE_API_KEY=... ZEROBOUNCE_WARMUP_API_KEY=...
./build/bin/ZeroBounceLiveApi                                   # free calls only
ZEROBOUNCE_LIVE_SPEND=1 ./build/bin/ZeroBounceLiveApi
ZEROBOUNCE_LIVE_CHANGES=1 ./build/bin/ZeroBounceLiveApi
```

Keys come from the environment only. Never write one into a file, a commit, a
log, a fixture or a message.

## Conventions

- **Only endpoints documented at https://www.zerobounce.net/docs/.** Do not add a method for an endpoint that the public docs do not describe, even if it exists on the server. Check before concluding something is undocumented: the S3 bulk endpoints live under https://www.zerobounce.net/docs/api-integrations/amazon-s3-bulk-api, not the main API reference.
- **The list evaluator and the Inbox Placement Tester have their own base URLs** (`listEvaluatorBaseUrl`, `inboxPlacementBaseUrl`, settable via `setListEvaluatorBaseUrl` / `setInboxPlacementBaseUrl`). They are not built from the bulk base, and `sendEvaluatedFile` stays on it. The three bulk hosts have setters too — `setBulkApiRootUrl`, `setBulkApiBaseUrl`, `setBulkApiScoringBaseUrl` — so every bulk endpoint can be pointed at a test host. Keep every base URL settable; a hardcoded `const` one makes its endpoints untestable.
- **Where the live API and the docs disagree, follow the API and say so in a comment at the call site.** The ones found so far: `validatebatch` takes `timeout` as a JSON *string*; the Inbox Placement Tester authenticates with `Authorization: Bearer`, not `api_key`; its listing is `GET /tests`, not `/test`; `send-evaluated-file` takes `file_id` as a form field, not a file upload; all six S3 and SFTP jobs require `return_url` and `file_name`, `validateS3File`, `scoreS3File` and `validateSFTPFile` included (`POST /validateS3file` without one answers 400 `Provided return URL is not permitted: None`); the Warmup `check-domain-dmarc` takes an email address, not a domain; and a failed SMTP login in `test-smtp-configuration` comes back as an HTTP 500.
- **The Inbox Placement Tester takes the API key as an `Authorization: Bearer` header and JSON bodies with camelCase fields.** Only fields the caller set are sent. Its error bodies use `detail`. It and the Warmup client are the only callers of `BaseRequestHandler::Put` and `Delete`, which have default implementations so existing subclasses keep compiling. The Warmup API's `DELETE /custom-engagement` carries a JSON body, which is why there is a `Delete` overload that takes one.
- **`{"success": false}` on an HTTP 200 is a failure.** The API answers an unknown bulk file id that way. `zbReadJsonResponse` turns it into an error callback.
- **`src/ZeroBounceWarmup.cpp` is a separate client for the Warmup API**, a different product with its own host and its own team-scoped key. It is not part of `ZeroBounce`, and it owns a `unique_ptr`, so like `ZeroBounce` it is not copyable.

- **One `ZeroBounce` per API key.** `getInstance()` is a process-wide singleton. Do not reuse it across users or requests. Construct a separate object per key. The offline concurrency test does this.
- **Column indexes are 1-based.** `SendFileOptions` name, gender, and IP columns default to `0`, and `0` means the field is left out. `ValidateS3FileOptions::emailAddressColumn` is `std::optional<int>`: when it is set, `0` is sent. `sendFile`'s email column is required and is sent as given, including `0`.
- **Optional parameters are omitted when unset.** `timeout`, `activityData`, `verifyPlus`, `allowPhase2`, `downloadType`, and `hasHeaderRow` (`std::optional<bool>`) are not sent until the caller sets them. Explicit `false` is sent as `false`. The S3 / SFTP jobs are the exception: their `hasHeaderRow` is a plain `bool` defaulting to `true` and is always sent, so all six agree on the wire.
- **Booleans on multipart must be the strings `true` and `false`.** `cpr::Part` has no bool constructor; a bool becomes `1` or `0` and the API will not treat that as a boolean. Pass `std::string("true")` or `std::string("false")`.
- **`allow_phase_2` is validation `sendFile` only**, and only when `allowPhase2IsSet` is true. Scoring `sendFile` ignores it. `activity_data` on get-file is validation only.
- **`validateBatch` uses `api.zerobounce.net`**, not `bulkapi.zerobounce.net`.
- **Failures use the error callback**, not exceptions. The message must be short: no raw HTML, no megabyte body, no `[json.exception…]` text, and no `Error: Error:` prefix. An account-level `validateBatch` error (`email_address` `all`) is an error callback. A per-address error stays on the success result.
- **Tests replace `requestHandler`.** It is `protected`. Subclass `ZeroBounce` in the test and assign a fake. Do not call the live API from the default test run; `live/live_api.cpp` is the separate, opt-in suite.
- **Errors carry the HTTP status.** `ZBErrorResponse::statusCode` is set on every failed response, so callers can tell a Cloudflare 403 from an API 400/404 and a Warmup 429. A failure that never reached the API leaves it 0.

## Releasing

**Run the live suite by hand before every release.** The offline suite proves the SDK sends what we think it sends; only the live suite proves the API accepts it. Run it with real keys, including `ZEROBOUNCE_LIVE_SPEND=1` and `ZEROBOUNCE_LIVE_CHANGES=1`, and do not release on a failure.
- Match the surrounding style. Do not reformat unrelated code.

## Changelog

`CHANGELOG.md` follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/), and the project adheres to [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).

- Add every user-visible change to `## [Unreleased]` in the same commit as the change.
- Group entries under these headings, in this order, omitting empty ones: `Added`, `Changed`, `Deprecated`, `Removed`, `Fixed`, `Security`.
- Write for SDK users: name the method, option, or behaviour, and say what changed. Leave out CI-only changes, internal refactors, and fixes to code that was never released.
- At 2.y.z, a breaking change bumps the major version, a new method or option bumps the minor version, and fixes alone bump the patch version.

## Releasing

Publishing and pushing tags need a maintainer's explicit approval. Do not bump the version, tag, or publish unless they asked.

1. Move `[Unreleased]` entries under `## [x.y.z] - YYYY-MM-DD`, leave an empty `[Unreleased]` section, and update the compare links at the bottom of `CHANGELOG.md`.
2. Set the same version in `project(ZeroBounce VERSION …)` in `CMakeLists.txt`. The publish workflow fails if it differs from the tag. That is the only version field. There is no lockfile and no runtime version constant.
3. `cmake --build build` and `ctest --test-dir build --output-on-failure`.
4. Tag `vx.y.z` on that commit and run the Publish workflow with that tag. It creates the GitHub release. There is no package registry.
