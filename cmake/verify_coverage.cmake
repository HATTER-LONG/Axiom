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
if (NOT axiom_sources)
    message(FATAL_ERROR "No Axiom implementation sources found")
endif ()
# AXIOM_COVERAGE_EXCLUDED_SOURCES lists sources compiled only in other build
# configurations (for example, pybind11-bound sources not compiled in static
# coverage builds). Entries must name real sources so exclusions cannot hide
# genuine gaps.
if (AXIOM_COVERAGE_EXCLUDED_SOURCES)
    foreach (excluded IN LISTS AXIOM_COVERAGE_EXCLUDED_SOURCES)
        if (NOT EXISTS "${excluded}")
            message(FATAL_ERROR "Coverage exclusion does not name an existing source: ${excluded}")
        endif ()
        if (NOT excluded MATCHES "/src/")
            message(FATAL_ERROR "Coverage exclusion must name a source under src/: ${excluded}")
        endif ()
    endforeach ()
endif ()
foreach (source IN LISTS axiom_sources)
    if (source IN_LIST covered_sources)
        continue()
    endif ()
    if (source IN_LIST AXIOM_COVERAGE_EXCLUDED_SOURCES)
        continue()
    endif ()
    message(FATAL_ERROR "Axiom source missing from coverage: ${source}")
endforeach ()
