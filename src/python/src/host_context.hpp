#pragma once

#include <axiom/action/runtime.hpp>
#include <axiom/introspection/introspection_service.hpp>
#include <axiom/resource/resource_registry.hpp>
#include <axiom/task/task_id.hpp>
#include <axiom/task/task_registry.hpp>

#include <memory>
#include <utility>

namespace axiom::python {

struct HostContext {
    std::shared_ptr<axiom::Runtime> runtime;
    std::shared_ptr<axiom::resource::ResourceRegistry> resources;
    std::shared_ptr<axiom::task::TaskRegistry> tasks;
    std::unique_ptr<axiom::introspection::IntrospectionService> introspection;

    HostContext(std::shared_ptr<axiom::Runtime> rt,
                std::shared_ptr<axiom::resource::ResourceRegistry> res,
                std::shared_ptr<axiom::task::TaskRegistry> tsk)
        : runtime(std::move(rt)), resources(std::move(res)), tasks(std::move(tsk)),
          introspection(std::make_unique<axiom::introspection::IntrospectionService>(
              *runtime, *resources, *tasks)) {}

    ~HostContext() { introspection.reset(); }
};

struct RuntimeWrapper {
    std::shared_ptr<HostContext> ctx;
    explicit RuntimeWrapper(std::shared_ptr<HostContext> context) : ctx(std::move(context)) {}
};

struct IntrospectionWrapper {
    std::shared_ptr<HostContext> ctx;
    explicit IntrospectionWrapper(std::shared_ptr<HostContext> context) : ctx(std::move(context)) {}
};

struct TaskWrapper {
    std::shared_ptr<HostContext> ctx;
    axiom::task::TaskId id;
    TaskWrapper(std::shared_ptr<HostContext> context, axiom::task::TaskId task_id)
        : ctx(std::move(context)), id(std::move(task_id)) {}
};

struct Host {
    std::shared_ptr<HostContext> ctx;
    explicit Host(std::shared_ptr<HostContext> context) : ctx(std::move(context)) {}
};

} // namespace axiom::python
