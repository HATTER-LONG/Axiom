include_guard(GLOBAL)

# SPDX-License-Identifier: MIT
#
# SPDX-FileCopyrightText: Copyright (c) 2019-2023 Lars Melchior and contributors

set(CPM_DOWNLOAD_VERSION 0.42.3)
set(CPM_HASH_SUM "a609e875fd532b067174250f6abbc3dac22fe2d64869783fb1e80bda1625c844")

# Respect explicit CMake and environment configuration. HOME is not required on Windows.
if (NOT DEFINED CPM_SOURCE_CACHE AND NOT DEFINED ENV{CPM_SOURCE_CACHE})
    if (WIN32 AND DEFINED ENV{LOCALAPPDATA})
        set(CPM_SOURCE_CACHE
            "$ENV{LOCALAPPDATA}/CPM"
            CACHE PATH "Persistent dependency source cache")
    elseif (DEFINED ENV{XDG_CACHE_HOME})
        set(CPM_SOURCE_CACHE
            "$ENV{XDG_CACHE_HOME}/CPM"
            CACHE PATH "Persistent dependency source cache")
    elseif (DEFINED ENV{HOME})
        set(CPM_SOURCE_CACHE
            "$ENV{HOME}/.cache/CPM"
            CACHE PATH "Persistent dependency source cache")
    endif ()
endif ()

if (CPM_SOURCE_CACHE)
    set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
elseif (DEFINED ENV{CPM_SOURCE_CACHE})
    set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
else ()
    set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif ()

# Expand relative path. This is important if the provided path contains a tilde (~)
get_filename_component(CPM_DOWNLOAD_LOCATION ${CPM_DOWNLOAD_LOCATION} ABSOLUTE)

file(DOWNLOAD
     https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
     ${CPM_DOWNLOAD_LOCATION} EXPECTED_HASH SHA256=${CPM_HASH_SUM})

include(${CPM_DOWNLOAD_LOCATION})
