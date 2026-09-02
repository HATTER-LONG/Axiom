#include "command_validation.hpp"

#include <axiom/action/detail/value_converter.hpp>
#include <axiom/command/command_methods.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/task/task_types.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace axiom::command::detail {
namespace {

using axiom::detail::appendArrayPath;
using axiom::detail::appendObjectPath;
using axiom::detail::value_converter_detail::typeMismatch;

struct MethodEntry {
    std::string_view name{}; // NOLINT(readability-redundant-member-init)
    CommandMethod method{};
};

constexpr std::array method_entries{
    MethodEntry{.name = action_list, .method = CommandMethod::ActionList},
    MethodEntry{.name = action_describe, .method = CommandMethod::ActionDescribe},
    MethodEntry{.name = action_invoke, .method = CommandMethod::ActionInvoke},
    MethodEntry{.name = resource_list, .method = CommandMethod::ResourceList},
    MethodEntry{.name = resource_describe, .method = CommandMethod::ResourceDescribe},
    MethodEntry{.name = task_list, .method = CommandMethod::TaskList},
    MethodEntry{.name = task_describe, .method = CommandMethod::TaskDescribe},
    MethodEntry{.name = task_cancel, .method = CommandMethod::TaskCancel},
    MethodEntry{.name = system_snapshot, .method = CommandMethod::SystemSnapshot},
};

constexpr std::array action_list_fields{CommandField{.name = "module", .required = false},
                                        CommandField{.name = "tags", .required = false}};
constexpr std::array action_describe_fields{CommandField{.name = "action", .required = true}};
constexpr std::array action_invoke_fields{CommandField{.name = "action", .required = true},
                                          CommandField{.name = "arguments", .required = true}};
constexpr std::array resource_list_fields{CommandField{.name = "type", .required = false}};
constexpr std::array resource_describe_fields{CommandField{.name = "resource", .required = true}};
constexpr std::array task_list_fields{CommandField{.name = "state", .required = false},
                                      CommandField{.name = "origin_action", .required = false},
                                      CommandField{.name = "origin_request", .required = false}};
constexpr std::array task_describe_fields{CommandField{.name = "task", .required = true}};
constexpr std::array task_cancel_fields{CommandField{.name = "task", .required = true}};

[[nodiscard]] Error unknownCommandError(const std::string_view method) {
    return {.code = ErrorCode::UnknownCommand,
            .message = "Unknown command: " + std::string{method},
            .path = std::nullopt,
            .details = std::nullopt};
}

[[nodiscard]] Error missingFieldError(const std::string_view name) {
    return {.code = ErrorCode::MissingArgument,
            .message = "Required argument is missing: " + std::string{name},
            .path = std::string{name},
            .details = std::nullopt};
}

[[nodiscard]] Error unknownFieldError(const std::string_view name) {
    return {.code = ErrorCode::UnknownArgument,
            .message = "Unknown argument: " + std::string{name},
            .path = appendObjectPath({}, name),
            .details = std::nullopt};
}

[[nodiscard]] bool isKnownField(const std::span<const CommandField> fields,
                                const std::string_view name) noexcept {
    return std::ranges::any_of(
        fields, [name](const CommandField& field) noexcept { return field.name == name; });
}

[[nodiscard]] const Value* findField(const Value::Object& params, const std::string_view name) {
    const auto found = params.find(name);
    if(found == params.end()) {
        return nullptr;
    }
    return &found->second;
}

[[nodiscard]] Result<std::vector<std::string>> readStringArray(const Value& value,
                                                               const std::string_view path) {
    if(!value.isArray()) {
        return Result<std::vector<std::string>>::failure(typeMismatch(path, "array", value.type()));
    }
    std::vector<std::string> items;
    items.reserve(value.asArray().size());
    std::size_t index = 0;
    for(const auto& element : value.asArray()) {
        if(!element.isString()) {
            return Result<std::vector<std::string>>::failure(
                typeMismatch(appendArrayPath(path, index), "string", element.type()));
        }
        items.push_back(element.asString());
        ++index;
    }
    return Result<std::vector<std::string>>::success(std::move(items));
}

[[nodiscard]] Result<task::TaskState> parseTaskStateText(const std::string_view text) {
    if(text == "pending") {
        return Result<task::TaskState>::success(task::TaskState::Pending);
    }
    if(text == "running") {
        return Result<task::TaskState>::success(task::TaskState::Running);
    }
    if(text == "completed") {
        return Result<task::TaskState>::success(task::TaskState::Completed);
    }
    if(text == "failed") {
        return Result<task::TaskState>::success(task::TaskState::Failed);
    }
    if(text == "cancelled") {
        return Result<task::TaskState>::success(task::TaskState::Cancelled);
    }
    return Result<task::TaskState>::failure(
        {.code = ErrorCode::InvalidArgument,
         .message = "Task state must be pending, running, completed, failed, or cancelled: " +
                    std::string{text},
         .path = std::nullopt,
         .details = std::nullopt});
}

} // namespace

