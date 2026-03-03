cmake_minimum_required(VERSION 3.20)
project(ZeroBounce VERSION 2.0.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)

# Download and build libcpr
FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG 1.10.2
)
# Download and build nlohmann_json
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
# Download and build googletest
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.13.0
)

FetchContent_MakeAvailable(cpr nlohmann_json googletest)

# Fix CodeQL cpp/incorrectly-checked-scanf: curl 7.80.0 (cpr dep) checks sscanf
# return only against truthiness; must check for expected conversions (1).
set(CURL_PARSEDATE "${CMAKE_BINARY_DIR}/_deps/curl-src/lib/parsedate.c")
if(EXISTS "${CURL_PARSEDATE}")
  file(READ "${CURL_PARSEDATE}" PARSEDATE_C)
  string(REPLACE "if(sscanf(date, " "if(1 == sscanf(date, " PARSEDATE_C "${PARSEDATE_C}")
  file(WRITE "${CURL_PARSEDATE}" "${PARSEDATE_C}")
  message(STATUS "Patched curl parsedate.c: sscanf return value check")
endif()

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_library(${PROJECT_NAME} SHARED ${SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC include)

target_link_libraries(${PROJECT_NAME} PUBLIC cpr::cpr nlohmann_json::nlohmann_json)

enable_testing()
add_executable(ZeroBounceTests test/ZeroBounceTest.cpp)
target_link_libraries(ZeroBounceTests PRIVATE ${PROJECT_NAME} gtest gtest_main)
include(GoogleTest)
gtest_add_tests(TARGET ZeroBounceTests)
