#include <axiom/core/core.hpp>

#include <cstdio>
#include <limits>
#include <memory>
#include <string_view>

static void runMemoryErrorTest() {
    auto value = std::make_unique<int>(42);
    const int* dangling_value = value.get();
    value.reset();

    // Intentional heap-use-after-free. Run this path only under ASan.
    std::printf("dangling value: %d\n", *dangling_value);
}

static void runUndefinedBehaviorTest() {
    const volatile int value = std::numeric_limits<int>::max();
    const int overflowed_value = value + 1;
    std::printf("overflowed value: %d\n", overflowed_value);
}

static void runCppcheckNullPointerTest() {
    int* value = nullptr;

    // Intentional null-pointer dereference for cppcheck verification.
    std::printf("null value: %d\n", *value);
}

static void runCppcheckUninitializedValueTest() {
    int value;

    // Intentional use of an uninitialized value for cppcheck verification.
    std::printf("uninitialized value: %d\n", value);
}

int main(int argc, char* argv[]) {
    if(argc > 1 && std::string_view(argv[1]) == "--memory-error") {
        runMemoryErrorTest();
        return 0;
    }
    if(argc > 1 && std::string_view(argv[1]) == "--ubsan-error") {
        runUndefinedBehaviorTest();
        return 0;
    }
    if(argc > 1 && std::string_view(argv[1]) == "--cppcheck-null-pointer") {
        runCppcheckNullPointerTest();
        return 0;
    }
    if(argc > 1 && std::string_view(argv[1]) == "--cppcheck-uninitialized") {
        runCppcheckUninitializedValueTest();
        return 0;
    }

    return std::puts("Axiom demo") == EOF;
}
