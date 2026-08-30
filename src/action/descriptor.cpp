#include <axiom/action/descriptor.hpp>
#include <axiom/action/module.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/type_descriptor.hpp>
#include <axiom/foundation/value.hpp>

#include <algorithm>
#include <functional>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace axiom {
namespace {

[[nodiscard]] bool isIdentifier(const std::string_view text) noexcept {
    return !text.empty() && std::ranges::all_of(text, [](const char character) noexcept {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
               character == '_';
    });
}

[[nodiscard]] Result<void> invalidDescriptor(std::string message,
                                             std::optional<std::string> path = {}) {
    return Result<void>::failure({
        .code = ErrorCode::InvalidDescriptor,
        .message = std::move(message),
        .path = std::move(path),
        .details = std::nullopt,
    });
}

[[nodiscard]] std::optional<Value::Type> scalarValueType(const TypeDescriptor::Kind kind) noexcept {
    switch(kind) {
    case TypeDescriptor::Kind::Boolean:
        return Value::Type::Boolean;
    case TypeDescriptor::Kind::Integer:
        return Value::Type::Integer;
    case TypeDescriptor::Kind::Number:
        return Value::Type::Number;
    case TypeDescriptor::Kind::String:
        return Value::Type::String;
    case TypeDescriptor::Kind::Null:
    case TypeDescriptor::Kind::Array:
    case TypeDescriptor::Kind::Object:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] bool matchesScalar(const Value& value, const TypeDescriptor& descriptor) noexcept {
    if(value.isNull()) {
        return descriptor.nullable || descriptor.kind == TypeDescriptor::Kind::Null;
    }
    const auto expected_type = scalarValueType(descriptor.kind);
    return expected_type &&
           (value.type() == *expected_type ||
            (descriptor.kind == TypeDescriptor::Kind::Number && value.isInteger()));
}

void queueArrayDefaults(const Value::Array& values,
                        const TypeDescriptor& element_type,
                        std::vector<std::pair<const Value*, const TypeDescriptor*>>& pending) {
    std::ranges::for_each(values, [&element_type, &pending](const Value& element) {
        pending.emplace_back(&element, &element_type);
    });
}

[[nodiscard]] bool
queueObjectDefaults(const Value::Object& values,
                    const TypeDescriptor& descriptor,
                    std::vector<std::pair<const Value*, const TypeDescriptor*>>& pending) {
    if(descriptor.value_type) {
        std::ranges::for_each(values, [&descriptor, &pending](const auto& member) {
            pending.emplace_back(&member.second, descriptor.value_type.get());
        });
        return true;
    }
    if(values.size() != descriptor.fields.size()) {
        return false;
    }
    for(const auto& [name, field] : descriptor.fields) {
        const auto member = values.find(name);
        if(member == values.end()) {
            return false;
        }
        pending.emplace_back(&member->second, field.get());
    }
    return true;
}

[[nodiscard]] bool
queueDefaultChildren(const Value& value,
                     const TypeDescriptor& descriptor,
                     std::vector<std::pair<const Value*, const TypeDescriptor*>>& pending) {
    if(descriptor.kind == TypeDescriptor::Kind::Array) {
        if(!value.isArray()) {
            return false;
        }
        queueArrayDefaults(value.asArray(), *descriptor.element_type, pending);
        return true;
    }
    if(descriptor.kind != TypeDescriptor::Kind::Object || !value.isObject()) {
        return false;
    }
    return queueObjectDefaults(value.asObject(), descriptor, pending);
}

[[nodiscard]] bool matchesDefault(const Value& value, const TypeDescriptor& descriptor) {
    std::vector<std::pair<const Value*, const TypeDescriptor*>> pending{{&value, &descriptor}};
    while(!pending.empty()) {
        const auto [candidate, expected] = pending.back();
        pending.pop_back();
        if(candidate->isNull() || (expected->kind != TypeDescriptor::Kind::Array &&
                                   expected->kind != TypeDescriptor::Kind::Object)) {
            if(!matchesScalar(*candidate, *expected)) {
                return false;
            }
        } else if(!queueDefaultChildren(*candidate, *expected, pending)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Result<void> validateParameters(const std::vector<ParameterDescriptor>& parameters) {
    std::set<std::string, std::less<>> names;
    for(const ParameterDescriptor& parameter : parameters) {
        if(!isIdentifier(parameter.name)) {
            return invalidDescriptor("Parameter names must match [a-z0-9_]+", parameter.name);
        }
        if(!names.insert(parameter.name).second) {
            return invalidDescriptor("Action parameter names must be unique", parameter.name);
        }
        auto parameter_type = validate(parameter.type);
        if(!parameter_type) {
            return parameter_type;
        }
        if(parameter.required && parameter.default_value) {
            return invalidDescriptor("Required parameters cannot declare a default value",
                                     parameter.name);
        }
        if(parameter.default_value && !matchesDefault(*parameter.default_value, parameter.type)) {
            return invalidDescriptor("Parameter default value does not match its type",
                                     parameter.name);
        }
    }
    return Result<void>::success();
}

} // namespace

Result<void> validate(const ActionDescriptor& descriptor) {
    auto result_type = validate(descriptor.return_type);
    if(!result_type) {
        return result_type;
    }
    if(descriptor.version && descriptor.version->empty()) {
        return invalidDescriptor("Action version cannot be empty");
    }
    return validateParameters(descriptor.parameters);
}

Result<void> validate(const ModuleDescriptor& descriptor) {
    if(!isIdentifier(descriptor.namespace_name)) {
        return invalidDescriptor("Module namespace must match [a-z0-9_]+",
                                 descriptor.namespace_name);
    }
    return Result<void>::success();
}

} // namespace axiom
