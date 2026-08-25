cmake_minimum_required(VERSION 3.25)

project(AxiomInstallConsumer LANGUAGES CXX)

include(CTest)

find_package(Axiom CONFIG REQUIRED)

add_executable(axiom_install_consumer main.cpp)
target_link_libraries(axiom_install_consumer PRIVATE Axiom::Core)

add_test(NAME axiom.install.consumer COMMAND axiom_install_consumer)
