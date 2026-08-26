add_library(axiom_core STATIC src/core.cpp)
add_library(Axiom::Core ALIAS axiom_core)

axiom_configure_language_diagnostics(axiom_core)
axiom_configure_static_analyzers(axiom_core)
axiom_configure_sanitizers(axiom_core)
axiom_configure_coverage(axiom_core)
target_compile_features(axiom_core PUBLIC cxx_std_20)
target_include_directories(
    axiom_core
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_definitions(axiom_core PRIVATE AXIOM_CORE_BUILD)

set_target_properties(axiom_core PROPERTIES EXPORT_NAME Core POSITION_INDEPENDENT_CODE ON)
