if (NOT DEFINED AXIOM_SOURCE_DIR OR NOT DEFINED AXIOM_BINARY_DIR OR NOT DEFINED AXIOM_GENERATOR
    OR NOT DEFINED AXIOM_MAKE_PROGRAM OR NOT DEFINED AXIOM_CXX_COMPILER)
    message(FATAL_ERROR "The Axiom install test requires source, binary, generator, and tool paths")
endif ()

set(axiom_install_prefix "${AXIOM_BINARY_DIR}/install-test")
set(axiom_consumer_binary_dir "${AXIOM_BINARY_DIR}/install-test-consumer")
file(REMOVE_RECURSE "${axiom_install_prefix}" "${axiom_consumer_binary_dir}")

set(axiom_consumer_configure_command
    ${CMAKE_COMMAND}
    -S "${AXIOM_SOURCE_DIR}/tests/install"
    -B "${axiom_consumer_binary_dir}"
    -G "${AXIOM_GENERATOR}"
    "-DCMAKE_MAKE_PROGRAM=${AXIOM_MAKE_PROGRAM}"
    "-DCMAKE_CXX_COMPILER=${AXIOM_CXX_COMPILER}"
    "-DCMAKE_PREFIX_PATH=${axiom_install_prefix}")
if (AXIOM_RC_COMPILER)
    list(APPEND axiom_consumer_configure_command "-DCMAKE_RC_COMPILER=${AXIOM_RC_COMPILER}")
endif ()

set(axiom_install_command ${CMAKE_COMMAND} --install "${AXIOM_BINARY_DIR}" --prefix "${axiom_install_prefix}")
if (AXIOM_INSTALL_CONFIG)
    list(APPEND axiom_install_command --config "${AXIOM_INSTALL_CONFIG}")
endif ()
execute_process(
    COMMAND ${axiom_install_command}
    RESULT_VARIABLE axiom_install_result)
if (NOT axiom_install_result EQUAL 0)
    message(FATAL_ERROR "Axiom installation failed with exit code ${axiom_install_result}")
endif ()

execute_process(
    COMMAND ${axiom_consumer_configure_command}
    RESULT_VARIABLE axiom_consumer_configure_result)
if (NOT axiom_consumer_configure_result EQUAL 0)
    message(FATAL_ERROR "Axiom install consumer configuration failed with exit code ${axiom_consumer_configure_result}")
endif ()

execute_process(
    COMMAND ${CMAKE_COMMAND} --build "${axiom_consumer_binary_dir}"
    RESULT_VARIABLE axiom_consumer_build_result)
if (NOT axiom_consumer_build_result EQUAL 0)
    message(FATAL_ERROR "Axiom install consumer build failed with exit code ${axiom_consumer_build_result}")
endif ()

set(axiom_consumer_test_command
    ${CMAKE_CTEST_COMMAND} --test-dir "${axiom_consumer_binary_dir}" --output-on-failure)
if (AXIOM_INSTALL_CONFIG)
    list(APPEND axiom_consumer_test_command -C "${AXIOM_INSTALL_CONFIG}")
endif ()
execute_process(
    COMMAND ${axiom_consumer_test_command}
    RESULT_VARIABLE axiom_consumer_test_result)
if (NOT axiom_consumer_test_result EQUAL 0)
    message(FATAL_ERROR "Axiom install consumer test failed with exit code ${axiom_consumer_test_result}")
endif ()
