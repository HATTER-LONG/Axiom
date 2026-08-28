#include <axiom/core/action/detail/dispatcher.hpp>

#include <axiom/core/action/action_id.hpp>
#include <axiom/core/action/descriptor.hpp>
#include <axiom/core/action/detail/value_converter.hpp>
#include <axiom/core/action/invocation_context.hpp>
#include <axiom/core/base/error.hpp>
#include <axiom/core/base/result.hpp>
#include <axiom/core/base/value.hpp>

#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace axiom::core::detail {
namespace {

[[nodiscard]] Error argumentError(const ErrorCode code, std::string message, std::string path) {
    return {
        .code = code,
        .message = std::move(message),
        .path = std::move(path),
        .details = std::nullopt,
    };
}

[[nodiscard]] Result<void> validateArguments(const ActionDescriptor& descriptor,
                                             const Arguments& arguments) {
    const auto missing = std::ranges::find_if(
        descriptor.parameters, [&arguments](const ParameterDescriptor& parameter) {
            return parameter.required && !arguments.contains(parameter.name);
        });
    if(missing != descriptor.parameters.end()) {
        return Result<void>::failure(argumentError(ErrorCode::MissingArgument,
                                                   "Required argument is missing: " + missing->name,
                                                   missing->name));
    }

    for(const auto& [name, value] : arguments) {
        static_cast<void>(value);
        const auto parameter =
            std::ranges::find(descriptor.parameters, name, &ParameterDescriptor::name);
        if(parameter == descriptor.parameters.end()) {
            return Result<void>::failure(argumentError(ErrorCode::UnknownArgument,
                                                       "Unknown argument: " + name,
                                                       appendObjectPath({}, name)));
        }
    }
    return Result<void>::success();
}

[[nodiscard]] Error invocationFailed() {
    return {
        .code = ErrorCode::InvocationFailed,
        .message = "Action invocation failed",
        .path = std::nullopt,
        .details = std::nullopt,
    };
}

[[nodiscard]] Error internalError() {
    return {
        .code = ErrorCode::InternalError,
        .message = "Unexpected internal failure during Action invocation",
        .path = std::nullopt,
        .details = std::nullopt,
    };
}

} // namespace

Result<Value> Dispatcher::invoke(const ActionId& id,
                                 const Arguments& arguments,
                                 const InvocationContext& context) const {
    try {
        const auto descriptor = registry.findAction(id);
        if(!descriptor) {
            return Result<Value>::failure(descriptor.error());
        }

        const auto validation = validateArguments(descriptor.value().get(), arguments);
        if(!validation) {
            return Result<Value>::failure(validation.error());
        }

        const auto implementation = registry.findImplementation(id);
        if(!implementation) {
            return Result<Value>::failure(implementation.error());
        }
        return implementation.value().get().invoke(arguments, context);
    } catch(const std::exception&) {
        return Result<Value>::failure(invocationFailed());
    } catch(...) {
        return Result<Value>::failure(internalError());
    }
}

} // namespace axiom::core::detail
