cmake_minimum_required(VERSION 3.25)

project(TestlibInstallConsumer LANGUAGES CXX)

include(CTest)

find_package(Testlib CONFIG REQUIRED)

add_executable(testlib_install_consumer main.cpp)
target_link_libraries(testlib_install_consumer PRIVATE Testlib::Core)

add_test(NAME testlib.install.consumer COMMAND testlib_install_consumer)

get_target_property(testlib_core_type Testlib::Core TYPE)
if (TESTLIB_EXPECT_SHARED AND NOT testlib_core_type STREQUAL "SHARED_LIBRARY")
    message(FATAL_ERROR "The installed Core must be shared")
elseif (NOT TESTLIB_EXPECT_SHARED AND NOT testlib_core_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "The installed Core must be static")
endif ()
if (WIN32 AND TESTLIB_EXPECT_SHARED)
    set_tests_properties(
        testlib.install.consumer
        PROPERTIES ENVIRONMENT_MODIFICATION
                   "PATH=path_list_prepend:$<TARGET_FILE_DIR:Testlib::Core>")
endif ()
