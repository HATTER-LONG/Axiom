cmake_minimum_required(VERSION 3.25)

project(
    Axiom
    VERSION 0.1.0
    DESCRIPTION "Axiom application framework"
    LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(AXIOM_BUILD_DEMO "Build the minimal demonstration application" ${PROJECT_IS_TOP_LEVEL})
option(AXIOM_BUILD_TESTS "Build Axiom unit and integration tests" OFF)
option(AXIOM_BUILD_INSTALL_TEST "Build the installed-package consumer test" OFF)
option(AXIOM_BUILD_DOCS "Build Doxygen API documentation" OFF)
option(AXIOM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(AXIOM_COVERAGE "Enable LLVM source-based coverage instrumentation" OFF)
option(AXIOM_MUTATION_TESTING "Instrument project targets for Mull mutation testing" OFF)
set(AXIOM_MULL_IR_FRONTEND "" CACHE FILEPATH "Path to the Mull LLVM IR frontend plugin")
option(AXIOM_STRICT_LANGUAGE_MODE "Enable strict C++ language diagnostics" ON)
set(AXIOM_SANITIZERS
    ""
    CACHE STRING
          "Sanitizers: Address, Memory, MemoryWithOrigins, Undefined, Thread, Leak, or a list")
set(AXIOM_STATIC_ANALYZERS
    ""
    CACHE STRING "Static analyzers: clang-tidy, cppcheck, or a semicolon-separated list")
option(AXIOM_USE_CCACHE "Enable ccache compiler launcher" OFF)

if (NOT CPM_SOURCE_CACHE)
    set(CPM_SOURCE_CACHE
        "${PROJECT_SOURCE_DIR}/.cache/CPM"
        CACHE PATH "Persistent CPM package source cache" FORCE)
endif ()

include(cmake/tools.cmake)
axiom_configure_tools()

add_subdirectory(src/core)

if (AXIOM_BUILD_DEMO)
    add_subdirectory(apps/demo)
endif ()

if (PROJECT_IS_TOP_LEVEL)
    include(CTest)

    if (AXIOM_BUILD_TESTS AND BUILD_TESTING)
        add_subdirectory(tests)

        if (AXIOM_BUILD_INSTALL_TEST)
            add_test(
                NAME axiom.install
                COMMAND
                    ${CMAKE_COMMAND}
                    -DAXIOM_SOURCE_DIR=${PROJECT_SOURCE_DIR}
                    -DAXIOM_BINARY_DIR=${PROJECT_BINARY_DIR}
                    -DAXIOM_GENERATOR=${CMAKE_GENERATOR}
                    -DAXIOM_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
                    -DAXIOM_CXX_COMPILER=${CMAKE_CXX_COMPILER}
                    -DAXIOM_RC_COMPILER=${CMAKE_RC_COMPILER}
                    -DAXIOM_INSTALL_CONFIG=$<CONFIG>
                    -P ${PROJECT_SOURCE_DIR}/cmake/test_install.cmake)
            if (AXIOM_SANITIZERS)
                # The install consumer intentionally does not inherit private sanitizer link flags.
                set_tests_properties(axiom.install PROPERTIES DISABLED TRUE)
            endif ()
        endif ()
    endif ()
endif ()

install(
    TARGETS axiom_core
    EXPORT AxiomTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY src/core/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
configure_package_config_file(cmake/AxiomConfig.cmake.in "${PROJECT_BINARY_DIR}/AxiomConfig.cmake"
                              INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Axiom")
write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/AxiomConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)
install(
    EXPORT AxiomTargets
    NAMESPACE Axiom::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Axiom)
install(FILES "${PROJECT_BINARY_DIR}/AxiomConfig.cmake"
              "${PROJECT_BINARY_DIR}/AxiomConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Axiom)
