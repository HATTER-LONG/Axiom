include("${PROJECT_SOURCE_DIR}/cmake/CPM.cmake")

set(axiom_gtest_options "INSTALL_GTEST OFF" "SYSTEM ON" "EXCLUDE_FROM_ALL ON")
if (MSVC)
    list(APPEND axiom_gtest_options "gtest_force_shared_crt ON")
endif ()

CPMAddPackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    VERSION 1.18.0
    OPTIONS ${axiom_gtest_options})

# Clang records sanitizer configuration in every object file.  Compile the
# GoogleTest libraries with the same configuration as the test binary so the
# Windows linker does not reject their otherwise incompatible annotations.
if (AXIOM_SANITIZERS)
    foreach (axiom_gtest_target gtest gtest_main)
        axiom_configure_sanitizers(${axiom_gtest_target})
    endforeach ()
endif ()

add_executable(axiom_core_test core_test.cpp descriptor_test.cpp dispatcher_test.cpp registry_test.cpp runtime_test.cpp value_converter_test.cpp logging_test.cpp resource_test.cpp)
axiom_configure_language_diagnostics(axiom_core_test)
axiom_configure_static_analyzers(axiom_core_test)
axiom_configure_sanitizers(axiom_core_test)
axiom_configure_coverage(axiom_core_test)
axiom_configure_mutation_testing(axiom_core_test)
target_compile_features(axiom_core_test PRIVATE cxx_std_20)
target_link_libraries(axiom_core_test PRIVATE Axiom::Core GTest::gtest_main)

include(GoogleTest)
set(axiom_discovery_options)
if (AXIOM_COVERAGE)
    # %p and %m keep profiles of concurrent test processes and binaries distinct.
    set(axiom_discovery_options PROPERTIES ENVIRONMENT
                                   "LLVM_PROFILE_FILE=axiom-%p-%m.profraw")
endif ()
if (AXIOM_SANITIZERS)
    # Sanitizer runtimes may not be usable in the linker/build process environment.
    list(APPEND axiom_discovery_options DISCOVERY_MODE PRE_TEST)
endif ()
gtest_discover_tests(axiom_core_test ${axiom_discovery_options})
axiom_configure_sanitizer_runtime(axiom_core_test)
