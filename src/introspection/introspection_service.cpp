#include <axiom/introspection/introspection_service.hpp>

#include "detail/type_descriptor_copy.hpp"

#include <axiom/action/descriptor.hpp>
#include <axiom/action/module.hpp>
#include <axiom/action/runtime.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/introspection/introspection_query.hpp>
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
            .tags = source.tags,
            .metadata = source.metadata};
}

[[nodiscard]] ModuleDescriptor copyModule(const ModuleDescriptor& source) {
    return {.namespace_name = source.namespace_name,
            .description = source.description,
            .version = source.version,
            .tags = source.tags,
            .metadata = source.metadata};
}

[[nodiscard]] task::TaskDescriptor copyTask(const task::TaskDescriptor& source) {
    return {.id = source.id,
            .name = source.name,
            .state = source.state,
            .progress = source.progress,
            .error = source.error,
            .origin = source.origin};
}

[[nodiscard]] Error invalidResourceType(std::string_view type) {
    return {.code = ErrorCode::InvalidArgument,
            .message =
                "Resource type must have the canonical form [a-z][a-z0-9_]*: " + std::string{type},
            .path = std::nullopt,
            .details = std::nullopt};
}

[[nodiscard]] bool matchesAction(const ActionDescriptor& action, const ActionQuery& query) {
    if(query.module.has_value() && action.id.module() != *query.module) {
        return false;
    }
    return std::ranges::all_of(query.tags, [&action](const std::string& tag) {
        return std::ranges::find(action.tags, tag) != action.tags.end();
    });
}

[[nodiscard]] bool matchesOriginField(const std::optional<task::TaskOrigin>& origin,
                                      const std::optional<std::string>& expected,
                                      const std::string task::TaskOrigin::* field) {
    if(!expected.has_value()) {
        return true;
    }
    return origin.has_value() && origin.value().*field == *expected;
}

[[nodiscard]] bool matchesTask(const task::TaskDescriptor& task, const TaskQuery& query) {
    if(query.state.has_value() && task.state != *query.state) {
        return false;
    }
    return matchesOriginField(task.origin, query.origin_action_id, &task::TaskOrigin::action_id) &&
           matchesOriginField(task.origin, query.origin_request_id, &task::TaskOrigin::request_id);
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
                           [](const auto& module) { return copyModule(module.get()); });
    return result;
}

std::vector<ActionDescriptor> IntrospectionService::actions() const {
    return actions(ActionQuery{});
}

std::vector<ActionDescriptor>
IntrospectionService::actions(const std::string_view module_namespace) const {
    return actions(ActionQuery{.module = std::string{module_namespace}, .tags = {}});
}

std::vector<ActionDescriptor> IntrospectionService::actions(const ActionQuery& query) const {
    const auto source = actions_->discoverActions();
    std::vector<ActionDescriptor> result;
    result.reserve(source.size());
    for(const auto& action : source) {
        if(matchesAction(action.get(), query)) {
            result.push_back(copyAction(action.get()));
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
    return resources(ResourceQuery{}).value();
}

Result<std::vector<resource::ResourceDescriptor>>
IntrospectionService::resources(const std::string_view type) const {
    return resources(ResourceQuery{.type = std::string{type}});
}

Result<std::vector<resource::ResourceDescriptor>>
IntrospectionService::resources(const ResourceQuery& query) const {
    if(query.type.has_value() && !resource::detail::isCanonicalTypeName(*query.type)) {
        return Result<std::vector<resource::ResourceDescriptor>>::failure(
            invalidResourceType(*query.type));
    }
    auto result = resources_->list();
    if(query.type.has_value()) {
        std::erase_if(result,
                      [&query](const auto& resource) { return resource.type != *query.type; });
    }
    return Result<std::vector<resource::ResourceDescriptor>>::success(std::move(result));
}

Result<resource::ResourceDescriptor>
IntrospectionService::describeResource(const resource::ResourceId& id) const {
    return resources_->describe(id);
}

std::vector<task::TaskDescriptor> IntrospectionService::tasks() const { return tasks(TaskQuery{}); }

std::vector<task::TaskDescriptor> IntrospectionService::tasks(const TaskQuery& query) const {
    auto source = tasks_->list();
    std::vector<task::TaskDescriptor> result;
    result.reserve(source.size());
    for(const auto& task : source) {
        if(matchesTask(task, query)) {
            result.push_back(copyTask(task));
        }
    }
    return result;
}

Result<task::TaskDescriptor> IntrospectionService::describeTask(const task::TaskId& id) const {
    auto found = tasks_->describe(id);
    if(!found) {
        return found;
    }
    return Result<task::TaskDescriptor>::success(copyTask(found.value()));
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
