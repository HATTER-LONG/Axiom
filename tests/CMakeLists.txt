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
axiom_configure_static_analyzers(axiom_core_test)
axiom_configure_sanitizers(axiom_core_test)
target_compile_features(axiom_core_test PRIVATE cxx_std_20)
target_link_libraries(axiom_core_test PRIVATE Axiom::Core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(axiom_core_test)
