#pragma once

#include <axiom/action/descriptor.hpp>
#include <axiom/action/module.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/type_descriptor.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/introspection/runtime_snapshot.hpp>
#include <axiom/resource/resource_descriptor.hpp>
#include <axiom/task/task_types.hpp>

#include <string_view>
#include <vector>

namespace axiom::command::detail {

[[nodiscard]] std::string_view errorCodeName(ErrorCode code);
[[nodiscard]] std::string_view taskStateName(task::TaskState state);
[[nodiscard]] Value encodeError(const Error& error);
[[nodiscard]] Value encodeTypeDescriptor(const TypeDescriptor& type);
[[nodiscard]] Value encodeParameterDescriptor(const ParameterDescriptor& parameter);
[[nodiscard]] Value encodeModuleDescriptor(const ModuleDescriptor& module);
[[nodiscard]] Value encodeActionDescriptor(const ActionDescriptor& action);
[[nodiscard]] Value encodeResourceDescriptor(const resource::ResourceDescriptor& resource);
[[nodiscard]] Value encodeTaskDescriptor(const task::TaskDescriptor& task);
[[nodiscard]] Value encodeModuleDescriptors(const std::vector<ModuleDescriptor>& modules);
[[nodiscard]] Value encodeActionDescriptors(const std::vector<ActionDescriptor>& actions);
[[nodiscard]] Value
encodeResourceDescriptors(const std::vector<resource::ResourceDescriptor>& resources);
[[nodiscard]] Value encodeTaskDescriptors(const std::vector<task::TaskDescriptor>& tasks);
[[nodiscard]] Value encodeSnapshot(const introspection::RuntimeSnapshot& snapshot);

} // namespace axiom::command::detail
