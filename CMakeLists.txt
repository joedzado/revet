cmake_minimum_required(VERSION 3.20)
project(revet C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Build Type (if not already set) ───────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ── Speed Boosts ──────────────────────────────────────────────────────────────
find_program(CCACHE_EXE NAMES "ccache")
if(CCACHE_EXE)
    execute_process(COMMAND ${CCACHE_EXE} --version RESULT_VARIABLE CCACHE_RESULT OUTPUT_QUIET ERROR_QUIET)
    if(CCACHE_RESULT EQUAL 0)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_EXE}")
    endif()
endif()

# ── Dependencies: vendored (libsodium, simdjson, ctre) + system/Conan (curl) ──
set(REVET_BUILDING_FRAMEWORK ON)
include(${CMAKE_SOURCE_DIR}/cmake/vendor.cmake)

# ── Codegen tool: built once here; app builds reuse it from <framework>/prebuilt ──
file(GLOB CODEGEN_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/cmake/codegen/*.cpp)
add_executable(revet_codegen_tool ${CODEGEN_SOURCES})
target_link_libraries(revet_codegen_tool PRIVATE ctre::ctre)
set_target_properties(revet_codegen_tool PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)

# ── revet CLI (cli/): the `revet` command, installed to <framework>/prebuilt/bin ──
find_package(Threads REQUIRED)
file(GLOB REVET_CLI_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/cli/*.cpp)
list(FILTER REVET_CLI_SOURCES EXCLUDE REGEX "/main\\.cpp$")
add_library(revet_cli_core STATIC ${REVET_CLI_SOURCES})
target_include_directories(revet_cli_core PUBLIC ${CMAKE_SOURCE_DIR}/cli PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(revet_cli_core PUBLIC ctre::ctre ftxui::component ftxui::dom ftxui::screen Threads::Threads)
target_compile_options(revet_cli_core PRIVATE -Wall -Wextra)
target_compile_definitions(revet_cli_core PRIVATE REVET_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_executable(revet_cli ${CMAKE_SOURCE_DIR}/cli/main.cpp)
target_link_libraries(revet_cli PRIVATE revet_cli_core)
set_target_properties(revet_cli PROPERTIES OUTPUT_NAME revet RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)
install(TARGETS revet_cli RUNTIME DESTINATION bin)

# `cmake --install build --prefix <framework>/prebuilt` (install.sh does this) gives app
# builds the tool and the vendored static libs, so they compile none of them.
install(TARGETS revet_codegen_tool RUNTIME DESTINATION bin)
install(TARGETS simdjson_vendor ARCHIVE DESTINATION lib)
install(FILES ${REVET_SODIUM_PREFIX}/lib/libsodium.a DESTINATION lib)
install(DIRECTORY ${REVET_SODIUM_PREFIX}/include/ DESTINATION include)
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/build/conan")
find_package(OpenSSL REQUIRED CONFIG)
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)

# ── Target: revet_core (Framework library) ──────────────────────────────────
add_library(revet_core src/http_client.cpp src/json.cpp src/rails_encryption.cpp src/webauthn.cpp)
target_include_directories(revet_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(revet_core PUBLIC libsodium::libsodium CURL::libcurl simdjson::simdjson ctre::ctre ZLIB::ZLIB OpenSSL::Crypto)
target_compile_options(revet_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
install(TARGETS revet_core ARCHIVE DESTINATION lib)  # prebuilt/lib: apps link it instead of compiling src/*.cpp

find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--fix;--fix-errors;--checks=-*,modernize-*")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(revet_core INTERFACE -fno-omit-frame-pointer)
endif()

option(REVET_ASAN "Enable AddressSanitizer + UBSanitizer" OFF)
if(REVET_ASAN)
    target_compile_options(revet_core INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(revet_core INTERFACE -fsanitize=address,undefined)
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
include(CTest)
# Codegen: golden outputs for a fixture app plus bad-input cases (tests/codegen/run.cmake).
add_test(NAME codegen
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:revet_codegen_tool> -DFRAMEWORK=${CMAKE_SOURCE_DIR}
            -DWORK=${CMAKE_BINARY_DIR}/codegen_test -P ${CMAKE_SOURCE_DIR}/tests/codegen/run.cmake)

# static/optimistic.ts: bun compiles it (static/optimistic.js, which tests/optimistic_test.js
# loads); node runs the functional test. Skipped when either tool is missing.
find_program(REVET_BUN bun HINTS $ENV{HOME}/.bun/bin)
find_program(REVET_NODE node)
if(REVET_BUN)
    add_test(NAME optimistic_ts_build
        COMMAND ${REVET_BUN} build --minify static/optimistic.ts --outfile static/optimistic.js
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    if(REVET_NODE)
        add_test(NAME optimistic_js COMMAND ${REVET_NODE} tests/optimistic_test.js WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        set_tests_properties(optimistic_js PROPERTIES DEPENDS optimistic_ts_build)
    endif()
endif()

# revet CLI end to end: --version, new --empty, add/remove (no network, no build).
add_test(NAME cli
    COMMAND ${CMAKE_COMMAND} -DREVET=$<TARGET_FILE:revet_cli> -DFRAMEWORK=${CMAKE_SOURCE_DIR}
            -DWORK=${CMAKE_BINARY_DIR}/cli_test -P ${CMAKE_SOURCE_DIR}/tests/cli/run.cmake)
set_tests_properties(cli PROPERTIES TIMEOUT 120) # a step that wrongly starts `revet dev` must not hang the suite

find_package(Catch2 3 QUIET)
if(Catch2_FOUND)
    include(CTest)
    file(GLOB test_sources "${CMAKE_SOURCE_DIR}/tests/*.cpp")

    # libpq is optional: without it the Postgres e2e test is not built.
    # Homebrew's libpq is keg-only, so it is not on the default search paths.
    find_library(REVET_TEST_PQ_LIB pq HINTS /opt/homebrew/opt/libpq/lib /opt/homebrew/lib/postgresql@17 /usr/local/opt/libpq/lib)
    find_path(REVET_TEST_PQ_INCLUDE libpq-fe.h
        HINTS /opt/homebrew/opt/libpq/include /opt/homebrew/include/postgresql@17 /usr/local/opt/libpq/include
        PATH_SUFFIXES postgresql)

    foreach(test_src ${test_sources})
        get_filename_component(test_name ${test_src} NAME_WE)

        if((${test_name} STREQUAL "test_postgres_e2e" OR ${test_name} MATCHES "_postgres$") AND NOT (REVET_TEST_PQ_LIB AND REVET_TEST_PQ_INCLUDE))
            message(STATUS "libpq not found: skipping ${test_name}")
            continue()
        endif()

        add_executable(${test_name} ${test_src})
        if(${test_name} STREQUAL "test_postgres_e2e" OR ${test_name} MATCHES "_postgres$")
            target_compile_definitions(${test_name} PRIVATE REVET_DB_POSTGRES)
            target_include_directories(${test_name} PRIVATE ${REVET_TEST_PQ_INCLUDE})
            target_link_libraries(${test_name} PRIVATE ${REVET_TEST_PQ_LIB})
        endif()
        target_link_libraries(${test_name} PRIVATE revet_core Catch2::Catch2WithMain)
        if(${test_name} STREQUAL "test_cli")
            target_link_libraries(${test_name} PRIVATE revet_cli_core)
        endif()
        if(${test_name} MATCHES "sqlite")
            target_compile_definitions(${test_name} PRIVATE REVET_DB_SQLITE)
            target_link_libraries(${test_name} PRIVATE sqlite3)
        endif()
        add_test(NAME ${test_name} COMMAND ${test_name})
        if(${test_name} STREQUAL "test_postgres_e2e" OR ${test_name} MATCHES "_postgres$")
            # Catch2 exits 4 when every case skips (no REVET_TEST_PG_URL); CTest should report that as skipped.
            set_tests_properties(${test_name} PROPERTIES SKIP_RETURN_CODE 4)
        endif()
    endforeach()
endif()

# ── Benchmarks ────────────────────────────────────────────────────────────────
option(BUILD_BENCHMARKS "Build Catch2 BENCHMARK suites" OFF)
if(BUILD_BENCHMARKS AND Catch2_FOUND)
    file(GLOB bench_sources "${CMAKE_SOURCE_DIR}/benchmarks/*.cpp")
    foreach(bench_src ${bench_sources})
        get_filename_component(bench_name ${bench_src} NAME_WE)
        add_executable(${bench_name} ${bench_src})
        target_link_libraries(${bench_name} PRIVATE revet_core Catch2::Catch2WithMain)
        target_compile_options(${bench_name} PRIVATE -O3 -DNDEBUG)
    endforeach()
endif()
