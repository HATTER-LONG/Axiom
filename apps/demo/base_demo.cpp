#include "base_demo.hpp"

#include <axiom/core/core.hpp>

#include <cstdio>

namespace axiom::demo {
namespace {

using core::Value;

bool showFrameworkIdentity() {
    const auto* name = axiom::core::frameworkName();
    std::printf("%s core walkthrough\n\n", name);
    if(!axiom::core::isFrameworkName(name)) {
        std::puts("frameworkName() and isFrameworkName() disagree");
        return false;
    }
    return true;
}

bool showDynamicValue() {
    std::puts("Value (JSON-shaped dynamic data):");
    const Value sample{Value::Object{
        {"enabled", Value{true}},
        {"items", Value{Value::Array{Value{1}, Value{2}}}},
        {"label", Value{"core"}},
    }};
    if(!sample.isObject()) {
        return false;
    }
    const auto& items = sample.asObject().at("items").asArray();
    std::printf("  fields=%zu items[1]=%lld label=%s\n\n", sample.asObject().size(),
                static_cast<long long>(items.at(1).asInteger()),
                sample.asObject().at("label").asString().c_str());
    return true;
}

} // namespace

bool runBaseDemo() { return showFrameworkIdentity() && showDynamicValue(); }

} // namespace axiom::demo
