#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

#include <axiom/runtime/action_id.hpp>
#include <axiom/runtime/descriptor.hpp>
#include <axiom/runtime/detail/action_adapter.hpp>
#include <axiom/runtime/detail/function_traits.hpp>
#include <axiom/runtime/detail/registry.hpp>
#include <axiom/runtime/result.hpp>

namespace axiom::runtime {

/**
 * @brief Fluent registration handle for one module, created by Runtime::module().
 *
 * Holds a non-owning pointer to the Runtime's internal registry, so the Runtime
 * must outlive every ModuleBuilder derived from it. Actions are registered under
 * ids of the form `module.action`.
 */
class ModuleBuilder {
public:
    ModuleBuilder(std::string name, detail::Registry& registry) noexcept
        : m_name{std::move(name)}, m_registry{&registry} {}

    /** @brief Registers a lambda, functor or function pointer as an action. */
    template <typename F, typename... Params>
    Result<void> action(std::string name, std::string description, F callable, Params... params) {
        using Callable = std::decay_t<F>;
        static_assert(sizeof...(Params) == detail::FunctionTraits<Callable>::ARITY,
                      "the number of parameter descriptors must match the callable arity");
        static_assert((std::is_same_v<std::decay_t<Params>, ParameterDescriptor> && ...),
                      "parameter descriptors must be built with param<T>()/optionalParam<T>()");
        return registerCallable(
            std::move(name), std::move(description), std::move(callable),
            std::array<ParameterDescriptor, sizeof...(Params)>{std::move(params)...});
    }

    /**
     * @brief Registers a member function bound to the given instance.
     *
     * The instance is captured by reference and must outlive the Runtime.
     */
    template <typename C, typename R, typename... Args, typename... Params>
    Result<void> action(std::string name,
                        std::string description,
                        C& instance,
                        R (C::*method)(Args...),
                        Params... params) {
        const auto bound = [&instance, method](Args... args) -> R {
            return (instance.*method)(std::move(args)...);
        };
        return action(std::move(name), std::move(description), bound, std::move(params)...);
    }

    /**
     * @brief Rejects rvalue instances; the instance is captured by reference
     * and would dangle once the temporary is destroyed.
     */
    template <typename C, typename R, typename... Args, typename... Params>
    Result<void>
    action(std::string, std::string, const C&&, R (C::*method)(Args...) const, Params...) = delete;

    /**
     * @brief Registers a const member function bound to the given instance.
     *
     * The instance is captured by reference and must outlive the Runtime.
     */
    template <typename C, typename R, typename... Args, typename... Params>
    Result<void> action(std::string name,
                        std::string description,
                        const C& instance,
                        R (C::*method)(Args...) const,
                        Params... params) {
        const auto bound = [&instance, method](Args... args) -> R {
            return (instance.*method)(std::move(args)...);
        };
        return action(std::move(name), std::move(description), bound, std::move(params)...);
    }

private:
    template <typename F, std::size_t Count>
    Result<void> registerCallable(std::string name,
                                  std::string description,
                                  F callable,
                                  std::array<ParameterDescriptor, Count> parameters) {
        using Callable = std::decay_t<F>;
        using ReturnType = typename detail::FunctionTraits<Callable>::ReturnType;
        ActionDescriptor descriptor{.m_name = name,
                                    .m_description = std::move(description),
                                    .m_parameters = {parameters.begin(), parameters.end()},
                                    .m_returnType = ValueTypeOf<ReturnType>::VALUE};
        return m_registry->registerAction(ActionId{m_name, name},
                                          detail::makeTypedAction<Callable>(std::move(descriptor),
                                                                            std::move(callable),
                                                                            std::move(parameters)));
    }

    std::string m_name;
    detail::Registry* m_registry;
};

} // namespace axiom::runtime
