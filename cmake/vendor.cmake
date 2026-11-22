# Vendored third-party dependencies (see vendor/README.md for versions).
# Provides: ctre::ctre, simdjson::simdjson, libsodium::libsodium.
# Include after CMAKE_CXX_STANDARD is set. Requires a C toolchain for libsodium.

set(REVET_VENDOR_DIR "${CMAKE_CURRENT_LIST_DIR}/../vendor")

# CTRE 3.11.0 — header-only.
add_library(ctre_vendor INTERFACE)
target_include_directories(ctre_vendor SYSTEM INTERFACE "${REVET_VENDOR_DIR}/ctre/include")
add_library(ctre::ctre ALIAS ctre_vendor)

# App builds reuse the libs built once with the framework (<framework>/prebuilt, written by
# install.sh). Cross builds and the framework's own build compile them from source.
if(NOT DEFINED REVET_PREBUILT_DIR)
    get_filename_component(REVET_PREBUILT_DIR "${CMAKE_CURRENT_LIST_DIR}/../prebuilt" ABSOLUTE)
endif()
set(REVET_USE_PREBUILT OFF)
if(NOT REVET_BUILDING_FRAMEWORK AND NOT CMAKE_CROSSCOMPILING
   AND EXISTS "${REVET_PREBUILT_DIR}/lib/libsodium.a"
   AND EXISTS "${REVET_PREBUILT_DIR}/lib/libsimdjson_vendor.a")
    set(REVET_USE_PREBUILT ON)
endif()

if(REVET_USE_PREBUILT)
    add_library(simdjson_vendor INTERFACE)
    target_include_directories(simdjson_vendor SYSTEM INTERFACE "${REVET_VENDOR_DIR}/simdjson")
    target_link_libraries(simdjson_vendor INTERFACE "${REVET_PREBUILT_DIR}/lib/libsimdjson_vendor.a")
    add_library(simdjson::simdjson ALIAS simdjson_vendor)

    add_library(libsodium_vendor INTERFACE)
    target_include_directories(libsodium_vendor SYSTEM INTERFACE "${REVET_PREBUILT_DIR}/include")
    target_link_libraries(libsodium_vendor INTERFACE "${REVET_PREBUILT_DIR}/lib/libsodium.a")
    add_library(libsodium::libsodium ALIAS libsodium_vendor)
else()
    enable_language(C)  # libsodium's configure needs a C compiler
    # simdjson 5.1.1 — amalgamated single source.
    add_library(simdjson_vendor STATIC "${REVET_VENDOR_DIR}/simdjson/simdjson.cpp")
    target_include_directories(simdjson_vendor SYSTEM PUBLIC "${REVET_VENDOR_DIR}/simdjson")
    target_compile_features(simdjson_vendor PUBLIC cxx_std_17)
    add_library(simdjson::simdjson ALIAS simdjson_vendor)

    # libsodium 1.0.22 — official release tarball, built with its configure script.
    set(REVET_SODIUM_PREFIX "${CMAKE_BINARY_DIR}/vendor/libsodium")
    # libsodium's configure does not know the macOS SDK; pass it explicitly.
    set(REVET_SODIUM_FLAGS "")
    if(APPLE)
        execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE REVET_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
        set(REVET_SODIUM_FLAGS "-isysroot ${REVET_SDK_PATH}")
    endif()
    include(ExternalProject)
    ExternalProject_Add(libsodium_build
        URL "${REVET_VENDOR_DIR}/libsodium/libsodium-1.0.22.tar.gz"
        URL_HASH SHA256=adbdd8f16149e81ac6078a03aca6fc03b592b89ef7b5ed83841c086191be3349
        PREFIX "${REVET_SODIUM_PREFIX}"
        BUILD_IN_SOURCE TRUE
        CONFIGURE_COMMAND ./configure --prefix=${REVET_SODIUM_PREFIX} --disable-shared --enable-static CC=${CMAKE_C_COMPILER} "CFLAGS=${REVET_SODIUM_FLAGS}" "LDFLAGS=${REVET_SODIUM_FLAGS}"
        BUILD_COMMAND make
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS "${REVET_SODIUM_PREFIX}/lib/libsodium.a"
    )
    add_library(libsodium_vendor INTERFACE)
    add_dependencies(libsodium_vendor libsodium_build)
    target_include_directories(libsodium_vendor SYSTEM INTERFACE "${REVET_SODIUM_PREFIX}/include")
    target_link_libraries(libsodium_vendor INTERFACE "${REVET_SODIUM_PREFIX}/lib/libsodium.a")
    add_library(libsodium::libsodium ALIAS libsodium_vendor)
endif()

# FTXUI 7.0.3 — terminal UI (used by the revet CLI). examples/ and doc/ are not vendored.
if(REVET_BUILDING_FRAMEWORK)
    set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${REVET_VENDOR_DIR}/ftxui" "${CMAKE_BINARY_DIR}/vendor/ftxui" EXCLUDE_FROM_ALL)
endif()
