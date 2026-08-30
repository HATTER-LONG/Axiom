cmake_minimum_required(VERSION 3.25)

project(
    Axiom
    VERSION 0.1.0
    DESCRIPTION "Axiom application framework"
    LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Declare this before dependencies so their defaults cannot change our library kind.
option(BUILD_SHARED_LIBS "Build Axiom as a shared library" OFF)
option(AXIOM_BUILD_DEMO "Build the demonstration application" ${PROJECT_IS_TOP_LEVEL})
option(AXIOM_BUILD_INSTALL_TEST "Test the installed, uninstrumented package" OFF)
option(AXIOM_BUILD_DOCS "Build Doxygen API documentation" OFF)
option(AXIOM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ${PROJECT_IS_TOP_LEVEL})
option(AXIOM_COVERAGE "Enable LLVM source-based coverage instrumentation" OFF)
option(AXIOM_MUTATION_TESTING "Instrument project targets for Mull mutation testing" OFF)
set(AXIOM_MULL_IR_FRONTEND
    ""
    CACHE FILEPATH "Path to the Mull LLVM IR frontend plugin")
mark_as_advanced(AXIOM_MULL_IR_FRONTEND)
set(AXIOM_SANITIZERS
    ""
    CACHE STRING
          "Semicolon-separated sanitizers: Address;Undefined;Thread;Memory;MemoryWithOrigins;Leak")

include("${PROJECT_SOURCE_DIR}/cmake/CPM.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/tools.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/runtime.cmake")
axiom_configure_instrumentation()

if (AXIOM_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    configure_file(Doxyfile.in Doxyfile @ONLY)
    add_custom_target(
        docs
        COMMAND Doxygen::doxygen "${PROJECT_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        VERBATIM)
endif ()

add_subdirectory(src/core)

if (AXIOM_BUILD_DEMO)
    add_subdirectory(apps/demo)
endif ()

if (PROJECT_IS_TOP_LEVEL)
    include(CTest)
    if (AXIOM_BUILD_INSTALL_TEST AND NOT BUILD_TESTING)
        message(FATAL_ERROR "AXIOM_BUILD_INSTALL_TEST requires BUILD_TESTING=ON")
    endif ()
    if (BUILD_TESTING)
        add_subdirectory(tests)
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
    COMPATIBILITY SameMinorVersion)
install(
    EXPORT AxiomTargets
    NAMESPACE Axiom::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Axiom)
install(FILES "${PROJECT_BINARY_DIR}/AxiomConfig.cmake"
              "${PROJECT_BINARY_DIR}/AxiomConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Axiom)
