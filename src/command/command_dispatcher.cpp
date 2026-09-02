#include <axiom/command/command_dispatcher.hpp>

#include "command_validation.hpp"
#include "descriptor_conversion.hpp"

#include <axiom/action/action_id.hpp>
#include <axiom/action/invocation_context.hpp>
#include <axiom/action/runtime.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/value.hpp>
#include <axiom/introspection/introspection_query.hpp>
#include <axiom/introspection/introspection_service.hpp>
#include <axiom/resource/resource_id.hpp>
#include <axiom/resource/resource_registry.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_registry.hpp>

#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace axiom::command {
namespace {

using detail::commandFields;
using detail::CommandMethod;
using detail::parseCommandMethod;
using detail::readOptionalString;
using detail::readOptionalStringArray;
using detail::readOptionalTaskState;
using detail::readRequiredObject;
using detail::readRequiredString;
using detail::validateCommandParams;
using detail::withDefaultPath;

[[nodiscard]] Result<ActionId> parseActionParam(const Value::Object& params) {
    const auto text = readRequiredString(params, "action");
    if(!text) {
        return Result<ActionId>::failure(text.error());
    }
    auto parsed = ActionId::parse(text.value());
    if(!parsed) {
        return Result<ActionId>::failure(withDefaultPath(parsed.error(), "action"));
    }
    return parsed;
}

[[nodiscard]] Result<resource::ResourceId> parseResourceParam(const Value::Object& params) {
    const auto text = readRequiredString(params, "resource");
    if(!text) {
        return Result<resource::ResourceId>::failure(text.error());
    }
    auto parsed = resource::ResourceId::parse(text.value());
    if(!parsed) {
        return Result<resource::ResourceId>::failure(withDefaultPath(parsed.error(), "resource"));
    }
    return parsed;
}

[[nodiscard]] Result<task::TaskId> parseTaskParam(const Value::Object& params) {
    const auto text = readRequiredString(params, "task");
    if(!text) {
        return Result<task::TaskId>::failure(text.error());
    }
    auto parsed = task::TaskId::parse(text.value());
    if(!parsed) {
        return Result<task::TaskId>::failure(withDefaultPath(parsed.error(), "task"));
    }
    return parsed;
}

[[nodiscard]] Result<introspection::ActionQuery> readActionQuery(const Value::Object& params) {
    const auto module = readOptionalString(params, "module");
    if(!module) {
        return Result<introspection::ActionQuery>::failure(module.error());
    }
    auto tags = readOptionalStringArray(params, "tags");
    if(!tags) {
        return Result<introspection::ActionQuery>::failure(tags.error());
    }
    return Result<introspection::ActionQuery>::success(
        introspection::ActionQuery{.module = module.value(), .tags = std::move(tags.value())});
}

[[nodiscard]] Result<introspection::ResourceQuery> readResourceQuery(const Value::Object& params) {
    const auto type = readOptionalString(params, "type");
    if(!type) {
        return Result<introspection::ResourceQuery>::failure(type.error());
    }
    return Result<introspection::ResourceQuery>::success(
        introspection::ResourceQuery{.type = type.value()});
}

[[nodiscard]] Result<introspection::TaskQuery> readTaskQuery(const Value::Object& params) {
    const auto state = readOptionalTaskState(params, "state");
    if(!state) {
        return Result<introspection::TaskQuery>::failure(state.error());
    }
    const auto origin_action = readOptionalString(params, "origin_action");
    if(!origin_action) {
        return Result<introspection::TaskQuery>::failure(origin_action.error());
    }
    const auto origin_request = readOptionalString(params, "origin_request");
    if(!origin_request) {
        return Result<introspection::TaskQuery>::failure(origin_request.error());
    }
    return Result<introspection::TaskQuery>::success(introspection::TaskQuery{
        .state = state.value(),
        .origin_action_id = origin_action.value(),
        .origin_request_id = origin_request.value(),
    });
}

} // namespace

class CommandDispatcher::Impl {
public:
    Impl(const Runtime& runtime,
         const resource::ResourceRegistry& resources,
         task::TaskRegistry& tasks)
        : runtime_(&runtime), tasks_(&tasks), introspection_(runtime, resources, tasks) {}

