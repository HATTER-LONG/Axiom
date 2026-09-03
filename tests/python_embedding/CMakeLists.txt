add_executable(axiom_python_embedding embedding_consumer.cpp)
axiom_configure_target(axiom_python_embedding)
target_compile_features(axiom_python_embedding PRIVATE cxx_std_20)
target_link_libraries(axiom_python_embedding PRIVATE Axiom::PythonHost pybind11::embed
                                                    Python::Module)
axiom_deploy_runtime(axiom_python_embedding)

add_test(NAME axiom.python_dispatch
         COMMAND axiom_python_embedding
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_command_dispatch.py"
                 "$<TARGET_FILE_DIR:_axiom>" "${PROJECT_SOURCE_DIR}/python/src")
add_test(NAME axiom.python_conversion
         COMMAND axiom_python_embedding
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_conversion_boundary.py"
                 "$<TARGET_FILE_DIR:_axiom>" "${PROJECT_SOURCE_DIR}/python/src")
add_test(NAME axiom.python_closed
         COMMAND axiom_python_embedding
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_host_closed.py"
                 "$<TARGET_FILE_DIR:_axiom>" "${PROJECT_SOURCE_DIR}/python/src")
add_test(NAME axiom.python_facade
         COMMAND axiom_python_embedding "${PROJECT_SOURCE_DIR}/python/tests/test_facade.py"
                 "$<TARGET_FILE_DIR:_axiom>" "${PROJECT_SOURCE_DIR}/python/src")
add_test(
    NAME axiom.python_ruff
    COMMAND "${Python_EXECUTABLE}" -m ruff check src tests
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/python")
add_test(
    NAME axiom.python_format
    COMMAND "${Python_EXECUTABLE}" -m ruff format --check src tests
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/python")
add_test(
    NAME axiom.python_mypy
    COMMAND "${Python_EXECUTABLE}" -m mypy
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/python")
execute_process(
    COMMAND "${Python_EXECUTABLE}" -c "import sys; print(sys.base_prefix, end='')"
    OUTPUT_VARIABLE _axiom_python_home
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${Python_EXECUTABLE}" -c
            "import sysconfig; print(sysconfig.get_path('purelib'), end='')"
    OUTPUT_VARIABLE _axiom_python_site
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
set(_axiom_python_env
    "AXIOM_PYTHON_EXECUTABLE=${Python_EXECUTABLE};AXIOM_PYTHON_HOME=${_axiom_python_home}")
set(_axiom_python_env_mod "PYTHONPATH=path_list_prepend:${_axiom_python_site}")
if (WIN32)
    foreach (_axiom_python_runtime_dir IN LISTS Python_RUNTIME_LIBRARY_DIRS)
        list(APPEND _axiom_python_env_mod
             "PATH=path_list_prepend:${_axiom_python_runtime_dir}")
    endforeach ()
    cmake_path(GET Python_EXECUTABLE PARENT_PATH _axiom_python_scripts)
    list(APPEND _axiom_python_env_mod
         "PATH=path_list_prepend:${_axiom_python_home}"
         "PATH=path_list_prepend:${_axiom_python_scripts}"
         "PATH=path_list_prepend:$<TARGET_FILE_DIR:_axiom>"
         "PATH=path_list_prepend:$<TARGET_FILE_DIR:axiom_python_host>"
         "PATH=path_list_prepend:$<TARGET_FILE_DIR:axiom>")
    unset(_axiom_python_scripts)
    unset(_axiom_python_runtime_dir)
endif ()
unset(_axiom_python_home)
unset(_axiom_python_site)
set_tests_properties(
    axiom.python_dispatch axiom.python_conversion axiom.python_closed axiom.python_facade
    axiom.python_ruff axiom.python_format axiom.python_mypy
    PROPERTIES TIMEOUT 300 LABELS python)
set_tests_properties(
    axiom.python_dispatch axiom.python_conversion axiom.python_closed axiom.python_facade
    PROPERTIES ENVIRONMENT "${_axiom_python_env}" ENVIRONMENT_MODIFICATION "${_axiom_python_env_mod}")
set_tests_properties(
    axiom.python_facade PROPERTIES ENVIRONMENT "${_axiom_python_env};AXIOM_PY_COVERAGE=1")
unset(_axiom_python_env)
unset(_axiom_python_env_mod)

if (AXIOM_BUILD_WHEEL_VERIFY)
    execute_process(
        COMMAND "${Python_EXECUTABLE}" -c "import pip"
        RESULT_VARIABLE _axiom_pip_result
        OUTPUT_QUIET
        ERROR_QUIET)
    if (_axiom_pip_result EQUAL 0)
        add_test(
            NAME axiom.python_wheel
            COMMAND "${Python_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/verify_python_wheel.py"
                    --consumer "${CMAKE_CURRENT_SOURCE_DIR}/wheel_consumer"
                    --source "${PROJECT_SOURCE_DIR}"
                    --build-tree "${PROJECT_BINARY_DIR}"
                    --compiler "${CMAKE_CXX_COMPILER}"
                    --dispatch "${CMAKE_CURRENT_SOURCE_DIR}/test_command_dispatch.py"
                    --closed "${CMAKE_CURRENT_SOURCE_DIR}/test_host_closed.py"
                    --facade "${PROJECT_SOURCE_DIR}/python/tests/test_facade.py"
                    --package "${PROJECT_SOURCE_DIR}/python")
        set_tests_properties(axiom.python_wheel PROPERTIES TIMEOUT 2400 LABELS python
                                                    ENVIRONMENT
                                                    "AXIOM_PYTHON_EXECUTABLE=${Python_EXECUTABLE}")
    else ()
        message(
            STATUS
                "Skipping axiom.python_wheel: ${Python_EXECUTABLE} has no pip "
                "(uv virtual environments omit pip unless seeded)")
    endif ()
    unset(_axiom_pip_result)
endif ()