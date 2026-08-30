cmake_minimum_required(VERSION 3.25)
project(AxiomEmbeddingContract LANGUAGES CXX)

# Stand in for settings owned by a consuming project's toolchain. No analyzer is executed by this
# configure-only regression; the exact values must survive.
set(CMAKE_CXX_CLANG_TIDY
    "parent-clang-tidy"
    CACHE STRING "Parent analyzer")
set(CMAKE_CXX_CPPCHECK
    "parent-cppcheck"
    CACHE STRING "Parent analyzer")
set(USE_SANITIZER
    "parent-sanitizer-policy"
    CACHE STRING "Parent sanitizer")
set(BUILD_TESTING
    ON
    CACHE BOOL "The parent owns its own tests")
get_filename_component(axiom_source "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_subdirectory("${axiom_source}" axiom)
foreach (variable IN ITEMS CMAKE_CXX_CLANG_TIDY CMAKE_CXX_CPPCHECK USE_SANITIZER)
    get_property(
        preserved
        CACHE ${variable}
        PROPERTY TYPE
        SET)
    if (NOT preserved)
        message(FATAL_ERROR "Axiom removed parent configuration: ${variable}")
    endif ()
endforeach ()
get_target_property(analyzer Axiom::Axiom CXX_CLANG_TIDY)
if (NOT analyzer STREQUAL "parent-clang-tidy")
    message(FATAL_ERROR "Axiom ignored the parent's analyzer")
endif ()
get_target_property(kind Axiom::Axiom TYPE)
if (NOT kind STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "Default Axiom library must be static")
endif ()
if (TARGET axiom_demo
    OR TARGET Axiom::DemoCore
    OR TARGET axiom_test)
    message(FATAL_ERROR "Embedding Axiom must not add demo or test targets")
endif ()
get_target_property(warnings Axiom::Axiom COMPILE_WARNING_AS_ERROR)
if (warnings)
    message(FATAL_ERROR "Embedded builds must not force warnings-as-errors")
endif ()

# Exercise both sides of the report guard, so a syntax error cannot masquerade as correctly
# rejecting header-only mutation reports.
set(report "${CMAKE_CURRENT_BINARY_DIR}/mutations.json")
foreach (source IN ITEMS "/project/include/axiom/foundation/value.hpp" "/project/src/action/runtime.cpp")
    file(WRITE "${report}" "{\"files\":{\"${source}\":{\"mutants\":[{}]}}}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DAXIOM_REPORT=${report}" -P
                "${axiom_source}/cmake/verify_mutation.cmake"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if (source MATCHES "[.]hpp$" AND (result EQUAL 0 OR NOT error MATCHES
                                                        "no Axiom implementation mutants"))
        message(
            FATAL_ERROR "Header-only mutation report was not correctly rejected: ${output}${error}")
    elseif (source MATCHES "[.]cpp$" AND NOT result EQUAL 0)
        message(FATAL_ERROR "Implementation report was rejected: ${output}${error}")
    endif ()
endforeach ()
