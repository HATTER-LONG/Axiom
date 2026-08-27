add_library(axiom_runtime STATIC src/value.cpp src/descriptor.cpp src/registry.cpp
                                  src/dispatcher.cpp src/runtime.cpp)
add_library(Axiom::Runtime ALIAS axiom_runtime)

axiom_configure_language_diagnostics(axiom_runtime)
axiom_configure_static_analyzers(axiom_runtime)
axiom_configure_sanitizers(axiom_runtime)
axiom_configure_coverage(axiom_runtime)
axiom_configure_mutation_testing(axiom_runtime)
target_compile_features(axiom_runtime PUBLIC cxx_std_20)
target_include_directories(
    axiom_runtime
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_definitions(axiom_runtime PRIVATE AXIOM_RUNTIME_BUILD)

set_target_properties(axiom_runtime PROPERTIES EXPORT_NAME Runtime POSITION_INDEPENDENT_CODE ON)
