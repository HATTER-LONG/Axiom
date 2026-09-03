include_guard(GLOBAL)

# Runtime deployment is private to executables. The generated script handles an empty DLL list and
# avoids copy -t, which is unavailable in CMake 3.25. python3XX.dll is left on PATH: copying it
# next to a host exe makes CPython treat that directory as prefix and miss the real stdlib.
function (axiom_deploy_runtime target_name)
    if (NOT WIN32)
        return()
    endif ()
    set(runtime_files "$<TARGET_RUNTIME_DLLS:${target_name}>")
    if ("Address" IN_LIST AXIOM_SANITIZERS)
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" --print-runtime-dir
            OUTPUT_VARIABLE runtime_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
        file(TO_CMAKE_PATH "${runtime_dir}" runtime_dir)
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" -print-target-triple
            OUTPUT_VARIABLE triple
            OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
        if (triple MATCHES "^(x86_64|amd64)-")
            set(architecture x86_64)
        elseif (triple MATCHES "^(aarch64|arm64)-")
            set(architecture aarch64)
        elseif (triple MATCHES "^i[3-6]86-")
            set(architecture i386)
        else ()
            message(FATAL_ERROR "Unsupported Windows ASan target: ${triple}")
        endif ()
        set(asan_runtime "${runtime_dir}/clang_rt.asan_dynamic-${architecture}.dll")
        if (NOT EXISTS "${asan_runtime}")
            message(FATAL_ERROR "Windows ASan runtime is missing: ${asan_runtime}")
        endif ()
        list(APPEND runtime_files "${asan_runtime}")
    endif ()
    set(script "${CMAKE_CURRENT_BINARY_DIR}/${target_name}-runtime-$<CONFIG>.cmake")
    file(
        GENERATE
        OUTPUT "${script}"
        CONTENT
            "set(runtime_files \"${runtime_files}\")\nlist(REMOVE_ITEM runtime_files \"\")\nlist(FILTER runtime_files EXCLUDE REGEX \"[Pp]ython[0-9]*\\\\.dll$\")\nforeach(runtime IN LISTS runtime_files)\n  execute_process(COMMAND \"${CMAKE_COMMAND}\" -E copy_if_different \"\${runtime}\" \"$<TARGET_FILE_DIR:${target_name}>\" COMMAND_ERROR_IS_FATAL ANY)\nendforeach()\n"
    )
    add_custom_command(
        TARGET ${target_name}
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -P "${script}"
        VERBATIM)
    set(remove_python_dlls "${CMAKE_CURRENT_BINARY_DIR}/${target_name}-remove-python-dlls-$<CONFIG>.cmake")
    file(
        GENERATE
        OUTPUT "${remove_python_dlls}"
        CONTENT
            "file(GLOB _stale \"$<TARGET_FILE_DIR:${target_name}>/python*.dll\")\nif (_stale)\n  file(REMOVE \${_stale})\nendif ()\n")
    add_custom_command(
        TARGET ${target_name}
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -P "${remove_python_dlls}"
        VERBATIM)
endfunction ()
