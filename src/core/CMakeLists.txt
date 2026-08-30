cpmaddpackage(
    NAME
    spdlog
    GITHUB_REPOSITORY
    gabime/spdlog
    VERSION
    1.17.0
    DOWNLOAD_ONLY
    YES)

set(axiom_core_sources
    src/core.cpp
    src/base/type_descriptor.cpp
    src/action/descriptor.cpp
    src/dispatcher.cpp
    src/module_builder.cpp
    src/registry.cpp
    src/runtime.cpp
    src/logging/logging_service.cpp
    src/logging/console_sink.cpp
    src/logging/log_collector.cpp
    src/resource/resource_id.cpp
    src/resource/resource_serial.cpp
    src/resource/resource_registry.cpp)

add_library(axiom_core ${axiom_core_sources})
add_library(Axiom::Core ALIAS axiom_core)
axiom_configure_target(axiom_core)
target_compile_features(axiom_core PUBLIC cxx_std_20)
target_include_directories(
    axiom_core PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
                      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
# Keep spdlog and bundled fmt out of the installed interface; preserve their platform threading
# requirement even though we consume the headers privately.
find_package(Threads REQUIRED)
target_link_libraries(axiom_core PRIVATE Threads::Threads)
target_include_directories(axiom_core SYSTEM PRIVATE "${spdlog_SOURCE_DIR}/include")
set_target_properties(
    axiom_core
    PROPERTIES EXPORT_NAME Core
               POSITION_INDEPENDENT_CODE ON
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN ON
               VERSION "${PROJECT_VERSION}"
               SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
if (BUILD_SHARED_LIBS)
    target_compile_definitions(
        axiom_core
        PRIVATE AXIOM_CORE_BUILD_SHARED
        INTERFACE AXIOM_CORE_USE_SHARED)
endif ()
