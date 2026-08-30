cmake_minimum_required(VERSION 3.25)

project(AxiomInstallConsumer LANGUAGES CXX)

include(CTest)

find_package(Axiom CONFIG REQUIRED)

add_executable(
    axiom_install_consumer
    main.cpp
    install_resource_check.cpp
    install_widget_register.cpp
    install_widget_resolve.cpp)
target_link_libraries(axiom_install_consumer PRIVATE Axiom::Core)

add_test(NAME axiom.install.consumer COMMAND axiom_install_consumer)

get_target_property(axiom_core_type Axiom::Core TYPE)
if (axiom_core_type STREQUAL "SHARED_LIBRARY")
    set(axiom_core_shared_location "")
    foreach (axiom_location_property IN ITEMS IMPORTED_LOCATION IMPORTED_LOCATION_RELWITHDEBINFO
                                              IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_DEBUG
                                              IMPORTED_LOCATION_MINSIZEREL)
        get_target_property(axiom_core_candidate Axiom::Core ${axiom_location_property})
        if (axiom_core_candidate)
            set(axiom_core_shared_location "${axiom_core_candidate}")
            break()
        endif ()
    endforeach ()
    if (NOT axiom_core_shared_location)
        message(FATAL_ERROR "Installed Axiom::Core shared library location is unavailable")
    endif ()
    cmake_path(GET axiom_core_shared_location PARENT_PATH axiom_core_shared_dir)
    set_target_properties(axiom_install_consumer PROPERTIES BUILD_RPATH "${axiom_core_shared_dir}")
    set_tests_properties(
        axiom.install.consumer PROPERTIES ENVIRONMENT_MODIFICATION
                                          "PATH=path_list_prepend:${axiom_core_shared_dir}")
endif ()
