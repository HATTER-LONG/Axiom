cmake_minimum_required(VERSION 3.25)

# Wheel consumer contract: compiles the embedding consumer against the source
# headers while every linked and loaded library comes from the installed wheel
# package directory. Nothing may resolve to the CMake build tree.
project(axiom_wheel_consumer LANGUAGES CXX)

set(AXIOM_SOURCE_DIR
    ""
    CACHE PATH "Axiom source repository root")
set(AXIOM_WHEEL_PACKAGE_DIR
    ""
    CACHE PATH "Installed axiom wheel package directory (site-packages/axiom)")
if (AXIOM_SOURCE_DIR STREQUAL "" OR AXIOM_WHEEL_PACKAGE_DIR STREQUAL "")
    message(FATAL_ERROR "AXIOM_SOURCE_DIR and AXIOM_WHEEL_PACKAGE_DIR are required")
endif ()

find_package(Python COMPONENTS Interpreter Development.Module Development.Embed REQUIRED)
set(PYBIND11_FINDPYTHON ON)
set(AXIOM_PYBIND11_CMAKE_DIR
    ""
    CACHE PATH "pybind11 CMake package directory (defaults to the interpreter's install)")
if (AXIOM_PYBIND11_CMAKE_DIR STREQUAL "")
    execute_process(
        COMMAND "${Python_EXECUTABLE}" -c
                "import pybind11; print(pybind11.get_cmake_dir(), end='')"
        OUTPUT_VARIABLE AXIOM_PYBIND11_CMAKE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
endif ()
find_package(
    pybind11 CONFIG REQUIRED
    PATHS "${AXIOM_PYBIND11_CMAKE_DIR}"
    NO_DEFAULT_PATH)

add_executable(axiom_wheel_consumer "${AXIOM_SOURCE_DIR}/tests/python_embedding/embedding_consumer.cpp")
target_compile_features(axiom_wheel_consumer PRIVATE cxx_std_20)
target_include_directories(axiom_wheel_consumer PRIVATE "${AXIOM_SOURCE_DIR}/include")

if (WIN32)
    target_compile_definitions(axiom_wheel_consumer PRIVATE AXIOM_USE_SHARED
                                                            AXIOM_PYTHON_USE_SHARED)
    set(_axiom_wheel_library "${AXIOM_WHEEL_PACKAGE_DIR}/axiom.lib")
    set(_axiom_host_library "${AXIOM_WHEEL_PACKAGE_DIR}/axiom_python_host.lib")
else ()
    set(_axiom_wheel_library "${AXIOM_WHEEL_PACKAGE_DIR}/libaxiom.so")
    set(_axiom_host_library "${AXIOM_WHEEL_PACKAGE_DIR}/libaxiom_python_host.so")
endif ()
foreach (_axiom_wheel_item IN ITEMS "${_axiom_wheel_library}" "${_axiom_host_library}")
    if (NOT EXISTS "${_axiom_wheel_item}")
        message(FATAL_ERROR "installed wheel package is missing ${_axiom_wheel_item}")
    endif ()
endforeach ()

target_link_libraries(axiom_wheel_consumer PRIVATE "${_axiom_host_library}"
                                                   "${_axiom_wheel_library}" pybind11::embed
                                                   Python::Module)
set_target_properties(
    axiom_wheel_consumer
    PROPERTIES BUILD_RPATH "${AXIOM_WHEEL_PACKAGE_DIR}" INSTALL_RPATH
                                                         "${AXIOM_WHEEL_PACKAGE_DIR}")
