#include "descriptor_conversion.hpp"

#include <axiom/action/descriptor.hpp>
#include <axiom/action/module.hpp>
#include <axiom/command/error_code_name.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/type_descriptor.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/introspection/runtime_snapshot.hpp>
#include <axiom/resource/resource_descriptor.hpp>
#include <axiom/task/task_types.hpp>

#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace axiom::command::detail {
namespace {

[[nodiscard]] std::string_view typeKindName(const TypeDescriptor::Kind kind) {
    switch(kind) {
    case TypeDescriptor::Kind::Null:
        return "null";
    case TypeDescriptor::Kind::Boolean:
        return "boolean";
    case TypeDescriptor::Kind::Integer:
        return "integer";
    case TypeDescriptor::Kind::Number:
        return "number";
    case TypeDescriptor::Kind::String:
        return "string";
    case TypeDescriptor::Kind::Array:
        return "array";
    case TypeDescriptor::Kind::Object:
        return "object";
    }
    throw std::logic_error{"Unknown TypeDescriptor kind"};
}

[[nodiscard]] Value encodeStringArray(const std::vector<std::string>& items) {
    Value::Array array;
    array.reserve(items.size());
    std::ranges::transform(items, std::back_inserter(array),
                           [](const std::string& item) { return Value{item}; });
    return Value{std::move(array)};
}

[[nodiscard]] Value encodeStringMap(const std::map<std::string, std::string, std::less<>>& items) {
    Value::Object object;
    for(const auto& [key, value] : items) {
        object.emplace(key, Value{value});
    }
    return Value{std::move(object)};
}

// NOLINTBEGIN(misc-no-recursion): TypeDescriptor is a recursive public value shape.
[[nodiscard]] Value encodeTypeFields(const TypeDescriptor::Fields& fields) {
    Value::Object object;
    for(const auto& [name, nested] : fields) {
        if(!nested) {
            throw std::logic_error{"Object TypeDescriptor field is missing"};
        }
        object.emplace(name, encodeTypeDescriptor(*nested));
    }
    return Value{std::move(object)};
}

void encodeArrayShape(Value::Object& object, const TypeDescriptor& type) {
    if(!type.element_type) {
        throw std::logic_error{"Array TypeDescriptor is missing element_type"};
    }
    object.emplace("element_type", encodeTypeDescriptor(*type.element_type));
}

void encodeObjectShape(Value::Object& object, const TypeDescriptor& type) {
    if(type.value_type && !type.fields.empty()) {
        throw std::logic_error{"Object TypeDescriptor has both fields and value_type"};
    }
    if(type.value_type) {
        object.emplace("value_type", encodeTypeDescriptor(*type.value_type));
        return;
    }
    object.emplace("fields", encodeTypeFields(type.fields));
}

void encodeTypeShape(Value::Object& object, const TypeDescriptor& type) {
    if(type.kind == TypeDescriptor::Kind::Array) {
        encodeArrayShape(object, type);
        return;
    }
    if(type.kind == TypeDescriptor::Kind::Object) {
        encodeObjectShape(object, type);
    }
}

[[nodiscard]] Value encodeProgress(const task::Progress& progress) {
    return Value{
        Value::Object{{"message", Value{progress.message}}, {"value", Value{progress.value}}}};
}

[[nodiscard]] Value encodeOrigin(const task::TaskOrigin& origin) {
    return Value{Value::Object{{"action_id", Value{origin.action_id}},
                               {"caller", Value{origin.caller}},
                               {"metadata", encodeStringMap(origin.metadata)},
                               {"request_id", Value{origin.request_id}},
                               {"trace_id", Value{origin.trace_id}}}};
}

template <typename Item, typename Encoder>
[[nodiscard]] Value encodeList(const std::vector<Item>& items, Encoder encoder) {
    Value::Array array;
    array.reserve(items.size());
    std::ranges::transform(items, std::back_inserter(array), encoder);
    return Value{std::move(array)};
}

} // namespace

std::string_view taskStateName(const task::TaskState state) {
    switch(state) {
    case task::TaskState::Pending:
        return "pending";
    case task::TaskState::Running:
        return "running";
    case task::TaskState::Completed:
        return "completed";
    case task::TaskState::Failed:
        return "failed";
    case task::TaskState::Cancelled:
        return "cancelled";
    }
    throw std::logic_error{"Unknown TaskState enumerator"};
}

