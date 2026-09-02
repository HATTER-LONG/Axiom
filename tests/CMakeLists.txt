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
               value_converter_test.cpp task_test.cpp task_concurrency_test.cpp introspection_test.cpp)
# Internal unit tests compile their private implementation locally. Public Runtime
# and installed-consumer tests still exercise the shared library boundary.
if (BUILD_SHARED_LIBS)
    target_sources(axiom_test PRIVATE "${PROJECT_SOURCE_DIR}/src/action/dispatcher.cpp"
                                      "${PROJECT_SOURCE_DIR}/src/action/registry.cpp")
endif ()
axiom_configure_target(axiom_test)
target_include_directories(axiom_test PRIVATE "${PROJECT_SOURCE_DIR}/src"
                                            "${PROJECT_SOURCE_DIR}/src/action")
# The console sink formats records with spdlog's thread identifier; the test that
# asserts the exact output needs the same private header the sink implementation uses.
target_include_directories(axiom_test SYSTEM
                           PRIVATE "${CPM_PACKAGE_spdlog_SOURCE_DIR}/include")
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
    list(APPEND discovery_options PROPERTIES ENVIRONMENT "LLVM_PROFILE_FILE=axiom-%4m.profraw")
endif ()
gtest_discover_tests(axiom_test ${discovery_options})

if (AXIOM_BUILD_DEMO AND NOT AXIOM_COVERAGE AND NOT AXIOM_MUTATION_TESTING)
    add_test(NAME axiom.demo COMMAND axiom_demo)
endif ()
if (AXIOM_BUILD_PYTHON)
    find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
    pybind11_add_module(axiom_python_test_host MODULE python/host_fixture.cpp)
    axiom_configure_target(axiom_python_test_host)
    target_link_libraries(axiom_python_test_host PRIVATE Axiom::Axiom pybind11::module)
    set_target_properties(axiom_python_test_host PROPERTIES
                          LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/python")
    foreach (python_test IN ITEMS smoke value action introspection task resource)
        add_test(
            NAME axiom.python.${python_test}
            COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=$<TARGET_FILE_DIR:axiom_python>"
                    "AXIOM_PYTHON_TEST_HOST_DIR=$<TARGET_FILE_DIR:axiom_python_test_host>"
                    "${AXIOM_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/python/test_${python_test}.py")
        set_tests_properties(axiom.python.${python_test} PROPERTIES TIMEOUT 60)
        if (AXIOM_COVERAGE)
            set_tests_properties(axiom.python.${python_test}
                                 PROPERTIES ENVIRONMENT
                                            "LLVM_PROFILE_FILE=${PROJECT_BINARY_DIR}/axiom-python-%m.profraw")
        endif ()
    endforeach ()
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
    set(axiom_verify_coverage_extra)
    if (AXIOM_BUILD_PYTHON)
        set(axiom_verify_coverage_extra
            "-DAXIOM_EXTRA_OBJECTS=$<TARGET_FILE:axiom_python>|$<TARGET_FILE:axiom_python_test_host>")
    endif ()
    add_custom_target(axiom_verify_coverage
        COMMAND "${CMAKE_COMMAND}" "-DAXIOM_LLVM_COV=${AXIOM_LLVM_COV}"
                "-DAXIOM_TEST_BINARY=$<TARGET_FILE:axiom_test>"
                ${axiom_verify_coverage_extra}
                "-DAXIOM_PROFILE=${PROJECT_BINARY_DIR}/coverage.profdata"
                "-DAXIOM_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/cmake/verify_coverage.cmake"
        DEPENDS axiom_test VERBATIM)
endif ()
if (AXIOM_MUTATION_TESTING)
    add_custom_target(axiom_verify_mutation
        COMMAND "${CMAKE_COMMAND}" "-DAXIOM_REPORT=${PROJECT_BINARY_DIR}/mull/mutations.json"
                "-DAXIOM_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/cmake/verify_mutation.cmake" VERBATIM)
endif ()
