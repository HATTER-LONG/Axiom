include_guard(GLOBAL)

# Probe instrumentation once, without changing caller-owned cache or directory flags.
function (axiom_configure_instrumentation)
    if (AXIOM_COVERAGE OR AXIOM_MUTATION_TESTING)
        if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT
                                                         STREQUAL "MSVC")
            message(FATAL_ERROR "Coverage and Mull require Clang with the GNU driver frontend")
        endif ()
        if (BUILD_SHARED_LIBS)
            message(
                FATAL_ERROR
                    "Coverage and Mull use static Core; use quality-shared for DLL validation")
        endif ()
    endif ()
    if (AXIOM_MUTATION_TESTING)
        if (AXIOM_COVERAGE OR AXIOM_SANITIZERS)
            message(FATAL_ERROR "Mull requires a separate build without coverage or sanitizers")
        endif ()
        if (NOT AXIOM_MULL_IR_FRONTEND OR NOT EXISTS "${AXIOM_MULL_IR_FRONTEND}")
            message(
                FATAL_ERROR "AXIOM_MULL_IR_FRONTEND must name an installed Mull frontend plugin")
        endif ()
    endif ()
    if (NOT AXIOM_SANITIZERS)
        return()
    endif ()
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT
                                                         STREQUAL "MSVC")
        message(FATAL_ERROR "AXIOM_SANITIZERS requires a Clang or GCC GNU-style driver")
    endif ()
    set(supported Address Undefined Thread Memory MemoryWithOrigins Leak)
    set(compile_options -fno-omit-frame-pointer)
    set(link_options)
    foreach (sanitizer IN LISTS AXIOM_SANITIZERS)
        if (NOT sanitizer IN_LIST supported)
            message(FATAL_ERROR "Unsupported sanitizer '${sanitizer}'; supported: ${supported}")
        endif ()
        string(TOLOWER "${sanitizer}" flag)
        if (sanitizer STREQUAL "MemoryWithOrigins")
            set(flag memory)
            list(APPEND compile_options -fsanitize-memory-track-origins=2)
        endif ()
        list(APPEND compile_options "-fsanitize=${flag}")
        list(APPEND link_options "-fsanitize=${flag}")
    endforeach ()
    if ("Undefined" IN_LIST AXIOM_SANITIZERS)
        # CTest must fail on UB, not merely capture a diagnostic from a successful process.
        list(APPEND compile_options -fno-sanitize-recover=undefined)
    endif ()
    include(CheckCXXSourceCompiles)
    string(JOIN " " CMAKE_REQUIRED_FLAGS ${compile_options})
    set(CMAKE_REQUIRED_LINK_OPTIONS ${link_options})
    string(
        SHA256
            probe_key
            "${CMAKE_CXX_COMPILER};${CMAKE_CXX_COMPILER_VERSION};${CMAKE_REQUIRED_FLAGS};${link_options}"
    )
    check_cxx_source_compiles("int main() { return 0; }" "AXIOM_SANITIZERS_${probe_key}")
    if (NOT AXIOM_SANITIZERS_${probe_key})
        message(
            FATAL_ERROR "Selected sanitizers are unavailable or incompatible: ${AXIOM_SANITIZERS}")
    endif ()
    set(axiom_sanitizer_compile_options
        "${compile_options}"
        PARENT_SCOPE)
    set(axiom_sanitizer_link_options
        "${link_options}"
        PARENT_SCOPE)
endfunction ()

# Instrument third-party test support without imposing our warnings policy on it.
function (axiom_apply_sanitizers target_name)
    target_compile_options(${target_name} PRIVATE ${axiom_sanitizer_compile_options})
    target_link_options(${target_name} PRIVATE ${axiom_sanitizer_link_options})
endfunction ()

# The single entry point for diagnostics and instrumentation on Axiom-owned targets.
function (axiom_configure_target target_name)
    set_target_properties(
        ${target_name} PROPERTIES COMPILE_WARNING_AS_ERROR "${AXIOM_WARNINGS_AS_ERRORS}"
                                  CXX_EXTENSIONS OFF)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL
                                                 "MSVC")
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -pedantic-errors)
    endif ()
    axiom_apply_sanitizers(${target_name})
    if (AXIOM_COVERAGE)
        target_compile_options(${target_name} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
        target_link_options(${target_name} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
    endif ()
    if (AXIOM_MUTATION_TESTING)
        target_compile_options(${target_name} PRIVATE "-fpass-plugin=${AXIOM_MULL_IR_FRONTEND}" -g
                                                      -grecord-command-line)
    endif ()
endfunction ()
