set(axiom_gtest_options "INSTALL_GTEST OFF" "BUILD_GMOCK OFF" "BUILD_SHARED_LIBS OFF")
if (MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    list(APPEND axiom_gtest_options "gtest_force_shared_crt ON")
endif ()
cpmaddpackage(NAME googletest GITHUB_REPOSITORY google/googletest VERSION 1.18.0 SYSTEM YES
              EXCLUDE_FROM_ALL YES OPTIONS ${axiom_gtest_options})
if (AXIOM_SANITIZERS)
    foreach (target IN ITEMS gtest gtest_main)
        axiom_apply_sanitizers(${target})
    endforeach ()
endif ()

add_executable(axiom_test core_test.cpp descriptor_test.cpp dispatcher_test.cpp events_test.cpp
               async_test.cpp logging_test.cpp registry_test.cpp resource_test.cpp runtime_test.cpp
               value_converter_test.cpp)
axiom_configure_target(axiom_test)
target_include_directories(axiom_test PRIVATE "${PROJECT_SOURCE_DIR}/src"
                                           "${PROJECT_SOURCE_DIR}/src/action")
if (AXIOM_COVERAGE OR AXIOM_MUTATION_TESTING)
    target_link_libraries(axiom_test PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,Axiom::Axiom>")
else ()
    target_link_libraries(axiom_test PRIVATE Axiom::Axiom)
endif ()
target_link_libraries(axiom_test PRIVATE GTest::gtest_main)
axiom_deploy_runtime(axiom_test)
include(GoogleTest)
set(discovery_options)
if (AXIOM_SANITIZERS)
    list(APPEND discovery_options DISCOVERY_MODE PRE_TEST)
endif ()
if (AXIOM_COVERAGE)
    list(APPEND discovery_options PROPERTIES ENVIRONMENT "LLVM_PROFILE_FILE=axiom-%p-%m.profraw")
endif ()
gtest_discover_tests(axiom_test ${discovery_options})

if (AXIOM_BUILD_DEMO AND NOT AXIOM_COVERAGE AND NOT AXIOM_MUTATION_TESTING)
    add_test(NAME axiom.demo COMMAND axiom_demo)
endif ()
if (AXIOM_BUILD_INSTALL_TEST)
    if (AXIOM_COVERAGE OR AXIOM_SANITIZERS OR AXIOM_MUTATION_TESTING)
        message(FATAL_ERROR "Installation tests require an uninstrumented build; use quality-static/shared")
    endif ()
    configure_file("${PROJECT_SOURCE_DIR}/cmake/test_install.cmake"
                   "${CMAKE_CURRENT_BINARY_DIR}/test_install.cmake" @ONLY)
    add_test(NAME axiom.install COMMAND "${CMAKE_COMMAND}" "-DAXIOM_INSTALL_CONFIG=$<CONFIG>" -P
             "${CMAKE_CURRENT_BINARY_DIR}/test_install.cmake")
    set_tests_properties(axiom.install PROPERTIES TIMEOUT 300)
endif ()
if (BUILD_SHARED_LIBS AND UNIX)
    if (NOT CMAKE_NM)
        message(FATAL_ERROR "Shared export verification requires the toolchain nm utility")
    endif ()
    add_test(NAME axiom.exports COMMAND "${CMAKE_COMMAND}" "-DAXIOM_NM=${CMAKE_NM}"
             "-DAXIOM_LIBRARY=$<TARGET_FILE:axiom>" "-DAXIOM_APPLE=${APPLE}" -P
             "${PROJECT_SOURCE_DIR}/cmake/verify_exports.cmake")
endif ()
if (AXIOM_COVERAGE)
    find_program(AXIOM_LLVM_COV NAMES llvm-cov REQUIRED)
    mark_as_advanced(AXIOM_LLVM_COV)
    add_custom_target(axiom_verify_coverage
        COMMAND "${CMAKE_COMMAND}" "-DAXIOM_LLVM_COV=${AXIOM_LLVM_COV}"
                "-DAXIOM_TEST_BINARY=$<TARGET_FILE:axiom_test>"
                "-DAXIOM_PROFILE=${PROJECT_BINARY_DIR}/coverage.profdata"
                "-DAXIOM_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/cmake/verify_coverage.cmake"
        DEPENDS axiom_test VERBATIM)
endif ()
if (AXIOM_MUTATION_TESTING)
    add_custom_target(axiom_verify_mutation
        COMMAND "${CMAKE_COMMAND}" "-DAXIOM_REPORT=${PROJECT_BINARY_DIR}/mull/mutations.json" -P
                "${PROJECT_SOURCE_DIR}/cmake/verify_mutation.cmake" VERBATIM)
endif ()
