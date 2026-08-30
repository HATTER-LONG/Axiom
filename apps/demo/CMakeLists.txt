add_executable(testlib_demo main.cpp)
testlib_configure_target(testlib_demo)
target_link_libraries(testlib_demo PRIVATE Testlib::Core)
testlib_deploy_runtime(testlib_demo)
