#include <axiom/axiom.hpp>

#include <iostream>

int main() {
    try {
        std::cout << axiom::frameworkName() << '\n';
        return 0;
    } catch(...) {
        return 1;
    }
}
