include(${PROJECT_SOURCE_DIR}/cmake/CPM.cmake)

set(axiom_gtest_options "INSTALL_GTEST OFF")
if (MSVC)
    list(APPEND axiom_gtest_options "gtest_force_shared_crt ON")
endif ()

CPMAddPackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    VERSION 1.18.0
    OPTIONS ${axiom_gtest_options})

add_executable(axiom_core_test core_test.cpp)
# Tests compile with warnings-as-errors as well.  Do not apply
# -fno-ms-compatibility here: GoogleTest's Windows portability layer depends on
# the Clang MSVC-compatible mode used by this project.
set_property(TARGET axiom_core_test PROPERTY COMPILE_WARNING_AS_ERROR "${AXIOM_WARNINGS_AS_ERRORS}")
set_property(TARGET axiom_core_test PROPERTY CXX_EXTENSIONS OFF)
axiom_configure_static_analyzers(axiom_core_test)
axiom_configure_sanitizers(axiom_core_test)
axiom_configure_coverage(axiom_core_test)
target_compile_features(axiom_core_test PRIVATE cxx_std_20)
target_link_libraries(axiom_core_test PRIVATE Axiom::Core GTest::gtest_main)

include(GoogleTest)
set(axiom_discovery_options)
if (AXIOM_COVERAGE)
    # %p and %m keep profiles of concurrent test processes and binaries distinct.
    set(axiom_discovery_options PROPERTIES ENVIRONMENT
                                   "LLVM_PROFILE_FILE=axiom-%p-%m.profraw")
endif ()
gtest_discover_tests(axiom_core_test ${axiom_discovery_options})
axiom_configure_sanitizer_runtime(axiom_core_test)
