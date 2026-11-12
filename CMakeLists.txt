cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0026)
  cmake_policy(SET CMP0026 NEW)
endif()
# URL downloads (curl, nlohmann_json) give their extracted files the time of
# extraction, so a changed URL rebuilds them. Without this, CMake 3.24 and newer
# warn that CMP0135 is unset for every URL-based FetchContent_Declare.
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(ZeroBounce VERSION 3.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)

# libcurl. cpr 1.11.2 fetches curl 8.10.1 itself; a FetchContent name keeps its
# first declaration, so declaring curl here, before cpr, makes cpr build this
# release instead, with cpr's TLS backend and CA bundle settings. It is not used
# with -DCPR_USE_SYSTEM_CURL=ON.
FetchContent_Declare(
    curl
    URL https://github.com/curl/curl/releases/download/curl-8_23_0/curl-8.23.0.tar.xz
    URL_HASH SHA256=8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5
)
# Since curl 8.13, CURL_USE_LIBPSL=ON (its default) fails the configure when
# libpsl is not installed. libpsl only checks cookie domains, and curl 8.10.1
# used it only when it happened to be found. -DCURL_USE_LIBPSL=ON requires it.
set(CURL_USE_LIBPSL OFF CACHE BOOL "Build the bundled libcurl with libpsl")
# HTTP and HTTPS only. curl 8.23 dropped the HTTP_ONLY option that cpr 1.11.2
# sets, so the other protocols are turned off here.
foreach(_zb_curl_protocol DICT FILE FTP GOPHER IMAP IPFS LDAP LDAPS MQTT POP3 RTSP SMTP
        TELNET TFTP WEBSOCKETS)
    set(CURL_DISABLE_${_zb_curl_protocol} ON CACHE BOOL "Disable ${_zb_curl_protocol} in the bundled libcurl")
endforeach()
# No optional libraries picked up from the build machine, so the bundled libcurl
# is the same everywhere: these are only used if found, and the SDK needs none.
foreach(_zb_curl_dependency CURL_USE_LIBSSH2 CURL_USE_LIBSSH CURL_USE_GSSAPI USE_NGHTTP2 USE_LIBIDN2)
    set(${_zb_curl_dependency} OFF CACHE BOOL "Set OFF for the bundled libcurl by ZeroBounce")
endforeach()
# zlib too: the SDK never asks for compressed responses. Left at curl's AUTO,
# the macOS build linked the system /usr/lib/libz while Linux, without zlib
# headers, built without it (and cpr fetched zlib-ng for it).
set(CURL_ZLIB OFF CACHE STRING "Build the bundled libcurl with zlib (AUTO, ON or OFF)")
set(CURL_BROTLI OFF CACHE STRING "Build the bundled libcurl with brotli (AUTO, ON or OFF)")
set(CURL_ZSTD OFF CACHE STRING "Build the bundled libcurl with zstd (AUTO, ON or OFF)")
# curl 8.15 removed Secure Transport, the TLS backend cpr 1.11.2 picks on macOS.
# The bundled curl uses OpenSSL there instead (Linux already does; Windows keeps
# Schannel).
if(APPLE AND NOT CPR_USE_SYSTEM_CURL AND NOT CPR_FORCE_OPENSSL_BACKEND
        AND NOT CPR_FORCE_MBEDTLS_BACKEND)
    set(CPR_FORCE_OPENSSL_BACKEND ON)
endif()
# Certificates are still checked against the macOS trust store (keychain),
# through Apple SecTrust, rather than only an OpenSSL CA bundle. A default rather
# than FORCE, so -DUSE_APPLE_SECTRUST=OFF still works.
if(APPLE AND NOT CPR_USE_SYSTEM_CURL)
    set(USE_APPLE_SECTRUST ON CACHE BOOL "Use Apple OS-native certificate verification")
endif()

# Download and build libcpr
FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG 1.11.2
)
# Download and build nlohmann_json. The hash is the SHA-256 the v3.11.2
# release notes publish for json.tar.xz.
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
    URL_HASH SHA256=8c4b26bf4b422252e13f332bc5e388ec0ab5c3443d24399acb675e68278d341f
)
# Download and build googletest
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.13.0
)

FetchContent_MakeAvailable(cpr nlohmann_json googletest)

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_library(${PROJECT_NAME} SHARED ${SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC include)
# The library builds as C++20 (CMAKE_CXX_STANDARD above); code that includes
# its headers needs C++17, which this passes on to every target linking it.
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

target_link_libraries(${PROJECT_NAME} PUBLIC cpr::cpr nlohmann_json::nlohmann_json)

enable_testing()
add_executable(ZeroBounceTests test/ZeroBounceTest.cpp test/OfflineApiTest.cpp test/OfflineWarmupTest.cpp
    test/TransportTest.cpp)
target_compile_definitions(ZeroBounceTests PRIVATE ZB_OFFLINE_CURRENT=1
    ZB_PUBLIC_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/include/ZeroBounce/ZeroBounce.h")
target_link_libraries(ZeroBounceTests PRIVATE ${PROJECT_NAME} gtest gtest_main)

# The live API suite is its own executable and is off by default, so a normal
# build and CI never compile it and ctest never runs it. Build it with
# -DZEROBOUNCE_BUILD_LIVE_TESTS=ON and run it by hand with keys in the
# environment.
option(ZEROBOUNCE_BUILD_LIVE_TESTS "Build the opt-in live API suite" OFF)
if(ZEROBOUNCE_BUILD_LIVE_TESTS)
    add_executable(ZeroBounceLiveApi live/live_api.cpp)
    target_link_libraries(ZeroBounceLiveApi PRIVATE ${PROJECT_NAME})
endif()
include(GoogleTest)
gtest_add_tests(TARGET ZeroBounceTests)