Value encodeError(const Error& error) {
    Value::Object object;
    object.emplace("code", Value{std::string{errorCodeName(error.code)}});
    if(error.details.has_value()) {
        object.emplace("details", *error.details);
    }
    object.emplace("message", Value{error.message});
    if(error.path.has_value()) {
        object.emplace("path", Value{*error.path});
    }
    return Value{std::move(object)};
}

Value encodeTypeDescriptor(const TypeDescriptor& type) {
    Value::Object object;
    object.emplace("description", Value{type.description});
    object.emplace("kind", Value{std::string{typeKindName(type.kind)}});
    object.emplace("nullable", Value{type.nullable});
    encodeTypeShape(object, type);
    return Value{std::move(object)};
}
// NOLINTEND(misc-no-recursion)

Value encodeParameterDescriptor(const ParameterDescriptor& parameter) {
    Value::Object object;
    if(parameter.default_value.has_value()) {
        object.emplace("default", *parameter.default_value);
    }
    object.emplace("description", Value{parameter.description});
    object.emplace("name", Value{parameter.name});
    object.emplace("required", Value{parameter.required});
    object.emplace("type", encodeTypeDescriptor(parameter.type));
    return Value{std::move(object)};
}

Value encodeModuleDescriptor(const ModuleDescriptor& module) {
    Value::Object object;
    object.emplace("description", Value{module.description});
    object.emplace("metadata", encodeStringMap(module.metadata));
    object.emplace("namespace", Value{module.namespace_name});
    object.emplace("tags", encodeStringArray(module.tags));
    if(module.version.has_value()) {
        object.emplace("version", Value{*module.version});
    }
    return Value{std::move(object)};
}

Value encodeActionDescriptor(const ActionDescriptor& action) {
    Value::Object object;
    object.emplace("description", Value{action.description});
    object.emplace("id", Value{std::string{action.id.str()}});
    object.emplace("metadata", encodeStringMap(action.metadata));
    object.emplace("parameters",
                   encodeList(action.parameters, [](const ParameterDescriptor& parameter) {
                       return encodeParameterDescriptor(parameter);
                   }));
    object.emplace("return_type", encodeTypeDescriptor(action.return_type));
    object.emplace("tags", encodeStringArray(action.tags));
    if(action.version.has_value()) {
        object.emplace("version", Value{*action.version});
    }
    return Value{std::move(object)};
}

Value encodeResourceDescriptor(const resource::ResourceDescriptor& resource) {
    return Value{Value::Object{{"id", Value{std::string{resource.id.str()}}},
                               {"type", Value{resource.type}}}};
}

Value encodeTaskDescriptor(const task::TaskDescriptor& task) {
    Value::Object object;
    if(task.error.has_value()) {
        object.emplace("error", encodeError(*task.error));
    }
    object.emplace("id", Value{std::string{task.id.str()}});
    object.emplace("name", Value{task.name});
    if(task.origin.has_value()) {
        object.emplace("origin", encodeOrigin(*task.origin));
    }
    object.emplace("progress", encodeProgress(task.progress));
    object.emplace("state", Value{std::string{taskStateName(task.state)}});
    return Value{std::move(object)};
}

Value encodeModuleDescriptors(const std::vector<ModuleDescriptor>& modules) {
    return encodeList(modules, encodeModuleDescriptor);
}

Value encodeActionDescriptors(const std::vector<ActionDescriptor>& actions) {
    return encodeList(actions, encodeActionDescriptor);
}

Value encodeResourceDescriptors(const std::vector<resource::ResourceDescriptor>& resources) {
    return encodeList(resources, encodeResourceDescriptor);
}

Value encodeTaskDescriptors(const std::vector<task::TaskDescriptor>& tasks) {
    return encodeList(tasks, encodeTaskDescriptor);
}

Value encodeSnapshot(const introspection::RuntimeSnapshot& snapshot) {
    return Value{Value::Object{{"actions", encodeActionDescriptors(snapshot.actions)},
                               {"modules", encodeModuleDescriptors(snapshot.modules)},
                               {"resources", encodeResourceDescriptors(snapshot.resources)},
                               {"tasks", encodeTaskDescriptors(snapshot.tasks)}}};
}

} // namespace axiom::command::detail
