add_executable(axiom_demo main.cpp)
target_compile_features(axiom_demo PRIVATE cxx_std_20)
target_link_libraries(axiom_demo PRIVATE Axiom::Core)
