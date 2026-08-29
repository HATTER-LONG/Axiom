include("${PROJECT_SOURCE_DIR}/cmake/CPM.cmake")

CPMAddPackage(
    NAME spdlog
    GITHUB_REPOSITORY gabime/spdlog
    VERSION 1.17.0
    DOWNLOAD_ONLY YES)

add_library(
    axiom_core STATIC src/core.cpp src/base/type_descriptor.cpp src/action/descriptor.cpp
                      src/dispatcher.cpp src/module_builder.cpp src/registry.cpp src/runtime.cpp
                      src/logging/logging_service.cpp src/logging/console_sink.cpp
                      src/logging/log_collector.cpp)
add_library(Axiom::Core ALIAS axiom_core)

axiom_configure_language_diagnostics(axiom_core)
axiom_configure_static_analyzers(axiom_core)
axiom_configure_sanitizers(axiom_core)
axiom_configure_coverage(axiom_core)
axiom_configure_mutation_testing(axiom_core)
target_compile_features(axiom_core PUBLIC cxx_std_20)
target_include_directories(
    axiom_core
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_definitions(axiom_core PRIVATE AXIOM_CORE_BUILD)
# spdlog is compiled into this static library as a private implementation detail.  In
# particular, do not link its CMake target: exported static-library dependencies would
# otherwise leak into installed consumers.
target_include_directories(axiom_core SYSTEM PRIVATE "${spdlog_SOURCE_DIR}/include")
target_compile_definitions(axiom_core PRIVATE SPDLOG_HEADER_ONLY)

set_target_properties(axiom_core PROPERTIES EXPORT_NAME Core POSITION_INDEPENDENT_CODE ON)
