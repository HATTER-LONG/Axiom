add_library(testlib_core src/core.cpp)
add_library(Testlib::Core ALIAS testlib_core)
testlib_configure_target(testlib_core)
target_compile_features(testlib_core PUBLIC cxx_std_20)
target_include_directories(
    testlib_core PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
                               "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
find_package(Threads REQUIRED)
target_link_libraries(testlib_core PRIVATE Threads::Threads)
set_target_properties(
    testlib_core
    PROPERTIES EXPORT_NAME Core
               POSITION_INDEPENDENT_CODE ON
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN ON
               VERSION "${PROJECT_VERSION}"
               SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
if (BUILD_SHARED_LIBS)
    target_compile_definitions(
        testlib_core
        PRIVATE TESTLIB_CORE_BUILD_SHARED
        INTERFACE TESTLIB_CORE_USE_SHARED)
endif ()
