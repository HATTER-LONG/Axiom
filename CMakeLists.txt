cmake_minimum_required(VERSION 3.25)

# clang-cl used outside a Visual Studio developer shell can miss the Windows SDK
# manifest tool, which CMake requires during compiler identification.
if (WIN32
    AND CMAKE_CXX_COMPILER MATCHES "clang-cl(\\.exe)?$"
    AND NOT CMAKE_MT)
    file(GLOB _axiom_mt_candidates "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/mt.exe")
    list(SORT _axiom_mt_candidates COMPARE NATURAL ORDER DESCENDING)
    if (_axiom_mt_candidates)
        list(GET _axiom_mt_candidates 0 _axiom_mt_selected)
        set(CMAKE_MT
            "${_axiom_mt_selected}"
            CACHE FILEPATH "Path to the Windows SDK manifest tool" FORCE)
    endif ()
    unset(_axiom_mt_candidates)
    unset(_axiom_mt_selected)
endif ()

project(
    Axiom
    VERSION 0.1.0
    DESCRIPTION "Axiom application framework"
    LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Declare this before dependencies so their defaults cannot change our library kind.
option(BUILD_SHARED_LIBS "Build Axiom as a shared library" OFF)
option(AXIOM_BUILD_DEMO "Build the demonstration application" ${PROJECT_IS_TOP_LEVEL})
option(AXIOM_BUILD_INSTALL_TEST "Test the installed, uninstrumented package" OFF)
option(AXIOM_BUILD_DOCS "Build Doxygen API documentation" OFF)
option(AXIOM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ${PROJECT_IS_TOP_LEVEL})
option(AXIOM_BUILD_PYTHON "Build the Python adapter extension" OFF)
option(AXIOM_BUILD_WHEEL_VERIFY "Verify the built Python wheel end to end" OFF)
option(AXIOM_ENABLE_TEST_SEAMS
       "Compile test-only dispatch fault injection into PythonHost (never install/wheel)" OFF)
option(AXIOM_COVERAGE "Enable LLVM source-based coverage instrumentation" OFF)
option(AXIOM_MUTATION_TESTING "Instrument project targets for Mull mutation testing" OFF)
set(AXIOM_MULL_IR_FRONTEND
    ""
    CACHE FILEPATH "Path to the Mull LLVM IR frontend plugin")
mark_as_advanced(AXIOM_MULL_IR_FRONTEND)
set(AXIOM_SANITIZERS
    ""
    CACHE STRING
          "Semicolon-separated sanitizers: Address;Undefined;Thread;Memory;MemoryWithOrigins;Leak")
if (AXIOM_ENABLE_TEST_SEAMS AND AXIOM_BUILD_PYTHON)
    message(
        FATAL_ERROR
            "AXIOM_ENABLE_TEST_SEAMS cannot be combined with AXIOM_BUILD_PYTHON; wheel and embedding builds must not compile test seams"
    )
endif ()
if (AXIOM_ENABLE_TEST_SEAMS AND AXIOM_BUILD_INSTALL_TEST)
    message(
        FATAL_ERROR
            "AXIOM_ENABLE_TEST_SEAMS cannot be combined with AXIOM_BUILD_INSTALL_TEST; installed packages must not compile test seams"
    )
endif ()

include("${PROJECT_SOURCE_DIR}/cmake/CPM.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/tools.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/runtime.cmake")
axiom_configure_instrumentation()

if (AXIOM_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    configure_file(Doxyfile.in Doxyfile @ONLY)
    add_custom_target(
        docs
        COMMAND Doxygen::doxygen "${PROJECT_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        VERBATIM)
endif ()

# Python tooling and its imported pybind11 targets are project-wide so every
# subdirectory (library, extension, embedding consumers) shares one scope.
if (AXIOM_BUILD_PYTHON)
    # Pin to the uv-managed environment at python/.venv. An explicit, usable
    # Python_EXECUTABLE still wins (wheel consumers, custom interpreters). A
    # missing venv is a hard error: never fall back to a global or Store
    # interpreter whose embeddable prefix is wrong.
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(_axiom_venv_python "${PROJECT_SOURCE_DIR}/python/.venv/Scripts/python.exe")
    else ()
        set(_axiom_venv_python "${PROJECT_SOURCE_DIR}/python/.venv/bin/python")
    endif ()
    if (NOT Python_EXECUTABLE)
        if (NOT EXISTS "${_axiom_venv_python}")
            message(
                FATAL_ERROR
                    "AXIOM_BUILD_PYTHON requires the pinned uv environment at python/.venv. "
                    "Run `uv sync --project python --python 3.12 --frozen` (or "
                    "`python tools/create_python_venv.py`) and reconfigure. Do not rely on a "
                    "global interpreter; set Python_EXECUTABLE only to override the venv.")
        endif ()
        set(Python_EXECUTABLE "${_axiom_venv_python}")
    elseif (NOT EXISTS "${Python_EXECUTABLE}" OR Python_EXECUTABLE MATCHES "WindowsApps|pythoncore-")
        if (NOT EXISTS "${_axiom_venv_python}")
            message(
                FATAL_ERROR
                    "AXIOM_BUILD_PYTHON: Python_EXECUTABLE=${Python_EXECUTABLE} is unusable and "
                    "python/.venv is missing. Run `uv sync --project python --python 3.12 --frozen`.")
        endif ()
        message(
            WARNING
                "Replacing unusable Python_EXECUTABLE=${Python_EXECUTABLE} with the pinned uv "
                "interpreter ${_axiom_venv_python}")
        set(Python_EXECUTABLE "${_axiom_venv_python}")
    endif ()
    # FindPython caches Development artifacts (import libraries, DLLs) separately
    # from the executable. Replacing a Store/global interpreter without clearing
    # those entries leaves TARGET_RUNTIME_DLLS pointing at the old python3XX.dll.
    if (NOT "$CACHE{AXIOM_PYTHON_INTERPRETER}" STREQUAL "${Python_EXECUTABLE}")
        message(STATUS "Clearing FindPython cache for ${Python_EXECUTABLE}")
        get_cmake_property(_axiom_cache_vars CACHE_VARIABLES)
        foreach (_axiom_cache_var IN LISTS _axiom_cache_vars)
            if (_axiom_cache_var MATCHES "^(_?Python)")
                unset(${_axiom_cache_var} CACHE)
            endif ()
        endforeach ()
        unset(_axiom_cache_var)
        unset(_axiom_cache_vars)
        set(Python_EXECUTABLE "${Python_EXECUTABLE}" CACHE FILEPATH "Python interpreter used by Axiom"
                                                           FORCE)
        set(AXIOM_PYTHON_INTERPRETER "${Python_EXECUTABLE}" CACHE INTERNAL
            "Interpreter whose FindPython artifacts are currently cached")
    endif ()
    unset(_axiom_venv_python)
    set(Python_FIND_VIRTUALENV FIRST)
    set(Python_FIND_STRATEGY LOCATION)
    find_package(Python COMPONENTS Interpreter Development.Module Development.Embed REQUIRED)
    # Support matrix interpreter contract: Linux ships CPython 3.12; Windows
    # ships CPython 3.12/3.13. Other combinations are out of scope for the
    # first release and must fail configuration loudly.
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_axiom_supported_pythons "3.12")
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(_axiom_supported_pythons "3.12" "3.13")
    else ()
        message(
            FATAL_ERROR
                "AXIOM_BUILD_PYTHON: ${CMAKE_SYSTEM_NAME} is not in the first-release support "
                "matrix (Linux: CPython 3.12; Windows: CPython 3.12/3.13)")
    endif ()
    set(_axiom_python_version "${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}")
    if (NOT _axiom_python_version IN_LIST _axiom_supported_pythons)
        message(
            FATAL_ERROR
                "AXIOM_BUILD_PYTHON on ${CMAKE_SYSTEM_NAME} requires CPython "
                "${_axiom_supported_pythons}, but the selected interpreter is ${Python_VERSION} "
                "(${Python_EXECUTABLE}). Point Python_EXECUTABLE at a supported interpreter; "
                "`uv sync --project python --python 3.12` provisions the pinned environment.")
    endif ()
    unset(_axiom_python_version)
    unset(_axiom_supported_pythons)
    # Drive pybind11's own discovery through modern FindPython results.
    set(PYBIND11_FINDPYTHON ON)
    # Prefer a pybind11 CMake package reachable from the selected interpreter
    # (pip-installed, offline friendly); fall back to CPM download when the
    # environment can reach GitHub.
    find_package(pybind11 CONFIG QUIET)
    if (NOT pybind11_FOUND AND Python_EXECUTABLE)
        execute_process(
            COMMAND "${Python_EXECUTABLE}" -c
                    "import pybind11; print(pybind11.get_cmake_dir(), end='')"
            OUTPUT_VARIABLE _axiom_pybind11_cmake_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if (_axiom_pybind11_cmake_dir)
            find_package(
                pybind11 CONFIG REQUIRED
                PATHS "${_axiom_pybind11_cmake_dir}"
                NO_DEFAULT_PATH)
        endif ()
    endif ()
    if (NOT pybind11_FOUND)
        cpmaddpackage(NAME pybind11 GITHUB_REPOSITORY pybind/pybind11 VERSION 2.13.6 DOWNLOAD_ONLY
                      YES)
        add_subdirectory("${pybind11_SOURCE_DIR}")
    endif ()
endif ()

add_subdirectory(src)

if (AXIOM_BUILD_DEMO)
    add_subdirectory(apps/demo)
endif ()

if (PROJECT_IS_TOP_LEVEL)
    include(CTest)
    if (AXIOM_BUILD_INSTALL_TEST AND NOT BUILD_TESTING)
        message(FATAL_ERROR "AXIOM_BUILD_INSTALL_TEST requires BUILD_TESTING=ON")
    endif ()
    if (BUILD_TESTING)
        add_subdirectory(tests)
    endif ()
endif ()

# Wheel builds keep only the Python package artifacts; the development
# package layout below is not part of a wheel.
if (NOT SKBUILD)
    install(
        TARGETS axiom axiom_python_host
        EXPORT AxiomTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    configure_package_config_file(cmake/AxiomConfig.cmake.in
                                  "${PROJECT_BINARY_DIR}/AxiomConfig.cmake"
                                  INSTALL_DESTINATION
                                  "${CMAKE_INSTALL_LIBDIR}/cmake/Axiom")
    write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/AxiomConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMinorVersion)
    install(
        EXPORT AxiomTargets
        NAMESPACE Axiom::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Axiom)
    install(
        FILES "${PROJECT_BINARY_DIR}/AxiomConfig.cmake"
              "${PROJECT_BINARY_DIR}/AxiomConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Axiom)
endif ()
