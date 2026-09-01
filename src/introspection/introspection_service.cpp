#include <axiom/introspection/introspection_service.hpp>

#include "detail/type_descriptor_copy.hpp"

#include <axiom/action/descriptor.hpp>
#include <axiom/action/module.hpp>
#include <axiom/action/runtime.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/introspection/runtime_snapshot.hpp>
#include <axiom/resource/resource_descriptor.hpp>
#include <axiom/resource/resource_id.hpp>
#include <axiom/resource/resource_registry.hpp>
#include <axiom/resource/resource_traits.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_registry.hpp>
#include <axiom/task/task_types.hpp>

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace axiom::introspection {
namespace {

[[nodiscard]] ActionDescriptor copyAction(const ActionDescriptor& source) {
    std::vector<ParameterDescriptor> parameters;
    parameters.reserve(source.parameters.size());
    std::ranges::transform(source.parameters, std::back_inserter(parameters),
                           [](const ParameterDescriptor& parameter) {
                               return ParameterDescriptor{
                                   .name = parameter.name,
                                   .description = parameter.description,
                                   .required = parameter.required,
                                   .type = detail::copyTypeDescriptor(parameter.type),
                                   .default_value = parameter.default_value};
                           });
    return {.id = source.id,
            .description = source.description,
            .parameters = std::move(parameters),
            .return_type = detail::copyTypeDescriptor(source.return_type),
            .version = source.version,
            .tags = source.tags};
}

[[nodiscard]] Error invalidResourceType(std::string_view type) {
    return {.code = ErrorCode::InvalidArgument,
            .message =
                "Resource type must have the canonical form [a-z][a-z0-9_]*: " + std::string{type},
            .path = std::nullopt,
            .details = std::nullopt};
}

} // namespace

IntrospectionService::IntrospectionService(const Runtime& actions,
                                           const resource::ResourceRegistry& resources,
                                           const task::TaskRegistry& tasks) noexcept
    : actions_{&actions}, resources_{&resources}, tasks_{&tasks} {}

std::vector<ModuleDescriptor> IntrospectionService::modules() const {
    const auto source = actions_->discoverModules();
    std::vector<ModuleDescriptor> result;
    result.reserve(source.size());
    std::ranges::transform(source, std::back_inserter(result),
                           [](const auto& module) { return module.get(); });
    return result;
}

std::vector<ActionDescriptor> IntrospectionService::actions() const {
    const auto source = actions_->discoverActions();
    std::vector<ActionDescriptor> result;
    result.reserve(source.size());
    std::ranges::transform(source, std::back_inserter(result),
                           [](const auto& action) { return copyAction(action.get()); });
    return result;
}

std::vector<ActionDescriptor>
IntrospectionService::actions(const std::string_view module_namespace) const {
    std::vector<ActionDescriptor> result;
    for(auto& action : actions()) {
        if(action.id.module() == module_namespace) {
            result.push_back(std::move(action));
        }
    }
    return result;
}

Result<ActionDescriptor> IntrospectionService::describeAction(const ActionId& id) const {
    const auto found = actions_->findAction(id);
    if(!found) {
        return Result<ActionDescriptor>::failure(found.error());
    }
    return Result<ActionDescriptor>::success(copyAction(found.value().get()));
}

std::vector<resource::ResourceDescriptor> IntrospectionService::resources() const {
    return resources_->list();
}

Result<std::vector<resource::ResourceDescriptor>>
IntrospectionService::resources(const std::string_view type) const {
    if(!resource::detail::isCanonicalTypeName(type)) {
        return Result<std::vector<resource::ResourceDescriptor>>::failure(
            invalidResourceType(type));
    }
    auto result = resources();
    std::erase_if(result, [type](const auto& resource) { return resource.type != type; });
    return Result<std::vector<resource::ResourceDescriptor>>::success(std::move(result));
}

Result<resource::ResourceDescriptor>
IntrospectionService::describeResource(const resource::ResourceId& id) const {
    return resources_->describe(id);
}

std::vector<task::TaskDescriptor> IntrospectionService::tasks() const { return tasks_->list(); }

Result<task::TaskDescriptor> IntrospectionService::describeTask(const task::TaskId& id) const {
    return tasks_->describe(id);
}

RuntimeSnapshot IntrospectionService::snapshot() const {
    RuntimeSnapshot result;
    result.modules = modules();
    result.actions = actions();
    result.resources = resources();
    result.tasks = tasks();
    return result;
}

} // namespace axiom::introspection
