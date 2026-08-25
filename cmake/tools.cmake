include_guard(GLOBAL)

#[[Configure optional repository development tools.]]
function (axiom_configure_developer_tools)
    if (AXIOM_ENABLE_CLANG_TIDY)
        find_program(AXIOM_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
        set(clang_tidy_command
            "${AXIOM_CLANG_TIDY_EXECUTABLE};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy")
        set(CMAKE_CXX_CLANG_TIDY
            "${clang_tidy_command}"
            PARENT_SCOPE)
    endif ()

    if (AXIOM_BUILD_DOCS)
        find_package(Doxygen REQUIRED)
        configure_file("${PROJECT_SOURCE_DIR}/Doxyfile.in" "${PROJECT_BINARY_DIR}/Doxyfile" @ONLY)
        add_custom_target(
            docs
            COMMAND Doxygen::doxygen "${PROJECT_BINARY_DIR}/Doxyfile"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            COMMENT "Generating Axiom API documentation"
            VERBATIM)
    endif ()
endfunction ()

#[[CPM.cmake is kept in this framework for future third-party dependencies.]]
