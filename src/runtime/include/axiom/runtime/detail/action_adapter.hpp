#pragma once

#include <array>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <axiom/runtime/action.hpp>
#include <axiom/runtime/descriptor.hpp>
#include <axiom/runtime/detail/function_traits.hpp>
#include <axiom/runtime/detail/value_converter.hpp>
#include <axiom/runtime/result.hpp>
#include <axiom/runtime/value.hpp>

namespace axiom::runtime::detail {

template <typename> struct ParameterSlot {
    using Type = ParameterDescriptor;
};

/** @brief Repeats ParameterDescriptor once per type, e.g. to size a per-parameter tuple. */
template <typename... Types> struct ParameterList {
    using Type = std::tuple<typename ParameterSlot<Types>::Type...>;
};

/**
 * @brief Adapts a typed callable to the IAction ABI.
 *
 * Converts named Arguments to the callable's parameter types via ValueConverter,
 * applies descriptor defaults for absent optional parameters, and wraps the
 * return value back into a Value. Exceptions never escape invoke(): std::exception
 * maps to InvocationFailed and anything else to InternalError. Unknown arguments
 * are ignored here; the Dispatcher rejects them later. Params must be default
 * constructible.
 */
template <typename F, typename... Params> class TypedActionAdapter final : public IAction {
public:
    using ReturnType = typename FunctionTraits<F>::ReturnType;
    using ParameterTuple = typename ParameterList<Params...>::Type;

    explicit TypedActionAdapter(ActionDescriptor descriptor, F callable, ParameterTuple parameters)
        : m_descriptor{std::move(descriptor)}, m_callable{std::move(callable)},
          m_parameters{std::move(parameters)} {}

    [[nodiscard]] const ActionDescriptor& descriptor() const noexcept override {
        return m_descriptor;
    }

    Result<Value> invoke(const Arguments& arguments,
                         [[maybe_unused]] const InvocationContext& context) override {
        try {
            return invokeChecked(arguments);
        } catch(const std::exception& exception) {
            return Result<Value>{Error{ErrorCode::InvocationFailed, exception.what(), {}, Value{}}};
        } catch(...) {
            return Result<Value>{
                Error{ErrorCode::InternalError, "Unknown action failure", {}, Value{}}};
        }
    }

private:
    Result<Value> invokeChecked(const Arguments& arguments) {
        Result<std::tuple<Params...>> converted =
            convertArguments(arguments, std::index_sequence_for<Params...>{});
        if(!converted.hasValue()) {
            return Result<Value>{converted.error()};
        }
        return callAndWrap(std::move(converted).value(), std::index_sequence_for<Params...>{});
    }

    template <std::size_t... Indices>
    Result<std::tuple<Params...>> convertArguments(const Arguments& arguments,
                                                   std::index_sequence<Indices...>) const {
        if constexpr(sizeof...(Indices) == 0) {
            return Result<std::tuple<Params...>>{std::tuple<Params...>{}};
        } else {
            std::tuple<Params...> converted{};
            std::optional<Error> first_error;
            (convertOne<Indices>(arguments, converted, first_error), ...);
            if(first_error.has_value()) {
                return Result<std::tuple<Params...>>{std::move(*first_error)};
            }
            return Result<std::tuple<Params...>>{std::move(converted)};
        }
    }

    template <std::size_t Index>
    void convertOne(const Arguments& arguments,
                    std::tuple<Params...>& converted,
                    std::optional<Error>& first_error) const {
        if(first_error.has_value()) {
            return;
        }
        using ParamType = std::tuple_element_t<Index, std::tuple<Params...>>;
        Result<ParamType> result =
            convertParameter<ParamType>(arguments, std::get<Index>(m_parameters));
        if(result.hasValue()) {
            std::get<Index>(converted) = std::move(result).value();
        } else {
            first_error = result.error();
        }
    }

    template <typename ParamType>
    static Result<ParamType> convertParameter(const Arguments& arguments,
                                              const ParameterDescriptor& parameter) {
        const Arguments::const_iterator argument = arguments.find(parameter.m_name);
        if(argument != arguments.end()) {
            return ValueConverter<ParamType>::fromValue(argument->second, parameter.m_name);
        }
        if(parameter.m_required) {
            return Result<ParamType>{Error{ErrorCode::MissingArgument,
                                           "Missing required argument '" + parameter.m_name + "'",
                                           parameter.m_name, Value{}}};
        }
        if(parameter.m_defaultValue.has_value()) {
            return ValueConverter<ParamType>::fromValue(*parameter.m_defaultValue,
                                                        parameter.m_name);
        }
        return Result<ParamType>{ParamType{}};
    }

    template <std::size_t... Indices>
    Result<Value> callAndWrap(std::tuple<Params...> arguments, std::index_sequence<Indices...>) {
        if constexpr(std::is_void_v<ReturnType>) {
            std::invoke(m_callable, std::move(std::get<Indices>(arguments))...);
            return Result<Value>{Value{}};
        } else {
            return Result<Value>{ValueConverter<ReturnType>::toValue(
                std::invoke(m_callable, std::move(std::get<Indices>(arguments))...))};
        }
    }

    ActionDescriptor m_descriptor;
    F m_callable;
    ParameterTuple m_parameters;
};

template <typename F, std::size_t... Indices>
std::unique_ptr<IAction>
makeTypedActionFrom(F callable,
                    ActionDescriptor descriptor,
                    const std::array<ParameterDescriptor, FunctionTraits<F>::ARITY>& parameters,
                    std::index_sequence<Indices...>) {
    using ArgsTuple = typename FunctionTraits<F>::ArgsTuple;
    using Adapter = TypedActionAdapter<F, std::tuple_element_t<Indices, ArgsTuple>...>;
    using ParameterTuple =
        typename ParameterList<std::tuple_element_t<Indices, ArgsTuple>...>::Type;
    return std::make_unique<Adapter>(std::move(descriptor), std::move(callable),
                                     ParameterTuple{std::get<Indices>(parameters)...});
}

/** @brief Builds a TypedActionAdapter deducing parameter types from the callable. */
template <typename F>
[[nodiscard]] std::unique_ptr<IAction>
makeTypedAction(ActionDescriptor descriptor,
                F callable,
                std::array<ParameterDescriptor, FunctionTraits<F>::ARITY> parameters = {}) {
    return makeTypedActionFrom(std::move(callable), std::move(descriptor), parameters,
                               std::make_index_sequence<FunctionTraits<F>::ARITY>{});
}

} // namespace axiom::runtime::detail
