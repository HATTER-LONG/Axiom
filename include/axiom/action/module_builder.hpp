#pragma once

/** @file module_builder.hpp
 * @brief Typed callable registration and explicit member ownership.
 */

#include <axiom/action/descriptor.hpp>
#include <axiom/action/detail/typed_action_adapter.hpp>
#include <axiom/action/module.hpp>
#include <axiom/export.hpp>
#include <axiom/foundation/error.hpp>
#include <axiom/foundation/result.hpp>
#include <axiom/foundation/type_descriptor.hpp>
#include <axiom/foundation/value.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace axiom {

namespace detail {

class ModuleBuilderState;
class IAction;

template <typename Target>
[[nodiscard]] Result<void> validateParameterDefault(const ParameterDescriptor& parameter) {
    if(!parameter.default_value) {
        return Result<void>::success();
    }
    const auto conversion = fromValue<Target>(*parameter.default_value, parameter.name);
    if(conversion) {
        return Result<void>::success();
    }
    return Result<void>::failure(
        {.code = ErrorCode::InvalidDescriptor,
         .message = "Parameter default value cannot convert to its inferred C++ type",
         .path = parameter.name,
         .details = std::nullopt});
}

template <typename Tuple>
[[nodiscard]] Result<void>
validateParameterDefaults(const std::vector<ParameterDescriptor>& parameters) {
    return [&parameters]<std::size_t... Index>(std::index_sequence<Index...>) {
        Result<void> result = Result<void>::success();
        ((result ? result = validateParameterDefault<
                       std::remove_cvref_t<std::tuple_element_t<Index, Tuple>>>(parameters[Index])
                 : result),
         ...);
        return result;
    }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}
} // namespace detail

/**
 * @brief Supplies the caller-facing name and documentation for one callable parameter.
 *
 * Callable C++ parameter and return types are always inferred. This object deliberately
 * cannot declare a second, potentially conflicting type.
 */
struct ParameterDocumentation {
    /** @brief Caller-facing parameter name. */
    std::string name;
    /** @brief Human-readable parameter documentation. */
    std::string description;
    /** @brief Optional fallback converted with the callable's inferred parameter type. */
    std::optional<Value> default_value;
};

/**
 * @brief Creates parameter documentation for ModuleBuilder registration.
 *
 * @param name Caller-facing parameter name matching `[a-z0-9_]+`.
 * @param description Human-readable explanation of the parameter.
 * @param default_value Optional fallback used when callers omit this parameter.
 * @return Documentation that ModuleBuilder combines with an inferred C++ type.
 */
[[nodiscard]] inline ParameterDocumentation
param(std::string name,
      std::string description = {},
      std::optional<Value> default_value = std::nullopt) {
    return {.name = std::move(name),
            .description = std::move(description),
            .default_value = std::move(default_value)};
}

/**
 * @brief Accumulates typed Action registrations for one Module before Runtime mutation.
 *
 * A builder owns each accepted callable, copying lvalue callables and moving rvalue
 * callables into pending storage. It accepts ordinary functions and unambiguous
 * copyable callable objects, including copyable lambdas. Generic lambdas, overloaded
 * function objects, non-copyable captures, and raw member-function pointers are
 * rejected at compile time; use bindMember() to retain a shared object explicitly.
 */
class AXIOM_API ModuleBuilder {
public:
    /**
     * @brief Starts a detached Module definition.
     * @param descriptor Module identity and metadata copied for later Runtime registration.
     * @throws std::bad_alloc If builder state cannot be allocated.
     */
    explicit ModuleBuilder(const ModuleDescriptor& descriptor);
    /** @brief Releases any pending callable registrations. */
    ~ModuleBuilder() noexcept;

    /**
     * @brief Transfers pending module ownership from another builder.
     * @param other Builder to leave empty.
     */
    ModuleBuilder(ModuleBuilder&& other) noexcept;
    /**
     * @brief Replaces pending module ownership with another builder's state.
     * @param other Builder to leave empty.
     * @return This builder.
     */
    ModuleBuilder& operator=(ModuleBuilder&& other) noexcept;
    ModuleBuilder(const ModuleBuilder&) = delete;
    ModuleBuilder& operator=(const ModuleBuilder&) = delete;

