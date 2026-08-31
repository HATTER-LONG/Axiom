#include <axiom/axiom.hpp>

#include "task_demo.hpp"

#include <iostream>

int main() {
    try {
        std::cout << axiom::frameworkName() << '\n';
        return runTaskDemo() ? 0 : 1;
    } catch(...) {
        return 1;
    }
}
