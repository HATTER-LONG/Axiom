cmake_minimum_required(VERSION 3.25)

# Configured by tests/CMakeLists.txt; each build tests only its own library kind.
set(package_build "@PROJECT_BINARY_DIR@")
set(stage "${package_build}/install-stage")
set(prefix "${package_build}/install-relocated")
set(consumer_build "${package_build}/install-consumer")
file(REMOVE_RECURSE "${stage}" "${prefix}" "${consumer_build}")
execute_process(COMMAND "@CMAKE_COMMAND@" --install "${package_build}" --prefix "${stage}" --config
                        "${TESTLIB_INSTALL_CONFIG}" COMMAND_ERROR_IS_FATAL ANY)
# Relocate the package to catch build-tree and original-prefix paths in exports.
file(RENAME "${stage}" "${prefix}")
set(configure_command
    "@CMAKE_COMMAND@" -S "@PROJECT_SOURCE_DIR@/tests/install" -B "${consumer_build}" -G
    "@CMAKE_GENERATOR@" "-DCMAKE_PREFIX_PATH=${prefix}"
    "-DTESTLIB_EXPECT_SHARED=@BUILD_SHARED_LIBS@")
set(CMAKE_MAKE_PROGRAM "@CMAKE_MAKE_PROGRAM@")
set(CMAKE_CXX_COMPILER "@CMAKE_CXX_COMPILER@")
set(CMAKE_RC_COMPILER "@CMAKE_RC_COMPILER@")
set(CMAKE_TOOLCHAIN_FILE "@CMAKE_TOOLCHAIN_FILE@")
set(CMAKE_CXX_COMPILER_TARGET "@CMAKE_CXX_COMPILER_TARGET@")
set(CMAKE_SYSROOT "@CMAKE_SYSROOT@")
set(CMAKE_OSX_ARCHITECTURES "@CMAKE_OSX_ARCHITECTURES@")
set(CMAKE_OSX_DEPLOYMENT_TARGET "@CMAKE_OSX_DEPLOYMENT_TARGET@")
set(CMAKE_MSVC_RUNTIME_LIBRARY "@CMAKE_MSVC_RUNTIME_LIBRARY@")
foreach (
    variable IN
    ITEMS CMAKE_MAKE_PROGRAM
          CMAKE_CXX_COMPILER
          CMAKE_RC_COMPILER
          CMAKE_TOOLCHAIN_FILE
          CMAKE_CXX_COMPILER_TARGET
          CMAKE_SYSROOT
          CMAKE_OSX_ARCHITECTURES
          CMAKE_OSX_DEPLOYMENT_TARGET
          CMAKE_MSVC_RUNTIME_LIBRARY)
    if (NOT "${${variable}}" STREQUAL "")
        string(REPLACE ";" "\;" value "${${variable}}")
        list(APPEND configure_command "-D${variable}=${value}")
    endif ()
endforeach ()
if (NOT "@CMAKE_GENERATOR_PLATFORM@" STREQUAL "")
    list(APPEND configure_command -A "@CMAKE_GENERATOR_PLATFORM@")
endif ()
if (NOT "@CMAKE_GENERATOR_TOOLSET@" STREQUAL "")
    list(APPEND configure_command -T "@CMAKE_GENERATOR_TOOLSET@")
endif ()
if ("@CMAKE_CONFIGURATION_TYPES@" STREQUAL "")
    list(APPEND configure_command "-DCMAKE_BUILD_TYPE=${TESTLIB_INSTALL_CONFIG}")
endif ()
execute_process(COMMAND ${configure_command} COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "@CMAKE_COMMAND@" --build "${consumer_build}" --config
                        "${TESTLIB_INSTALL_CONFIG}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "@CMAKE_CTEST_COMMAND@" --test-dir "${consumer_build}" -C
            "${TESTLIB_INSTALL_CONFIG}" --output-on-failure --no-tests=error
            COMMAND_ERROR_IS_FATAL ANY)