    [[nodiscard]] Result<Value> dispatch(std::string_view method,
                                         const Value::Object& params,
                                         const InvocationContext& context) const {
        const auto parsed = parseCommandMethod(method);
        if(!parsed) {
            return Result<Value>::failure(parsed.error());
        }
        const auto valid = validateCommandParams(params, commandFields(parsed.value()));
        if(!valid) {
            return Result<Value>::failure(valid.error());
        }
        return dispatchMethod(parsed.value(), params, context);
    }

private:
    [[nodiscard]] Result<Value> dispatchMethod(const CommandMethod method,
                                               const Value::Object& params,
                                               const InvocationContext& context) const {
        switch(method) {
        case CommandMethod::ActionList:
            return listActions(params);
        case CommandMethod::ActionDescribe:
            return describeAction(params);
        case CommandMethod::ActionInvoke:
            return invokeAction(params, context);
        case CommandMethod::ResourceList:
            return listResources(params);
        case CommandMethod::ResourceDescribe:
            return describeResource(params);
        case CommandMethod::TaskList:
            return listTasks(params);
        case CommandMethod::TaskDescribe:
            return describeTask(params);
        case CommandMethod::TaskCancel:
            return cancelTask(params);
        case CommandMethod::SystemSnapshot:
            return snapshotSystem();
        }
        throw std::logic_error{"Unknown CommandMethod enumerator"};
    }

    [[nodiscard]] Result<Value> listActions(const Value::Object& params) const {
        const auto query = readActionQuery(params);
        if(!query) {
            return Result<Value>::failure(query.error());
        }
        return Result<Value>::success(
            detail::encodeActionDescriptors(introspection_.actions(query.value())));
    }

    [[nodiscard]] Result<Value> describeAction(const Value::Object& params) const {
        const auto id = parseActionParam(params);
        if(!id) {
            return Result<Value>::failure(id.error());
        }
        const auto described = introspection_.describeAction(id.value());
        if(!described) {
            return Result<Value>::failure(described.error());
        }
        return Result<Value>::success(detail::encodeActionDescriptor(described.value()));
    }

    [[nodiscard]] Result<Value> invokeAction(const Value::Object& params,
                                             const InvocationContext& context) const {
        const auto id = parseActionParam(params);
        if(!id) {
            return Result<Value>::failure(id.error());
        }
        const auto arguments = readRequiredObject(params, "arguments");
        if(!arguments) {
            return Result<Value>::failure(arguments.error());
        }
        return runtime_->invoke(id.value(), arguments.value(), context);
    }

    [[nodiscard]] Result<Value> listResources(const Value::Object& params) const {
        const auto query = readResourceQuery(params);
        if(!query) {
            return Result<Value>::failure(query.error());
        }
        auto listed = introspection_.resources(query.value());
        if(!listed) {
            return Result<Value>::failure(withDefaultPath(listed.error(), "type"));
        }
        return Result<Value>::success(detail::encodeResourceDescriptors(listed.value()));
    }

    [[nodiscard]] Result<Value> describeResource(const Value::Object& params) const {
        const auto id = parseResourceParam(params);
        if(!id) {
            return Result<Value>::failure(id.error());
        }
        const auto described = introspection_.describeResource(id.value());
        if(!described) {
            return Result<Value>::failure(described.error());
        }
        return Result<Value>::success(detail::encodeResourceDescriptor(described.value()));
    }

    [[nodiscard]] Result<Value> listTasks(const Value::Object& params) const {
        const auto query = readTaskQuery(params);
        if(!query) {
            return Result<Value>::failure(query.error());
        }
        return Result<Value>::success(
            detail::encodeTaskDescriptors(introspection_.tasks(query.value())));
    }

    [[nodiscard]] Result<Value> describeTask(const Value::Object& params) const {
        const auto id = parseTaskParam(params);
        if(!id) {
            return Result<Value>::failure(id.error());
        }
        const auto described = introspection_.describeTask(id.value());
        if(!described) {
            return Result<Value>::failure(described.error());
        }
        return Result<Value>::success(detail::encodeTaskDescriptor(described.value()));
    }

    [[nodiscard]] Result<Value> cancelTask(const Value::Object& params) const {
        const auto id = parseTaskParam(params);
        if(!id) {
            return Result<Value>::failure(id.error());
        }
        const auto cancelled = tasks_->cancel(id.value());
        if(!cancelled) {
            return Result<Value>::failure(cancelled.error());
        }
        return Result<Value>::success(Value{nullptr});
    }

    [[nodiscard]] Result<Value> snapshotSystem() const {
        return Result<Value>::success(detail::encodeSnapshot(introspection_.snapshot()));
    }

    const Runtime* runtime_;
    task::TaskRegistry* tasks_;
    introspection::IntrospectionService introspection_;
};

CommandDispatcher::CommandDispatcher(const Runtime& runtime,
                                     const resource::ResourceRegistry& resources,
                                     task::TaskRegistry& tasks)
    : impl_(std::make_unique<Impl>(runtime, resources, tasks)) {}

CommandDispatcher::~CommandDispatcher() noexcept = default;

Result<Value> CommandDispatcher::dispatch(const std::string_view method,
                                          const Value::Object& params,
                                          const InvocationContext& context) const {
    return impl_->dispatch(method, params, context);
}

} // namespace axiom::command
