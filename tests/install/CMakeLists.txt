cmake_minimum_required(VERSION 3.25)

project(AxiomInstallConsumer LANGUAGES CXX)

include(CTest)

find_package(Axiom CONFIG REQUIRED)

add_executable(axiom_install_consumer install_resource_check.cpp install_widget_register.cpp
                                      install_widget_resolve.cpp main.cpp)
target_link_libraries(axiom_install_consumer PRIVATE Axiom::Core)

add_test(NAME axiom.install.consumer COMMAND axiom_install_consumer)

get_target_property(axiom_core_type Axiom::Core TYPE)
if (AXIOM_EXPECT_SHARED AND NOT axiom_core_type STREQUAL "SHARED_LIBRARY")
    message(FATAL_ERROR "The installed Core must be shared")
elseif (NOT AXIOM_EXPECT_SHARED AND NOT axiom_core_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "The installed Core must be static")
endif ()
if (WIN32 AND AXIOM_EXPECT_SHARED)
    set_tests_properties(
        axiom.install.consumer PROPERTIES ENVIRONMENT_MODIFICATION
                                          "PATH=path_list_prepend:$<TARGET_FILE_DIR:Axiom::Core>")
endif ()
# CMake supplies build RPATH from the imported target on ELF/Mach-O platforms.