    /**
     * @brief Adds a callable Action without changing a Runtime.
     *
     * @tparam Callable A supported ordinary function or copyable callable object with an
     * unambiguous non-generic signature.
     * @tparam Documentation Parameter documentation argument types.
     * @param action_name Local Action name; Runtime combines it with the Module namespace.
     * @param description Caller-facing Action description.
     * @param callable Business callable copied (or moved, when an rvalue) into pending storage.
     * @param documentation One param() entry for every callable parameter, in signature order.
     * @return Success, or an empty-builder, invalid/duplicate descriptor error without
     *         changing this builder.
     * @throws std::bad_alloc If descriptor or callable storage preparation cannot allocate.
     * @throws Any exception raised while copying or moving callable into owned storage. The
     *
     * builder remains unchanged when callable construction throws.
     * @note After Runtime
     * registers this builder, one stored callable instance serves every
     *       invocation of
     * its Action. Calls may overlap, so a callable with mutable state or
     *       mutable
     * captures must provide its own synchronization. Runtime does not serialize
     * callable
     * execution.
     * @note This overload participates only when callable storage can be copied
     * and invoked
     *       with the converted lvalue argument objects. In particular,
     * rvalue-reference-only,
     *       generic, overloaded, non-copyable, and type-erased
     * callables are rejected during
     *       overload resolution. Ordinary function names decay
     * to function pointers before
     *       their signature is inspected and stored.
     */
    template <typename Callable, typename... Documentation>
        requires detail::AdaptableCallable<Callable>
    [[nodiscard]] Result<void> add(std::string_view action_name,
                                   std::string description,
                                   Callable&& callable,
                                   Documentation&&... documentation);

    /**
     * @brief Adds a callable Action with explicit discovery tags.
     *
     * Tags are stored on ActionDescriptor in presentation order. Matching is
     * case-sensitive and exact; empty or duplicate tags are rejected by validation.
     * Parameter documentation still follows the callable, as with add().
     *
     * @tparam Callable A supported ordinary function or copyable callable object.
     * @tparam Documentation Parameter documentation argument types.
     * @param action_name Local Action name; Runtime combines it with the Module namespace.
     * @param description Caller-facing Action description.
     * @param callable Business callable copied (or moved, when an rvalue) into pending storage.
     * @param tags Searchable labels copied onto the Action descriptor.
     * @param documentation One param() entry for every callable parameter, in signature order.
     * @return Success, or an empty-builder, invalid/duplicate descriptor error without
     *         changing this builder.
     */
    template <typename Callable, typename... Documentation>
        requires detail::AdaptableCallable<Callable>
    [[nodiscard]] Result<void> add(std::string_view action_name,
                                   std::string description,
                                   Callable&& callable,
                                   std::vector<std::string> tags,
                                   Documentation&&... documentation);

    /**
     * @brief Adds a callable Action that receives the current invocation identity.
     *
     * The callable's first parameter must accept `const ActionInvocation&`. That
     * injected view is valid only on the synchronous invoke stack and is omitted
     * from ActionDescriptor::parameters. Remaining parameters use the same
     * documentation, Value conversion, return conversion, and exception
     * normalization as add().
     *
     * @tparam Callable A supported copyable callable whose first parameter is
     *         ActionInvocation and whose remaining parameters are Value-convertible.
     * @tparam Documentation Parameter documentation argument types.
     * @param action_name Local Action name; Runtime combines it with the Module namespace.
     * @param description Caller-facing Action description.
     * @param callable Business callable copied (or moved, when an rvalue) into pending storage.
     * @param documentation One param() entry for every non-injected callable parameter, in
     *        signature order after ActionInvocation.
     * @return Success, or an empty-builder, invalid/duplicate descriptor error without
     *         changing this builder.
     * @throws std::bad_alloc If descriptor or callable storage preparation cannot allocate.
     * @throws Any exception raised while copying or moving callable into owned storage. The
     *         builder remains unchanged when callable construction throws.
     * @note Ordinary add() does not treat ActionInvocation as a special first parameter.
     */
    template <typename Callable, typename... Documentation>
        requires detail::AdaptableContextualCallable<Callable>
    [[nodiscard]] Result<void> addContextual(std::string_view action_name,
                                             std::string description,
                                             Callable&& callable,
                                             Documentation&&... documentation);

private:
    friend class Runtime;