Result<CommandMethod> parseCommandMethod(const std::string_view method) {
    const auto found = std::ranges::find_if(
        method_entries, [method](const MethodEntry& entry) { return entry.name == method; });
    if(found != method_entries.end()) {
        return Result<CommandMethod>::success(found->method);
    }
    return Result<CommandMethod>::failure(unknownCommandError(method));
}

std::span<const CommandField> commandFields(const CommandMethod method) {
    switch(method) {
    case CommandMethod::ActionList:
        return action_list_fields;
    case CommandMethod::ActionDescribe:
        return action_describe_fields;
    case CommandMethod::ActionInvoke:
        return action_invoke_fields;
    case CommandMethod::ResourceList:
        return resource_list_fields;
    case CommandMethod::ResourceDescribe:
        return resource_describe_fields;
    case CommandMethod::TaskList:
        return task_list_fields;
    case CommandMethod::TaskDescribe:
        return task_describe_fields;
    case CommandMethod::TaskCancel:
        return task_cancel_fields;
    case CommandMethod::SystemSnapshot:
        return {};
    }
    throw std::logic_error{"Unknown CommandMethod enumerator"};
}

Result<void> validateCommandParams(const Value::Object& params,
                                   const std::span<const CommandField> fields) {
    const auto missing = std::ranges::find_if(fields, [&params](const CommandField& field) {
        return field.required && findField(params, field.name) == nullptr;
    });
    if(missing != fields.end()) {
        return Result<void>::failure(missingFieldError(missing->name));
    }
    const auto unknown = std::ranges::find_if(
        params, [fields](const auto& entry) { return !isKnownField(fields, entry.first); });
    if(unknown != params.end()) {
        return Result<void>::failure(unknownFieldError(unknown->first));
    }
    return Result<void>::success();
}

Error withDefaultPath(Error error, std::string path) {
    if(!error.path.has_value()) {
        error.path = std::move(path);
    }
    return error;
}

Result<std::optional<std::string>> readOptionalString(const Value::Object& params,
                                                      const std::string_view name) {
    const auto* value = findField(params, name);
    if(value == nullptr) {
        return Result<std::optional<std::string>>::success(std::nullopt);
    }
    if(!value->isString()) {
        return Result<std::optional<std::string>>::failure(
            typeMismatch(std::string{name}, "string", value->type()));
    }
    return Result<std::optional<std::string>>::success(value->asString());
}

Result<std::string> readRequiredString(const Value::Object& params, const std::string_view name) {
    const auto* value = findField(params, name);
    if(value == nullptr || !value->isString()) {
        const auto actual = value == nullptr ? Value::Type::Null : value->type();
        return Result<std::string>::failure(typeMismatch(std::string{name}, "string", actual));
    }
    return Result<std::string>::success(value->asString());
}

Result<Value::Object> readRequiredObject(const Value::Object& params, const std::string_view name) {
    const auto* value = findField(params, name);
    if(value == nullptr || !value->isObject()) {
        const auto actual = value == nullptr ? Value::Type::Null : value->type();
        return Result<Value::Object>::failure(typeMismatch(std::string{name}, "object", actual));
    }
    return Result<Value::Object>::success(value->asObject());
}

Result<std::vector<std::string>> readOptionalStringArray(const Value::Object& params,
                                                         const std::string_view name) {
    const auto* value = findField(params, name);
    if(value == nullptr) {
        return Result<std::vector<std::string>>::success(std::vector<std::string>{});
    }
    return readStringArray(*value, name);
}

Result<std::optional<task::TaskState>> readOptionalTaskState(const Value::Object& params,
                                                             const std::string_view name) {
    const auto text = readOptionalString(params, name);
    if(!text) {
        return Result<std::optional<task::TaskState>>::failure(text.error());
    }
    if(!text.value().has_value()) {
        return Result<std::optional<task::TaskState>>::success(std::nullopt);
    }
    auto parsed = parseTaskStateText(*text.value());
    if(!parsed) {
        return Result<std::optional<task::TaskState>>::failure(
            withDefaultPath(parsed.error(), std::string{name}));
    }
    return Result<std::optional<task::TaskState>>::success(parsed.value());
}

} // namespace axiom::command::detail
