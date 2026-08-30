add_executable(axiom_demo main.cpp action_demo.cpp base_demo.cpp demo_output.cpp logging_demo.cpp
                          resource_demo.cpp)
axiom_configure_target(axiom_demo)
target_link_libraries(axiom_demo PRIVATE Axiom::Core)
axiom_deploy_runtime(axiom_demo)