    struct PreparedAction {
        std::string_view action_name;
        std::string description;
        std::unique_ptr<detail::IAction> implementation;
        std::vector<ParameterDescriptor> parameters;
        TypeDescriptor return_type;
        std::vector<std::string> tags;
    };

    [[nodiscard]] Result<void> addPreparedAction(PreparedAction prepared);

    std::unique_ptr<detail::ModuleBuilderState> state_;
};

} // namespace axiom

namespace axiom {

namespace detail {

/** @brief Copyable callable retaining a shared owner for a bound member function. */
template <auto Method, typename Object, typename Return, typename... Arguments>
class MemberBinding {
public:
    /**
     * @brief Retains the object used for each member invocation.
     * @param object Non-null shared owner of the member function receiver.
     */
    explicit MemberBinding(std::shared_ptr<Object> object) : object_(std::move(object)) {}

    /**
     * @brief Invokes the bound member function.
     * @param arguments Arguments forwarded to the member function.
     * @return The member function's result when it is non-void.
     */
    Return operator()(Arguments... arguments) const {
        return std::invoke(Method, *object_, std::forward<Arguments>(arguments)...);
    }

private:
    std::shared_ptr<Object> object_;
};

template <auto Method, typename Object, typename Return, typename... Arguments>
[[nodiscard]] MemberBinding<Method, Object, Return, Arguments...>
makeMemberBinding(std::shared_ptr<Object> object,
                  FunctionSignature<Return, Arguments...> signature) {
    static_cast<void>(signature);
    return MemberBinding<Method, Object, Return, Arguments...>{std::move(object)};
}

template <typename ArgumentTuple, typename... Documentation>
[[nodiscard]] Result<std::vector<ParameterDescriptor>>
describeCallableParameters(Documentation&&... documentation) {
    constexpr auto parameter_count = std::tuple_size_v<ArgumentTuple>;
    static_assert((std::same_as<std::remove_cvref_t<Documentation>, ParameterDocumentation> && ...),
                  "Each Action parameter must be described with param(name, description)");
    static_assert(
        sizeof...(Documentation) == parameter_count,
        "Action registration needs exactly one param(name, description) per callable parameter");

    std::vector<ParameterDescriptor> parameters;
    parameters.reserve(parameter_count);
    (parameters.push_back({.name = documentation.name,
                           .description = documentation.description,
                           .required = !documentation.default_value.has_value(),
                           .type = {},
                           .default_value = documentation.default_value}),
     ...);
    [&]<std::size_t... Index>(std::index_sequence<Index...>) {
        ((parameters[Index].type =
              typeDescriptorFor<std::remove_cvref_t<std::tuple_element_t<Index, ArgumentTuple>>>()),
         ...);
    }(std::make_index_sequence<parameter_count>{});

    auto default_validation = validateParameterDefaults<ArgumentTuple>(parameters);
    if(!default_validation) {
        return Result<std::vector<ParameterDescriptor>>::failure(default_validation.error());
    }
    return Result<std::vector<ParameterDescriptor>>::success(std::move(parameters));
}

} // namespace detail

/**
 * @brief Creates a lifetime-safe callable wrapper for a member function.
 *
 * @tparam Method Pointer to a non-static member function.
 * @tparam Object Member function owner type.
 * @param object Shared owner retained by the returned callable.
 * @return A copyable callable that keeps object lifetime explicit through invocation.
 * @pre object is non-null.
 */
template <auto Method, typename Object>
[[nodiscard]] auto bindMember(std::shared_ptr<Object> object) {
    static_assert(std::is_member_function_pointer_v<decltype(Method)>,
                  "bindMember requires a non-static member-function pointer");
    return detail::makeMemberBinding<Method>(std::move(object),
                                             detail::FunctionTraits<decltype(Method)>{});
}

template <typename Callable, typename... Documentation>
    requires detail::AdaptableCallable<Callable>
Result<void> ModuleBuilder::add(std::string_view action_name,
                                std::string description,
                                Callable&& callable,
                                Documentation&&... documentation) {
    using StoredCallable = std::decay_t<Callable>;
    using Traits = detail::FunctionTraits<StoredCallable>;
    using SourceArguments = Traits::ArgumentTypes;
    using Return = Traits::ReturnType;

    auto parameters = detail::describeCallableParameters<SourceArguments>(
        std::forward<Documentation>(documentation)...);
    if(!parameters) {
        return Result<void>::failure(parameters.error());
    }

    auto implementation = std::make_unique<detail::TypedActionAdapter<StoredCallable>>(
        std::forward<Callable>(callable), parameters.value());
    PreparedAction prepared;
    prepared.action_name = action_name;
    prepared.description = std::move(description);
    prepared.implementation = std::move(implementation);
    prepared.parameters = std::move(parameters.value());
    prepared.return_type = detail::returnTypeDescriptor<Return>();
    return addPreparedAction(std::move(prepared));
}

template <typename Callable, typename... Documentation>
    requires detail::AdaptableCallable<Callable>
Result<void> ModuleBuilder::add(std::string_view action_name,
                                std::string description,
                                Callable&& callable,
                                std::vector<std::string> tags,
                                Documentation&&... documentation) {
    using StoredCallable = std::decay_t<Callable>;
    using Traits = detail::FunctionTraits<StoredCallable>;
    using SourceArguments = Traits::ArgumentTypes;
    using Return = Traits::ReturnType;

    auto parameters = detail::describeCallableParameters<SourceArguments>(
        std::forward<Documentation>(documentation)...);
    if(!parameters) {
        return Result<void>::failure(parameters.error());
    }

    auto implementation = std::make_unique<detail::TypedActionAdapter<StoredCallable>>(
        std::forward<Callable>(callable), parameters.value());
    PreparedAction prepared;
    prepared.action_name = action_name;
    prepared.description = std::move(description);
    prepared.implementation = std::move(implementation);
    prepared.parameters = std::move(parameters.value());
    prepared.return_type = detail::returnTypeDescriptor<Return>();
    prepared.tags = std::move(tags);
    return addPreparedAction(std::move(prepared));
}

template <typename Callable, typename... Documentation>
    requires detail::AdaptableContextualCallable<Callable>
Result<void> ModuleBuilder::addContextual(std::string_view action_name,
                                          std::string description,
                                          Callable&& callable,
                                          Documentation&&... documentation) {
    using StoredCallable = std::decay_t<Callable>;
    using Traits = detail::FunctionTraits<StoredCallable>;
    using SourceArguments = Traits::ArgumentTypes;
    using ParameterArguments = detail::ParameterArgumentTypes<true, SourceArguments>::Type;
    using Return = Traits::ReturnType;

    auto parameters = detail::describeCallableParameters<ParameterArguments>(
        std::forward<Documentation>(documentation)...);
    if(!parameters) {
        return Result<void>::failure(parameters.error());
    }

    auto implementation = std::make_unique<detail::TypedActionAdapter<StoredCallable, true>>(
        std::forward<Callable>(callable), parameters.value());
    PreparedAction prepared;
    prepared.action_name = action_name;
    prepared.description = std::move(description);
    prepared.implementation = std::move(implementation);
    prepared.parameters = std::move(parameters.value());
    prepared.return_type = detail::returnTypeDescriptor<Return>();
    return addPreparedAction(std::move(prepared));
}

} // namespace axiom
