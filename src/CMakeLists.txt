set(axiom_sources
    axiom.cpp
    foundation/type_descriptor.cpp
    action/descriptor.cpp
    action/action_invoker.cpp
    action/module_builder.cpp
    action/registry.cpp
    action/runtime.cpp
    introspection/introspection_service.cpp
    introspection/detail/type_descriptor_copy.cpp
    command/command_dispatcher.cpp
    command/command_validation.cpp
    command/descriptor_conversion.cpp
    logging/logging_service.cpp
    logging/console_sink.cpp
    logging/log_collector.cpp
    resource/resource_id.cpp
    resource/resource_serial.cpp
    resource/resource_registry.cpp
    task/task_id.cpp
    task/task_control.cpp
    task/task_registry.cpp
    async/src/executor.cpp
    async/src/scheduler.cpp)

cpmaddpackage(
    NAME spdlog
    GITHUB_REPOSITORY gabime/spdlog
    VERSION 1.17.0
    DOWNLOAD_ONLY YES)

add_library(axiom ${axiom_sources})
add_library(Axiom::Axiom ALIAS axiom)
axiom_configure_target(axiom)
target_compile_features(axiom PUBLIC cxx_std_20)
target_include_directories(
    axiom
    PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/action")
find_package(Threads REQUIRED)
target_link_libraries(axiom PRIVATE Threads::Threads)
target_include_directories(axiom SYSTEM PRIVATE "${spdlog_SOURCE_DIR}/include")
set_target_properties(
    axiom
    PROPERTIES EXPORT_NAME Axiom
               POSITION_INDEPENDENT_CODE ON
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN ON
               VERSION "${PROJECT_VERSION}"
               SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
if (BUILD_SHARED_LIBS)
    target_compile_definitions(axiom PRIVATE AXIOM_BUILD_SHARED INTERFACE AXIOM_USE_SHARED)
endif ()
