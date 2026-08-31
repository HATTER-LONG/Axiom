add_executable(axiom_demo main.cpp task_demo.cpp)
axiom_configure_target(axiom_demo)
target_link_libraries(axiom_demo PRIVATE Axiom::Axiom)
axiom_deploy_runtime(axiom_demo)
