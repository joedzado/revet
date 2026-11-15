cmake_minimum_required(VERSION 3.20)
project({{name}} C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Runtime Data Directory ────────────────────────────────────────────────────
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/build/data)

# ── Framework location ────────────────────────────────────────────────────────
set(FRAMEWORK_DIR "{{framework_dir}}")
set(PROJ_DIR "${CMAKE_SOURCE_DIR}")

# ── Framework core library ────────────────────────────────────────────────────
add_library(revet_core INTERFACE)
target_include_directories(revet_core INTERFACE ${FRAMEWORK_DIR}/include)
target_compile_options(revet_core INTERFACE -Wall -Wextra -Wpedantic)

# Opt-in strict mode: treat warnings as errors. Off by default so a stray
# warning in user code doesn't break the build for newcomers. Enable with
# `cmake -DREVET_STRICT=ON ...` or in CI.
option(REVET_STRICT "Treat warnings as errors" OFF)
if(REVET_STRICT)
    target_compile_options(revet_core INTERFACE -Werror)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(revet_core INTERFACE -fno-omit-frame-pointer)
endif()

option(REVET_ASAN "Enable AddressSanitizer + UBSanitizer" OFF)
if(REVET_ASAN)
    target_compile_options(revet_core INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(revet_core INTERFACE -fsanitize=address,undefined)
endif()

# ── Database Configuration ────────────────────────────────────────────────────
# Parse revet.toml to detect database usage
set(REVET_DB_BACKENDS "")
if(EXISTS ${CMAKE_SOURCE_DIR}/revet.toml)
    file(READ ${CMAKE_SOURCE_DIR}/revet.toml TOML_CONTENT)
    if(TOML_CONTENT MATCHES "type[ \t]*=[ \t]*\"sqlite\"")
        list(APPEND REVET_DB_BACKENDS "sqlite")
    endif()
    if(TOML_CONTENT MATCHES "type[ \t]*=[ \t]*\"postgres\"")
        list(APPEND REVET_DB_BACKENDS "postgres")
    endif()
endif()

# Configure compile definitions and libraries
if("sqlite" IN_LIST REVET_DB_BACKENDS)
    target_compile_definitions(revet_core INTERFACE REVET_DB_SQLITE)
    find_library(SQLITE3_LIB sqlite3)
    if(SQLITE3_LIB)
        target_link_libraries(revet_core INTERFACE ${SQLITE3_LIB})
    else()
        message(FATAL_ERROR "SQLite3 library not found. Install with: brew install sqlite3 (macOS) or apt-get install libsqlite3-dev (Linux)")
    endif()
endif()

if("postgres" IN_LIST REVET_DB_BACKENDS)
    target_compile_definitions(revet_core INTERFACE REVET_DB_POSTGRES)
    # Homebrew's libpq is keg-only, so it is not on the default search paths.
    find_library(POSTGRES_LIB pq HINTS /opt/homebrew/opt/libpq/lib /usr/local/opt/libpq/lib)
    find_path(POSTGRES_INCLUDE libpq-fe.h
        HINTS /opt/homebrew/opt/libpq/include /usr/local/opt/libpq/include
        PATH_SUFFIXES postgresql)
    if(POSTGRES_LIB AND POSTGRES_INCLUDE)
        target_link_libraries(revet_core INTERFACE ${POSTGRES_LIB})
        target_include_directories(revet_core INTERFACE ${POSTGRES_INCLUDE})
    else()
        message(FATAL_ERROR "PostgreSQL client library not found")
    endif()
endif()

# ── External Dependencies (generated from revet.toml [build.dependencies]) ──
# Note: Dependency linking happens after include(dependencies.cmake) in the site block below

# ── Deps: vendored (libsodium, simdjson, ctre) + system/Conan (curl) ─────────
include(${FRAMEWORK_DIR}/cmake/vendor.cmake)
# Conan installs Release packages; map Debug cmake builds to use them too
set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release)
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/build/conan")
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)  # websocket permessage-deflate, rails_encryption
target_link_libraries(revet_core INTERFACE
    libsodium::libsodium CURL::libcurl simdjson::simdjson ctre::ctre ZLIB::ZLIB)

