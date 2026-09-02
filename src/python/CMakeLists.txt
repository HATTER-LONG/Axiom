find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
set(AXIOM_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}" PARENT_SCOPE)

if(NOT DEFINED AXIOM_PYTHON_INSTALL_DIR)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c
                "import os, sys, sysconfig; print(os.path.relpath(sysconfig.get_path('platlib'), sys.prefix))"
        RESULT_VARIABLE axiom_python_site_result
        OUTPUT_VARIABLE axiom_python_site_relative
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT axiom_python_site_result EQUAL 0 OR IS_ABSOLUTE "${axiom_python_site_relative}" OR
       axiom_python_site_relative MATCHES "^\\.\\.")
        message(FATAL_ERROR "Cannot derive a relocatable Python install path; set AXIOM_PYTHON_INSTALL_DIR")
    endif()
    set(AXIOM_PYTHON_INSTALL_DIR "${axiom_python_site_relative}" CACHE PATH
        "Relative install directory for the axiom Python extension")
endif()
if(IS_ABSOLUTE "${AXIOM_PYTHON_INSTALL_DIR}" OR AXIOM_PYTHON_INSTALL_DIR MATCHES "^\\.\\.")
    message(FATAL_ERROR "AXIOM_PYTHON_INSTALL_DIR must be a relative path below the install prefix")
endif()

cpmaddpackage(
    NAME pybind11
    GITHUB_REPOSITORY pybind/pybind11
    VERSION 3.1.0
    EXCLUDE_FROM_ALL YES)

set(python_sources
    src/module.cpp
    src/value_binding.cpp
    src/error_binding.cpp
    src/action_binding.cpp
    src/resource_binding.cpp
    src/introspection_binding.cpp
    src/task_binding.cpp)

pybind11_add_module(axiom_python ${python_sources})
set_target_properties(axiom_python PROPERTIES OUTPUT_NAME axiom)
set_target_properties(axiom_python PROPERTIES PREFIX "${PYTHON_MODULE_PREFIX}")

add_library(Axiom::Python ALIAS axiom_python)

axiom_configure_target(axiom_python)
target_compile_features(axiom_python PUBLIC cxx_std_20)
target_include_directories(
    axiom_python
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(axiom_python PRIVATE Axiom::Axiom pybind11::module)

set_target_properties(
    axiom_python
    PROPERTIES EXPORT_NAME Python
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN ON)

target_compile_definitions(axiom_python PRIVATE PYBIND11_DETAILED_ERROR_MESSAGES)

install(TARGETS axiom_python EXPORT AxiomTargets
        LIBRARY DESTINATION "${AXIOM_PYTHON_INSTALL_DIR}"
        RUNTIME DESTINATION "${AXIOM_PYTHON_INSTALL_DIR}")
