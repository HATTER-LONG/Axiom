add_library(axiom_python_host host_bridge.cpp)
add_library(Axiom::PythonHost ALIAS axiom_python_host)
axiom_configure_target(axiom_python_host)
target_compile_features(axiom_python_host PUBLIC cxx_std_20)
target_include_directories(
    axiom_python_host
    PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(axiom_python_host PUBLIC Axiom::Axiom)
set_target_properties(
    axiom_python_host
    PROPERTIES EXPORT_NAME PythonHost
               POSITION_INDEPENDENT_CODE ON
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN ON
               VERSION "${PROJECT_VERSION}"
               SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
if (BUILD_SHARED_LIBS)
    target_compile_definitions(axiom_python_host PRIVATE AXIOM_PYTHON_BUILD_SHARED
                                                   INTERFACE AXIOM_PYTHON_USE_SHARED)
endif ()
if (AXIOM_ENABLE_TEST_SEAMS)
    target_compile_definitions(axiom_python_host PRIVATE AXIOM_ENABLE_TEST_SEAMS)
endif ()

if (AXIOM_BUILD_PYTHON)
    if (NOT BUILD_SHARED_LIBS)
        message(
            FATAL_ERROR
                "AXIOM_BUILD_PYTHON requires BUILD_SHARED_LIBS=ON so the extension and the host share one Axiom library"
        )
    endif ()
    if (AXIOM_COVERAGE OR AXIOM_SANITIZERS OR AXIOM_MUTATION_TESTING)
        message(
            FATAL_ERROR
                "AXIOM_BUILD_PYTHON requires an uninstrumented build; use the quality-python preset"
        )
    endif ()
    pybind11_add_module(_axiom MODULE module.cpp value_conversion.cpp error_translation.cpp)
    axiom_configure_target(_axiom)
    target_compile_features(_axiom PRIVATE cxx_std_20)
    target_link_libraries(_axiom PRIVATE Axiom::PythonHost)
    axiom_deploy_runtime(_axiom)
    if (SKBUILD)
        # Wheel layout: the extension becomes axiom._axiom and the matching
        # shared libraries sit beside it so loader resolution never falls back
        # to a global installation. Static archives never ship in wheels; on
        # Windows the import libraries are the sole linkable wheel artifacts.
        if (WIN32)
            install(
                TARGETS _axiom axiom_python_host axiom
                RUNTIME DESTINATION axiom
                LIBRARY DESTINATION axiom
                ARCHIVE DESTINATION axiom)
        else ()
            install(
                TARGETS _axiom axiom_python_host axiom
                RUNTIME DESTINATION axiom
                LIBRARY DESTINATION axiom)
        endif ()
        if (UNIX)
            set_target_properties(_axiom axiom_python_host PROPERTIES INSTALL_RPATH "$ORIGIN")
        endif ()
    endif ()
endif ()