# ── Codegen & Build ──────────────────────────────────────────────────────────
if(EXISTS ${CMAKE_SOURCE_DIR}/app)
    # Codegen output is target-independent; cross builds (build/<target>) share it.
    set(GEN_DIR ${CMAKE_SOURCE_DIR}/build/generated)
    file(MAKE_DIRECTORY ${GEN_DIR})

    # Codegen tool (C++, from framework) — built once per project, run per generator.
    file(GLOB CODEGEN_SOURCES CONFIGURE_DEPENDS ${FRAMEWORK_DIR}/cmake/codegen/*.cpp)
    add_executable(revet_codegen_tool ${CODEGEN_SOURCES})
    target_compile_features(revet_codegen_tool PRIVATE cxx_std_23)
    set_target_properties(revet_codegen_tool PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools)

    # Codegen helpers (from framework)
    function(add_codegen NAME GENERATOR OUTPUTS)
        file(GLOB_RECURSE DEPS CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/app/**"
            "${CMAKE_SOURCE_DIR}/revet.toml"
        )
        add_custom_command(
            OUTPUT ${OUTPUTS}
            COMMAND revet_codegen_tool ${GENERATOR} ${CMAKE_SOURCE_DIR} ${FRAMEWORK_DIR}
            DEPENDS revet_codegen_tool ${DEPS}
            COMMENT "Running codegen: ${NAME}"
            VERBATIM
        )
    endfunction()

    add_codegen(JsonModels gen_json_models "${GEN_DIR}/json_models.hpp")
    add_codegen(States    gen_states    "${GEN_DIR}/states.hpp")
    add_codegen(Forms     gen_forms     "${GEN_DIR}/forms.hpp")
    add_codegen(Components gen_components "${GEN_DIR}/components.hpp")
    add_codegen(Routes    gen_routes    "${GEN_DIR}/routes.cpp;${GEN_DIR}/routes.hpp;${GEN_DIR}/scripts.hpp")
    add_codegen(Resources gen_resources "${GEN_DIR}/resources.hpp;${GEN_DIR}/db_backends.cmake")
    add_codegen(Queries   gen_queries   "${GEN_DIR}/queries.hpp")
    add_codegen(API       gen_api       "${GEN_DIR}/apis.hpp")
    add_codegen(Styles    gen_styles    "${GEN_DIR}/styles.hpp")
    add_codegen(Toggles   gen_toggles   "${GEN_DIR}/toggles.hpp")
    add_codegen(Uploads   gen_uploads   "${GEN_DIR}/uploads.hpp")
    add_codegen(Server    gen_server    "${GEN_DIR}/server.hpp")
    add_codegen(Dependencies gen_dependencies "${GEN_DIR}/dependencies.cmake")

    # I18n — always runs; produces empty namespace if no locale/strings.csv
    add_codegen(I18n gen_i18n "${GEN_DIR}/i18n.hpp")

    set(GENERATED_SOURCES
        ${GEN_DIR}/json_models.hpp
        ${GEN_DIR}/states.hpp
        ${GEN_DIR}/forms.hpp
        ${GEN_DIR}/components.hpp
        ${GEN_DIR}/routes.cpp
        ${GEN_DIR}/routes.hpp
        ${GEN_DIR}/resources.hpp
        ${GEN_DIR}/queries.hpp
        ${GEN_DIR}/apis.hpp
        ${GEN_DIR}/styles.hpp
        ${GEN_DIR}/toggles.hpp
        ${GEN_DIR}/uploads.hpp
        ${GEN_DIR}/server.hpp
        ${GEN_DIR}/dependencies.cmake
        ${GEN_DIR}/i18n.hpp
    )

    # dependencies.cmake is included at configure time, so the codegen tool must exist
    # before the build runs. Built here only when missing; the build-time target rebuilds it.
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tools)
    set(REVET_CODEGEN_BIN ${CMAKE_BINARY_DIR}/tools/revet_codegen_tool)
    if(NOT EXISTS ${REVET_CODEGEN_BIN})
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} -std=c++23 -O1 ${CODEGEN_SOURCES} -o ${REVET_CODEGEN_BIN}
                        RESULT_VARIABLE _codegen_build_rc)
        if(NOT _codegen_build_rc EQUAL 0)
            message(FATAL_ERROR "revet_codegen_tool failed to build (${_codegen_build_rc})")
        endif()
    endif()
    # Generate at configure time: Conan files from `revet build` must already exist for find_package.
    execute_process(COMMAND ${REVET_CODEGEN_BIN} gen_dependencies ${CMAKE_SOURCE_DIR} ${FRAMEWORK_DIR}
                    RESULT_VARIABLE _gen_deps_rc)
    if(NOT _gen_deps_rc EQUAL 0)
        message(FATAL_ERROR "gen_dependencies failed (${_gen_deps_rc})")
    endif()
    include(${GEN_DIR}/dependencies.cmake)

    # Every .cpp in src/ is compiled, plus components and layouts. src/main.cpp is optional:
    # without it the framework's default entry point is used.
    file(GLOB component_sources
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/app/components/*.cpp"
        "${CMAKE_SOURCE_DIR}/app/layouts/*.cpp"
    )
    if(EXISTS ${CMAKE_SOURCE_DIR}/src/main.cpp)
        set(app_main "")
    else()
        set(app_main ${FRAMEWORK_DIR}/src/app/main.cpp)
    endif()

    file(GLOB site_init_sources
        "${CMAKE_SOURCE_DIR}/app/routes/site_init.cpp"
    )

    # Framework implementation sources (non-header .cpp files shipped with framework)
    file(GLOB framework_impl_sources "${FRAMEWORK_DIR}/src/*.cpp")
    list(FILTER framework_impl_sources EXCLUDE REGEX "/main\\.cpp$")

    add_executable(revet
        ${app_main}
        ${site_init_sources}
        ${GEN_DIR}/routes.cpp
        ${component_sources}
        ${GENERATED_SOURCES}
        ${framework_impl_sources}
    )
    # revet.toml `binary` sets the file name. The cmake target stays `revet`.
    set_target_properties(revet PROPERTIES OUTPUT_NAME "{{binary}}")
    target_include_directories(revet PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/app/routes ${GEN_DIR})
    target_link_libraries(revet PRIVATE revet_core)
    target_compile_options(revet PRIVATE -Wno-unknown-attributes)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        if(NOT CMAKE_CROSSCOMPILING)
            target_compile_options(revet PRIVATE -march=native)
        endif()
        target_compile_options(revet PRIVATE -flto)
        target_link_options(revet PRIVATE -flto)
    endif()

    find_package(Threads REQUIRED)
    target_link_libraries(revet PRIVATE Threads::Threads)

    if("sqlite" IN_LIST REVET_DB_BACKENDS)
        target_compile_definitions(revet PRIVATE REVET_DB_SQLITE)
        target_link_libraries(revet PRIVATE sqlite3)
    endif()
endif()

# ── Tests (optional) ──────────────────────────────────────────────────────────
find_package(Catch2 3 QUIET)
if(Catch2_FOUND)
    include(CTest)
    file(GLOB test_sources "${CMAKE_SOURCE_DIR}/tests/*.cpp")
    foreach(test_src ${test_sources})
        get_filename_component(test_name ${test_src} NAME_WE)
        add_executable(${test_name} ${test_src})
        target_include_directories(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/src ${GEN_DIR})
        target_link_libraries(${test_name} PRIVATE revet_core Catch2::Catch2WithMain)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
