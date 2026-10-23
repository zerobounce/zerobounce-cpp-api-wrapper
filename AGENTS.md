# AGENTS.md

Guidance for AI coding agents working in this repository.

## Project

`zerobounce-cpp-api-wrapper` is the ZeroBounce C++ SDK. Callers construct a `ZeroBounce`, call `initialize` with an API key, and receive results through success and error callbacks. HTTP goes through libcpr behind `BaseRequestHandler`. There is no package registry: releases are GitHub tags.

## Layout

- `include/ZeroBounce/`: public headers. `ZeroBounce.h` is the client.
- `src/ZeroBounce.cpp`: validation, bulk file, credits, activity, scoring.
- `src/Priority3.cpp`: bulk email finder, domain search, list evaluator, filters, the S3 / SFTP bulk jobs, and the Inbox Placement Tester. The six S3 / SFTP file jobs build their form through `bulkFileJobFields`; two shapes differ from the rest and are easy to get wrong — `validateSFTPFile`'s `fileName` may be unset, and `exportSFTPFile` sends `export_path`, not `export_bucket`.
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

## Conventions

- **Only endpoints documented at https://www.zerobounce.net/docs/.** Do not add a method for an endpoint that the public docs do not describe, even if it exists on the server. Check before concluding something is undocumented: the S3 bulk endpoints live under https://www.zerobounce.net/docs/api-integrations/amazon-s3-bulk-api, not the main API reference.
- **The list evaluator and the Inbox Placement Tester have their own base URLs** (`listEvaluatorBaseUrl`, `inboxPlacementBaseUrl`, settable via `setListEvaluatorBaseUrl` / `setInboxPlacementBaseUrl`). They are not built from the bulk base, and `sendEvaluatedFile` stays on it.
- **The Inbox Placement Tester takes the API key as the `api_key` query parameter and JSON bodies with camelCase fields.** Only fields the caller set are sent. Its error bodies use `detail`. It is the only caller of `BaseRequestHandler::Put` and `Delete`, which have default implementations so existing subclasses keep compiling.

- **One `ZeroBounce` per API key.** `getInstance()` is a process-wide singleton. Do not reuse it across users or requests. Construct a separate object per key. The offline concurrency test does this.
- **Column indexes are 1-based.** `SendFileOptions` name, gender, and IP columns default to `0`, and `0` means the field is left out. `ValidateS3FileOptions::emailAddressColumn` is `std::optional<int>`: when it is set, `0` is sent. `sendFile`'s email column is required and is sent as given, including `0`.
- **Optional parameters are omitted when unset.** `timeout`, `activityData`, `verifyPlus`, `allowPhase2`, `downloadType`, and `hasHeaderRow` (`std::optional<bool>`) are not sent until the caller sets them. Explicit `false` is sent as `false`.
- **Booleans on multipart must be the strings `true` and `false`.** `cpr::Part` has no bool constructor; a bool becomes `1` or `0` and the API will not treat that as a boolean. Pass `std::string("true")` or `std::string("false")`.
- **`allow_phase_2` is validation `sendFile` only**, and only when `allowPhase2IsSet` is true. Scoring `sendFile` ignores it. `activity_data` on get-file is validation only.
- **`validateBatch` uses `api.zerobounce.net`**, not `bulkapi.zerobounce.net`.
- **Failures use the error callback**, not exceptions. The message must be short: no raw HTML, no megabyte body, no `[json.exception…]` text, and no `Error: Error:` prefix. An account-level `validateBatch` error (`email_address` `all`) is an error callback. A per-address error stays on the success result.
- **Tests replace `requestHandler`.** It is `protected`. Subclass `ZeroBounce` in the test and assign a fake. Do not call the live API from the default test run.
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
