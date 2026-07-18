cmake_minimum_required(VERSION 3.20)
project(options_pricing_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

# --- nlohmann/json (header-only JSON parser) --------------------------------
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

# --- clickhouse-cpp (native TCP client, block/batch inserts) ----------------
FetchContent_Declare(
  clickhouse_cpp
  GIT_REPOSITORY https://github.com/ClickHouse/clickhouse-cpp.git
  GIT_TAG        v2.5.1
  GIT_SHALLOW    TRUE
)

# --- Catch2 (unit tests) -----------------------------------------------------
FetchContent_Declare(
  catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.6.0
  GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(nlohmann_json clickhouse_cpp catch2)

find_package(Threads REQUIRED)

add_executable(ope
  src/main.cpp
  src/time_util.cpp
  src/ivcurve_parser.cpp
  src/inserter.cpp
)

target_include_directories(ope
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    # clickhouse-cpp public headers + its bundled contrib (abseil) headers
    ${clickhouse_cpp_SOURCE_DIR}
    ${clickhouse_cpp_SOURCE_DIR}/contrib/absl
)

target_link_libraries(ope
  PRIVATE
    clickhouse-cpp-lib
    nlohmann_json::nlohmann_json
    Threads::Threads
)

# --- Unit tests --------------------------------------------------------------
# Links the parser/time-util sources directly (not the `ope` binary) so tests
# don't need a ClickHouse connection: parsing is pure and DB-agnostic per
# parser_specification.md §21 Non-Goals.
enable_testing()

add_executable(ope_tests
  src/time_util.cpp
  src/ivcurve_parser.cpp
  tests/time_util_test.cpp
  tests/ivcurve_parser_test.cpp
)

target_include_directories(ope_tests
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ope_tests
  PRIVATE
    nlohmann_json::nlohmann_json
    Catch2::Catch2WithMain
)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(ope_tests)
