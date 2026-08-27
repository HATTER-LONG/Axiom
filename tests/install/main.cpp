#include <axiom/core/core.hpp>
#include <axiom/runtime/runtime.hpp>

#include <cstdlib>
#include <string>

namespace {

bool runtimeConsumerWorks() {
    axiom::runtime::Runtime runtime;
    axiom::runtime::ModuleBuilder math = runtime.module("math", "Install smoke test");
    if(!math.action(
                "add", "Add two numbers",
                [](double first, double second) { return first + second; },
                axiom::runtime::param<double>("a"), axiom::runtime::param<double>("b"))
            .hasValue()) {
        return false;
    }

    const axiom::runtime::Result<axiom::runtime::Value> result =
        runtime.invoke("math.add", axiom::runtime::Arguments{{"a", axiom::runtime::Value{10.0}},
                                                             {"b", axiom::runtime::Value{20.0}}});
    return result.hasValue() && result.value().isNumber() && result.value().asNumber() == 30.0;
}

} // namespace

int main() {
    if(std::string{axiom::core::frameworkName()} != "Axiom") {
        return EXIT_FAILURE;
    }
    return runtimeConsumerWorks() ? EXIT_SUCCESS : EXIT_FAILURE;
}
