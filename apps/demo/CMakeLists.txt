add_executable(axiom_demo main.cpp)
axiom_configure_language_diagnostics(axiom_demo)
axiom_configure_static_analyzers(axiom_demo)
axiom_configure_sanitizers(axiom_demo)
axiom_configure_coverage(axiom_demo)
target_compile_features(axiom_demo PRIVATE cxx_std_20)
target_link_libraries(axiom_demo PRIVATE Axiom::DemoCore)

if (WIN32)
    add_custom_command(
        TARGET axiom_demo
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:axiom_demo>
                $<TARGET_RUNTIME_DLLS:axiom_demo>
        COMMAND_EXPAND_LISTS
        VERBATIM)
endif ()

axiom_configure_sanitizer_runtime(axiom_demo)
