#include "answer.hpp"

#include <memory>

int main() {
    auto value = std::make_unique<int>(answer());
    const int* dangling = value.get();
    value.reset();
    return *dangling;
}
