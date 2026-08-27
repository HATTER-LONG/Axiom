#include "dispatcher.hpp"

#include <algorithm>
#include <exception>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <axiom/runtime/action.hpp>
#include <axiom/runtime/action_id.hpp>
#include <axiom/runtime/descriptor.hpp>
#include <axiom/runtime/detail/registry.hpp>
#include <axiom/runtime/result.hpp>
#include <axiom/runtime/value.hpp>

namespace axiom::runtime {

struct InvocationContext;

} // namespace axiom::runtime

namespace axiom::runtime::detail {

static bool declaresParameter(const ActionDescriptor& descriptor, std::string_view name) {
    return std::ranges::any_of(
        descriptor.m_parameters,
        [&name](const ParameterDescriptor& parameter) { return parameter.m_name == name; });
}

static std::optional<Error> validatePresence(const ActionDescriptor& descriptor,
                                             const Arguments& arguments) {
    const auto missing = std::ranges::find_if(
        descriptor.m_parameters, [&arguments](const ParameterDescriptor& parameter) {
            return parameter.m_required && !arguments.contains(parameter.m_name);
        });
    if(missing != descriptor.m_parameters.end()) {
        return Error{ErrorCode::MissingArgument,
                     "Missing required argument '" + missing->m_name + "'", missing->m_name,
                     Value{}};
    }
    const auto unknown =
        std::ranges::find_if(arguments, [&descriptor](const Arguments::value_type& argument) {
            return !declaresParameter(descriptor, argument.first);
        });
    if(unknown != arguments.end()) {
        return Error{ErrorCode::InvalidArgument, "Unknown argument '" + unknown->first + "'",
                     unknown->first, Value{}};
    }
    return std::nullopt;
}

static Result<Value>
invokeSafely(IAction& action, const Arguments& arguments, const InvocationContext& context) {
    try {
        return action.invoke(arguments, context);
    } catch(const std::exception& exception) {
        return Result<Value>{Error{ErrorCode::InvocationFailed, exception.what(), {}, Value{}}};
    } catch(...) {
        return Result<Value>{
            Error{ErrorCode::InternalError, "Unknown action failure", {}, Value{}}};
    }
}

Dispatcher::Dispatcher(Registry& registry) : m_registry{registry} {}

Result<Value> Dispatcher::invoke(const ActionId& id,
                                 const Arguments& arguments,
                                 const InvocationContext& context) {
    IAction* action = m_registry.find(id);
    if(action == nullptr) {
        return Result<Value>{Error{ErrorCode::ActionNotFound,
                                   "Action '" + id.toString() + "' is not registered",
                                   id.toString(), Value{}}};
    }
    const std::optional<Error> validation = validatePresence(action->descriptor(), arguments);
    if(validation.has_value()) {
        return Result<Value>{*validation};
    }
    return invokeSafely(*action, arguments, context);
}

} // namespace axiom::runtime::detail
