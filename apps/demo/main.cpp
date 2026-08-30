#include "action_demo.hpp"
#include "base_demo.hpp"
#include "logging_demo.hpp"
#include "resource_demo.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>

namespace {

int runDemo() {
    if(!axiom::demo::runBaseDemo()) {
        return EXIT_FAILURE;
    }

    axiom::demo::LoggingDemo logging;
    if(!axiom::demo::runActionDemo(logging.runtimeLogger()) || !axiom::demo::runResourceDemo() ||
       !logging.run()) {
        return EXIT_FAILURE;
    }
    std::puts("\nDemo finished successfully");
    return EXIT_SUCCESS;
}

} // namespace

int main() {
    try {
        return runDemo();
    } catch(const std::exception& exception) {
        std::printf("Demo failed: %s\n", exception.what());
        return EXIT_FAILURE;
    } catch(...) {
        std::puts("Demo failed with an unexpected exception");
        return EXIT_FAILURE;
    }
}
