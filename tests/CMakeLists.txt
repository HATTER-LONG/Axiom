set(testlib_gtest_options "INSTALL_GTEST OFF" "BUILD_GMOCK OFF" "BUILD_SHARED_LIBS OFF")
if (MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    list(APPEND testlib_gtest_options "gtest_force_shared_crt ON")
endif ()
cpmaddpackage(
    NAME
    googletest
    GITHUB_REPOSITORY
    google/googletest
    VERSION
    1.18.0
    SYSTEM
    YES
    EXCLUDE_FROM_ALL
    YES
    OPTIONS
    ${testlib_gtest_options})
if (TESTLIB_SANITIZERS)
    foreach (target IN ITEMS gtest gtest_main)
        testlib_apply_sanitizers(${target})
    endforeach ()
endif ()

add_executable(testlib_core_test core_test.cpp)
testlib_configure_target(testlib_core_test)
if (TESTLIB_COVERAGE OR TESTLIB_MUTATION_TESTING)
    target_link_libraries(testlib_core_test PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,Testlib::Core>")
else ()
    target_link_libraries(testlib_core_test PRIVATE Testlib::Core)
endif ()
target_link_libraries(testlib_core_test PRIVATE GTest::gtest_main)
testlib_deploy_runtime(testlib_core_test)
include(GoogleTest)
set(discovery_options)
if (TESTLIB_SANITIZERS)
    list(APPEND discovery_options DISCOVERY_MODE PRE_TEST)
endif ()
if (TESTLIB_COVERAGE)
    list(APPEND discovery_options PROPERTIES ENVIRONMENT "LLVM_PROFILE_FILE=testlib-%p-%m.profraw")
endif ()
gtest_discover_tests(testlib_core_test ${discovery_options})

if (TESTLIB_BUILD_DEMO
    AND NOT TESTLIB_COVERAGE
    AND NOT TESTLIB_MUTATION_TESTING)
    add_test(NAME testlib.demo COMMAND testlib_demo)
endif ()

if (TESTLIB_BUILD_INSTALL_TEST)
    if (TESTLIB_COVERAGE
        OR TESTLIB_SANITIZERS
        OR TESTLIB_MUTATION_TESTING)
        message(
            FATAL_ERROR
                "Installation tests require an uninstrumented build; use quality-static/shared")
    endif ()
    configure_file("${PROJECT_SOURCE_DIR}/cmake/test_install.cmake"
                   "${CMAKE_CURRENT_BINARY_DIR}/test_install.cmake" @ONLY)
    add_test(NAME testlib.install
             COMMAND "${CMAKE_COMMAND}" "-DTESTLIB_INSTALL_CONFIG=$<CONFIG>" -P
                     "${CMAKE_CURRENT_BINARY_DIR}/test_install.cmake")
    set_tests_properties(testlib.install PROPERTIES TIMEOUT 300)
endif ()
