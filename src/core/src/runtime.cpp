#include <axiom/core/action/runtime.hpp>

#include <axiom/core/action/action_id.hpp>
#include <axiom/core/action/descriptor.hpp>
#include <axiom/core/action/detail/dispatcher.hpp>
#include <axiom/core/action/detail/module_builder_state.hpp>
#include <axiom/core/action/detail/registry.hpp>
#include <axiom/core/action/invocation_context.hpp>
#include <axiom/core/action/module.hpp>
#include <axiom/core/action/module_builder.hpp>
#include <axiom/core/base/error.hpp>
#include <axiom/core/base/result.hpp>
#include <axiom/core/base/value.hpp>
#include <axiom/core/logging/log_level.hpp>
#include <axiom/core/logging/logger.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace axiom::core::detail {

namespace {

[[nodiscard]] const char* outcomeStatus(const ErrorCode code) noexcept {
    switch(code) {
    case ErrorCode::InvalidArgument:
        return "invalid_argument";
    case ErrorCode::MissingArgument:
        return "missing_argument";
    case ErrorCode::UnknownArgument:
        return "unknown_argument";
    case ErrorCode::TypeMismatch:
        return "type_mismatch";
    case ErrorCode::NotFound:
        return "not_found";
    case ErrorCode::AlreadyExists:
        return "already_exists";
    case ErrorCode::InvalidDescriptor:
        return "invalid_descriptor";
    case ErrorCode::InvocationFailed:
        return "invocation_failed";
    case ErrorCode::InternalError:
        return "internal_error";
    }
    return "internal_error";
}

[[nodiscard]] logging::LogLevel registrationLevel(const Result<void>& result) noexcept {
    if(result) {
        return logging::LogLevel::Info;
    }
    return result.error().code == ErrorCode::InternalError ? logging::LogLevel::Error
                                                           : logging::LogLevel::Warning;
}

[[nodiscard]] logging::LogLevel invocationLevel(const Result<Value>& result) noexcept {
    if(result) {
        return logging::LogLevel::Info;
    }
    const auto code = result.error().code;
    return code == ErrorCode::InvocationFailed || code == ErrorCode::InternalError
               ? logging::LogLevel::Error
               : logging::LogLevel::Warning;
}

[[nodiscard]] Value::Object invocationContextFields(const InvocationContext& context) {
    Value::Object fields;
    for(const auto& [key, value] : context.metadata) {
        fields.insert_or_assign(key, Value{value});
    }
    if(!context.request_id.empty()) {
        fields.insert_or_assign("request_id", Value{context.request_id});
    }
    if(!context.trace_id.empty()) {
        fields.insert_or_assign("trace_id", Value{context.trace_id});
    }
    if(!context.caller.empty()) {
        fields.insert_or_assign("caller", Value{context.caller});
    }
    return fields;
}

[[nodiscard]] Value::Object invocationFields(const ActionId& id) {
    return {{"module", Value{std::string{id.module()}}}, {"action", Value{std::string{id.str()}}}};
}

} // namespace

class RuntimeState {
public:
    explicit RuntimeState(logging::Logger logger)
        : dispatcher{registry}, module_logger{logger.child("module")},
          action_logger{logger.child("action")} {}

    void logRegistration(const Result<void>& result, const std::string_view module) const noexcept {
        try {
            Value::Object fields;
            if(!module.empty()) {
                fields.insert_or_assign("module", Value{std::string{module}});
            }
            if(result) {
                AXIOM_LOG(module_logger, registrationLevel(result), std::move(fields),
                          "registered module {}", module);
            } else {
                AXIOM_LOG(module_logger, registrationLevel(result), std::move(fields),
                          "module registration failed: {}", outcomeStatus(result.error().code));
            }
        } catch(...) {
        }
    }

    void logActionStart(const logging::Logger& logger) const noexcept {
        try {
            AXIOM_LOG_DEBUG(logger, "action invocation started");
        } catch(...) {
        }
    }

    void logActionEnd(const logging::Logger& logger,
                      const Result<Value>& result,
                      const std::int64_t duration_ms) const noexcept {
        try {
            Value::Object fields{
                {"status", Value{result ? "success" : outcomeStatus(result.error().code)}},
                {"duration_ms", Value{std::max<std::int64_t>(duration_ms, 0)}}};
            AXIOM_LOG(logger, invocationLevel(result), std::move(fields),
                      "action invocation finished");
        } catch(...) {
        }
    }

    Registry registry;
    Dispatcher dispatcher{registry};
    logging::Logger module_logger;
    logging::Logger action_logger;
};

} // namespace axiom::core::detail

namespace axiom::core {

Runtime::Runtime() : Runtime(logging::Logger{}) {}
Runtime::Runtime(logging::Logger logger)
    : state_(std::make_unique<detail::RuntimeState>(std::move(logger))) {}
Runtime::~Runtime() noexcept = default;

Result<void> Runtime::registerModule(ModuleBuilder&& builder) {
    std::string module;
    try {
        if(builder.state_) {
            module = builder.state_->descriptor.namespace_name;
        }
    } catch(...) {
    }
    if(!builder.state_) {
        auto result = Result<void>::failure({.code = ErrorCode::InvalidArgument,
                                             .message = "ModuleBuilder must not be empty",
                                             .path = std::nullopt,
                                             .details = std::nullopt});
        state_->logRegistration(result, module);
        return result;
    }
    auto result = state_->registry.registerModuleWithActions(builder.state_->descriptor,
                                                             builder.state_->actions);
    if(result) {
        builder.state_.reset();
    }
    state_->logRegistration(result, module);
    return result;
}

Result<Value> Runtime::invoke(const ActionId& id,
                              const Arguments& arguments,
                              const InvocationContext& context) const {
    logging::ScopedLogContext scoped_context;
    logging::Logger invocation_logger;
    try {
        scoped_context =
            state_->action_logger.scopedContext(detail::invocationContextFields(context));
    } catch(...) {
    }
    try {
        invocation_logger = state_->action_logger.withFields(detail::invocationFields(id));
    } catch(...) {
    }

    state_->logActionStart(invocation_logger);
    const auto started = std::chrono::steady_clock::now();
    auto result = state_->dispatcher.invoke(id, arguments, context);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto duration_ms = std::max<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 0);
    state_->logActionEnd(invocation_logger, result, duration_ms);
    return result;
}

Result<std::reference_wrapper<const ModuleDescriptor>>
Runtime::findModule(const std::string_view namespace_name) const {
    return state_->registry.findModule(namespace_name);
}

Result<std::reference_wrapper<const ActionDescriptor>>
Runtime::findAction(const ActionId& id) const {
    return state_->registry.findAction(id);
}

std::vector<std::reference_wrapper<const ModuleDescriptor>> Runtime::discoverModules() const {
    return state_->registry.discoverModules();
}

std::vector<std::reference_wrapper<const ActionDescriptor>> Runtime::discoverActions() const {
    return state_->registry.discoverActions();
}

} // namespace axiom::core
