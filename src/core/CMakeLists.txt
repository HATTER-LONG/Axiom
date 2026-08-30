include("${PROJECT_SOURCE_DIR}/cmake/CPM.cmake")

CPMAddPackage(
    NAME spdlog
    GITHUB_REPOSITORY gabime/spdlog
    VERSION 1.17.0
    DOWNLOAD_ONLY YES)

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
    src/resource/resource_id.cpp)

function (axiom_configure_core_compilation target_name)
    axiom_configure_language_diagnostics(${target_name})
    axiom_configure_static_analyzers(${target_name})
    axiom_configure_sanitizers(${target_name})
    axiom_configure_coverage(${target_name})
    axiom_configure_mutation_testing(${target_name})
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    target_include_directories(
        ${target_name}
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_include_directories(${target_name} SYSTEM PRIVATE "${spdlog_SOURCE_DIR}/include")
    target_compile_definitions(${target_name} PRIVATE SPDLOG_HEADER_ONLY)
endfunction ()

function (axiom_configure_core_interface target_name is_shared)
    target_compile_features(${target_name} PUBLIC cxx_std_20)
    target_include_directories(
        ${target_name}
        PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
               "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
    set_target_properties(${target_name} PROPERTIES POSITION_INDEPENDENT_CODE ON)

    if (is_shared)
        target_compile_definitions(
            ${target_name}
            PRIVATE AXIOM_CORE_BUILD_SHARED
            INTERFACE AXIOM_CORE_USE_SHARED)
    endif ()
endfunction ()

if (AXIOM_BUILD_DEMO AND NOT BUILD_SHARED_LIBS)
    # Compile once so quality tools see one command per source, then use the same
    # objects for the installed static library and the demo's shared library.
    add_library(axiom_core_objects OBJECT ${axiom_core_sources})
    axiom_configure_core_compilation(axiom_core_objects)
    target_compile_definitions(axiom_core_objects PRIVATE AXIOM_CORE_BUILD_SHARED)
    set_target_properties(axiom_core_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

    add_library(axiom_core STATIC $<TARGET_OBJECTS:axiom_core_objects>)
    axiom_configure_core_interface(axiom_core FALSE)

    add_library(axiom_demo_core SHARED $<TARGET_OBJECTS:axiom_core_objects>)
    axiom_configure_core_interface(axiom_demo_core TRUE)
    set_target_properties(
        axiom_demo_core
        PROPERTIES OUTPUT_NAME axiom_core
                   ARCHIVE_OUTPUT_NAME axiom_demo_core)
    add_library(Axiom::DemoCore ALIAS axiom_demo_core)
else ()
    set(axiom_core_library_type STATIC)
    if (BUILD_SHARED_LIBS)
        set(axiom_core_library_type SHARED)
    endif ()

    add_library(axiom_core ${axiom_core_library_type} ${axiom_core_sources})
    axiom_configure_core_compilation(axiom_core)
    axiom_configure_core_interface(axiom_core ${BUILD_SHARED_LIBS})

    if (AXIOM_BUILD_DEMO)
        add_library(Axiom::DemoCore ALIAS axiom_core)
    endif ()
endif ()

add_library(Axiom::Core ALIAS axiom_core)

# spdlog is compiled into Core as a private implementation detail. Do not link its
# CMake target: exported dependencies would otherwise leak into installed consumers.
set_target_properties(axiom_core PROPERTIES EXPORT_NAME Core)
