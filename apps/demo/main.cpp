#include <axiom/core/action/action_id.hpp>
#include <axiom/core/action/module.hpp>
#include <axiom/core/action/module_builder.hpp>
#include <axiom/core/action/runtime.hpp>
#include <axiom/core/base/error.hpp>
#include <axiom/core/base/result.hpp>
#include <axiom/core/base/value.hpp>
#include <axiom/core/core.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <optional>
#include <string_view>
#include <utility>

namespace {

using axiom::core::ActionId;
using axiom::core::Arguments;
using axiom::core::Error;
using axiom::core::ErrorCode;
using axiom::core::ModuleBuilder;
using axiom::core::ModuleDescriptor;
using axiom::core::param;
using axiom::core::Result;
using axiom::core::Runtime;
using axiom::core::Value;

axiom::core::Result<double> divideChecked(const double dividend, const double divisor) {
    if(divisor == 0.0) {
        return Result<double>::failure({.code = ErrorCode::InvalidArgument,
                                        .message = "divisor must not be zero",
                                        .path = "divisor",
                                        .details = std::nullopt});
    }
    return Result<double>::success(dividend / divisor);
}

Result<Value>
invoke(const Runtime& runtime, const std::string_view id_text, const Arguments& arguments) {
    const auto id = ActionId::parse(id_text);
    if(!id) {
        return Result<Value>::failure(id.error());
    }
    return runtime.invoke(id.value(), arguments, {});
}

void printError(const Error& error) {
    std::printf("  -> %s", error.message.c_str());
    if(error.path) {
        std::printf(" (at %s)", error.path->c_str());
    }
    std::printf("\n");
}

bool registerMathModule(Runtime& runtime) {
    ModuleBuilder math{
        ModuleDescriptor{.namespace_name = "math", .metadata = {{"title", "Mathematics"}}}};
    const auto prepared =
        math.add(
            "add", "Adds two integers",
            [](const int left, const int right) { return left + right; },
            param("left", "Left operand"), param("right", "Right operand")) &&
        math.add("divide", "Divides a dividend by a divisor", &divideChecked,
                 param("dividend", "Value to divide"), param("divisor", "Value that divides"));
    if(!prepared || !runtime.registerModule(std::move(math))) {
        std::puts("Failed to register the math module");
        return false;
    }
    return true;
}

void printRegisteredActions(const Runtime& runtime) {
    std::puts("Registered actions:");
    for(const auto& action : runtime.discoverActions()) {
        const auto id = action.get().id.str();
        std::printf("  %.*s: %s\n", static_cast<int>(id.size()), id.data(),
                    action.get().description.c_str());
    }
    std::puts("");
}

bool showAddition(const Runtime& runtime) {
    std::puts("math.add(left=20, right=22)");
    const auto sum = invoke(runtime, "math.add", {{"left", Value{20}}, {"right", Value{22}}});
    if(!sum || !sum.value().isInteger()) {
        std::puts("  unexpected invocation failure");
        return false;
    }
    std::printf("  -> %lld\n", static_cast<long long>(sum.value().asInteger()));
    return true;
}

bool showDivision(const Runtime& runtime) {
    std::puts("math.divide(dividend=21.0, divisor=2.0)");
    const auto quotient =
        invoke(runtime, "math.divide", {{"dividend", Value{21.0}}, {"divisor", Value{2.0}}});
    if(!quotient || !quotient.value().isNumber()) {
        std::puts("  unexpected invocation failure");
        return false;
    }
    std::printf("  -> %g\n", quotient.value().asNumber());
    return true;
}

bool showBusinessError(const Runtime& runtime) {
    std::puts("math.divide(dividend=1.0, divisor=0.0)");
    const auto result =
        invoke(runtime, "math.divide", {{"dividend", Value{1.0}}, {"divisor", Value{0.0}}});
    if(result || result.error().code != ErrorCode::InvalidArgument) {
        std::puts("  expected an InvalidArgument business error");
        return false;
    }
    printError(result.error());
    return true;
}

bool showStructuralError(const Runtime& runtime) {
    std::puts("math.add(left=1, right=2, unexpected.key=3)");
    const auto result =
        invoke(runtime, "math.add",
               {{"left", Value{1}}, {"right", Value{2}}, {"unexpected.key", Value{3}}});
    if(result || result.error().code != ErrorCode::UnknownArgument) {
        std::puts("  expected an UnknownArgument structural error");
        return false;
    }
    printError(result.error());
    return true;
}

int runDemo() {
    std::printf("%s core demo\n\n", axiom::core::frameworkName());
    Runtime runtime;
    if(!registerMathModule(runtime)) {
        return EXIT_FAILURE;
    }
    printRegisteredActions(runtime);
    const auto scenarios_ok = showAddition(runtime) && showDivision(runtime) &&
                              showBusinessError(runtime) && showStructuralError(runtime);
    if(!scenarios_ok) {
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
