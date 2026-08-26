include_guard(GLOBAL)

#[[Apply compiler conformance and diagnostic options to an Axiom-owned target.]]
function (axiom_configure_language_diagnostics target_name)
    if (NOT TARGET "${target_name}")
        message(FATAL_ERROR "Cannot configure diagnostics for unknown target '${target_name}'")
    endif ()

    set_property(TARGET "${target_name}" PROPERTY COMPILE_WARNING_AS_ERROR
                                                  "${AXIOM_WARNINGS_AS_ERRORS}")
    set_property(TARGET "${target_name}" PROPERTY CXX_EXTENSIONS OFF)

    if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        if (AXIOM_STRICT_LANGUAGE_MODE)
            target_compile_options("${target_name}" PRIVATE /permissive-)
        endif ()
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        if (AXIOM_STRICT_LANGUAGE_MODE)
            target_compile_options("${target_name}" PRIVATE -pedantic-errors)
        endif ()
        if (AXIOM_DISABLE_MSVC_COMPATIBILITY
            AND CMAKE_CXX_COMPILER_ID MATCHES "Clang"
            AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
            target_compile_options("${target_name}" PRIVATE -fno-ms-compatibility)
        endif ()
    endif ()
endfunction ()

#[[Apply selected static analyzers to an Axiom-owned target.]]
function (axiom_configure_static_analyzers target_name)
    if (NOT TARGET "${target_name}")
        message(FATAL_ERROR "Cannot configure analyzers for unknown target '${target_name}'")
    endif ()
    if (NOT AXIOM_STATIC_ANALYZERS)
        return()
    endif ()

    set(axiom_supported_analyzers clang-tidy iwyu cppcheck)
    foreach (axiom_analyzer IN LISTS AXIOM_STATIC_ANALYZERS)
        if (NOT axiom_analyzer IN_LIST axiom_supported_analyzers)
            message(FATAL_ERROR "Unsupported static analyzer '${axiom_analyzer}'")
        endif ()
    endforeach ()

    if ("clang-tidy" IN_LIST AXIOM_STATIC_ANALYZERS)
        find_program(AXIOM_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
        set_property(
            TARGET "${target_name}"
            PROPERTY
                CXX_CLANG_TIDY
                "${AXIOM_CLANG_TIDY_EXECUTABLE};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy")
    endif ()
    if ("iwyu" IN_LIST AXIOM_STATIC_ANALYZERS)
        find_program(AXIOM_IWYU_EXECUTABLE NAMES include-what-you-use REQUIRED)
        set_property(TARGET "${target_name}" PROPERTY CXX_INCLUDE_WHAT_YOU_USE
                                                      "${AXIOM_IWYU_EXECUTABLE}")
    endif ()
    if ("cppcheck" IN_LIST AXIOM_STATIC_ANALYZERS)
        find_program(AXIOM_CPPCHECK_EXECUTABLE NAMES cppcheck REQUIRED)
        # CMake otherwise prints cppcheck diagnostics but lets compilation succeed.
        # A quality gate must propagate any analyzer finding as a build failure.
        set_property(TARGET "${target_name}" PROPERTY CXX_CPPCHECK
                                                   "${AXIOM_CPPCHECK_EXECUTABLE};--error-exitcode=1")
    endif ()
endfunction ()

#[[Apply selected sanitizers to an Axiom-owned target.]]
function (axiom_configure_sanitizers target_name)
    if (NOT TARGET "${target_name}")
        message(FATAL_ERROR "Cannot configure sanitizers for unknown target '${target_name}'")
    endif ()
    if (NOT AXIOM_SANITIZERS)
        return()
    endif ()

    set(axiom_sanitizer_compile_options)
    set(axiom_sanitizer_link_options)
    foreach (axiom_sanitizer IN LISTS AXIOM_SANITIZERS)
        string(TOLOWER "${axiom_sanitizer}" axiom_sanitizer_lower)
        string(TOUPPER "${axiom_sanitizer}" axiom_sanitizer_upper)
        set_sanitizer_options("${axiom_sanitizer_lower}")
        if (NOT SANITIZER_${axiom_sanitizer_upper}_AVAILABLE)
            message(FATAL_ERROR "Sanitizer '${axiom_sanitizer}' is not available for this compiler")
        endif ()
        list(APPEND axiom_sanitizer_compile_options
             "-fsanitize=${SANITIZER_${axiom_sanitizer_upper}_SANITIZER}"
             ${SANITIZER_${axiom_sanitizer_upper}_OPTIONS})
        list(APPEND axiom_sanitizer_link_options
             "-fsanitize=${SANITIZER_${axiom_sanitizer_upper}_SANITIZER}")
    endforeach ()

    test_san_flags(axiom_sanitizer_combination_available axiom_sanitizer_link_options
                   ${axiom_sanitizer_compile_options})
    if (NOT axiom_sanitizer_combination_available)
        message(FATAL_ERROR "Selected sanitizers are not compatible: ${AXIOM_SANITIZERS}")
    endif ()

    target_compile_options("${target_name}" PRIVATE ${axiom_sanitizer_compile_options})
    target_link_options("${target_name}" PRIVATE ${axiom_sanitizer_link_options})
endfunction ()

#[[Configure project-level optional developer tools and integrations.]]
function (axiom_configure_tools)
    # Do not let the legacy cmake-scripts integration restore global sanitizer flags from a build
    # directory configured by an older Axiom revision.
    unset(USE_SANITIZER CACHE)

    # Clear cache variables written by the previous global analyzer integration. Target properties
    # configured above own analyzer activation from now on.
    foreach (axiom_analyzer_variable
             CMAKE_C_CLANG_TIDY CMAKE_CXX_CLANG_TIDY CMAKE_C_INCLUDE_WHAT_YOU_USE
             CMAKE_CXX_INCLUDE_WHAT_YOU_USE CMAKE_C_CPPCHECK CMAKE_CXX_CPPCHECK)
        unset(${axiom_analyzer_variable} CACHE)
    endforeach ()

    if (AXIOM_SANITIZERS)
        include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CPM.cmake")
        cpmaddpackage("gh:StableCoder/cmake-scripts#25.08")
        include("${cmake-scripts_SOURCE_DIR}/sanitizers.cmake")
    endif ()

    if (AXIOM_USE_CCACHE AND PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CPM.cmake")
        cpmaddpackage("gh:TheLartians/Ccache.cmake@1.2.5")
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

#[[Copy the Windows Clang ASan runtime next to sanitizer-enabled executables.]]
function (axiom_configure_sanitizer_runtime target_name)
    if (NOT WIN32 OR NOT "Address" IN_LIST AXIOM_SANITIZERS)
        return()
    endif ()

    get_filename_component(axiom_llvm_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(axiom_llvm_root "${axiom_llvm_bin_dir}" DIRECTORY)
    file(
        GLOB axiom_llvm_runtime_dirs
        LIST_DIRECTORIES true
        "${axiom_llvm_root}/lib/clang/*/lib/windows")
    find_file(
        axiom_asan_runtime
        NAMES clang_rt.asan_dynamic-x86_64.dll
        PATHS ${axiom_llvm_runtime_dirs}
        NO_DEFAULT_PATH)

    if (axiom_asan_runtime)
        add_custom_command(
            TARGET ${target_name}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${axiom_asan_runtime}"
                    "$<TARGET_FILE_DIR:${target_name}>"
            COMMENT "Copying Clang AddressSanitizer runtime"
            VERBATIM)
    else ()
        message(
            WARNING
                "AddressSanitizer is enabled, but clang_rt.asan_dynamic-x86_64.dll was not found")
    endif ()
endfunction ()

#[[CPM.cmake is kept in this framework for future third-party dependencies.]]
