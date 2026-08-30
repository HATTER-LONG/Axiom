#include <axiom/core/base/error.hpp>
#include <axiom/core/base/result.hpp>
#include <axiom/core/base/type_descriptor.hpp>

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace axiom::core {
namespace {

[[nodiscard]] Result<void> invalidDescriptor(std::string message,
                                             std::optional<std::string> path = {}) {
    return Result<void>::failure({
        .code = ErrorCode::InvalidDescriptor,
        .message = std::move(message),
        .path = std::move(path),
        .details = std::nullopt,
    });
}

[[nodiscard]] bool isIdentifier(const std::string& name) noexcept {
    return !name.empty() && std::ranges::all_of(name, [](const char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
               character == '_';
    });
}

[[nodiscard]] bool isKnownKind(const TypeDescriptor::Kind kind) noexcept {
    switch(kind) {
    case TypeDescriptor::Kind::Null:
    case TypeDescriptor::Kind::Boolean:
    case TypeDescriptor::Kind::Integer:
    case TypeDescriptor::Kind::Number:
    case TypeDescriptor::Kind::String:
    case TypeDescriptor::Kind::Array:
    case TypeDescriptor::Kind::Object:
        return true;
    }
    return false;
}

[[nodiscard]] Result<void> validateArray(const TypeDescriptor& descriptor,
                                         std::vector<const TypeDescriptor*>& children) {
    if(!descriptor.element_type) {
        return invalidDescriptor("Array type descriptor requires an element type");
    }
    if(!descriptor.fields.empty() || descriptor.value_type) {
        return invalidDescriptor("Array type descriptor cannot declare object members");
    }
    children.push_back(descriptor.element_type.get());
    return Result<void>::success();
}

[[nodiscard]] Result<void> validateObject(const TypeDescriptor& descriptor,
                                          std::vector<const TypeDescriptor*>& children) {
    if(!descriptor.fields.empty() && descriptor.value_type) {
        return invalidDescriptor("Object type descriptor cannot mix fixed fields and a value type");
    }
    for(const auto& [name, field] : descriptor.fields) {
        if(!field || !isIdentifier(name)) {
            return invalidDescriptor("Object fields require a type and a name matching [a-z0-9_]+",
                                     name);
        }
        children.push_back(field.get());
    }
    if(descriptor.value_type) {
        children.push_back(descriptor.value_type.get());
    }
    return Result<void>::success();
}

[[nodiscard]] Result<void> validateNode(const TypeDescriptor& descriptor,
                                        std::vector<const TypeDescriptor*>& children) {
    if(!isKnownKind(descriptor.kind)) {
        return invalidDescriptor("Type descriptor kind is not supported");
    }
    if(descriptor.kind == TypeDescriptor::Kind::Array) {
        return validateArray(descriptor, children);
    }
    if(descriptor.element_type) {
        return invalidDescriptor("Only an array type descriptor can declare an element type");
    }
    if(descriptor.kind == TypeDescriptor::Kind::Object) {
        return validateObject(descriptor, children);
    }
    if(!descriptor.fields.empty() || descriptor.value_type) {
        return invalidDescriptor("Only an object type descriptor can declare object members");
    }
    return Result<void>::success();
}

enum class VisitState : unsigned char { Visiting, Visited };

struct PendingNode {
    const TypeDescriptor* descriptor;
    bool finish;
};

} // namespace

Result<void> validate(const TypeDescriptor& descriptor) {
    // Iterative DFS with Visiting/Visited marks detects cycles without recursion.
    std::unordered_map<const TypeDescriptor*, VisitState> visits;
    std::vector<PendingNode> pending{{.descriptor = &descriptor, .finish = false}};
    while(!pending.empty()) {
        const PendingNode node = pending.back();
        pending.pop_back();
        if(node.finish) {
            visits.at(node.descriptor) = VisitState::Visited;
            continue;
        }
        const auto existing = visits.find(node.descriptor);
        if(existing != visits.end()) {
            if(existing->second == VisitState::Visiting) {
                return invalidDescriptor("Type descriptor contains a recursive cycle");
            }
            continue;
        }
        visits.emplace(node.descriptor, VisitState::Visiting);
        std::vector<const TypeDescriptor*> children;
        const auto result = validateNode(*node.descriptor, children);
        if(!result) {
            return result;
        }
        // Schedule finish after children so cycle detection spans the whole subtree.
        pending.push_back({.descriptor = node.descriptor, .finish = true});
        std::ranges::transform(children | std::views::reverse, std::back_inserter(pending),
                               [](const TypeDescriptor* child) {
                                   return PendingNode{.descriptor = child, .finish = false};
                               });
    }
    return Result<void>::success();
}

} // namespace axiom::core
