cmake_minimum_required(VERSION 3.25)

project(
    Axiom
    VERSION 0.1.0
    DESCRIPTION "Axiom application framework"
    LANGUAGES CXX)

include(GNUInstallDirs)

option(AXIOM_BUILD_DEMO "Build the minimal demonstration application" ${PROJECT_IS_TOP_LEVEL})
option(AXIOM_BUILD_DOCS "Build Doxygen API documentation" OFF)
option(AXIOM_ENABLE_CLANG_TIDY "Run clang-tidy while compiling" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(cmake/tools.cmake)
axiom_configure_developer_tools()

add_subdirectory(src/core)

if (AXIOM_BUILD_DEMO)
    add_subdirectory(apps/demo)
endif ()

install(
    TARGETS axiom_core
    EXPORT AxiomTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY src/core/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    EXPORT AxiomTargets
    NAMESPACE Axiom::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Axiom)
