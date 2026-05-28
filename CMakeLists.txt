cmake_minimum_required(VERSION 3.20)
project(revet CXX)

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

# ── Conan-provided dependencies ──────────────────────────────────────────────
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/build/conan")
find_package(libsodium REQUIRED)
find_package(CURL REQUIRED)
find_package(simdjson REQUIRED)
find_package(ctre REQUIRED)

# ── Target: revet_core (Framework library) ──────────────────────────────────
add_library(revet_core src/http_client.cpp src/json.cpp)
target_include_directories(revet_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(revet_core PUBLIC libsodium::libsodium CURL::libcurl simdjson::simdjson ctre::ctre)
target_compile_options(revet_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

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
find_package(Catch2 3 REQUIRED)
if(Catch2_FOUND)
    include(CTest)
    file(GLOB test_sources "${CMAKE_SOURCE_DIR}/tests/*.cpp")
    foreach(test_src ${test_sources})
        get_filename_component(test_name ${test_src} NAME_WE)

        add_executable(${test_name} ${test_src})
        target_link_libraries(${test_name} PRIVATE revet_core Catch2::Catch2WithMain)
        if(${test_name} MATCHES "sqlite")
            target_compile_definitions(${test_name} PRIVATE REVET_DB_SQLITE)
            target_link_libraries(${test_name} PRIVATE sqlite3)
        endif()
        add_test(NAME ${test_name} COMMAND ${test_name})
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
