#include "answer.hpp"

#include <limits>

int main() {
    const volatile int maximum = std::numeric_limits<int>::max();
    return maximum + answer();
}
