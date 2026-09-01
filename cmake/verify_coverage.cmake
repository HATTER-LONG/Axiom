cmake_minimum_required(VERSION 3.25)

# Fail on LLVM mapping diagnostics even when llvm-cov exits successfully, and ensure every Axiom
# implementation file participates in the coverage denominator.
execute_process(
    COMMAND "${AXIOM_LLVM_COV}" export "${AXIOM_TEST_BINARY}" "--instr-profile=${AXIOM_PROFILE}"
            --summary-only --skip-functions
    OUTPUT_VARIABLE report
    ERROR_VARIABLE diagnostics COMMAND_ERROR_IS_FATAL ANY)
if (NOT diagnostics STREQUAL "")
    message(FATAL_ERROR "LLVM coverage diagnostics: ${diagnostics}")
endif ()
string(JSON file_count LENGTH "${report}" data 0 files)
set(covered_sources)
if (file_count GREATER 0)
    math(EXPR last "${file_count} - 1")
    foreach (index RANGE 0 ${last})
        string(
            JSON
            source
            GET
            "${report}"
            data
            0
            files
            ${index}
            filename)
        cmake_path(NORMAL_PATH source)
        list(APPEND covered_sources "${source}")
    endforeach ()
endif ()
file(GLOB_RECURSE axiom_sources "${AXIOM_SOURCE_DIR}/src/*.cpp")
list(FILTER axiom_sources EXCLUDE REGEX "/src/python/")
if (NOT axiom_sources)
    message(FATAL_ERROR "No Axiom implementation sources found")
endif ()
foreach (source IN LISTS axiom_sources)
    if (NOT source IN_LIST covered_sources)
        message(FATAL_ERROR "Axiom source missing from coverage: ${source}")
    endif ()
endforeach ()
