# Codegen golden test. Runs every generator on tests/codegen/fixture and compares the
# output with tests/codegen/expected; then checks that each bad input in errors/ fails
# with the expected message.
#
#   cmake -DTOOL=<revet_codegen_tool> -DFRAMEWORK=<repo root> -DWORK=<scratch dir> -P run.cmake
#   add -DUPDATE=1 to rewrite expected/ from the current output.
cmake_minimum_required(VERSION 3.20)
set(HERE ${CMAKE_CURRENT_LIST_DIR})
set(GENERATORS gen_json_models gen_states gen_forms gen_components gen_resources gen_queries gen_api
    gen_styles gen_toggles gen_uploads gen_dependencies gen_i18n gen_server gen_scripts gen_routes)
# htmx_asset.hpp is the vendored htmx verbatim and optimistic_asset.hpp depends on bun: not compared.
set(SKIP htmx_asset.hpp optimistic_asset.hpp)
set(failures "")

# ── golden outputs ──
set(PROJ ${WORK}/fixture)
file(REMOVE_RECURSE ${WORK})
file(COPY ${HERE}/fixture DESTINATION ${WORK})
file(COPY ${HERE}/fixture/conan DESTINATION ${PROJ}/build)
foreach(gen ${GENERATORS})
    execute_process(COMMAND ${TOOL} ${gen} ${PROJ} ${FRAMEWORK}
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        string(APPEND failures "${gen} exited ${rc}: ${err}\n")
    endif()
endforeach()

file(GLOB generated RELATIVE ${PROJ}/build/generated ${PROJ}/build/generated/*)
foreach(name ${generated})
    if(name IN_LIST SKIP)
        continue()
    endif()
    file(READ ${PROJ}/build/generated/${name} actual)
    string(REPLACE "${PROJ}" "<PROJ>" actual "${actual}")   # #line directives carry absolute paths
    if(UPDATE)
        file(WRITE ${HERE}/expected/${name} "${actual}")
    elseif(NOT EXISTS ${HERE}/expected/${name})
        string(APPEND failures "${name}: generated but not in expected/\n")
    else()
        file(READ ${HERE}/expected/${name} expected)
        if(NOT actual STREQUAL expected)
            file(WRITE ${WORK}/actual/${name} "${actual}")
            string(APPEND failures "${name}: differs from expected (diff ${HERE}/expected/${name} ${WORK}/actual/${name})\n")
        endif()
    endif()
endforeach()
file(GLOB wanted RELATIVE ${HERE}/expected ${HERE}/expected/*)
foreach(name ${wanted})
    if(NOT EXISTS ${PROJ}/build/generated/${name})
        string(APPEND failures "${name}: expected but not generated\n")
    endif()
endforeach()

# ── error cases: errors/<case>/expect.txt holds "<generator>\n<stderr substring>" ──
file(GLOB cases ${HERE}/errors/*)
foreach(case ${cases})
    get_filename_component(case_name ${case} NAME)
    file(STRINGS ${case}/expect.txt expect)
    list(GET expect 0 gen)
    list(GET expect 1 needle)
    file(COPY ${case} DESTINATION ${WORK}/errors)
    execute_process(COMMAND ${TOOL} ${gen} ${WORK}/errors/${case_name} ${FRAMEWORK}
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    string(FIND "${err}" "${needle}" at)
    if(rc EQUAL 0)
        string(APPEND failures "errors/${case_name}: ${gen} succeeded, expected a failure\n")
    elseif(at EQUAL -1)
        string(APPEND failures "errors/${case_name}: stderr lacks '${needle}': ${err}\n")
    endif()
endforeach()

if(failures)
    message(FATAL_ERROR "codegen test failed:\n${failures}")
endif()
message(STATUS "codegen: outputs match expected/, error cases fail as expected")
