#pragma once

#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/task/task_types.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace axiom::command::detail {

enum class CommandMethod : std::uint8_t {
    ActionList,
    ActionDescribe,
    ActionInvoke,
    ResourceList,
    ResourceDescribe,
    TaskList,
    TaskDescribe,
    TaskCancel,
    SystemSnapshot,
};

struct CommandField {
    std::string_view name{}; // NOLINT(readability-redundant-member-init)
    bool required{false};
};

[[nodiscard]] Result<CommandMethod> parseCommandMethod(std::string_view method);
[[nodiscard]] std::span<const CommandField> commandFields(CommandMethod method);
[[nodiscard]] Result<void> validateCommandParams(const Value::Object& params,
                                                 std::span<const CommandField> fields);
[[nodiscard]] Error withDefaultPath(Error error, std::string path);
[[nodiscard]] Result<std::optional<std::string>> readOptionalString(const Value::Object& params,
                                                                    std::string_view name);
[[nodiscard]] Result<std::string> readRequiredString(const Value::Object& params,
                                                     std::string_view name);
[[nodiscard]] Result<Value::Object> readRequiredObject(const Value::Object& params,
                                                       std::string_view name);
[[nodiscard]] Result<std::vector<std::string>> readOptionalStringArray(const Value::Object& params,
                                                                       std::string_view name);
[[nodiscard]] Result<std::optional<task::TaskState>>
readOptionalTaskState(const Value::Object& params, std::string_view name);

} // namespace axiom::command::detail
